#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>

int     set_nonblocking(int fd);
ssize_t write_all(int fd, const void* buf, size_t n);
ssize_t read_drain(int fd, std::string& out);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

int main() {
    std::printf("=== 01-nonblocking-io ===\n");

    // --- set_nonblocking + EAGAIN ---
    {
        int sv[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        CHECK(set_nonblocking(sv[0]) == 0, "set_nonblocking возвращает 0");
        CHECK((fcntl(sv[0], F_GETFL, 0) & O_NONBLOCK) != 0, "флаг O_NONBLOCK реально установлен");
        char b;
        ssize_t r = read(sv[0], &b, 1);   // пусто → должно вернуть -1/EAGAIN, не блокировать
        CHECK(r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK),
              "read из пустого неблокирующего fd → -1, errno=EAGAIN");
        close(sv[0]); close(sv[1]);
    }

    // --- read_drain: вычитывает всё доступное, потом 0 ---
    {
        int sv[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        const char* msg = "the quick brown fox";
        size_t len = std::strlen(msg);
        write(sv[1], msg, len);
        set_nonblocking(sv[0]);

        std::string out;
        ssize_t got = read_drain(sv[0], out);
        CHECK(got == (ssize_t)len, "read_drain вернул число прочитанных байт");
        CHECK(out == msg, "read_drain собрал все данные правильно");

        out.clear();
        ssize_t again = read_drain(sv[0], out);   // больше ничего нет
        CHECK(again == 0 && out.empty(), "повторный read_drain на пустом → 0 (EAGAIN, не ошибка)");
        close(sv[0]); close(sv[1]);
    }

    // --- write_all: пишет ровно n байт, в т.ч. при переполнении буфера ---
    {
        int sv[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        const size_t N = 1u << 20;              // 1 МБ — заведомо больше буфера сокета
        std::string data(N, '\0');
        for (size_t i = 0; i < N; ++i) data[i] = (char)(i & 0x7f);

        // Читатель в отдельном потоке — иначе write_all упрётся в полный буфер.
        std::string recv;
        std::thread reader([&] {
            char buf[65536];
            size_t got = 0;
            while (got < N) {
                ssize_t r = read(sv[0], buf, sizeof buf);
                if (r <= 0) break;
                recv.append(buf, (size_t)r);
                got += (size_t)r;
            }
        });

        ssize_t w = write_all(sv[1], data.data(), N);
        reader.join();

        CHECK(w == (ssize_t)N, "write_all вернул n (записал всё, обработав частичные write)");
        CHECK(recv.size() == N && recv == data, "получатель собрал ровно те же N байт");
        close(sv[0]); close(sv[1]);
    }

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
