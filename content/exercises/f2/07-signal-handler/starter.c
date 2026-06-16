#define _GNU_SOURCE
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* Глобальный флаг для graceful shutdown */
static volatile sig_atomic_t g_shutdown = 0;
static volatile sig_atomic_t g_counter  = 0;

/* Обработчик SIGTERM и SIGINT: устанавливает g_shutdown = 1 */
void shutdown_handler(int sig) {
    /* TODO: установить g_shutdown = 1 */
    (void)sig;
}

/* Обработчик SIGUSR1: увеличивает g_counter на 1 */
void counter_handler(int sig) {
    /* TODO */
    (void)sig;
}

/* Установить обработчики через sigaction (не signal()).
   SIGTERM и SIGINT → shutdown_handler
   SIGUSR1          → counter_handler
   Все обработчики с флагом SA_RESTART.
   Возвращает 0 при успехе, -1 при ошибке. */
int setup_handlers(void) {
    return -1; /* TODO */
}

/* Вернуть текущее значение g_shutdown */
int get_shutdown(void) { return (int)g_shutdown; }

/* Вернуть текущее значение g_counter */
int get_counter(void) { return (int)g_counter; }

/* Сбросить оба флага в 0 */
void reset_flags(void) { g_shutdown = 0; g_counter = 0; }
