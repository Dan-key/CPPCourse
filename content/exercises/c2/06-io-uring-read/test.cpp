#include <cstdio>
#include <cstring>
#include <unistd.h>

int uring_read_once(int fd, void* buf, unsigned len);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

int main() {
    std::printf("=== 06-io-uring-read ===\n");

    int p[2];
    if (pipe(p) != 0) { std::printf("pipe failed\n"); return 1; }

    const char* msg = "io_uring read works";
    size_t mlen = std::strlen(msg);
    if (write(p[1], msg, mlen) < 0) { /* ignore */ }

    char buf[128] = {0};
    int res = uring_read_once(p[0], buf, sizeof buf);

    if (res == -2) {
        std::printf("  [SKIP] io_uring недоступен в этом окружении — тест засчитан.\n");
        std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
        close(p[0]); close(p[1]);
        return 0;   // не валим студента из-за отсутствия io_uring в ядре
    }

    CHECK(res == (int)mlen, "io_uring READ вернул число прочитанных байт (cqe.res)");
    CHECK(std::memcmp(buf, msg, mlen) == 0, "данные реально прочитаны ядром в буфер");

    // Второе чтение после закрытия пишущего конца → EOF (0).
    close(p[1]);
    char buf2[16] = {0};
    int eof = uring_read_once(p[0], buf2, sizeof buf2);
    CHECK(eof == 0, "чтение после закрытия пишущего конца → 0 (EOF)");

    close(p[0]);

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
