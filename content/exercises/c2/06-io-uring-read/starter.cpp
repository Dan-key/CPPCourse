#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <atomic>

// Сырой io_uring (без liburing — её нет в этом окружении). Ты увидишь «голую»
// механику: два кольца в общей памяти, SQE/CQE, io_uring_enter — то, что
// liburing прячет. Это прямое продолжение C1: индексы колец синхронизируются
// acquire/release, как в SPSC-очереди (§11.1 C1).
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog
//
// glibc-обёрток для этих сисколлов нет — вызываем через syscall().
static int io_uring_setup(unsigned entries, io_uring_params* p) {
    return (int)syscall(__NR_io_uring_setup, entries, p);
}
static int io_uring_enter(int fd, unsigned to_submit, unsigned min_complete, unsigned flags) {
    return (int)syscall(__NR_io_uring_enter, fd, to_submit, min_complete, flags,
                        (void*)nullptr, (size_t)0);
}

// --- Кольца. Вся настройка (setup + mmap) уже сделана за тебя. ---
struct Ring {
    int ring_fd = -1;
    io_uring_params params{};
    void* sq = MAP_FAILED;
    void* cq = MAP_FAILED;
    io_uring_sqe* sqes = (io_uring_sqe*)MAP_FAILED;
    size_t sq_sz = 0, cq_sz = 0, sqe_sz = 0;

    // Указатели на поля колец (заполняются в init()).
    unsigned* sq_tail = nullptr; unsigned* sq_mask = nullptr; unsigned* sq_array = nullptr;
    unsigned* cq_head = nullptr; unsigned* cq_tail = nullptr; unsigned* cq_mask = nullptr;
    io_uring_cqe* cqes = nullptr;

    // Вернуть 0 при успехе, -1 если io_uring недоступен (старое ядро / запрещён).
    int init(unsigned entries) {
        ring_fd = io_uring_setup(entries, &params);
        if (ring_fd < 0) return -1;
        sq_sz  = params.sq_off.array + params.sq_entries * sizeof(unsigned);
        cq_sz  = params.cq_off.cqes  + params.cq_entries * sizeof(io_uring_cqe);
        sqe_sz = params.sq_entries * sizeof(io_uring_sqe);
        sq   = mmap(0, sq_sz, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, ring_fd, IORING_OFF_SQ_RING);
        cq   = mmap(0, cq_sz, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, ring_fd, IORING_OFF_CQ_RING);
        sqes = (io_uring_sqe*)mmap(0, sqe_sz, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, ring_fd, IORING_OFF_SQES);
        if (sq == MAP_FAILED || cq == MAP_FAILED || sqes == (void*)MAP_FAILED) return -1;
        sq_tail  = (unsigned*)((char*)sq + params.sq_off.tail);
        sq_mask  = (unsigned*)((char*)sq + params.sq_off.ring_mask);
        sq_array = (unsigned*)((char*)sq + params.sq_off.array);
        cq_head  = (unsigned*)((char*)cq + params.cq_off.head);
        cq_tail  = (unsigned*)((char*)cq + params.cq_off.tail);
        cq_mask  = (unsigned*)((char*)cq + params.cq_off.ring_mask);
        cqes     = (io_uring_cqe*)((char*)cq + params.cq_off.cqes);
        return 0;
    }
    ~Ring() {
        if (sq   != MAP_FAILED)        munmap(sq, sq_sz);
        if (cq   != MAP_FAILED)        munmap(cq, cq_sz);
        if (sqes != (void*)MAP_FAILED) munmap(sqes, sqe_sz);
        if (ring_fd >= 0)              close(ring_fd);
    }
};

// ЗАДАЧА: через готовое кольцо r выполнить ОДНО чтение fd в buf[0..len) и
// вернуть результат (число прочитанных байт; 0 = EOF; <0 = -errno).
//
// Шаги:
//   1) Взять свободный SQE:
//        unsigned tail = *r.sq_tail;  unsigned idx = tail & *r.sq_mask;
//        io_uring_sqe& sqe = r.sqes[idx];  memset(&sqe,0,sizeof sqe);
//   2) Заполнить операцию READ:
//        sqe.opcode = IORING_OP_READ; sqe.fd = fd;
//        sqe.addr = (uintptr_t)buf; sqe.len = len; sqe.off = 0;
//        sqe.user_data = 0x42;
//        r.sq_array[idx] = idx;
//   3) Опубликовать (release — как tail в SPSC из C1):
//        std::atomic_ref<unsigned>(*r.sq_tail).store(tail + 1, std::memory_order_release);
//   4) Один сисколл: отправить 1, дождаться 1 завершения:
//        io_uring_enter(r.ring_fd, 1, 1, IORING_ENTER_GETEVENTS);
//   5) Забрать CQE (acquire на cq_tail), прочитать res, продвинуть cq_head (release):
//        unsigned head = *r.cq_head;
//        io_uring_cqe& cqe = r.cqes[head & *r.cq_mask];
//        int res = cqe.res;
//        std::atomic_ref<unsigned>(*r.cq_head).store(head + 1, std::memory_order_release);
//        return res;
int uring_read(Ring& r, int fd, void* buf, unsigned len) {
    (void)r; (void)fd; (void)buf; (void)len;
    return -1; // TODO
}

// Обёртка для теста: настроить кольцо и сделать одно чтение.
// Вернуть >=0 — результат чтения; -2 — io_uring недоступен (тест засчитает).
int uring_read_once(int fd, void* buf, unsigned len) {
    Ring r;
    if (r.init(8) != 0) return -2;       // ядро без io_uring — тест пропустит
    return uring_read(r, fd, buf, len);
}
