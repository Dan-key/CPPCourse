#include <cstdio>
#include <cstring>
#include <string>
#include <mqueue.h>
#include <fcntl.h>
#include <unistd.h>

mqd_t   mq_setup(const char* name);
int     mq_send_msg(mqd_t q, const char* s, unsigned prio);
ssize_t mq_recv_msg(mqd_t q, char* buf, size_t buflen, unsigned* prio);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

// Независимая проверка: поддерживает ли окружение POSIX mqueue?
static bool mqueue_available() {
    std::string n = "/c3_probe_" + std::to_string(getpid());
    mqd_t q = mq_open(n.c_str(), O_CREAT | O_RDWR, 0600, nullptr);
    if (q == (mqd_t)-1) return false;
    mq_close(q); mq_unlink(n.c_str());
    return true;
}

int main() {
    std::printf("=== 05-posix-mqueue ===\n");

    if (!mqueue_available()) {
        std::printf("  [SKIP] POSIX mqueue недоступна в этом окружении — тест засчитан.\n");
        std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
        return 0;
    }

    std::string name = "/c3_mq_" + std::to_string(getpid());
    mq_unlink(name.c_str());   // на случай остатков

    mqd_t q = mq_setup(name.c_str());
    CHECK(q != (mqd_t)-1, "mq_setup открыл очередь");

    // Шлём в порядке приоритетов 1, 5, 3 — принять должны 5, 3, 1.
    CHECK(mq_send_msg(q, "low",  1) == 0, "send prio=1");
    CHECK(mq_send_msg(q, "high", 5) == 0, "send prio=5");
    CHECK(mq_send_msg(q, "mid",  3) == 0, "send prio=3");

    char buf[256]; unsigned prio = 0;
    auto recv1 = [&](const char* want, unsigned wp) {
        std::memset(buf, 0, sizeof buf);
        ssize_t n = mq_recv_msg(q, buf, sizeof buf, &prio);
        return n >= 0 && std::strncmp(buf, want, (size_t)n) == 0 &&
               (size_t)n == std::strlen(want) && prio == wp;
    };
    CHECK(recv1("high", 5), "первым принято сообщение с наибольшим приоритетом (high, 5)");
    CHECK(recv1("mid",  3), "затем средний приоритет (mid, 3)");
    CHECK(recv1("low",  1), "затем низший приоритет (low, 1)");

    mq_close(q);
    mq_unlink(name.c_str());

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
