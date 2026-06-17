#include <cstdio>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>

struct DirWatch;
extern "C" {
    DirWatch* dw_create();
    void dw_destroy(DirWatch* w);
    int  dw_init(DirWatch* w, const char* path);
    void dw_process(DirWatch* w, int timeout_ms);
    int  dw_contains(DirWatch* w, const char* name);
    int  dw_count(DirWatch* w);
}

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

static std::string g_dir;
static std::string path(const char* name) { return g_dir + "/" + name; }
static void make(const char* name) { int fd = creat(path(name).c_str(), 0600); if (fd>=0) close(fd); }

int main() {
    std::printf("=== 06-inotify-dirwatch ===\n");

    char tmpl[] = "/tmp/c4dwXXXXXX";
    char* d = mkdtemp(tmpl);
    if (!d) { std::printf("mkdtemp failed\n"); return 1; }
    g_dir = d;

    DirWatch* w = dw_create();
    CHECK(dw_init(w, g_dir.c_str()) == 0, "dw_init: inotify создан и watch добавлен");

    // Создание файла.
    make("a.txt");
    dw_process(w, 1000);
    CHECK(dw_contains(w, "a.txt") == 1 && dw_count(w) == 1, "после create: модель содержит a.txt");

    // Ещё два файла.
    make("b.txt"); make("c.txt");
    dw_process(w, 1000);
    CHECK(dw_count(w) == 3 && dw_contains(w, "b.txt") && dw_contains(w, "c.txt"),
          "после ещё двух create: в модели 3 файла");

    // Переименование a.txt -> z.txt (IN_MOVED_FROM + IN_MOVED_TO).
    rename(path("a.txt").c_str(), path("z.txt").c_str());
    dw_process(w, 1000);
    CHECK(dw_contains(w, "a.txt") == 0 && dw_contains(w, "z.txt") == 1 && dw_count(w) == 3,
          "после rename a→z: a.txt убран, z.txt добавлен (счёт прежний)");

    // Удаление.
    unlink(path("b.txt").c_str());
    dw_process(w, 1000);
    CHECK(dw_contains(w, "b.txt") == 0 && dw_count(w) == 2, "после delete b.txt: убран, в модели 2");

    // Нет изменений — process не должен ничего поломать (и не зависнуть по таймауту).
    dw_process(w, 100);
    CHECK(dw_count(w) == 2, "без изменений модель стабильна");

    dw_destroy(w);

    // Уборка temp-каталога.
    unlink(path("c.txt").c_str());
    unlink(path("z.txt").c_str());
    rmdir(g_dir.c_str());

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
