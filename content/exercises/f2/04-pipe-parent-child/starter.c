#define _GNU_SOURCE
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Запустить дочерний процесс, передать ему сообщение через pipe,
   дочерний процесс должен вернуть strlen(message) как exit code.
   Родитель возвращает exit code дочернего процесса (0..255).
   При ошибке возвращает -1. */
int send_to_child(const char *message) {
    /* TODO:
       1. pipe()
       2. fork()
       3. Потомок: close write-конец, read всё сообщение, _exit(длина)
       4. Родитель: close read-конец, write_full(message), close write-конец, waitpid
       5. Извлечь exit code через WEXITSTATUS, вернуть его */
    (void)message;
    return -1;
}

/* Прочитать весь вывод дочернего процесса через pipe в буфер.
   Запускает команду через execv, читает stdout через pipe.
   Возвращает количество байт прочитанных (без нуль-терминатора).
   buf должен быть достаточно большим (bufsz байт).
   Возвращает -1 при ошибке.
   Примечание: ASan с fork может давать ложные срабатывания.
   Запускай с ASAN_OPTIONS=detect_leaks=0 при необходимости. */
ssize_t capture_command_output(const char *const argv[], char *buf, size_t bufsz) {
    /* TODO:
       1. pipe()
       2. fork()
       3. Потомок: dup2(pfd[1], STDOUT_FILENO), закрыть pfd[0] и pfd[1], execv
       4. Родитель: close pfd[1], читать pfd[0] в цикле до EOF, waitpid
       5. Вернуть количество прочитанных байт */
    (void)argv; (void)buf; (void)bufsz;
    return -1;
}
