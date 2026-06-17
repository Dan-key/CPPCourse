#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>

int echo_server_et(int listen_fd, int stop_fd);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

static int make_listener(int& port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    bind(fd, (sockaddr*)&a, sizeof a);
    listen(fd, 128);
    socklen_t sl = sizeof a;
    getsockname(fd, (sockaddr*)&a, &sl);
    port = ntohs(a.sin_port);
    return fd;
}

// Отправляет БОЛЬШОЕ сообщение (больше одного read-буфера сервера), читает эхо.
// Отправку и приём ведём параллельно, чтобы не упереться в буфер сокета.
static bool roundtrip_big(int port, size_t n) {
    int c = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t)port);
    if (connect(c, (sockaddr*)&a, sizeof a) != 0) { close(c); return false; }

    std::string msg(n, '\0');
    for (size_t i = 0; i < n; ++i) msg[i] = (char)('A' + (i % 26));

    std::string got;
    std::thread reader([&] {
        char buf[8192];
        while (got.size() < n) {
            ssize_t r = read(c, buf, sizeof buf);
            if (r <= 0) break;
            got.append(buf, (size_t)r);
        }
    });

    size_t off = 0;
    while (off < n) {
        ssize_t w = write(c, msg.data() + off, n - off);
        if (w <= 0) break;
        off += (size_t)w;
    }
    reader.join();
    close(c);
    return got == msg;
}

int main() {
    std::printf("=== 03-epoll-edge-triggered ===\n");

    int port = 0;
    int listen_fd = make_listener(port);
    int stop_fd   = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    CHECK(listen_fd >= 0 && port > 0 && stop_fd >= 0, "слушающий сокет и stop-eventfd готовы");

    int srv_rc = 123;
    std::thread server([&] { srv_rc = echo_server_et(listen_fd, stop_fd); });

    // Большие сообщения — заставляют ET-сервер читать в цикле до EAGAIN.
    // Если дренажа нет, эхо будет неполным и тест провалится (или повиснет → таймаут).
    bool all_ok = true;
    for (int i = 0; i < 5; ++i)
        if (!roundtrip_big(port, 50000)) all_ok = false;
    CHECK(all_ok, "ET-сервер вернул ПОЛНОЕ эхо больших сообщений (читал до EAGAIN)");

    uint64_t one = 1;
    if (write(stop_fd, &one, sizeof one) < 0) { /* ignore */ }
    server.join();
    CHECK(srv_rc == 0, "echo_server_et штатно вернул 0 по stop_fd");

    close(listen_fd); close(stop_fd);

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
