# Задание: разбудить блокирующий read из отложенной работы (полный круг)

Это замыкает круг K3 §13: устройство получает «событие» → откладывает обработку в
**workqueue** → bottom half делает `wake_up` → процесс, спавший в **блокирующем
`read`**, просыпается и забирает результат. Та же wait queue обслуживает и `poll`/
`epoll` (C2) — ты реализуешь обе стороны: `wake_up` из работы и `.poll`.

`/dev/cppwake`: `write` — источник события (как top half: только сохраняет данные и
**планирует** работу, не обрабатывает). `work_fn` (process context) переводит данные в
верхний регистр, выставляет «готово» и **будит** ждущих. `read` **блокируется**
(`wait_event_interruptible`), пока работа не выставит готовность, затем отдаёт
результат. `.poll` сообщает `EPOLLIN`, когда готово.

## Что реализовать

- **`write(text)`** — под `lock` сохранить данные, сбросить готовность
  (`data_ready=false`), **`schedule_work(&work)`**;
- **`work_fn`** (process context) — под `lock` перевести в **верхний регистр** в
  результат, выставить `data_ready=true`, **`wake_up_interruptible(&wq)`**;
- **`read`** — **`wait_event_interruptible(wq, READ_ONCE(data_ready))`** (уснуть до
  готовности; `-ERESTARTSYS` при сигнале), затем под `lock` забрать результат и
  `data_ready=false`, `copy_to_user` **вне** лока;
- **`.poll`** — `poll_wait(f, &wq, pt)`; вернуть `EPOLLIN | EPOLLRDNORM`, если
  `data_ready`, иначе 0;
- **`exit`** — `misc_deregister` → **`cancel_work_sync(&work)`**.

## Ключевые API (мост K1↔K3↔C2)

- **`wait_event_interruptible(wq, COND)`** (K1 §11) — спит, пока `COND` ложно;
  просыпается на `wake_up` и перепроверяет (предикат-цикл). `READ_ONCE` в предикате
  обязателен (флаг читается вне лока).
- **`wake_up_interruptible(&wq)`** — будит ждущих в `read` **и** в `poll`/epoll; не
  спит (можно из top half/bottom half).
- **`poll_wait(f, &wq, pt)` + `.poll`** (K1 §12) — подписать fd на ту же очередь, чтобы
  `epoll_wait` (C2) проснулся по тому же `wake_up`.
- **`cancel_work_sync`** — синхронизация с остановкой (как `01`).

## Полный круг (K3 §13)

```
write (событие, «top half»)  → schedule_work
   → work_fn (process context): uppercase, data_ready=1, wake_up
      → блокирующий read (wait_event) просыпается, отдаёт результат
      → poll/epoll (C2) на той же wq получает EPOLLIN
```

Так **реальное** устройство делает fd «читаемым»: прерывание/событие → bottom half →
`wake_up` → `read`/`epoll`. Здесь источник симулирован `write`, механика — настоящая.

## Проверка

QEMU: фоновый читатель блокируется на пустом устройстве (`dd bs=64 count=1` — один
`read()`), через секунду `echo -n "ping" > /dev/cppwake` планирует работу → `work_fn`
переводит в верхний регистр, выставляет готовность и будит читателя → тот
просыпается и получает `"PING"`. `dmesg` чист.
