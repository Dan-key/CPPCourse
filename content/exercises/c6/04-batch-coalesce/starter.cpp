// Syscall batching: коалесцировать операции и «отправлять» пачками одним флашем.
// Модель io_uring (C2): push ≈ положить SQE без сисколла; flush ≈ io_uring_enter.
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g solution.cpp test.cpp -o prog

struct Batcher {
    int batch_size = 1;
    int pending    = 0;   // накоплено, но не флашнуто
    int syscalls   = 0;   // число флашей ("io_uring_enter")
    int total      = 0;   // всего отправлено операций
};

extern "C" Batcher* batch_create(int batch_size) {
    Batcher* b = new Batcher();
    b->batch_size = batch_size > 0 ? batch_size : 1;
    return b;
}

extern "C" void batch_destroy(Batcher* b) { delete b; }

// Поставить операцию; при заполнении буфера — авто-флаш (syscalls++, total+=size, pending=0).
extern "C" void batch_push(Batcher* b, int op) {
    (void)b; (void)op;
    // TODO
}

// Принудительно отправить накопленное (если pending>0). Пустой буфер → ничего.
extern "C" void batch_flush(Batcher* b) {
    (void)b;
    // TODO
}

extern "C" int batch_syscalls(Batcher* b)  { return b->syscalls; }
extern "C" int batch_total_ops(Batcher* b) { return b->total; }
extern "C" int batch_pending(Batcher* b)   { return b->pending; }
