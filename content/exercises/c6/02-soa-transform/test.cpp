#include <cstdio>

extern "C" void soa_advance(long* x, long* y, const long* vx, const long* vy,
                            int n, long dt);
extern "C" int  soa_count_in_box(const long* x, const long* y, int n,
                                 long x0, long y0, long x1, long y1);
extern "C" long soa_sum_active_vx(const long* vx, const unsigned char* alive, int n);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

int main() {
    std::printf("=== 02-soa-transform ===\n");

    // advance: x += vx*dt, y += vy*dt.
    {
        long x[]  = { 0, 10, -5 };
        long y[]  = { 0,  0, 100 };
        long vx[] = { 1,  2, -3 };
        long vy[] = { 0, -1,  4 };
        soa_advance(x, y, vx, vy, 3, 10);
        CHECK(x[0]==10 && x[1]==30 && x[2]==-35, "advance: x += vx*dt");
        CHECK(y[0]==0  && y[1]==-10 && y[2]==140, "advance: y += vy*dt");
        // ещё шаг (накопление):
        soa_advance(x, y, vx, vy, 3, 1);
        CHECK(x[0]==11 && x[1]==32 && x[2]==-38, "advance накапливается");
    }

    // count_in_box (включительно по границам).
    {
        long x[] = { 0, 5, 10, 3, -1, 5 };
        long y[] = { 0, 5,  0, 8,  2, 10 };
        // бокс [0,0]..[5,5]: (0,0)✓ (5,5)✓ (10,0)✗ (3,8)✗ (-1,2)✗ (5,10)✗ → 2
        CHECK(soa_count_in_box(x, y, 6, 0,0, 5,5) == 2, "count_in_box: 2 внутри");
        CHECK(soa_count_in_box(x, y, 6, -100,-100, 100,100) == 6, "огромный бокс → все 6");
        CHECK(soa_count_in_box(x, y, 6, 1000,1000, 2000,2000) == 0, "далёкий бокс → 0");
        CHECK(soa_count_in_box(x, y, 6, 5,5, 5,5) == 1, "точечный бокс (5,5) → 1 (граница включ.)");
    }

    // sum_active_vx.
    {
        long vx[]           = { 10, 20, 30, 40, 50 };
        unsigned char al[]  = {  1,  0,  1,  0,  1 };
        CHECK(soa_sum_active_vx(vx, al, 5) == 90, "sum активных vx = 10+30+50");
        unsigned char none[] = { 0,0,0,0,0 };
        CHECK(soa_sum_active_vx(vx, none, 5) == 0, "никто не активен → 0");
        unsigned char all[]  = { 2,2,2,2,2 };
        CHECK(soa_sum_active_vx(vx, all, 5) == 150, "alive!=0 (не только 1) считается");
    }

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
