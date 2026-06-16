#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

/*
 * Прочитать файл целиком. Вернуть буфер (caller освобождает через free).
 * *out_len — число байт содержимого (без нуль-терминатора).
 * При любой ошибке вернуть NULL; утечек fd и памяти быть не должно.
 *
 * Обязательно: идиома goto-cleanup (одна точка выхода на ошибку).
 */
char *read_whole_file(const char *path, size_t *out_len) {
    char *buf = NULL;
    FILE *f   = NULL;

    /* TODO: открыть, получить размер, выделить память, прочитать */

    /* Метки должны идти в обратном порядке захвата ресурсов */
    /* out_close: */
    /* out_free:  */
    /* out:       */
    (void)path; (void)out_len;
    return NULL;
}
