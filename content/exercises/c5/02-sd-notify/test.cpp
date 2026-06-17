#include <cstdio>
#include <cstring>

extern "C" int notify_ready(char* buf, int cap, const char* status);
extern "C" int notify_reloading(char* buf, int cap, unsigned long long mono_usec);
extern "C" int notify_stopping(char* buf, int cap);
extern "C" int notify_watchdog(char* buf, int cap);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

int main() {
    std::printf("=== 02-sd-notify ===\n");
    char buf[256];

    int n = notify_ready(buf, sizeof buf, nullptr);
    CHECK(n == 8 && std::strcmp(buf, "READY=1\n") == 0, "ready без status → READY=1\\n");

    n = notify_ready(buf, sizeof buf, "accepting connections");
    CHECK(std::strcmp(buf, "READY=1\nSTATUS=accepting connections\n") == 0
          && n == (int)std::strlen(buf),
          "ready со status → READY + STATUS, длина верна");

    n = notify_ready(buf, sizeof buf, "");
    CHECK(std::strcmp(buf, "READY=1\n") == 0, "пустой status не добавляет STATUS");

    n = notify_reloading(buf, sizeof buf, 42);
    CHECK(std::strcmp(buf, "RELOADING=1\nMONOTONIC_USEC=42\n") == 0
          && n == (int)std::strlen(buf),
          "reloading с MONOTONIC_USEC");

    n = notify_stopping(buf, sizeof buf);
    CHECK(n == 11 && std::strcmp(buf, "STOPPING=1\n") == 0, "stopping → STOPPING=1\\n");

    n = notify_watchdog(buf, sizeof buf);
    CHECK(n == 11 && std::strcmp(buf, "WATCHDOG=1\n") == 0, "watchdog → WATCHDOG=1\\n");

    // Слишком маленький буфер → -1 (и без выхода за границу — проверяет ASan).
    char tiny[4];
    CHECK(notify_ready(tiny, (int)sizeof tiny, nullptr) == -1, "тесный буфер ready → -1");
    CHECK(notify_stopping(tiny, (int)sizeof tiny) == -1, "тесный буфер stopping → -1");
    CHECK(notify_reloading(tiny, (int)sizeof tiny, 123456) == -1, "тесный буфер reloading → -1");

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
