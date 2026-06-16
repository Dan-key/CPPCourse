# Задание: обработчики сигналов через sigaction

Реализуй graceful shutdown и счётчик событий через обработчики сигналов.

## Интерфейс

```c
void shutdown_handler(int sig);   /* устанавливает g_shutdown = 1 */
void counter_handler(int sig);    /* увеличивает g_counter на 1 */
int  setup_handlers(void);        /* устанавливает обработчики */
int  get_shutdown(void);
int  get_counter(void);
void reset_flags(void);
```

## Требования

### `setup_handlers`
Установить через `sigaction()` (не `signal()`):
- `SIGTERM` → `shutdown_handler`, флаг `SA_RESTART`
- `SIGINT`  → `shutdown_handler`, флаг `SA_RESTART`
- `SIGUSR1` → `counter_handler`, флаг `SA_RESTART`

### Глобальные флаги
- `static volatile sig_atomic_t g_shutdown = 0;`
- `static volatile sig_atomic_t g_counter  = 0;`

`volatile` и `sig_atomic_t` обязательны: компилятор не должен оптимизировать обращения к этим переменным.

## Ключевые детали `sigaction`

```c
struct sigaction sa;
memset(&sa, 0, sizeof(sa));
sa.sa_handler = my_handler;
sa.sa_flags   = SA_RESTART;
sigemptyset(&sa.sa_mask);
sigaction(SIGTERM, &sa, NULL);
```

`sigemptyset(&sa.sa_mask)` — **обязательно** инициализировать маску.

## Почему не `signal()`

`signal()` имеет implementation-defined поведение: в некоторых системах обработчик сбрасывается в `SIG_DFL` после первой доставки. `sigaction()` стабилен на всех POSIX-системах.

## Компиляция

```bash
gcc -std=c17 -Wall -Wextra -Wconversion -Wsign-conversion \
    -fsanitize=address,undefined -O1 -g solution.c test.c -o prog
./prog
```
