// Branchless-примитивы: убрать непредсказуемые data-dependent ветви из hot path.
//
// Реализуй формулы БЕЗ ветвей (см. подсказки в problem.md). Тест проверяет
// корректность (совпадение с обычной логикой); branchless — свойство ассемблера,
// проверяй на godbolt под -O2 (cmov/setcc вместо jne).
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g solution.cpp test.cpp -o prog

extern "C" int bl_min(int a, int b) {
    (void)a; (void)b;
    return -1; // TODO: b ^ ((a ^ b) & -(int)(a < b))
}

extern "C" int bl_max(int a, int b) {
    (void)a; (void)b;
    return -1; // TODO
}

extern "C" int bl_abs(int x) {
    (void)x;
    return -1; // TODO: int m = x >> 31; (x ^ m) - m
}

extern "C" int bl_clamp(int x, int lo, int hi) {
    (void)x; (void)lo; (void)hi;
    return -1; // TODO: bl_min(bl_max(x, lo), hi)
}

extern "C" int bl_select(int cond, int a, int b) {
    (void)cond; (void)a; (void)b;
    return -1; // TODO: b ^ ((a ^ b) & -(int)(cond != 0))
}

extern "C" int bl_sign(int x) {
    (void)x;
    return -2; // TODO: (x > 0) - (x < 0)
}

extern "C" int bl_count_ge(const int* a, int n, int t) {
    (void)a; (void)n; (void)t;
    return -1; // TODO: c += (a[i] >= t)
}
