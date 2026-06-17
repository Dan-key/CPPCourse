# Задание: неблокирующий I/O — `O_NONBLOCK`, `EAGAIN`, дренаж

Базовые кирпичи любого event loop. Реализуй три функции и, главное, научись
железно различать **четыре исхода** неблокирующего `read`/`write`.

## Что реализовать

```cpp
int     set_nonblocking(int fd);                       // добавить O_NONBLOCK; 0/-1
ssize_t write_all(int fd, const void* buf, size_t n);  // записать ровно n; n/-1
ssize_t read_drain(int fd, std::string& out);          // вычитать всё до EAGAIN; байт/-1
```

### `set_nonblocking`
```cpp
int flags = fcntl(fd, F_GETFL, 0);
if (flags < 0) return -1;
return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

### `write_all` (fd блокирующий)
Цикл, пока не записаны все `n` байт; `write` может вернуть **меньше**
запрошенного (частичная запись) — продолжай с того места. `EINTR` — повторить.
Любая другая `-1` — вернуть `-1`.

### `read_drain` (fd НЕблокирующий)
Шаблон «дренаж до `EAGAIN`» — основа edge-triggered epoll:
```cpp
for (;;) {
    ssize_t r = read(fd, buf, sizeof buf);
    if (r > 0)        { out.append(buf, r); total += r; }
    else if (r == 0)  break;                       // EOF
    else { if (errno == EINTR)  continue;          // прерван — повтор
           if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // пусто — выход
           return -1; }                            // настоящая ошибка
}
return total;
```

## Четыре исхода `read` — выучить намертво

| Результат | Значение | Реакция |
|-----------|----------|---------|
| `> 0` | прочитано столько байт | обработать |
| `== 0` | **EOF** — пир закрылся | закрыть соединение |
| `-1`, `errno==EAGAIN` | данных пока нет | **не ошибка**, вернуться позже |
| `-1`, `errno==EINTR` | прерван сигналом | повторить вызов |
| `-1`, прочее | реальная ошибка (`ECONNRESET`…) | закрыть соединение |

## C-эквивалент

Всё это — чистые сисколлы (`fcntl`, `read`, `write`), идентичные в C. На C
вместо `std::string& out` передавали бы буфер `char*`/`size_t` или растущий
буфер вручную. Та же логика лежит в основе read-loop любого ядрового драйвера.

## Проверка

Автопрогон (ASan/UBSan): `set_nonblocking` реально ставит флаг и `read` пустого
fd даёт `EAGAIN`; `read_drain` собирает все данные и возвращает 0 на пустом;
`write_all` пишет ровно 1 МБ через `socketpair` (с читателем в потоке), получатель
собирает те же байты.

Поиграй вживую: запусти `scripts/io-playground.py source --port 9000` и читай из
него своим `read_drain` под `strace -e read` — увидишь `EAGAIN` в конце.
