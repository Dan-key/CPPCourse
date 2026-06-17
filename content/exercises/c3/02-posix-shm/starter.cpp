#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstddef>
#include <cerrno>

// POSIX разделяемая память: shm_open создаёт именованный объект в ядре (виден в
// /dev/shm), ftruncate задаёт размер, mmap отображает его в адресное
// пространство. Несколько процессов (или маппингов), открывших ОДИН объект,
// видят одни и те же байты — это самый быстрый IPC (без копирования через ядро).
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog
//   (shm_open/mmap есть в libc — отдельный -lrt не нужен на современной glibc)

// Создать/открыть разделяемый объект name размером size байт и вернуть его fd.
// Шаги: shm_open(name, O_CREAT|O_RDWR, 0600); ftruncate(fd, size).
// Вернуть fd (>=0) или -1.
int shm_setup(const char* name, size_t size) {
    (void)name; (void)size;
    return -1; // TODO
}

// Отобразить уже открытый объект fd размером size в память для чтения и записи,
// РАЗДЕЛЯЕМО (MAP_SHARED) — чтобы записи были видны другим маппингам/процессам.
// Вернуть указатель или MAP_FAILED.
//   mmap(nullptr, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)
void* shm_map(int fd, size_t size) {
    (void)fd; (void)size;
    return MAP_FAILED; // TODO
}
