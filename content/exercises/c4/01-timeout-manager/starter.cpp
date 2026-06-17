#include <cstdint>
#include <cstddef>
#include <queue>
#include <vector>
#include <unordered_set>

// Менеджер таймаутов — структура данных, которая стоит за таймаутами соединений
// в КАЖДОМ сервере. Тысячи соединений, у каждого свой дедлайн; нужно эффективно
// узнавать «ближайший дедлайн» (чтобы вооружить ОДИН timerfd, C2/C4) и забирать
// все истёкшие. Реализуется min-heap'ом с ленивым удалением отменённых.
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog
//
// Подсказка по архитектуре: один timerfd вооружается на next_deadline(); когда он
// срабатывает, зовём expire(now) и перевооружаем на новый ближайший дедлайн. Так
// 10 000 таймеров обслуживаются одним дескриптором.

class TimeoutManager {
public:
    // Запланировать таймер id на момент deadline_ms (монотонные мс).
    // id уникален; повторный add того же id в тесте не делается после cancel.
    void add(uint64_t id, uint64_t deadline_ms) {
        (void)id; (void)deadline_ms;
        // TODO: положить (deadline, id) в кучу
    }

    // Отменить таймер id (ленивое удаление: пометить отменённым, выкинуть при extract).
    void cancel(uint64_t id) {
        (void)id;
        // TODO
    }

    // Ближайший дедлайн среди ЖИВЫХ таймеров (пропуская отменённые). false если пусто.
    bool next_deadline(uint64_t* out) {
        (void)out;
        return false; // TODO
    }

    // Забрать ВСЕ таймеры с deadline <= now, в порядке возрастания дедлайна, в
    // out_ids[0..max). Отменённые пропускать. Вернуть число записанных.
    size_t expire(uint64_t now_ms, uint64_t* out_ids, size_t max) {
        (void)now_ms; (void)out_ids; (void)max;
        return 0; // TODO
    }

private:
    struct Node { uint64_t deadline; uint64_t id; };
    struct Cmp { bool operator()(const Node& a, const Node& b) const {
        return a.deadline > b.deadline;            // min-heap по дедлайну
    }};
    std::priority_queue<Node, std::vector<Node>, Cmp> heap_;
    std::unordered_set<uint64_t> cancelled_;        // ленивое удаление
};

// C-совместимые обёртки.
extern "C" {
    TimeoutManager* tm_create()                                  { return new TimeoutManager(); }
    void   tm_destroy(TimeoutManager* t)                         { delete t; }
    void   tm_add(TimeoutManager* t, uint64_t id, uint64_t dl)   { t->add(id, dl); }
    void   tm_cancel(TimeoutManager* t, uint64_t id)            { t->cancel(id); }
    int    tm_next(TimeoutManager* t, uint64_t* out)            { return t->next_deadline(out) ? 1 : 0; }
    size_t tm_expire(TimeoutManager* t, uint64_t now, uint64_t* out, size_t max) {
        return t->expire(now, out, max);
    }
}
