#include <cstdint>
#include <algorithm>

// Token bucket — rate limiter, который стоит за каждым «N запросов в секунду»
// (API-лимиты, throttling, QoS). В «ведре» лежат токены; каждый запрос забирает
// один; токены доливаются с постоянной скоростью до ёмкости. Допускает всплески
// (burst до capacity), но в среднем держит заданную скорость.
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog
//
// В реальной системе долив привязывают к timerfd или считают «лениво» по времени
// (как здесь) — второе дешевле: пересчитываем токены при каждом запросе.

class TokenBucket {
public:
    // capacity — максимум токенов (размер всплеска); rate_per_sec — скорость долива.
    // Ведро стартует ПОЛНЫМ.
    TokenBucket(double capacity, double rate_per_sec)
        : cap_(capacity), rate_per_ms_(rate_per_sec / 1000.0),
          tokens_(capacity), last_ms_(0) {}

    // Запрос в момент now_ms: долить токены по прошедшему времени (с ограничением
    // сверху ёмкостью), затем если есть хотя бы 1 токен — забрать его и разрешить.
    // Вернуть true (разрешено) / false (превышен лимит).
    bool allow(uint64_t now_ms) {
        (void)now_ms;
        return false; // TODO
    }

private:
    double   cap_;
    double   rate_per_ms_;
    double   tokens_;
    uint64_t last_ms_;
};

extern "C" {
    TokenBucket* tb_create(double cap, double rate_per_sec) { return new TokenBucket(cap, rate_per_sec); }
    void tb_destroy(TokenBucket* b)                         { delete b; }
    int  tb_allow(TokenBucket* b, uint64_t now)             { return b->allow(now) ? 1 : 0; }
}
