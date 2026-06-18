// False sharing: развести счётчики по отдельным кэш-линиям (alignas(64) + padding).
//
// Стартер БЕЗ выравнивания: счётчики плотно (по 8 байт) → много на одной линии →
// проверки размера и distinct-lines проваливаются. Добавь alignas(64).
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g solution.cpp test.cpp -o prog

struct PaddedCounter {        // TODO: alignas(64), чтобы каждый счётчик был на своей линии
    long value = 0;
};

struct Counters {
    PaddedCounter* a = nullptr;
    int n = 0;
};

extern "C" Counters* counters_create(int n) {
    Counters* c = new Counters();
    c->a = new PaddedCounter[n];
    c->n = n;
    return c;
}

extern "C" void counters_destroy(Counters* c) {
    if (c) { delete[] c->a; delete c; }
}

extern "C" void counters_inc(Counters* c, int i) {
    c->a[i].value++;
}

extern "C" long counters_get(Counters* c, int i) {
    return c->a[i].value;
}

extern "C" int counters_elem_size() {
    return (int)sizeof(PaddedCounter);     // без alignas будет 8 — провалит тест
}

extern "C" int counters_on_distinct_lines(Counters* c, int n) {
    (void)c; (void)n;
    return 0; // TODO: проверить, что никакие два &value не делят 64-байтную линию
}
