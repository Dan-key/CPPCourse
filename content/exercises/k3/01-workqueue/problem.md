# Задание: отложить обработку из «прерывания» в workqueue

Главная идиома K3: обработчик прерывания (top half) должен быть **коротким** и
**не спать** — тяжёлую/сонную работу он **откладывает** в контекст, где спать
**можно**. Самый частый инструмент отложенной работы — **workqueue**: она выполняет
твою функцию в **process context** (поток ядра `kworker`), где разрешены `mutex`,
`copy_*_user`, `kmalloc(GFP_KERNEL)`, ожидание.

Здесь ты строишь этот паттерн на misc-устройстве `/dev/cppwq`: `write` играет роль
**источника события** (как top half прерывания — он только **планирует** работу), а
**workqueue** делает саму обработку (bottom half в process context). Так в реальном
драйвере IRQ-обработчик вызывает `schedule_work`, а тяжёлое преобразование/разбор
идёт в `kworker`.

## Что реализовать

Misc-устройство, где:
- **`write(data)`** — сохранить данные в буфер ядра и **запланировать** работу:
  `schedule_work(&work)` (не обрабатывать прямо здесь — это «top half»);
- **функция работы** (`work_fn`, выполняется в **process context**) — обработать
  данные: перевести в **верхний регистр** в буфер результата;
- **`read`** — дождаться завершения отложенной работы (`flush_work`) и отдать
  **результат**;
- **`exit`** — `cancel_work_sync(&work)` перед выгрузкой (синхронизация с остановкой).

## Скелет (заполни TODO)

```c
#include <linux/workqueue.h>

static struct work_struct work;
static char in_buf[BUF_SIZE];  static size_t in_len;
static char res_buf[BUF_SIZE]; static size_t res_len;
static DEFINE_MUTEX(lock);

static void work_fn(struct work_struct *w)         // ← process context: можно спать
{
    // TODO: под lock — перевести in_buf[0..in_len) в верхний регистр в res_buf; res_len = in_len.
}

static ssize_t wq_write(struct file *f, const char __user *u, size_t count, loff_t *ppos)
{
    // TODO: copy_from_user в in_buf (под lock); in_len = count;
    //       schedule_work(&work);   // ОТЛОЖИТЬ обработку (как IRQ top half)
    //       return count;
}

static ssize_t wq_read(struct file *f, char __user *u, size_t count, loff_t *ppos)
{
    // TODO: flush_work(&work);  // дождаться, пока отложенная работа доделает
    //       отдать res_buf с учётом *ppos (EOF при *ppos>=res_len), copy_to_user.
}

static int __init wq_init(void){
    // TODO: INIT_WORK(&work, work_fn); return misc_register(&cppwq);
}
static void __exit wq_exit(void){
    // TODO: misc_deregister(&cppwq); cancel_work_sync(&work);   // порядок: снять устройство, потом добить работу
}
```

## Ключевые API

- **`INIT_WORK(&work, work_fn)`** — связать `work_struct` с функцией; `work_fn`
  получает `struct work_struct *` (если нужна объемлющая структура — `container_of`,
  K1 §9).
- **`schedule_work(&work)`** — поставить работу в **системную** очередь (`system_wq`);
  ядро выполнит `work_fn` в `kworker` (process context) «когда дойдут руки». Вернёт
  `false`, если работа **уже** запланирована (не дублируется).
- **`flush_work(&work)`** — заблокироваться, пока конкретная работа не **завершится**
  (если запланирована/выполняется). В `read` — гарантия, что результат готов.
- **`cancel_work_sync(&work)`** — отменить запланированную работу **и дождаться**
  завершения уже бегущей. В `exit` — **обязательно**: иначе `kworker` может выполнить
  `work_fn` уже **после** выгрузки модуля → обращение к выгруженному коду/памяти
  (use-after-free, паника).

## Почему workqueue, а не tasklet/softirq

`work_fn` выполняется в **process context** — в нём можно `mutex_lock`,
`copy_*_user`, `kmalloc(GFP_KERNEL)`, спать. Tasklet/softirq выполняются в
**атомарном** контексте (спать нельзя), и их **вытесняют** (K3 §8). Для отложенной
работы, которой нужно копировать в userspace/спать, — **workqueue**. (Альтернатива —
**threaded IRQ**, K3 §11: ядро само заводит поток под обработчик.)

## Синхронизация с остановкой (ЭКСПЕРТ трека)

Самая частая ошибка отложенной работы — **выгрузить модуль / освободить память**,
пока работа ещё **в очереди**. Тогда `kworker` позже дёрнет `work_fn`, которой уже
нет, или тронет освобождённый буфер → паника. Поэтому в `exit` (и перед `kfree`
буфера, если он динамический): сначала прекрати **источник** (снять устройство /
`free_irq`), затем **`cancel_work_sync`** — она гарантирует, что после неё `work_fn`
**не выполнится**. Порядок: «закрыть кран → добить то, что в полёте → освободить».

## Проверка

Автопрогон (QEMU): `insmod cppmod.ko` → `/dev/cppwq`; `echo -n "hello" > /dev/cppwq`
(планирует работу), затем `cat` → `"HELLO"` (работа в `kworker` перевела в верхний
регистр — отложенная обработка сработала); перезапись `"Wq-42"` → `"WQ-42"`. Реализуй
`work_fn`/`write`/`read`/`init`/`exit` — все пройдут.
