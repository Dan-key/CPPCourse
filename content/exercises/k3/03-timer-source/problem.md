# Задание: таймер ядра как источник «прерываний» → workqueue

Реального прерывания в QEMU без железа не получить, но **таймер ядра**
(`timer_list`) — отличная **модель атомарного источника событий**: его колбэк бежит
в **softirq context** (атомарный, спать **нельзя** — ровно как top half обработчика
прерывания). Поэтому таймер делает **минимум** и **откладывает** тяжёлое в workqueue
(process context) — та самая разбивка «атомарный источник → process-context
обработчик», что в реальном драйвере соединяет IRQ и workqueue.

Здесь ты строишь `/dev/cpptimer`: таймер тикает каждые 50 мс, в **атомарном** колбэке
увеличивает счётчик тиков и **`schedule_work`** (не обрабатывает сам), а `work_fn` в
**process context** «обрабатывает» (переносит число тиков в результат под мьютексом).
`read` отдаёт `"ticks=<a> processed=<b>\n"`. При выгрузке — **сначала остановить
таймер** (`timer_delete_sync`), **потом** добить работу (`cancel_work_sync`): иначе
таймер перепланирует работу после её отмены → use-after-free.

## Что реализовать

- **`timer_fn`** (АТОМАРНЫЙ контекст — спать нельзя!) — `atomic_inc(&ticks)`,
  **`schedule_work(&work)`**, перевзвести таймер: `mod_timer(&mytimer, jiffies +
  msecs_to_jiffies(50))`;
- **`work_fn`** (process context) — под `lock` записать `processed = atomic_read(&ticks)`
  (здесь могла бы быть тяжёлая/сонная обработка);
- **`init`** — `timer_setup(&mytimer, timer_fn, 0)`, `INIT_WORK(&work, work_fn)`,
  запустить таймер `mod_timer(&mytimer, jiffies + msecs_to_jiffies(50))`,
  `misc_register`;
- **`exit`** — **`timer_delete_sync(&mytimer)`** → **`cancel_work_sync(&work)`** →
  `misc_deregister` (порядок: остановить источник, потом добить отложенную работу).

## Ключевые API

- **`timer_setup(&t, fn, flags)`** — связать таймер с колбэком `void fn(struct
  timer_list *)`. Колбэк **атомарный** — `mutex_lock`/`copy_*_user`/`kmalloc(GFP_KERNEL)`
  внутри запрещены (как в top half).
- **`mod_timer(&t, expires)`** — запустить/перевзвести таймер на `expires` (в jiffies).
- **`schedule_work(&work)`** — отложить обработку в process context (можно из
  атомарного колбэка — `schedule_work` не спит).
- **`timer_delete_sync(&t)`** — остановить таймер и дождаться завершения колбэка
  (бывш. `del_timer_sync`).
- **`cancel_work_sync(&work)`** — отменить + дождаться отложенной работы.

## Почему этот порядок остановки

Если сначала `cancel_work_sync`, а таймер ещё тикает — `timer_fn` сделает новый
`schedule_work` **после** отмены, и `kworker` дёрнет `work_fn` уже при выгруженном
модуле → use-after-free. Поэтому **сначала глушим источник** (`timer_delete_sync`),
**потом** добиваем то, что он успел запланировать (`cancel_work_sync`).

## Проверка

QEMU: `cat /dev/cpptimer` → `ticks=A processed=..`; `sleep 1`; снова `cat` →
`ticks=B ..` с **B > A** (таймер тикает) и **processed > 0** (работа выполнялась в
process context). `dmesg` чист (нет `scheduling while atomic` — было бы, если уснуть
в колбэке таймера). `rmmod` проходит (корректный порядок остановки).
