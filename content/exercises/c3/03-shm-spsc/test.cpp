#include <cstdio>
#include <cstring>
#include <atomic>
#include <cstddef>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

// Идентичное определение структуры (общего заголовка нет — см. правила курса).
struct SpscRing {
    static constexpr std::size_t CAP = 1024;
    std::atomic<std::size_t> head;
    std::atomic<std::size_t> tail;
    int buf[CAP];
};

int spsc_push(SpscRing* q, int v);
int spsc_pop(SpscRing* q, int* out);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

static constexpr int N = 200000;

int main() {
    std::printf("=== 03-shm-spsc ===\n");

    // Кольцо — в анонимной РАЗДЕЛЯЕМОЙ памяти: переживёт fork и будет общим.
    void* mem = mmap(nullptr, sizeof(SpscRing), PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) { std::printf("mmap failed\n"); return 1; }
    std::memset(mem, 0, sizeof(SpscRing));      // индексы = 0 (валидно для int-атомиков)
    auto* q = static_cast<SpscRing*>(mem);

    // Однопоточная проверка семантики (до fork).
    int out = -1;
    CHECK(spsc_pop(q, &out) == 0, "pop из пустого кольца → 0");
    CHECK(spsc_push(q, 42) == 1 && spsc_pop(q, &out) == 1 && out == 42, "push/pop одного значения");

    pid_t pid = fork();
    if (pid == 0) {
        // ДОЧЕРНИЙ процесс — производитель: шлёт 0..N-1.
        for (int i = 0; i < N; ++i)
            while (spsc_push(q, i) == 0) { /* полна — ждём потребителя */ }
        _exit(0);                                // _exit: не запускать LSan/atexit в потомке
    }

    // РОДИТЕЛЬ — потребитель: принимает строго по порядку.
    bool order_ok = true;
    long expect = 0;
    for (long got = 0; got < N; ) {
        int v;
        if (spsc_pop(q, &v)) {
            if (v != (int)expect) order_ok = false;
            ++expect; ++got;
        }
    }
    int status = 0;
    waitpid(pid, &status, 0);

    CHECK(order_ok && expect == N,
          "потребитель получил 0..N-1 строго по порядку ЧЕРЕЗ ГРАНИЦУ ПРОЦЕССА (FIFO, без потерь)");
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0, "производитель-процесс завершился штатно");

    munmap(mem, sizeof(SpscRing));

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
