#include <cstdio>

struct Batcher;
extern "C" Batcher* batch_create(int batch_size);
extern "C" void batch_destroy(Batcher*);
extern "C" void batch_push(Batcher*, int op);
extern "C" void batch_flush(Batcher*);
extern "C" int  batch_syscalls(Batcher*);
extern "C" int  batch_total_ops(Batcher*);
extern "C" int  batch_pending(Batcher*);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

int main() {
    std::printf("=== 04-batch-coalesce ===\n");

    // batch_size=4: пуш 10 → авто-флаши на 4 и 8.
    {
        Batcher* b = batch_create(4);
        for (int i = 0; i < 10; ++i) batch_push(b, i);
        CHECK(batch_syscalls(b) == 2, "10 пушей при size=4 → 2 авто-флаша");
        CHECK(batch_total_ops(b) == 8, "отправлено 8 операций (2×4)");
        CHECK(batch_pending(b) == 2, "осталось 2 непрофлашенных");
        batch_flush(b);
        CHECK(batch_syscalls(b) == 3, "явный flush → 3-й сисколл");
        CHECK(batch_total_ops(b) == 10, "все 10 отправлены");
        CHECK(batch_pending(b) == 0, "ничего не осталось");
        batch_destroy(b);
    }

    // Ровно один батч.
    {
        Batcher* b = batch_create(4);
        for (int i = 0; i < 4; ++i) batch_push(b, i);
        CHECK(batch_syscalls(b) == 1 && batch_pending(b) == 0, "ровно size пушей → 1 флаш, 0 в очереди");
        batch_destroy(b);
    }

    // Флаш пустого буфера не делает «пустой сисколл».
    {
        Batcher* b = batch_create(8);
        batch_flush(b);
        CHECK(batch_syscalls(b) == 0, "flush пустого → 0 сисколлов");
        batch_push(b, 1); batch_push(b, 2);
        batch_flush(b);
        CHECK(batch_syscalls(b) == 1 && batch_total_ops(b) == 2, "flush 2 накопленных → 1 сисколл");
        batch_flush(b);
        CHECK(batch_syscalls(b) == 1, "повторный flush пустого → без сисколла");
        batch_destroy(b);
    }

    // size=1 (вырожденный случай — как epoll: сисколл на операцию).
    {
        Batcher* b = batch_create(1);
        batch_push(b, 1); batch_push(b, 2); batch_push(b, 3);
        CHECK(batch_syscalls(b) == 3 && batch_total_ops(b) == 3, "size=1 → сисколл на каждую (как epoll)");
        batch_destroy(b);
    }

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
