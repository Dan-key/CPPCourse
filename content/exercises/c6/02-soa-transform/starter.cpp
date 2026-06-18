// SoA (Structure of Arrays): плотные горячие циклы над частицами.
// На правильном layout цикл прост, плотно использует кэш и векторизуется.
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g solution.cpp test.cpp -o prog

// x[i] += vx[i]*dt; y[i] += vy[i]*dt для всех i.
extern "C" void soa_advance(long* x, long* y, const long* vx, const long* vy,
                            int n, long dt) {
    (void)x; (void)y; (void)vx; (void)vy; (void)n; (void)dt;
    // TODO
}

// Сколько частиц внутри [x0,x1]×[y0,y1] (включительно).
extern "C" int soa_count_in_box(const long* x, const long* y, int n,
                                long x0, long y0, long x1, long y1) {
    (void)x; (void)y; (void)n; (void)x0; (void)y0; (void)x1; (void)y1;
    return -1; // TODO
}

// Сумма vx[i] по i, где alive[i] != 0.
extern "C" long soa_sum_active_vx(const long* vx, const unsigned char* alive, int n) {
    (void)vx; (void)alive; (void)n;
    return -1; // TODO
}
