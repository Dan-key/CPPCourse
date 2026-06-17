#include <cstdio>
#include <cstring>
#include <string>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

int   shm_setup(const char* name, size_t size);
void* shm_map(int fd, size_t size);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

struct Shared { int counter; char text[64]; };

int main() {
    std::printf("=== 02-posix-shm ===\n");

    // Уникальное имя на процесс — чтобы не конфликтовать с остатками прошлых прогонов.
    std::string name = "/c3_shm_" + std::to_string(getpid());
    const size_t SZ = sizeof(Shared);

    int fd = shm_setup(name.c_str(), SZ);
    CHECK(fd >= 0, "shm_setup создал объект и задал размер");

    // Два НЕЗАВИСИМЫХ отображения одного объекта — должны видеть одни данные.
    void* a = shm_map(fd, SZ);
    void* b = shm_map(fd, SZ);
    CHECK(a != MAP_FAILED && b != MAP_FAILED, "оба mmap успешны");
    CHECK(a != b, "это два разных виртуальных адреса");

    auto* sa = static_cast<Shared*>(a);
    auto* sb = static_cast<Shared*>(b);

    sa->counter = 12345;
    std::strcpy(sa->text, "shared-memory");

    CHECK(sb->counter == 12345, "запись через маппинг A видна в маппинге B (counter)");
    CHECK(std::strcmp(sb->text, "shared-memory") == 0, "и текст тоже виден (MAP_SHARED)");

    // Обратное направление.
    sb->counter = 777;
    CHECK(sa->counter == 777, "запись через B видна в A (память реально общая)");

    munmap(a, SZ); munmap(b, SZ);
    close(fd);
    shm_unlink(name.c_str());

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
