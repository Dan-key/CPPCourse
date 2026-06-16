# Задание: write_all с EINTR-retry

Реализуй `write_all` — гарантированную полную запись с обработкой `EINTR` и поддержкой частичного `write`.

## Проблема

`write(2)` может:
1. Быть прерван сигналом → вернуть `-1, errno=EINTR` (нужно повторить)
2. Записать **меньше** запрошенного байт без ошибки (частичный write) — особенно на сокетах
3. Вернуть реальную ошибку (EPIPE, EIO и т.д.)

## Интерфейс

```c
/*
 * Записать ровно n байт из buf в fd.
 * Повторяет при EINTR и частичном write.
 * Возвращает n при успехе, -1 при реальной ошибке (errno установлен).
 */
ssize_t write_all(int fd, const void *buf, size_t n);
```

## Алгоритм

```
bytes_written = 0
while bytes_written < n:
    r = write(fd, buf + bytes_written, n - bytes_written)
    if r < 0:
        if errno == EINTR: continue   # сигнал — просто повторить
        return -1                      # реальная ошибка
    bytes_written += r
return n
```

## Тесты

- Обычная запись: `write_all(fd, "hello", 5)` → 5
- EINTR (симулируется через mock): продолжает после EINTR
- Ошибка на закрытом fd: возвращает -1, errno == EBADF
