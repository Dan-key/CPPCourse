#include <cstdio>

extern "C" int bl_min(int a, int b);
extern "C" int bl_max(int a, int b);
extern "C" int bl_abs(int x);
extern "C" int bl_clamp(int x, int lo, int hi);
extern "C" int bl_select(int cond, int a, int b);
extern "C" int bl_sign(int x);
extern "C" int bl_count_ge(const int* a, int n, int t);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

int main() {
    std::printf("=== 01-branchless ===\n");

    // Эталон — обычная «ветвистая» логика; branchless обязан совпасть на всех входах.
    int xs[] = { -1000, -7, -1, 0, 1, 7, 42, 1000, -2147483647, 2147483647 };
    bool min_ok = true, max_ok = true, sel_ok = true;
    for (int a : xs) for (int b : xs) {
        if (bl_min(a, b) != (a < b ? a : b)) min_ok = false;
        if (bl_max(a, b) != (a > b ? a : b)) max_ok = false;
        if (bl_select(1, a, b) != a || bl_select(0, a, b) != b ||
            bl_select(5, a, b) != a) sel_ok = false;       // cond!=0 → a
    }
    CHECK(min_ok, "bl_min совпадает с a<b?a:b на всех парах");
    CHECK(max_ok, "bl_max совпадает с a>b?a:b на всех парах");
    CHECK(sel_ok, "bl_select(cond,a,b): cond!=0→a, cond==0→b");

    // abs/sign — без INT_MIN (переполнение); проверяем безопасный диапазон.
    bool abs_ok = true, sign_ok = true;
    for (int x = -1000; x <= 1000; ++x) {
        if (bl_abs(x) != (x < 0 ? -x : x)) abs_ok = false;
        if (bl_sign(x) != ((x > 0) - (x < 0))) sign_ok = false;
    }
    CHECK(abs_ok, "bl_abs == |x| на [-1000,1000]");
    CHECK(sign_ok, "bl_sign даёт -1/0/+1");
    CHECK(bl_sign(0) == 0 && bl_sign(123) == 1 && bl_sign(-123) == -1, "bl_sign точки");

    // clamp.
    CHECK(bl_clamp(5, 0, 10) == 5, "clamp внутри → как есть");
    CHECK(bl_clamp(-3, 0, 10) == 0, "clamp ниже lo → lo");
    CHECK(bl_clamp(99, 0, 10) == 10, "clamp выше hi → hi");
    CHECK(bl_clamp(0, 0, 10) == 0 && bl_clamp(10, 0, 10) == 10, "clamp на границах");

    // count_ge.
    int arr[] = { 5, 1, 9, 3, 7, 2, 8, 4 };
    CHECK(bl_count_ge(arr, 8, 5) == 4, "count_ge: >=5 их 4 (5,9,7,8)");
    CHECK(bl_count_ge(arr, 8, 100) == 0, "count_ge: >=100 их 0");
    CHECK(bl_count_ge(arr, 8, 1) == 8, "count_ge: >=1 все 8");
    CHECK(bl_count_ge(arr, 0, 5) == 0, "count_ge: n=0 → 0");

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
