#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

int send_fd(int sock, int fd);
int recv_fd(int sock);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

int main() {
    std::printf("=== 01-fdpass ===\n");

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) { std::printf("socketpair failed\n"); return 1; }

    int p[2];
    if (pipe(p) != 0) { std::printf("pipe failed\n"); return 1; }

    // Передаём ПИШУЩИЙ конец пайпа через сокет.
    CHECK(send_fd(sv[0], p[1]) == 0, "send_fd отправил дескриптор");
    int got = recv_fd(sv[1]);
    CHECK(got >= 0, "recv_fd вернул валидный дескриптор");
    CHECK(got != p[1], "полученный fd имеет ДРУГОЙ номер (это новый дескриптор)");

    // Пишем через ПОЛУЧЕННЫЙ fd — читаем через исходный читающий конец пайпа:
    // если это то же открытое описание, данные пройдут.
    const char* m = "fd-passing-works";
    ssize_t w = write(got, m, std::strlen(m));
    char buf[64] = {0};
    ssize_t r = read(p[0], buf, sizeof buf);
    CHECK(w == (ssize_t)std::strlen(m) && r == w && std::memcmp(buf, m, (size_t)r) == 0,
          "данные, записанные в полученный fd, пришли в исходный пайп (тот же OFD)");

    close(got); close(p[0]); close(p[1]); close(sv[0]); close(sv[1]);

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
