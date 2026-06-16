# Задание: pipe — родитель и потомок

Реализуй передачу данных через pipe между процессами и захват вывода команды.

## Интерфейс

```c
int send_to_child(const char *message);
ssize_t capture_command_output(const char *const argv[], char *buf, size_t bufsz);
```

## Требования

### `send_to_child`
1. Создать pipe (до fork!)
2. fork()
3. Потомок: закрыть write-конец, прочитать все данные из read-конца, `_exit(strlen_прочитанного)`
4. Родитель: закрыть read-конец, записать `message`, закрыть write-конец (иначе потомок никогда не получит EOF!), `waitpid`
5. Вернуть `WEXITSTATUS(status)` — exit code потомка

### `capture_command_output`
1. Создать pipe
2. fork()
3. Потомок: `dup2(pfd[1], STDOUT_FILENO)`, закрыть оба конца pipe, `execv(argv[0], argv)`
4. Родитель: закрыть write-конец, читать в цикле из read-конца, `waitpid`
5. Вернуть суммарное число прочитанных байт

## Критические ошибки-ловушки
- Не закрыть write-конец в родителе перед `waitpid` → потомок не получит EOF → зависание
- Не закрыть неиспользуемые концы после fork → EOF придёт только когда ВСЕ write-концы закрыты
- Использовать `exit()` вместо `_exit()` в потомке → двойной flush буферов stdio

## Компиляция

```bash
gcc -std=c17 -Wall -Wextra -Wconversion -Wsign-conversion \
    -fsanitize=address,undefined -O1 -g solution.c test.c -o prog
ASAN_OPTIONS=detect_leaks=0 ./prog
```
