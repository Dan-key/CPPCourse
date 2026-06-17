#include <cstdio>

extern "C" int my_listen_fds(int my_pid, const char* listen_pid, const char* listen_fds);
extern "C" int my_listen_fd_by_name(int my_pid, const char* listen_pid,
                                    const char* listen_fds, const char* listen_fdnames,
                                    const char* name);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

int main() {
    std::printf("=== 01-listen-fds ===\n");

    // Нас активировали: PID совпал, передали 2 дескриптора.
    CHECK(my_listen_fds(1234, "1234", "2") == 2, "наш PID + LISTEN_FDS=2 → 2");
    CHECK(my_listen_fds(1234, "1234", "1") == 1, "наш PID + LISTEN_FDS=1 → 1");
    CHECK(my_listen_fds(1234, "1234", "0") == 0, "LISTEN_FDS=0 → 0");

    // Не нам: PID не совпал → игнорируем (0).
    CHECK(my_listen_fds(1234, "9999", "2") == 0, "чужой LISTEN_PID → 0");

    // Не активированы / мусор → 0.
    CHECK(my_listen_fds(1234, nullptr, "2") == 0, "LISTEN_PID не задан → 0");
    CHECK(my_listen_fds(1234, "", "2") == 0, "LISTEN_PID пуст → 0");
    CHECK(my_listen_fds(1234, "1234", nullptr) == 0, "LISTEN_FDS не задан → 0");
    CHECK(my_listen_fds(1234, "1234", "abc") == 0, "LISTEN_FDS нечисло → 0");
    CHECK(my_listen_fds(1234, "1234", "-1") == 0, "LISTEN_FDS<0 → 0");
    CHECK(my_listen_fds(1234, "notapid", "2") == 0, "LISTEN_PID нечисло → 0");

    // Поиск дескриптора по имени: "http:metrics" → http=3, metrics=4.
    CHECK(my_listen_fd_by_name(7, "7", "2", "http:metrics", "http") == 3,
          "имя http → fd 3 (SD_LISTEN_FDS_START)");
    CHECK(my_listen_fd_by_name(7, "7", "2", "http:metrics", "metrics") == 4,
          "имя metrics → fd 4");
    CHECK(my_listen_fd_by_name(7, "7", "2", "http:metrics", "absent") == -1,
          "несуществующее имя → -1");
    CHECK(my_listen_fd_by_name(7, "7", "2", nullptr, "http") == -1,
          "нет LISTEN_FDNAMES → -1");
    CHECK(my_listen_fd_by_name(7, "8", "2", "http:metrics", "http") == -1,
          "чужой PID → -1 (нам не передавали)");

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
