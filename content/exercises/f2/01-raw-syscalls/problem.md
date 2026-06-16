# Задание: Raw syscall через syscall(2)

Реализуй обёртки над syscalls через `syscall(2)`, минуя именованные glibc-функции.

## Интерфейс

```c
#include <unistd.h>
#include <sys/types.h>

ssize_t my_write(int fd, const void *buf, size_t n);
ssize_t my_read(int fd, void *buf, size_t n);
int     my_close(int fd);
pid_t   my_getpid(void);
```

## Требования

- Использовать `syscall(SYS_write)` / `syscall(SYS_read)` / `syscall(SYS_close)` / `syscall(SYS_getpid)`
- errno должен устанавливаться корректно при ошибке (это делает сам syscall(2))
- Возвращаемые значения идентичны glibc-версиям

## Включения

```c
#define _GNU_SOURCE
#include <sys/syscall.h>
#include <unistd.h>
```

## Проверка

```bash
strace ./prog  # должен показывать те же номера syscall
```
