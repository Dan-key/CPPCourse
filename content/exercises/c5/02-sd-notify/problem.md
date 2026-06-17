# Задание: протокол `sd_notify` — как сервис сообщает systemd о своём состоянии

`Type=notify` — лучший способ интеграции сервиса с systemd. В отличие от `simple`
(systemd считает сервис «готовым» сразу после `fork`/`exec`), сервис типа `notify`
**сам сообщает** «я готов» — и только тогда systemd считает запуск завершённым и
запускает зависящие юниты. Это убирает гонку «сервис ещё поднимает кэш/слушающий
сокет, а зависимые уже стартовали и получили отказ».

Сообщение — это **датаграмма** в `AF_UNIX`-сокет из `$NOTIFY_SOCKET`, тело —
несколько строк `KEY=VALUE`, разделённых `\n`. Здесь ты реализуешь **построение
тела** этих сообщений по канону systemd (`sd_notify(3)`).

## Что реализовать

```cpp
// Все функции пишут текст в buf (с завершающим '\0') и возвращают ДЛИНУ строки
// (без '\0'), или -1, если buf слишком мал.

int notify_ready(char* buf, int cap, const char* status);     // "READY=1\n"[+"STATUS=...\n"]
int notify_reloading(char* buf, int cap, unsigned long long mono_usec); // RELOADING + MONOTONIC_USEC
int notify_stopping(char* buf, int cap);                       // "STOPPING=1\n"
int notify_watchdog(char* buf, int cap);                       // "WATCHDOG=1\n"
```

## Точный формат (канон systemd)

- **`notify_ready`**: строка `"READY=1\n"`. Если `status` не `nullptr` и не пустой —
  добавить **второй** строкой `"STATUS=<status>\n"`. Пример:
  `notify_ready(buf, cap, "accepting connections")` →
  `"READY=1\nSTATUS=accepting connections\n"`.
- **`notify_reloading`**: `"RELOADING=1\nMONOTONIC_USEC=<mono_usec>\n"`. С systemd ≥
  v253 `RELOADING=1` **обязан** сопровождаться `MONOTONIC_USEC` (момент начала
  перезагрузки по `CLOCK_MONOTONIC` в микросекундах) — иначе systemd не дождётся
  завершения reload. Пример при `mono_usec=42` → `"RELOADING=1\nMONOTONIC_USEC=42\n"`.
- **`notify_stopping`**: ровно `"STOPPING=1\n"` — «я начал штатную остановку»
  (systemd не считает это сбоем и не рестартует).
- **`notify_watchdog`**: ровно `"WATCHDOG=1\n"` — «я жив» (см. упражнение
  `03-watchdog`).

Если в `buf` не помещается строка **с завершающим `'\0'`** — вернуть `-1` и не
портить память за границей (важно для ASan).

## Почему именно так

- **`READY=1`** закрывает гонку готовности: зависимые юниты (`After=`,
  `Requires=`) ждут именно его. С `Type=simple` они стартуют слишком рано.
- **`STATUS=…`** — человекочитаемая строка в `systemctl status` («accepting», «12
  active conns», «draining») — бесценно для эксплуатации.
- **`RELOADING`/`STOPPING`** дают systemd точную картину жизненного цикла: он знает,
  что ты **намеренно** перезагружаешься/останавливаешься, и не считает это падением.
- Протокол **текстовый и простой** именно чтобы его можно было реализовать без
  libsystemd (как ты сейчас и делаешь) — даже из shell (`systemd-notify`).

## C-эквивалент

Это тело того, что libsystemd шлёт в `sd_notify(0, "READY=1")` /
`sd_notify(0, "RELOADING=1\nMONOTONIC_USEC=...")`. Реальная функция ещё открывает
датаграммный сокет на `$NOTIFY_SOCKET` и делает `sendmsg`; здесь — самое важное и
тестируемое: **корректное тело**.

## Проверка

Автопрогон (ASan/UBSan): точные строки для ready (с и без status), reloading с
числом, stopping, watchdog; и возврат `-1` при недостаточном буфере без выхода за
границы. Реализуй четыре функции — все пройдут.
