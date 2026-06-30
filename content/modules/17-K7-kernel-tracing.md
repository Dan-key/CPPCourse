# Модуль K7 — Трассировка из ядра

> Этап 2B, Сторона ядра. Метка трека: *(частично есть)*. Это финал ядрового
> этапа: ты учишься **видеть**, что ядро делает на самом деле, не вставляя
> `printk` и не пересобирая. Сначала — встроенный **ftrace** (трассировка функций
> ядра почти бесплатно), затем **статические tracepoints** и **динамические
> kprobes** (точка на ЛЮБОЙ функции ядра без перекомпиляции), потом —
> программируемая трассировка **eBPF/bpftrace** и готовые **bcc**-инструменты,
> **flame graph** ядра под нагрузкой. Закрываем тем, что делают, когда ядро уже
> упало: декодируем **oops/panic** (kallsyms, build-id, `addr2line`) и снимаем
> посмертный дамп через **kdump → vmcore → crash**.
>
> **Язык — C** (модули ядра) плюс много работы в `tracefs` и языке `bpftrace`.
> Всё, что ты освоил в K1–K6, здесь становится **наблюдаемым**: вместо «добавлю
> `printk` и пересоберу» — «повешу kprobe и сниму flame graph живого ядра».
>
> **Опирается на K1** (структура LKM, символы), **K2** (контексты: трейсер-хендлер
> бежит в atomic context — спать нельзя), **K3** (softirq/прерывания — что
> трассируем), **K5** (procfs/tracefs — интерфейс через файлы), **K6** (hot path —
> почему `printk` дорог), **Ф4** (perf, flame graph, core dump со стороны userspace
> — здесь обратная, ядровая сторона тех же инструментов).

**Читать к модулю:**

- Brendan Gregg, *BPF Performance Tools* (2019) — библия динамической трассировки:
  bpftrace, bcc, методология. Главы 1–4 (введение, ftrace, bcc, bpftrace), дальше
  как справочник по подсистемам.
- Brendan Gregg, *Systems Performance* (2nd ed.) — методологии (USE, off-CPU),
  flame graphs.
- Документация ядра: `Documentation/trace/` (`ftrace.rst`, `kprobes.rst`,
  `events.rst`, `tracepoints.rst`, `kprobetrace.rst`, `uprobetracer.rst`),
  `Documentation/admin-guide/kdump/kdump.rst`.
- Исходники: `kernel/trace/`, `include/linux/tracepoint.h`,
  `include/linux/kprobes.h`, `include/linux/ftrace.h`.
- man: `bpftrace(8)`, `perf-probe(1)`, `perf-trace(1)`, `trace-cmd(1)`, `crash(8)`.

---

## 0. Карта модуля

| Раздел | О чём | Зачем |
|--------|-------|-------|
| 1 | Зачем трассировка, а не `printk` | Наблюдаемость без пересборки и без флуда |
| 2 | ftrace: function и function_graph | Базовый трассировщик в самом ядре |
| 3 | Фильтры ftrace | Сузить до нужных функций/PID — иначе утонешь |
| 4 | Как устроен динамический ftrace | `__fentry__`/nop-патчинг — почему почти бесплатно |
| 5 | Tracepoints (статические точки) | Стабильные точки, на которых стоит весь tracing |
| 6 | kprobes/kretprobes | Точка на ЛЮБОЙ функции/возврате без перекомпиляции |
| 7 | Свой инструментарий из модуля | Зарегистрировать kprobe/обработчик в своём LKM |
| 8 | perf для ядра | `perf probe`/`record`/`trace` по символам ядра |
| 9 | eBPF: программируемая трассировка | Верификатор, maps, BTF/CO-RE — почему вытесняет |
| 10 | bpftrace | Язык однострочников: probes, агрегаты, `hist`, `stack` |
| 11 | bcc-инструменты | Готовые `funccount`/`funclatency`/`offcputime`/... |
| 12 | Flame graph ядра | Профиль стеков под нагрузкой |
| 13 | `trace_printk` vs `printk` | Дешёвый лог в ring buffer на hot path |
| 14 | Декодирование oops/panic | kallsyms, build-id, `addr2line`, `decode_stacktrace` |
| 15 | kdump → vmcore → crash | Посмертный дамп ядра: настроить ЗАРАНЕЕ |
| 16 | Overhead и безопасность | Какой трассировщик сколько стоит и что может уронить |
| 17 | Инструменты, галерея ошибок, чеклист | Сводка |
| 18–21 | Практика, банк, глоссарий, дальше | Закрепление |

**Время на модуль:** 35–50 часов.

**Что значит «освоено»:** ты инструментируешь **собственные** функции
ядра/драйвера через kprobe/ftrace, пишешь bpftrace-скрипт на kprobe своей функции,
снимаешь **flame graph** ядра под нагрузкой, умеешь декодировать стек `oops` без
исходников и знаешь, что нужно настроить **заранее**, чтобы получить `vmcore` после
паники.

---

## 1. Зачем трассировка ядра, а не `printk`

В K1–K6 единственным «отладчиком» был `printk`/`pr_info`. Он незаменим для редких
событий, но как инструмент **наблюдаемости** не масштабируется:

- **Дорог на hot path.** Как мы видели в K6 §15, `printk` на каждый пакет/каждый
  вызов кладёт throughput: форматирование + запись в кольцевой буфер + сериализация
  под локом консоли. На горячем пути ядра (планировщик, сеть, блочный слой) это
  неприемлемо.
- **Требует пересборки.** Чтобы добавить `printk` в чужую функцию ядра (или в свою,
  но в проде), нужно править исходник и пересобирать модуль/ядро. На живой системе
  это недоступно.
- **Флудит общий лог.** `dmesg` — общий ресурс; диагностический спам затирает важные
  сообщения и сам по себе меняет тайминги (heisenbug).
- **Не агрегирует.** «Сколько раз вызвана функция X», «гистограмма латентностей Y»,
  «кто чаще всех аллоцирует» — на `printk` это постобработка тоннами текста.

Трассировка решает это: **точки наблюдения внедряются динамически**, считают и
агрегируют **в ядре** с минимальным overhead, включаются и выключаются на лету.

### 1.1 Три поколения трассировщиков

```
ptrace (strace/ltrace)  →  статический ftrace/tracepoints  →  программируемый eBPF
   останавливает              почти бесплатные                верифицируемый байткод
   процесс на каждом          точки в ядре, агрегация         в ядре, maps, безопасность,
   событии (~10x)             в kernel-буфере                 произвольная логика
```

Каждый шаг — **ниже overhead** и **выше гибкость**. Ф4 показал верхушку со стороны
userspace (`perf`, `strace`, flame graph); K7 — изнутри ядра: как эти точки
устроены, как поставить свои и как читать результат.

### 1.2 Карта инструментов K7

| Хочу | Инструмент | Раздел |
|------|-----------|--------|
| Увидеть, какие функции ядра вызываются и их вложенность | `ftrace` (function_graph) | §2 |
| Точка на **своей**/любой функции без перекомпиляции | kprobe/kretprobe | §6–7 |
| Стабильное событие, на которое можно подписаться | tracepoint | §5 |
| Посчитать/агрегировать события с произвольной логикой | eBPF/bpftrace | §9–10 |
| Готовый ответ «латентность/счётчики/off-CPU» | bcc-инструменты | §11 |
| Где тратится CPU в ядре | flame graph (perf/profile) | §12 |
| Дешёвый лог на hot path | `trace_printk` | §13 |
| Почему ядро упало (без исходников) | kallsyms + `addr2line` | §14 |
| Полный посмертный дамп ядра | kdump/`crash` | §15 |

### 1.3 Контекст исполнения трейсера — спать нельзя (мост в K2)

Критично и повторяется весь модуль: обработчик kprobe, ftrace-callback,
eBPF-программа **исполняются в контексте той функции, которую трассируют**. Часто
это **атомарный контекст** (прерывание, softirq, удержан спинлок, выключено
вытеснение). Значит — **те же правила, что в K2 §2 и K6 §1.4**: нельзя `mutex_lock`,
`kmalloc(GFP_KERNEL)`, `msleep`, `copy_to_user`, любые блокирующие операции. Только
`GFP_ATOMIC`, атомики, per-CPU, `spin_lock` (если вообще нужен лок). eBPF снимает
часть этого риска **верификатором** (он просто не пропустит спящий вызов), а
kprobe-обработчик на C — целиком твоя ответственность.

---

## 2. ftrace — встроенный трассировщик функций

**ftrace** (function tracer) встроен в ядро (`CONFIG_FUNCTION_TRACER`,
`CONFIG_FUNCTION_GRAPH_TRACER`, `CONFIG_DYNAMIC_FTRACE`) и управляется **только
через файлы** в `tracefs` — никаких userspace-демонов. Это первый инструмент, когда
вопрос «что вообще происходит в ядре на этом пути».

### 2.1 tracefs — интерфейс через файловую систему

```sh
# Современный путь монтирования (старый — /sys/kernel/debug/tracing):
mount -t tracefs nodev /sys/kernel/tracing
cd /sys/kernel/tracing
ls
# available_tracers  current_tracer  trace  trace_pipe  set_ftrace_filter
# tracing_on  events/  set_graph_function  set_ftrace_pid  trace_options ...
```

Ключевые файлы:

| Файл | Назначение |
|------|-----------|
| `available_tracers` | какие трассировщики собраны (`function`, `function_graph`, `nop`, ...) |
| `current_tracer` | выбрать активный (запись имени включает) |
| `trace` | прочитать накопленный буфер (снимок) |
| `trace_pipe` | потоковое чтение (блокирующее, «вытягивает» события) |
| `tracing_on` | `1`/`0` — глобальный выключатель записи |
| `set_ftrace_filter` | ограничить трассировку перечнем функций (§3) |
| `set_graph_function` | для function_graph — корни графа |
| `set_ftrace_pid` | трассировать только указанные PID |
| `buffer_size_kb` | размер кольцевого буфера на CPU |
| `trace_options` | флаги вывода (`funcgraph-proc`, `latency-format`, ...) |

Принцип ftrace: **выбрал tracer → (опц.) сузил фильтром → включил `tracing_on` →
прочитал `trace`/`trace_pipe` → выключил**.

### 2.2 function tracer — плоский список вызовов

```sh
cd /sys/kernel/tracing
echo function > current_tracer        # включить
echo 1 > tracing_on
cat trace | head -20
echo 0 > tracing_on
echo nop > current_tracer             # выключить (вернуть пустой tracer)
```

Вывод — **плоский** поток: «какая задача на каком CPU вызвала какую функцию»:

```text
# tracer: function
#       TASK-PID     CPU#   TIMESTAMP  FUNCTION
           bash-1234  [001]  9012.345678: vfs_read <-ksys_read
           bash-1234  [001]  9012.345679: rw_verify_area <-vfs_read
           bash-1234  [001]  9012.345680: security_file_permission <-vfs_read
```

Колонка `FUNCTION <-caller` показывает функцию и **кто её вызвал**. Без фильтра это
**пожарный шланг** (миллионы строк/с) — почти всегда сразу нужен фильтр (§3).

### 2.3 function_graph — дерево вызовов с временем

```sh
echo function_graph > current_tracer
echo 1 > tracing_on ; sleep 0.01 ; echo 0 > tracing_on
cat trace
```

`function_graph` рисует **вложенность** вызовов и **длительность** каждого — это и
есть «где в ядре теряется время»:

```text
# CPU  DURATION                  FUNCTION CALLS
 1)               |  vfs_read() {
 1)               |    rw_verify_area() {
 1)   0.234 us    |      security_file_permission();
 1)   0.512 us    |    }
 1)               |    __vfs_read() {
 1) + 18.300 us   |      ext4_file_read_iter();   /* ← основная задержка */
 1)   0.110 us    |    }
 1) + 21.045 us   |  }
```

`+` — длительность > 10 мкс, `!` — > 100 мкс (быстро искать «толстые» вызовы).
`function_graph` — главный инструмент K7 для **латентностного** анализа пути в ядре.

### 2.4 function vs function_graph — когда что (вопрос трека)

| | `function` | `function_graph` |
|--|-----------|------------------|
| Вывод | плоский список «функция ← вызыватель» | дерево вызовов с отступами |
| Время | нет (только timestamp входа) | **длительность** каждой функции |
| Видно вложенность | нет | да (вход `{` и выход `}`) |
| Overhead | ниже (только вход) | выше (вход **и** выход каждой функции) |
| Когда | «вызывается ли X и кем», поток событий | «куда уходит время», структура пути |

Правило: **`function`** — когда нужен факт вызова и вызыватель (быстро, дёшево);
**`function_graph`** — когда нужна структура и латентность (дороже, но показывает,
*где* время). На горячем коде сначала суживай фильтром, потом включай graph.

### 2.5 trace-cmd и KernelShark — обёртки

Ручная работа с `tracefs` утомительна; `trace-cmd` автоматизирует запись/чтение, а
`KernelShark` даёт GUI:

```sh
trace-cmd record -p function_graph -g vfs_read ./trigger   # записать в trace.dat
trace-cmd report | less                                     # текстовый отчёт
kernelshark trace.dat                                       # GUI-таймлайн
```

`-p` — plugin (tracer), `-g` — graph-функция (корень), как `set_graph_function`.
Для повседневной работы `trace-cmd` удобнее голого `echo > tracefs`.

### 2.6 Latency-трейсеры: irqsoff, preemptoff, wakeup

Помимо `function`/`function_graph`, ftrace содержит **latency-трейсеры** — они
ищут не «что вызвано», а **максимальные задержки реакции системы**, критичные для
real-time (мост в K2/K3 — где и почему выключают прерывания/вытеснение):

| Трейсер | Что измеряет |
|---------|--------------|
| `irqsoff` | максимальный непрерывный участок с **выключенными прерываниями** |
| `preemptoff` | максимальный участок с **выключенным вытеснением** |
| `preemptirqsoff` | оба сразу |
| `wakeup` | задержка от пробуждения задачи до её запуска (для любой задачи) |
| `wakeup_rt` | то же, но только для **RT**-задач (планировочная латентность RT) |

```sh
echo 0 > tracing_on
echo irqsoff > current_tracer
echo 1 > tracing_on
# ... дать системе поработать / прогнать нагрузку ...
cat tracing_max_latency     # рекорд (мкс) за сессию
cat trace                   # ГДЕ именно: стек участка, державшего IRQ выключенными
```

latency-трейсер запоминает **самый худший** случай и его трассу: видно, какая
функция держала прерывания выключенными слишком долго. Это прямой инструмент для
«почему у нас джиттер/пропуски в RT-нагрузке» — то, ради чего в K2/K3 так осторожны
со `spin_lock_irqsave` и длинными секциями в atomic.

### 2.7 Кольцевой буфер: размер, snapshot, потеря событий

ftrace пишет в **per-CPU кольцевой буфер**. Если события идут быстрее, чем ты
читаешь, старые **затираются** (overrun) — частая причина «в трассе не то, что я
ждал»:

```sh
cat per_cpu/cpu0/stats         # entries, overrun, dropped — сколько потеряно
echo 16384 > buffer_size_kb    # увеличить буфер (на CPU) под бурсты
```

- **`trace` vs `trace_pipe`:** `trace` — **снимок** буфера (читать можно много раз,
  не «съедает»); `trace_pipe` — **поток** (блокирует и **вынимает** события, как
  очередь). Для «поймать и разобрать» — `trace`; для «следить вживую» — `trace_pipe`.
- **snapshot** — заморозить текущий буфер в отдельный, чтобы спокойно прочитать, пока
  основной продолжает писать:
  ```sh
  echo 1 > snapshot          # переключить буферы (swap)
  cat snapshot               # читать замороженный, trace продолжает копиться
  ```
- **overrun != баг трейсера**, а сигнал «фильтруй сильнее (§3) или увеличь буфер».
  На горячем коде агрегируй в ядре (hist/bpftrace), а не лей поток в буфер (§16.3).

---

## 3. Фильтры ftrace — иначе утонешь

Без фильтра ftrace трассирует **все** функции ядра — гигабайты в секунду. Фильтры
сужают до нужного.

### 3.1 Фильтр по функциям

```sh
# только эти функции (поддерживает glob):
echo vfs_read > set_ftrace_filter
echo ext4_* >> set_ftrace_filter        # добавить по маске
cat set_ftrace_filter                    # что сейчас в фильтре
echo > set_ftrace_filter                 # очистить (трассировать всё)

# ИСКЛЮЧИТЬ функции (notrace):
echo 'cpuidle_*' > set_ftrace_notrace
```

Какие функции вообще доступны для трассировки:

```sh
cat available_filter_functions | wc -l     # десятки тысяч
grep ext4_file_read available_filter_functions
```

Функции из `notrace`-списка (например, сам код трассировщика, некоторые
`noinstr`-секции) трассировать нельзя — их в `available_filter_functions` нет.

### 3.2 Фильтр по PID и графу

```sh
# только процесс с этим PID (и его дети, если установлен event-fork):
echo $$ > set_ftrace_pid

# для function_graph — с каких функций НАЧИНАТЬ дерево:
echo vfs_read > set_graph_function
# и докуда углубляться:
echo 3 > max_graph_depth
```

`set_graph_function` критичен: он делает граф читаемым — дерево строится **только**
от указанных корней, а не от всего подряд.

### 3.3 Триггеры на событиях

ftrace умеет **условные действия** на trace-событиях (§5): включить/выключить
трассировку, сделать стек-дамп, посчитать. Например, снять стек ядра каждый раз,
когда процесс открывает файл:

```sh
cd events/syscalls/sys_enter_openat
echo stacktrace > trigger        # к каждому событию добавить стек
echo 1 > enable
cat ../../../trace | head
echo '!stacktrace' > trigger     # снять триггер
```

Другие действия триггеров: `traceon`/`traceoff` (включить/выключить запись при
условии), `snapshot`, `hist` (гистограммы в ядре — §5.4).

---

## 4. Как устроен динамический ftrace (почему почти бесплатно)

Понимание механизма объясняет, почему ftrace можно держать в проде.

### 4.1 `__fentry__` / mcount и nop-патчинг

GCC с `-pg` (`CONFIG_FUNCTION_TRACER`) вставляет в **начало каждой** функции ядра
вызов `__fentry__` (раньше — `mcount`). Но постоянный `call __fentry__` в каждой
функции — это overhead **всегда**, даже когда трассировка выключена. Поэтому ядро
при загрузке **переписывает** все эти `call` в **`nop`** (пустую инструкцию):

```text
функция, трассировка ВЫКЛ:    функция, трассировка ВКЛ (для неё):
   foo:                          foo:
     nop  (5 байт)                 call __fentry__   ← пропатчено на лету
     push %rbp                     push %rbp
     ...                           ...
```

Когда ты добавляешь функцию в `set_ftrace_filter`, ядро **на лету** патчит её `nop`
обратно в `call` к трамплину трассировщика (через `text_poke`, с синхронизацией
всех CPU). Итог:

- трассировка **выключена** → в коде `nop` → стоимость ≈ ноль;
- трассировка **включена для конкретных функций** → только они платят за `call`;
- включение/выключение — динамическое, без перезагрузки.

Это и есть **`CONFIG_DYNAMIC_FTRACE`**: трассировать можно десятки тысяч функций, но
платят только активные. Поэтому ftrace включён в проде почти всех дистрибутивов.

### 4.2 Кто ещё стоит на `__fentry__`

Тот же механизм `fentry`/nop-патчинга — фундамент под:
- **function_graph** (трамплин ставит и на вход, и перехватывает возврат);
- **kprobes на входе функции** (оптимизированные kprobe используют ftrace, §6.4);
- **BPF trampolines** (`fentry`/`fexit`-программы eBPF — самый дешёвый способ
  прицепить BPF к функции ядра, §9);
- **live patching** (`klp`) — замена функций на лету.

То есть `__fentry__` — общая «розетка» в начале каждой функции ядра, в которую
втыкаются все современные трассировщики и патчеры.

### 4.3 Static keys — почему выключенный tracepoint ≈ бесплатен

Тот же приём «патчим инструкцию, а не проверяем флаг в рантайме» лежит под
**tracepoints** (§5) и под любым «включаемым» путём в ядре — это **static keys**
(`jump labels`). Наивно «включаемый» tracepoint был бы `if (tracepoint_enabled) {...}`
— ветвление на **каждом** вызове, даже когда трассировка выключена. Static key
убирает даже это ветвление:

```text
выключено:  ...; nop;  ...           ← на месте проверки стоит nop (ветка не берётся)
включено:   ...; jmp trace_block; ...  ← ядро ПЕРЕПИСАЛО nop в jmp при первом enable
```

При `echo 1 > .../enable` ядро через `text_poke` **переписывает** `nop` на `jmp` к
блоку трассировки (и обратно при выключении). Итог: выключенный tracepoint стоит ровно
один `nop` — отсюда и слова «почти бесплатно» (§5.5). Static keys — общий механизм
ядра для редко меняющихся условий на горячем пути (фичефлаги, шедулер-классы), и
наблюдаемость — его главный потребитель.

---

## 5. Tracepoints — статические точки трассировки

**Tracepoint** — это заранее расставленный разработчиками ядра **именованный
маркер** в коде («здесь произошло X с такими-то полями»). В отличие от kprobe (которую
ты ставишь на произвольный адрес), tracepoint — **часть ABI наблюдаемости**: его имя
и поля относительно стабильны между версиями.

### 5.1 Где они и как выглядят

```sh
cd /sys/kernel/tracing/events
ls                          # подсистемы: sched/ syscalls/ block/ net/ kmem/ ...
ls sched/                   # sched_switch sched_wakeup sched_process_exec ...
cat sched/sched_switch/format
```

`format` описывает поля события — их и печатает трассировщик:

```text
name: sched_switch
    field:char prev_comm[16];   offset:8;  size:16;
    field:pid_t prev_pid;       offset:24; size:4;
    field:int   prev_prio;      offset:28; size:4;
    field:char next_comm[16];   offset:36; size:16;
    field:pid_t next_pid;       offset:52; size:4;
print fmt: "prev_comm=%s prev_pid=%d ... next_comm=%s next_pid=%d", ...
```

### 5.2 Включить и читать tracepoint

```sh
cd /sys/kernel/tracing
echo 1 > events/sched/sched_switch/enable     # включить одно событие
echo 1 > events/syscalls/enable               # включить всю подсистему
echo 1 > tracing_on
cat trace_pipe | head
echo 0 > events/sched/sched_switch/enable
```

Фильтры на полях события (в ядре, до записи — дёшево):

```sh
echo 'prev_pid == 1234' > events/sched/sched_switch/filter
echo 'comm ~ "nginx*"' > events/syscalls/sys_enter_openat/filter
```

### 5.3 tracepoint vs kprobe (вопрос трека)

| | tracepoint | kprobe |
|--|-----------|--------|
| Кто создал | разработчик ядра (в исходнике) | ты, динамически, на любой адрес |
| Стабильность | **стабильны** (имя/поля — ~ABI) | **хрупки**: имя функции может исчезнуть/инлайниться |
| Семантика | осмысленные поля (`prev_pid`, ...) | сырые регистры/аргументы (`arg0` = pt_regs) |
| Покрытие | только там, где разработчик поставил | **любая** не-inline функция ядра |
| Overhead | минимальный (nop-патчинг) | чуть выше (kprobe) / как tracepoint (optimized) |
| Когда | есть готовый tracepoint на нужное событие | нужного tracepoint нет — лезешь kprobe |

Правило: **сначала ищи tracepoint** (стабильно, есть имена полей) — `ls events/`.
**Нет нужного — ставь kprobe** на конкретную функцию (мощно, но привязано к имени
функции, которое может поменяться между ядрами). bpftrace умеет оба (`tracepoint:` и
`kprobe:`).

### 5.4 Гистограммы в ядре (hist triggers)

ftrace умеет агрегировать события **в ядре**, без userspace — через hist-триггеры:

```sh
cd /sys/kernel/tracing/events/syscalls/sys_enter_read
# сколько read() по каждому процессу:
echo 'hist:key=common_pid:val=hitcount' > trigger
cat hist
# гистограмма размеров запрашиваемого чтения:
echo 'hist:key=count.log2' > trigger
```

Это «бесплатная аналитика» прямо в tracefs — предшественник того, что удобнее
делать в bpftrace (§10), но доступно без eBPF.

### 5.5 Свой tracepoint в модуле/драйвере

Лучший способ сделать **свой** код наблюдаемым на постоянной основе — не `printk`, а
**собственный tracepoint**: он почти бесплатен, когда выключен, и даёт стабильное
событие с именованными полями в `events/`. Объявляется макросом `TRACE_EVENT`
(обычно в отдельном заголовке `include/trace/events/<mod>.h`):

```c
/* trace/mydrv.h — описание события */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mydrv
TRACE_EVENT(mydrv_request,
    TP_PROTO(int id, unsigned int len),          /* прототип «сигнала» */
    TP_ARGS(id, len),                            /* имена аргументов */
    TP_STRUCT__entry(                            /* поля события (в кольце) */
        __field(int, id)
        __field(unsigned int, len)
    ),
    TP_fast_assign(                              /* как заполнить поля из аргументов */
        __entry->id  = id;
        __entry->len = len;
    ),
    TP_printk("id=%d len=%u", __entry->id, __entry->len)  /* как печатать */
);
```

```c
/* в .c-файле модуля: */
#define CREATE_TRACE_POINTS          /* ровно в ОДНОМ .c — материализует точки */
#include "trace/mydrv.h"

void handle_request(int id, unsigned int len)
{
    trace_mydrv_request(id, len);    /* «выстрел» — почти ноль, когда никто не слушает */
    /* ... обработка ... */
}
```

После загрузки модуля событие появляется само:

```sh
cat /sys/kernel/tracing/events/mydrv/mydrv_request/format
echo 1 > /sys/kernel/tracing/events/mydrv/mydrv_request/enable
echo 'len > 1024' > /sys/kernel/tracing/events/mydrv/mydrv_request/filter
cat /sys/kernel/tracing/trace_pipe
```

Почему это лучше kprobe на своей функции: tracepoint **стабилен** (не зависит от
того, заинлайнилась ли функция), несёт **осмысленные поля** (а не сырые регистры) и
к нему может подцепиться и ftrace-фильтр, и **bpftrace** (`tracepoint:mydrv:...`), и
eBPF. Цена выстрела `trace_mydrv_request()` при выключенном событии ≈ один проверочный
`nop` (тот же механизм статических ключей, что у §4). Это «правильный `printk`» для
постоянной наблюдаемости своего драйвера.

### 5.6 Самые полезные готовые tracepoints

Прежде чем ставить kprobe, проверь, нет ли готового события — вот часто нужные:

| Подсистема | События | Для чего |
|-----------|---------|----------|
| `sched` | `sched_switch`, `sched_wakeup`, `sched_process_exec/exit` | планировщик, off-CPU, кто что запускает |
| `syscalls` | `sys_enter_*`, `sys_exit_*` | вход/выход любого syscall с аргументами |
| `irq` | `irq_handler_entry/exit`, `softirq_*` | прерывания/softirq (K3) |
| `block` | `block_rq_issue`, `block_rq_complete` | латентность блочного I/O |
| `net` | `net_dev_xmit`, `netif_receive_skb` | путь пакета (K6) |
| `kmem` | `kmalloc`, `kfree`, `mm_page_alloc` | аллокации (K4) |
| `workqueue`, `timer`, `signal` | `*_entry/exit` | отложенная работа, таймеры (K3) |

```sh
ls /sys/kernel/tracing/events/         # все подсистемы на ЭТОМ ядре
grep -rl mydrv /sys/kernel/tracing/events/ 2>/dev/null   # есть ли событие по имени
```

**Raw tracepoints** (`raw_tracepoint`/`BPF_RAW_TRACEPOINT` в eBPF) — доступ к тем же
точкам, но **без** разбора в текстовый формат: программа получает сырые аргументы
быстрее (нет форматирования). Для bpftrace это `rawtracepoint:`; на горячих событиях
(`sched_switch` идёт десятки тысяч раз/с) разница ощутима. Правило прежнее: **есть
tracepoint — бери его** (стабилен, дешёв, осмыслен), kprobe — только когда готового
события нет (§5.3).

---

## 6. kprobes и kretprobes — точка на любой функции

**kprobe** — механизм поставить **обработчик на произвольную инструкцию** ядра
(чаще всего — на вход функции) **без перекомпиляции**. Это основной инструмент,
когда нужного tracepoint нет.

### 6.1 Что такое kprobe

kprobe сохраняет оригинальную инструкцию по целевому адресу и заменяет её на
**breakpoint** (`int3` на x86). Когда исполнение доходит туда:

1. срабатывает breakpoint → ядро отдаёт управление **pre_handler** твоей kprobe;
2. исполняется оригинальная (сохранённая) инструкция в режиме single-step;
3. вызывается **post_handler** (после инструкции);
4. исполнение продолжается.

```text
        до:                      kprobe armed:
  do_sys_openat2:            do_sys_openat2:
     push %rbp                  int3        ← breakpoint, отдаёт в pre_handler
     mov  ...                   mov  ...     (оригинал исполнится в single-step)
```

### 6.2 kretprobe — перехват ВОЗВРАТА

Часто нужно не «функция вызвана», а «функция **вернулась** и вот результат/сколько
длилась». **kretprobe** подменяет адрес возврата на трамплин: ставит свой обработчик
**на выходе** функции.

- `entry_handler` — на входе (можно сохранить аргументы/время в `ri->data`);
- `handler` — на возврате (доступен код возврата через `regs_return_value(regs)` и
  сохранённые в `entry_handler` данные).

Это канонический способ мерить **латентность** функции ядра: время на входе, дельта
на выходе.

### 6.3 kprobe vs uprobe

- **kprobe/kretprobe** — точки в **ядре** (функции ядра/драйвера).
- **uprobe/uretprobe** — точки в **userspace** (функции в бинарнике/`.so`), но
  ставятся и обрабатываются ядром (Ф4 §6.6 `perf probe -x`). bpftrace умеет оба:
  `kprobe:vfs_read` и `uprobe:/lib/libc.so.6:malloc`.

### 6.4 Оптимизированные kprobes (почему быстро)

`int3`-breakpoint — это исключение, оно дорого. Поэтому ядро по возможности
**оптимизирует** kprobe: если kprobe стоит на входе ftrace-функции, она использует
**ftrace** (`__fentry__`, §4) вместо `int3` — почти бесплатно. А отдельные kprobe в
середине функции оптимизируются заменой `int3` на `jmp` к трамплину (jump
optimization, `CONFIG_OPTPROBES`). Поэтому современная kprobe на входе функции по
стоимости близка к tracepoint.

### 6.5 Ограничения kprobe

- **Нельзя** ставить на функции из `notrace`/`__kprobes` blacklist (сам код
  kprobes, обработчики исключений) — рекурсия/краш. Список: `kprobes/blacklist` в
  tracefs.
- **Inline-функции** не имеют адреса — на них kprobe не поставить (только на место,
  куда они заинлайнены, по имени внешней функции).
- Имя функции — **не ABI**: между версиями ядра функция может быть переименована,
  удалена, заинлайнена → kprobe «отвалится» (вот почему tracepoint предпочтительнее,
  когда есть).
- Обработчик — **атомарный контекст** (§1.3): спать нельзя.

### 6.6 kprobe изнутри: рекурсия, blacklist, пропуски

Несколько тонкостей, объясняющих странности и краши:

- **Рекурсия и `nmissed`.** Если probe срабатывает, пока её обработчик уже
  выполняется на том же CPU (реентерабельность), kprobes **не** вызывает обработчик
  повторно, а инкрементирует `kp.nmissed`. Большой `nmissed` = ты повесился на
  слишком горячую/реентерабельную функцию.
- **Blacklist.** Функции, помеченные `NOKPROBE_SYMBOL()`/`__kprobes` или живущие в
  `noinstr`-секциях (вход в исключения, NMI, сам код kprobes), **нельзя**
  трассировать — иначе бесконечная рекурсия/краш при обработке breakpoint. Список:
  ```sh
  cat /sys/kernel/tracing/../debug/kprobes/blacklist   # или kprobes/blacklist в tracefs
  ```
- **`fault_handler`.** kprobe может задать обработчик ошибок памяти, возникших
  **внутри** её pre/post-handler (например, при разыменовании кривого аргумента) —
  способ не уронить ядро на своей же ошибке.
- **Где взять адрес.** `register_kprobe` резолвит `symbol_name` через kallsyms; можно
  задать `.offset` (probe в середине функции) или прямой `.addr` (но это хрупко).

Эти детали — причина, по которой **eBPF предпочтительнее** для трассировки (§9):
верификатор и инфраструктура BPF снимают целый класс этих граблей, которые в
самописном kprobe-модуле целиком на тебе.

---

## 7. Свой инструментарий из модуля ядра

Теперь — как поставить kprobe **из своего LKM** (основа упражнений K7).

### 7.1 Регистрация kprobe

```c
#include <linux/module.h>
#include <linux/kprobes.h>

static int my_pre(struct kprobe *p, struct pt_regs *regs)
{
    /* АТОМАРНЫЙ контекст: спать нельзя, только atomic/per-CPU (§1.3). */
    pr_info("k7: %s вызвана\n", p->symbol_name);
    return 0;   /* 0 = продолжить как обычно */
}

static struct kprobe kp = {
    .symbol_name = "do_sys_openat2",   /* функция, на которую вешаемся */
    .pre_handler = my_pre,
};

static int __init m_init(void)
{
    int ret = register_kprobe(&kp);    /* поставить probe */
    if (ret < 0) {
        pr_err("k7: register_kprobe failed: %d\n", ret);
        return ret;
    }
    pr_info("k7: kprobe на %s @ %p\n", kp.symbol_name, kp.addr);
    return 0;
}
static void __exit m_exit(void)
{
    unregister_kprobe(&kp);            /* снять — ОБЯЗАТЕЛЬНО перед выгрузкой */
}
module_init(m_init);
module_exit(m_exit);
MODULE_LICENSE("GPL");
```

- `register_kprobe` находит адрес по `symbol_name` (через kallsyms), сохраняет
  оригинальную инструкцию и «вооружает» probe.
- `unregister_kprobe` в `exit` **обязателен**: оставишь probe → после выгрузки кода
  модуля breakpoint прыгнет в освобождённую память → oops.
- Возврат `pre_handler` `0` — продолжить нормально (ненулевой используется в особых
  сценариях, например, когда probe сама «эмулирует» функцию).

### 7.2 Чтение аргументов функции из probe

Аргументы лежат в регистрах согласно ABI (Ф2 §1.1). Переносимо их достаёт
`regs_get_kernel_argument`:

```c
static int my_pre(struct kprobe *p, struct pt_regs *regs)
{
    /* 1-й аргумент do_sys_openat2(int dfd, ...) — dfd: */
    unsigned long dfd = regs_get_kernel_argument(regs, 0);
    if ((int)dfd == AT_FDCWD)
        this_cpu_inc(...);   /* per-CPU счётчик (K2 §9) — на hot path без локов */
    return 0;
}
```

`regs_get_kernel_argument(regs, n)` — n-й аргумент функции (абстрагирует
архитектурный ABI). Это безопаснее ручного `regs->di`/`regs->si`. Для x86-специфики
можно `regs->di` (1-й аргумент SysV ABI), но переносимый код — через хелпер.

### 7.3 kretprobe из модуля — латентность функции

```c
#include <linux/kprobes.h>
#include <linux/ktime.h>

struct lat_data { ktime_t t0; };       /* что сохраняем между входом и выходом */

static int ent(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct lat_data *d = (struct lat_data *)ri->data;
    d->t0 = ktime_get();               /* засечь время входа */
    return 0;
}
static int ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct lat_data *d = (struct lat_data *)ri->data;
    s64 ns = ktime_to_ns(ktime_sub(ktime_get(), d->t0));
    long rv = regs_return_value(regs); /* код возврата функции */
    /* агрегировать ns (per-CPU max/sum), rv — успех/ошибка */
    return 0;
}

static struct kretprobe krp = {
    .kp.symbol_name = "do_sys_openat2",
    .entry_handler  = ent,
    .handler        = ret,
    .data_size      = sizeof(struct lat_data),
    .maxactive      = 64,              /* сколько одновременных «в полёте» вызовов */
};
/* register_kretprobe(&krp) / unregister_kretprobe(&krp) */
```

- `data_size` — сколько байт `ri->data` ядро резервирует под твои данные на **каждый
  одновременный** вызов (вход↔выход).
- `maxactive` — сколько вызовов функции могут быть «в полёте» параллельно (на разных
  CPU/рекурсивно). Превышение → `nmissed` (пропуски), смотри `krp.nmissed`.
- `regs_return_value(regs)` — то, что функция вернула.

### 7.4 Дисциплина обработчика (повтор, но критично)

Обработчик kprobe/kretprobe — **атомарный контекст**:
- нельзя спать (`mutex`, `GFP_KERNEL`, `msleep`, `copy_to_user`);
- считай в **per-CPU**/атомики (K2 §9), отдавай агрегат через `/proc`/`debugfs`
  (K5) на **редком** чтении — ровно паттерн K6 §15.2;
- не ставь kprobe на функции, которые сам обработчик может вызвать → рекурсия
  (kprobes частично защищают, но не злоупотребляй).

### 7.5 kprobe без модуля: kprobe-events через tracefs

Часто свой модуль не нужен — ту же kprobe можно поставить **из userspace** через
`tracefs`, описав её строкой (это то, что под капотом делает `perf probe`/bpftrace):

```sh
cd /sys/kernel/tracing
# поставить kprobe на do_sys_openat2, вытащить dfd (1-й арг) и filename (2-й):
echo 'p:myopen do_sys_openat2 dfd=%di filename=+0(%si):string' > kprobe_events
echo 1 > events/kprobes/myopen/enable
cat trace_pipe | head
echo 0 > events/kprobes/myopen/enable
echo > kprobe_events                # снять все динамические kprobe-события
```

`p:` — probe на входе (`r:` — kretprobe). `%di`/`%si` — регистры (аргументы по ABI);
`+0(%si):string` — разыменовать как строку. Это «kprobe для бедных» без C-модуля —
но для агрегации/логики всё равно удобнее bpftrace (§10) или свой модуль (§7), когда
нужна нетривиальная обработка.

### 7.6 Полный рабочий модуль: kprobe + per-CPU + procfs

Соберём §6–§7 и дисциплину K6 §15.2 в законченный модуль — счётчик вызовов функции
(символ — параметр), накапливаемый **per-CPU** на hot path и отдаваемый агрегатом
через `/proc`:

```c
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/percpu.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static char *symbol = "do_sys_openat2";       // параметр: какую функцию считать
module_param(symbol, charp, 0444);
MODULE_PARM_DESC(symbol, "kernel symbol to probe");

static DEFINE_PER_CPU(u64, hits);             // по экземпляру на ядро (без локов)

static int pre(struct kprobe *p, struct pt_regs *regs)
{
    this_cpu_inc(hits);                       // АТОМАРНЫЙ контекст — только per-CPU/atomic
    return 0;
}
static struct kprobe kp = { .pre_handler = pre };

static int show(struct seq_file *m, void *v)  // /proc read — РЕДКО: агрегируем
{
    u64 total = 0; int cpu;
    for_each_possible_cpu(cpu)
        total += per_cpu(hits, cpu);
    seq_printf(m, "%s hits=%llu\n", kp.symbol_name, (unsigned long long)total);
    return 0;
}
static int open(struct inode *i, struct file *f) { return single_open(f, show, NULL); }
static const struct proc_ops pops = {
    .proc_open = open, .proc_read = seq_read,
    .proc_lseek = seq_lseek, .proc_release = single_release,
};
static struct proc_dir_entry *ent;

static int __init m_init(void)
{
    int ret;
    kp.symbol_name = symbol;
    ent = proc_create("k7_kprobe", 0444, NULL, &pops);   // procfs ДО probe
    if (!ent)
        return -ENOMEM;
    ret = register_kprobe(&kp);                          // probe — последним
    if (ret) {
        proc_remove(ent);
        return ret;
    }
    pr_info("k7: probe на %s\n", kp.symbol_name);
    return 0;
}
static void __exit m_exit(void)
{
    unregister_kprobe(&kp);     // 1. снять probe (новых срабатываний не будет)
    proc_remove(ent);           // 2. убрать procfs
}
module_init(m_init);
module_exit(m_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K7: kprobe counter");
```

Здесь сошлось всё: безопасная регистрация/снятие (§7.1), per-CPU без локов на hot
path (K2 §9, K6 §15.2), агрегат на редком чтении `/proc` (K5), параметр-символ
(можно нацелить на свою функцию драйвера). Это каркас упражнения `01-kprobe-count`.
Меняешь цель: `insmod cppmod.ko symbol=vfs_read`.

---

## 8. perf для ядра

`perf` (Ф4 §6) умеет не только профилировать userspace, но и **видеть ядро**: его
символы, tracepoints, kprobes. На стороне ядра это рабочая лошадь между «голым
ftrace» и «программируемым eBPF».

### 8.1 perf probe — динамические точки

`perf probe` создаёт kprobe/uprobe «по-человечески», по имени функции и переменной
(использует DWARF ядра, если есть `vmlinux`/debuginfo):

```sh
perf probe --add 'do_sys_openat2 dfd filename'   # kprobe + захват переменных
perf probe --add 'do_sys_openat2%return $retval'  # kretprobe + код возврата
perf record -e probe:do_sys_openat2 -aR sleep 5   # записать срабатывания по системе
perf script                                        # вывести события со значениями
perf probe --del '*'                               # снять все
```

Это альтернатива ручному `kprobe_events` (§7.5), но с разрешением имён переменных
через debuginfo. `perf probe -L do_sys_openat2` покажет, какие строки/переменные
доступны для захвата.

### 8.2 perf record по символам ядра

```sh
perf record -a -g -e cycles -- sleep 10     # сэмплировать всю систему со стеками
perf report                                  # ядро и userspace в одном профиле
perf annotate do_sys_openat2                 # горячие инструкции функции ядра
```

`-a` — вся система (включая ядро), `-g` — стеки (нужны для flame graph, §12). Чтобы
символы ядра разрешались, нужен доступ к `/proc/kallsyms` (см.
`kernel.perf_event_paranoid`, §16) и, для аннотации, `vmlinux` с символами.

### 8.3 perf trace — strace ядра с малым overhead

```sh
perf trace -a sleep 5                  # syscalls всей системы (как strace, но дёшево)
perf trace -e 'openat*,read,write' ./prog
perf trace --call-graph dwarf -p $PID  # со стеками
```

`perf trace` использует tracepoints `raw_syscalls`/`syscalls` (не ptrace), поэтому
не останавливает процесс на каждом вызове — пригоден под нагрузкой, в отличие от
`strace` (Ф4 §4).

### 8.4 Специализированные perf-подкоманды

perf поверх tracepoints ядра даёт готовые «срезы» по подсистемам:

```sh
perf sched record -- sleep 5 ; perf sched latency   # задержки планировщика по задачам
perf sched timehist                                  # таймлайн переключений контекста
perf lock record -- ./load ; perf lock report        # контеншн локов ядра (где ждут)
perf kmem record -- ./load ; perf kmem stat --caller # кто аллоцирует в slab/page alloc
perf mem record ./load ; perf mem report             # уровни доступа к памяти (L1/LLC/RAM)
perf c2c record ./load ; perf c2c report             # false sharing / cache-line bouncing
```

- **`perf sched`** — почему задача «не бежит»: задержка от готовности до запуска,
  кто её вытеснил (планировочная латентность, мост в RT и §2.6 wakeup-трейсер).
- **`perf lock`** — где в ядре конкуренция за локи (дополняет flame graph, где виден
  `queued_spin_lock_slowpath`).
- **`perf kmem`** — аллокации ядра по вызывающему: утечки/фрагментация slab (K4).
- **`perf mem`/`perf c2c`** — те же, что в Ф4 §17, но видят и ядро: NUMA-доступы и
  false sharing в ядровых структурах.

Это «второй эшелон» после flame graph: профиль показал *где* горячо, эти подкоманды
объясняют *почему* (лок? планировщик? аллокатор? память?).

---

## 9. eBPF — программируемая трассировка

**eBPF** — виртуальная машина в ядре: ты пишешь маленькую программу, ядро
**верифицирует** её (доказывает, что она завершается и не лезет, куда нельзя),
**JIT-компилирует** в нативный код и цепляет к точке (kprobe/tracepoint/perf-event).
Это современная замена связке ftrace+kprobe-обработчик, и именно eBPF делает
трассировку **безопасной** и **программируемой**.

### 9.1 Почему eBPF лучше старых трейсеров (вопрос трека)

| Свойство | ftrace/kprobe-модуль | eBPF |
|----------|----------------------|------|
| **Безопасность** | C-обработчик может уронить ядро (oops, рекурсия, sleep в atomic) | **верификатор** отвергает небезопасную программу ДО загрузки |
| **Программируемость** | фиксированная логика трассировщика | произвольная логика (фильтры, агрегация) в самой точке |
| **Состояние** | per-CPU/глобалы вручную, свой `/proc` | **maps** (хэши/массивы/гистограммы) из коробки, доступны userspace |
| **Overhead** | низкий, но `int3`/копии | JIT-нативный код; `fentry`-программы почти бесплатны |
| **Доставка данных** | свой интерфейс (tracefs/proc) | ring buffer / perf buffer / maps — стандартно |
| **Переносимость** | модуль под конкретную версию | **CO-RE** (Compile Once–Run Everywhere) через BTF |
| **Без перекомпиляции ядра** | модуль грузить надо | программа грузится в работающее ядро |

Ключевое: **верификатор**. Он статически доказывает, что программа завершается (нет
бесконечных циклов — раньше вообще запрещали циклы, теперь bounded loops), не
разыменует непроверенный указатель, не спит в atomic, не лезет за границы. Поэтому
eBPF можно дать в прод и даже непривилегированным сценариям — она **не может**
уронить ядро так, как может кривой kprobe-модуль.

### 9.2 eBPF maps и доставка данных

Программа eBPF **без состояния между вызовами** — состояние держат **maps**:

| Тип map | Для чего |
|---------|----------|
| `BPF_MAP_TYPE_HASH` | ключ→значение (напр. PID→счётчик) |
| `BPF_MAP_TYPE_PERCPU_ARRAY` | per-CPU счётчики без контеншна (C1/K2!) |
| `BPF_MAP_TYPE_LRU_HASH` | хэш с вытеснением (ограниченная память) |
| `BPF_MAP_TYPE_STACK_TRACE` | стеки для flame graph |
| `BPF_MAP_TYPE_RINGBUF` | поток событий в userspace (заменил perf buffer) |

userspace читает maps через `bpf()` syscall / `bpftool` / libbpf. Так данные из
ядра попадают в инструмент без своего `/proc`.

### 9.3 BTF и CO-RE — переносимость

Проблема старых BPF-инструментов (bcc): они тащили clang/LLVM и **компилировали
программу на целевой машине** под её заголовки ядра — тяжело и хрупко. **BTF** (BPF
Type Format — компактные типы ядра, встроенные в `vmlinux`) + **CO-RE** позволяют
скомпилировать программу **один раз**, а libbpf на загрузке подставит правильные
смещения полей под конкретное ядро. Итог: один маленький бинарь работает на разных
ядрах без clang на целевой машине — основа продакшн-инструментов на libbpf.

### 9.4 Где eBPF цепляется

- **kprobe/kretprobe**, **uprobe** — на функции (как §6, но логика в BPF);
- **tracepoint**, **raw_tracepoint** — на статические точки (§5);
- **fentry/fexit** — самый дешёвый хук на вход/выход функции через BPF trampoline
  (§4.2), с типизированным доступом к аргументам;
- **perf_event** — на сэмплирование/PMU (профилирование, §12);
- плюс не-трассировочные (XDP/tc из K6, LSM, cgroup) — но в K7 нас интересует
  трассировка.

Писать «голый» eBPF на C+libbpf — это разработка инструмента (мост в спец. треки).
Для **повседневной** трассировки есть `bpftrace` (§10) и готовые `bcc` (§11), где
всё это доступно без ручного BPF.

### 9.5 Как выглядит свой инструмент на libbpf/CO-RE

Чтобы понимать, что bpftrace/bcc делают под капотом — минимальный «свой
opensnoop». BPF-программа (компилируется clang в `.bpf.o`, **один раз**):

```c
// trace.bpf.c — выполняется В ЯДРЕ на tracepoint
#include <vmlinux.h>                 // все типы ядра из BTF (CO-RE)
#include <bpf/bpf_helpers.h>

struct { __uint(type, BPF_MAP_TYPE_RINGBUF); __uint(max_entries, 1<<20); } rb SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_openat")
int on_openat(struct trace_event_raw_sys_enter *ctx)
{
    char *e = bpf_ringbuf_reserve(&rb, 256, 0);    // взять место в кольце
    if (!e) return 0;                               // верификатор требует проверки!
    bpf_probe_read_user_str(e, 256, (void *)ctx->args[1]);  // путь из userspace
    bpf_ringbuf_submit(e, 0);                       // отдать в userspace
    return 0;
}
char LICENSE[] SEC("license") = "GPL";
```

userspace-загрузчик (libbpf) грузит `.bpf.o`, цепляет программу и читает ring buffer
— всё на C, без clang на целевой машине (CO-RE подставит смещения через BTF, §9.3).
Сравни с §13.6 K6 (XDP): тот же стиль — ограниченный C, обязательные проверки границ,
`SEC()`-секции, верификатор. **Когда это нужно:** продакшн-инструмент с минимальным
footprint и переносимостью между ядрами; для ad-hoc вопросов — bpftrace (§10).

---

## 10. bpftrace — язык однострочников

**bpftrace** — высокоуровневый язык поверх eBPF: ты пишешь короткий скрипт «на какой
точке — что сделать», bpftrace компилирует его в BPF, грузит, собирает агрегаты.
Это главный инструмент ad-hoc трассировки.

### 10.1 Анатомия скрипта

```text
probe /filter/ { action }
```

```sh
# на входе в openat: имя процесса и путь
bpftrace -e 'tracepoint:syscalls:sys_enter_openat {
    printf("%s -> %s\n", comm, str(args->filename)); }'

# сколько раз вызвана vfs_read по процессам (kprobe + агрегат):
bpftrace -e 'kprobe:vfs_read { @[comm] = count(); }'

# гистограмма латентности vfs_read (kprobe + kretprobe + map):
bpftrace -e '
kprobe:vfs_read { @t[tid] = nsecs; }
kretprobe:vfs_read /@t[tid]/ {
    @ns = hist(nsecs - @t[tid]); delete(@t[tid]); }'
```

Элементы:
- `comm` — имя процесса, `pid`/`tid`, `nsecs` — время, `args->` — поля
  tracepoint/аргументы, `arg0..argN` — аргументы kprobe, `retval` — возврат
  kretprobe;
- `@name[key] = agg()` — **map** с агрегатом: `count()`, `sum()`, `avg()`, `hist()`,
  `lhist()` (линейная), `stats()`;
- `/filter/` — условие (как `if` перед действием);
- `str()`, `kstack`/`ustack` (стеки ядра/userspace), `printf` — хелперы.

### 10.2 Типы probe в bpftrace

| Probe | Что цепляет |
|-------|-------------|
| `kprobe:func` / `kretprobe:func` | вход/возврат функции ядра (§6) |
| `uprobe:/bin/app:func` / `uretprobe:` | функция userspace |
| `tracepoint:subsys:event` | статический tracepoint (§5) — **предпочтительно** |
| `fentry:func` / `fexit:func` | дешёвый BPF-трамплин на функцию (§4.2) |
| `profile:hz:99` | сэмплирование стеков 99 раз/с (для flame graph, §12) |
| `interval:s:1` | периодически (для печати агрегатов раз в секунду) |
| `software:`/`hardware:` | perf-события (page-faults, cache-misses) |

### 10.3 Канонические однострочники

```sh
# off-CPU: где задачи СПЯТ и сколько (стек в момент ухода с CPU):
bpftrace -e 'kprobe:finish_task_switch { @[kstack] = count(); }'

# кто порождает процессы:
bpftrace -e 'tracepoint:sched:sched_process_exec { printf("%s\n", comm); }'

# латентность блочного I/O, гистограмма:
bpftrace -e '
tracepoint:block:block_rq_issue { @s[args->dev] = nsecs; }
tracepoint:block:block_rq_complete /@s[args->dev]/ {
    @us = hist((nsecs - @s[args->dev]) / 1000); delete(@s[args->dev]); }'

# счётчик срабатываний СВОЕЙ функции драйвера раз в секунду:
bpftrace -e 'kprobe:my_driver_func { @c = count(); }
             interval:s:1 { print(@c); clear(@c); }'
```

Агрегаты печатаются при `Ctrl-C` (или явным `print`). Это и есть «bpftrace-скрипт на
kprobe своей функции» из задания трека — добавь к нему `profile:hz:99 { @[kstack] =
count(); }` и получишь данные для flame graph ядра (§12).

### 10.4 Встроенные переменные и worked-скрипт

Главные builtins bpftrace (доступны в действиях):

| Builtin | Значение |
|---------|----------|
| `pid`, `tid` | PID/TID текущей задачи |
| `comm` | имя процесса (16 байт) |
| `nsecs` | монотонное время в нс |
| `cpu` | номер CPU |
| `arg0..argN` | аргументы для `kprobe` (сырые) |
| `args->field` | именованные поля для `tracepoint`/`fentry` |
| `retval` | возврат для `kretprobe`/`fexit` |
| `kstack` / `ustack` | стек ядра / userspace (для агрегации по стеку) |
| `curtask` | указатель на `task_struct` (с CO-RE — поля по имени) |

Полноценный скрипт (файл `.bt`) с `BEGIN`/`END` и периодической печатью —
«мини-инструмент»: латентность своей функции драйвера, гистограмма + топ вызывающих
стеков, раз в секунду:

```text
#!/usr/bin/env bpftrace
BEGIN { printf("tracing my_driver_func... Ctrl-C to stop\n"); }

kprobe:my_driver_func { @start[tid] = nsecs; }

kretprobe:my_driver_func /@start[tid]/ {
    $dt = nsecs - @start[tid];
    @ns = hist($dt);                 // гистограмма латентности
    @by_comm[comm] = count();        // кто вызывает
    @slow[kstack] = max($dt);        // самый медленный стек
    delete(@start[tid]);
}

interval:s:1 { print(@by_comm); clear(@by_comm); }   // топ за секунду
END { clear(@start); }               // не печатать служебный map
```

Разбор: `@start[tid]` — scratch-map «время входа по треду» (ключ `tid`, чтобы
параллельные вызовы на разных ядрах не путались — это решение той же проблемы, что
`maxactive` у kretprobe, §7.3); `/@start[tid]/` — фильтр «вход был записан»; `hist()`
агрегирует в ядре (дёшево, не поток событий — §16.3); `clear` в `END` прячет
служебный map из вывода. Этот шаблон — прямой ответ на задание трека и на §18.2.

### 10.5 bpftrace на практике: файлы, нацеливание, безопасность

```sh
bpftrace lat.bt                       # запустить скрипт из файла
bpftrace -l 'tracepoint:syscalls:*'   # СПИСОК доступных probe по маске (что вообще есть)
bpftrace -l 'kprobe:vfs_*'            # какие функции ядра можно зацепить
bpftrace -p $PID prog.bt              # ограничить трассировку процессом
bpftrace -c './app args' prog.bt      # запустить и трассировать ровно эту команду
```

Нацеливание внутри скрипта — фильтрами по builtins (дёшево, в ядре):

```text
kprobe:vfs_read /pid == 1234/        { @[kstack] = count(); }     // один процесс
kprobe:vfs_read /comm == "nginx"/    { @ = count(); }             // по имени
tracepoint:syscalls:sys_enter_openat /str(args->filename) == "/etc/passwd"/ {
    printf("%s читает passwd\n", comm); }                          // по аргументу
```

Безопасность и совместимость:
- bpftrace требует прав (root/`CAP_BPF`+`CAP_PERFMON`) и `perf_event_paranoid`/BTF
  (§16.2) — без BTF многие probe и доступ к полям `curtask` не заработают;
- `bpftrace -l` — первый шаг любой сессии: проверить, что нужная точка вообще есть на
  **этом** ядре (имена kprobe-функций версионно-зависимы, §5.3);
- скрипт компилируется в BPF и проходит **верификатор** (§9.1) — слишком сложный
  (большие циклы/глубокие стеки) может не пройти; это фича безопасности, не баг.

Связка с треком: `bpftrace -l 'kprobe:my_driver_*'` находит твои функции,
скрипт из §10.4 даёт по ним латентность и стеки, а `profile:hz:99 { @[kstack]=count();}`
— flame graph. Всё «инструментирование своего драйвера» из критерия освоения — здесь.

---

## 11. bcc-инструменты — готовые ответы

Набор **bcc** (BPF Compiler Collection) и связанные `bpftrace`-tools (Brendan Gregg)
— десятки **готовых** утилит на eBPF под типовые вопросы. Не нужно ничего писать:

| Инструмент | Отвечает на вопрос |
|-----------|--------------------|
| `funccount 'vfs_*'` | сколько раз вызвана каждая функция по маске |
| `funclatency vfs_read` | гистограмма **латентности** функции |
| `stackcount func` | по каким **стекам** вызывается функция (кто зовёт) |
| `offcputime` | где задачи **спят** (off-CPU стек + время) — Ф4 §17.5 |
| `profile` | сэмплирующий профиль стеков (для flame graph) |
| `execsnoop` / `opensnoop` | каждый `exec`/`open` в системе |
| `biolatency` / `biosnoop` | латентность блочного I/O |
| `tcplife` / `tcpconnect` | соединения TCP (жизнь/установка) |
| `argdist` / `trace` | гибкие однострочные пробы (как мини-bpftrace) |

Под капотом они делают то же, что ты бы написал в bpftrace, но оформлены, протестированы
и параметризованы. Правило выбора (как io_uring vs epoll в C2): **готовый bcc/tool →
если нет, bpftrace-однострочник → если нужна сложная логика/продакшн-инструмент,
libbpf+CO-RE на C** (§9.3).

### 11.1 Как читать вывод (на примере funclatency)

```text
$ funclatency -u vfs_read           # -u: микросекунды
Tracing vfs_read... Ctrl-C to end.
     usecs        : count    distribution
         0 -> 1   : 18       |                          |
         2 -> 3   : 1532     |****************          |
         4 -> 7   : 2104     |**********************    |
         8 -> 15  : 940      |**********                |
        16 -> 31  : 88       |*                         |
       ...
      2048 -> 4095: 12       |                          |   ← «хвост»: редкие медленные
```

Гистограмма — **степени двойки** (`log2`): основная масса `vfs_read` за 2–15 мкс, но
есть **хвост** до миллисекунд — те самые редкие медленные вызовы, которые портят
P99. Дальше `funcslower vfs_read 1000` (только дольше 1 мс) + `kstack` покажет, **на
чём** медленные застревают. Это типовой цикл: гистограмма → выделить хвост →
стек хвоста → корень. `funccount`/`stackcount` дают тот же стиль для «сколько/откуда».

### 11.2 bpftool — инспекция загруженного BPF

```sh
bpftool prog show              # все загруженные BPF-программы (тип, имя, id)
bpftool map show               # все maps
bpftool map dump id 42         # содержимое map (твои счётчики/гистограммы)
bpftool prog profile id 7 ...  # счётчики PMU на конкретной программе
```

Полезно, когда инструмент (bcc/bpftrace/свой libbpf) уже работает, а ты хочешь
заглянуть в его maps или понять, что вообще висит в ядре.

---

## 12. Flame graph ядра под нагрузкой

**Flame graph** (Ф4 §6.4) — визуализация профиля: ширина = доля CPU-времени,
вертикаль = стек. Для ядра это «куда уходит CPU в kernel space под нагрузкой» —
прямое задание трека.

### 12.1 Через perf

```sh
# 1. сэмплировать всю систему со стеками (включая ядро) под нагрузкой:
perf record -F 99 -a -g -- sleep 30

# 2. развернуть и схлопнуть стеки:
perf script > out.stacks
./FlameGraph/stackcollapse-perf.pl out.stacks > out.folded

# 3. сгенерировать SVG (FlameGraph Брендана Грегга):
./FlameGraph/flamegraph.pl --color=java out.folded > kernel-flame.svg
```

### 12.2 Через bpftrace/bcc (eBPF)

```sh
# профиль стеков ядра 99 Гц через bpftrace:
bpftrace -e 'profile:hz:99 { @[kstack] = count(); }' > out.bt
# или готовый bcc:
profile -F 99 -f 30 > out.folded     # -f: уже свёрнутый формат для flamegraph.pl
./FlameGraph/flamegraph.pl out.folded > kernel-flame.svg
```

### 12.3 Как читать (ядровая специфика)

- **широкие плато наверху** — горячие листовые функции ядра (там тратится CPU):
  спинлок-контеншн (`native_queued_spin_lock_slowpath` — много времени в локах!),
  копирование (`copy_*_user`), аллокатор, сеть;
- **`__fentry__`/трамплины** в стеке — артефакт самой трассировки, не пугайся;
- **обрыв стека** — нет frame pointer или kallsyms; для ядра нужен
  `CONFIG_FRAME_POINTER`/ORC unwinder (`CONFIG_UNWINDER_ORC` — по умолчанию на
  x86-64, даёт точные стеки без FP);
- сравни **CPU flame graph** (где считаем) и **off-CPU** (`offcputime`, где спим,
  §11) — вместе покрывают всё время (Ф4 §17.5).

Важно (мост в §16): чтобы стеки и символы ядра вообще были видны,
`kernel.perf_event_paranoid` должен это разрешать и должен быть доступен
`/proc/kallsyms` (`kptr_restrict`).

### 12.4 Off-CPU и дифференциальные flame graph

- **Off-CPU flame graph** — где задачи **спят**, а не считают (Ф4 §17.5). Снимается
  иначе: стек берётся в момент ухода с CPU, ширина = **время сна**:
  ```sh
  offcputime -f 30 > off.folded      # bcc: уже свёрнутый формат
  ./FlameGraph/flamegraph.pl --color=io --title="Off-CPU" off.folded > off.svg
  ```
  Широкие плато тут — блокировки (`mutex`/`io`/`schedule`), а не вычисления. CPU- и
  off-CPU графы **вместе** покрывают всё время потока.
- **Дифференциальный flame graph** — разница «до/после» (регрессия или эффект
  изменения): красное = стало хуже, синее = лучше:
  ```sh
  ./FlameGraph/difffolded.pl before.folded after.folded | ./FlameGraph/flamegraph.pl > diff.svg
  ```
- **Hot/cold** — объединить CPU (горячее) и off-CPU (холодное) на одном графе.

Методология (Брендан Грегг): начинай с **CPU** flame graph; если CPU не занят, а
медленно — **off-CPU**; для подтверждения регрессии — **дифференциальный**. Это и
есть «снять flame graph под нагрузкой» из критерия освоения K7.

---

## 13. `trace_printk` vs `printk` (вопрос трека)

Иногда всё же нужно «вывести значение из точки кода» — но `printk` на hot path
губителен (§1, K6 §15). Для отладки **внутри трассируемых путей** есть
`trace_printk()`.

### 13.1 В чём разница

| | `printk`/`pr_info` | `trace_printk` |
|--|--------------------|----------------|
| Куда пишет | системный лог (`dmesg`), консоль | **ftrace ring buffer** (`tracefs/trace`) |
| Стоимость | высокая: формат + лок консоли + (serial!) | низкая: запись в per-CPU кольцо, без консоли |
| Где смотреть | `dmesg` | `cat /sys/kernel/tracing/trace` |
| Тайминги | искажает (медленный вывод) | почти не искажает (буфер в памяти) |
| Время в записи | грубое | точное (тот же таймстамп, что у трасс) |
| Для прода | нет на hot path | **только отладка** (печатает баннер-предупреждение) |

`trace_printk` пишет в то же кольцо, что function_graph/tracepoints, поэтому твои
сообщения **встают в общий таймлайн** с трассировкой ядра — видно, что произошло
между вызовами функций. Это его суперсила.

### 13.2 Использование

```c
/* в любом месте кода ядра/модуля, в т.ч. в kprobe-обработчике или на hot path: */
trace_printk("k7: dev=%s len=%u state=%d\n", name, len, state);
```

```sh
cat /sys/kernel/tracing/trace | grep k7
```

**Важно:** `trace_printk` **не для продакшена** — при первом вызове ядро печатает в
`dmesg` громкое предупреждение («This means that this is a DEBUG kernel and it is
unsafe for production»), потому что он держит выделенный буфер и заметен. Это
**отладочный** инструмент: вставил, посмотрел в трассе, убрал. Для постоянной
наблюдаемости — tracepoint/трассировка, не `trace_printk`.

### 13.3 dynamic debug — третий путь

Для статического, но управляемого лога есть `pr_debug` + **dynamic debug**: `pr_debug`
компилируется в no-op, но при `CONFIG_DYNAMIC_DEBUG` включается **на лету** по
файлу/строке/функции:

```sh
echo 'module mydrv +p' > /sys/kernel/debug/dynamic_debug/control   # включить pr_debug модуля
echo 'func my_func +p'  > /sys/kernel/debug/dynamic_debug/control
```

Итог трёх инструментов: **`printk`** — редкие важные события; **`trace_printk`** —
отладка внутри hot path/трассируемых путей (временно); **`pr_debug`+dynamic_debug** —
управляемый отладочный лог без пересборки.

### 13.4 trace_marker: метки из userspace в общий таймлайн

Зеркальный приём для userspace: записать **свою метку** прямо в ftrace-буфер ядра,
чтобы события приложения встали в **один таймлайн** с трассировкой ядра:

```sh
# из шелла/приложения:
echo "app: начал обработку запроса 42" > /sys/kernel/tracing/trace_marker
```

Теперь в `cat trace` твоя строка стоит **между** ядровыми событиями с тем же
таймстампом — видно, что именно ядро делало во время «обработки запроса 42»
(syscalls, переключения, I/O). Это связывает userspace-логику с ядровой трассой без
догадок по разнесённым логам — частый приём при отладке латентности
«приложение↔ядро». Программно: `open("/sys/kernel/tracing/trace_marker")` один раз и
`write()` меток на горячем пути (дёшево — это ring buffer, §13.1).

Родственно — **USDT** (User Statically-Defined Tracepoints): статические точки в
userspace-программах (как tracepoint, но в приложении), к которым цепляются bpftrace
(`usdt:/path:probe`) и bcc. Так инструментируют БД/рантаймы (PostgreSQL, JVM) на
постоянной основе.

---

## 14. Декодирование oops и panic (без исходников)

Когда ядро всё же падает (oops/panic), оно печатает дамп: регистры, стек, адрес.
Задача — превратить адреса в **функции и строки**. Это вопрос трека «декодировать
стек oops без исходников».

### 14.1 Анатомия oops

```text
BUG: kernel NULL pointer dereference, address: 0000000000000008
RIP: 0010:my_driver_handle+0x42/0x120 [mydrv]
Call Trace:
 <TASK>
  my_driver_irq+0x1a/0x60 [mydrv]
  __handle_irq_event_percpu+0x4c/0x190
  handle_irq_event+0x39/0x80
 </TASK>
Code: 48 8b 47 08 ...
```

- **`RIP: ...my_driver_handle+0x42/0x120`** — упали в `my_driver_handle`, на **+0x42**
  от начала, длина функции **0x120**. Суффикс `[mydrv]` — функция в модуле `mydrv`.
- **Call Trace** — стек вызовов (имена уже разрешены через **kallsyms** — встроенную
  в ядро таблицу `адрес↔имя`, `/proc/kallsyms`).
- **`Code:`** — байты вокруг `RIP` (можно дизассемблировать).

### 14.2 kallsyms — почему имена уже есть

Ядро встраивает `kallsyms` — таблицу всех своих символов — поэтому oops печатает
**имена**, а не голые адреса (если `CONFIG_KALLSYMS=y`, что почти всегда). Но имя+offset
(`+0x42`) — это не строка исходника. Чтобы получить **файл:строку**, нужен
`addr2line` по `vmlinux`/`.ko` с debuginfo:

```sh
# offset внутри функции → файл:строка (нужен модуль/ядро С символами):
addr2line -e mydrv.ko -f -i 0x42        # для функции в модуле (offset от её начала — не всегда напрямую)
# надёжнее — по адресу функции из /proc/kallsyms + offset, или по полному vmlinux:
addr2line -e vmlinux -f -i 0xffffffff81234242
```

### 14.3 decode_stacktrace.sh — автоматизация

В дереве ядра есть скрипт, который берёт сырой oops и разрешает **все** строки разом:

```sh
# скопировать текст oops в oops.txt, затем:
./scripts/decode_stacktrace.sh vmlinux < oops.txt
# для модулей укажи путь к .ko:
./scripts/decode_stacktrace.sh vmlinux /path/to/modules < oops.txt
```

Он сам прогонит `addr2line` по каждой строке Call Trace и подставит `файл:строка` —
это самый быстрый способ «прочитать» oops.

### 14.4 build-id — сопоставить дамп с правильным бинарником

Как и в Ф4 §18.4, символы должны быть **от той же сборки**. У ядра и модулей есть
**build-id** (`readelf -n vmlinux`, `.note.gnu.build-id`), который печатается и в
oops/panic, и хранится в debuginfo-пакете. Совпадение build-id гарантирует, что
`addr2line` врёт. `debuginfod` умеет отдавать `vmlinux` с символами по build-id
автоматически.

### 14.5 Что включить заранее

- `CONFIG_KALLSYMS=y` (имена в трейсе) — почти всегда есть;
- `CONFIG_DEBUG_INFO`/split debuginfo для `addr2line` (часто — отдельный
  `kernel-debuginfo` пакет);
- `CONFIG_UNWINDER_ORC=y` — точные стеки без frame pointer;
- `panic_on_oops`/`panic_on_warn` (sysctl) — если хочешь, чтобы баг сразу вёл к
  panic+kdump (§15), а не «продолжали жить в повреждённом состоянии».

### 14.6 Worked: oops модуля → строка кода

Дано (упавший драйвер `mydrv`), сырой фрагмент:

```text
RIP: 0010:mydrv_write+0x73/0x120 [mydrv]
Call Trace:
  vfs_write+0xc5/0x2b0
  ksys_write+0x6b/0xf0
  do_syscall_64+0x59/0x90
```

Шаги декодирования:

1. **Функция и offset** — упали в `mydrv_write`, на `+0x73`. Для модуля offset
   считается **от начала функции**; точнее всего — `faddr2line` (понимает
   функция+offset напрямую):
   ```sh
   ./scripts/faddr2line mydrv.ko 'mydrv_write+0x73'
   # mydrv_write+0x73/0x120:
   # mydrv_write at /home/me/mydrv/mydrv.c:142
   ```
2. **Или весь стек разом** — `decode_stacktrace.sh`:
   ```sh
   ./scripts/decode_stacktrace.sh vmlinux . < oops.txt   # '.' — где лежат .ko
   ```
3. **Совпадение сборки** — `modinfo mydrv.ko | grep vermagic` и build-id
   (`readelf -n`) должны соответствовать загруженному модулю и oops; иначе строки
   будут от другой сборки (мост в заметку про vermagic).

Строка 142 `mydrv.c` — где разыменование. Дальше — обычный анализ: что на этой
строке могло быть NULL/освобождено (для подтверждения «когда освободили» — KASAN,
K2 §15, который печатает и стек аллокации, и стек освобождения).

---

## 15. kdump → vmcore → crash: посмертный дамп ядра

oops-текст в `dmesg` часто недостаточен: нужна **вся память ядра** в момент паники
для разбора структур, стеков всех задач, slab. Это даёт **kdump** — и его надо
**настроить заранее** (вопрос трека «что нужно настроить заранее»).

### 15.1 Идея: второе ядро через kexec

```text
[рабочее ядро]  --panic-->  kexec мгновенно загружает [capture-ядро]
                            в ЗАРЕЗЕРВИРОВАННОЙ заранее памяти (crashkernel=)
                                  ↓
                            capture-ядро дампит память упавшего → vmcore
                                  ↓
                            записывает /var/crash/.../vmcore, перезагрузка
```

`kexec` умеет загрузить новое ядро **без BIOS-перезагрузки**. kdump использует это:
при панике основное ядро передаёт управление заранее загруженному **capture-ядру**,
которое живёт в отдельном куске RAM и снимает образ памяти упавшего.

### 15.2 Что настроить ЗАРАНЕЕ (иначе vmcore не получишь)

1. **Зарезервировать память** под capture-ядро параметром загрузки:
   ```text
   crashkernel=256M           # в cmdline (GRUB) — память НЕДОСТУПНА основному ядру
   ```
   Без `crashkernel=` kdump невозможен — память не зарезервирована.
2. **Установить и включить** службу:
   ```sh
   # дистрибутивно: kexec-tools + служба
   systemctl enable --now kdump
   kdumpctl status        # или: cat /sys/kernel/kexec_crash_loaded → 1 = готово
   ```
3. **debuginfo ядра** (`vmlinux` с символами) — для разбора `crash` (см. ниже).
4. (опц.) `makedumpfile`-фильтрация: исключить нулевые/пользовательские страницы,
   сжать — иначе vmcore = размер RAM.

`cat /sys/kernel/kexec_crash_loaded` == `1` означает, что capture-ядро загружено и
kdump сработает при панике. Это проверка готовности.

### 15.3 Анализ vmcore через crash

`crash` (Red Hat) — «gdb для ядра» поверх `vmcore` + `vmlinux`:

```sh
crash vmlinux /var/crash/127.0.0.1-.../vmcore
crash> bt              # backtrace упавшей задачи
crash> bt -a           # стеки всех CPU/задач
crash> ps              # все процессы в момент паники
crash> log             # буфер dmesg из дампа
crash> kmem -i         # статистика памяти
crash> struct task_struct <addr>   # разобрать структуру по адресу
crash> dis my_func     # дизассемблировать
```

`crash` понимает структуры ядра (через debuginfo), поэтому даёт post-mortem уровня
исходников: пройтись по спискам, проверить инварианты, найти, какая задача держала
лок. Это аналог анализа core dump из Ф4 §12, но для **всего ядра**.

### 15.4 Когда что

- **oops в `dmesg` + `decode_stacktrace`** (§14) — большинство багов: NULL-дереф,
  простой use-after-free, видно по стеку.
- **kdump/`crash`** — когда нужно состояние **всей** системы: повреждение из другого
  контекста, дедлок/хардлокап, баг, который roняет систему целиком (panic, не
  recoverable oops), редкий «раз в неделю» краш в проде.

Ключевая мысль трека: **kdump настраивают ДО** аварии (`crashkernel=`, служба,
debuginfo). После паники без подготовки у тебя только текст oops — и всё.

### 15.5 Worked: спровоцировать и разобрать дамп

Проверить, что kdump вообще работает, можно **управляемой** паникой через
magic-sysrq (в лабе/QEMU, не на проде):

```sh
# 0. убедиться, что capture-ядро загружено:
cat /sys/kernel/kexec_crash_loaded         # → 1

# 1. спровоцировать crash (sysrq 'c' = crash):
echo 1 > /proc/sys/kernel/sysrq
echo c > /proc/sysrq-trigger               # → паника → kdump → vmcore → reboot

# 2. после перезагрузки — дамп на месте:
ls /var/crash/                             # 127.0.0.1-2026-.../vmcore

# 3. разобрать:
crash /usr/lib/debug/.../vmlinux /var/crash/.../vmcore
crash> log | tail            # что было в dmesg перед крахом
crash> bt                    # стек паниковавшего контекста
crash> bt -a                 # стеки всех CPU — кто что делал
crash> ps | grep UN          # задачи в uninterruptible (висящие на I/O/локе)
crash> foreach bt            # стеки всех задач разом
```

Так на учебном стенде убеждаешься, что цепочка `crashkernel=` → служба → `vmcore` →
`crash` собрана **до** того, как понадобится в проде. Для реального драйверного бага
вместо sysrq панику вызовет сам баг (или `panic_on_oops=1`), а дальше — те же
команды `crash`. `makedumpfile -d 31` при сборке vmcore выкинет нулевые/user-страницы
и сожмёт — иначе дамп = размер RAM (десятки ГБ).

---

## 16. Overhead и безопасность трассировщиков

Трассировка не бесплатна и не всегда безопасна — выбор инструмента это компромисс.

### 16.1 Сравнение overhead

| Инструмент | Overhead | Риск уронить ядро | Прод |
|-----------|----------|-------------------|------|
| `ftrace` function (нефильтрованный) | высокий (всё подряд) | низкий | сузить фильтром |
| `ftrace` function_graph (фильтр) | средний | низкий | да, точечно |
| tracepoint (включённый) | минимальный | нет | да |
| kprobe (`int3`) | средний | **есть** (кривой обработчик) | осторожно |
| kprobe (optimized/ftrace) | низкий | есть | да |
| eBPF (kprobe/fentry) | низкий (JIT) | **нет** (верификатор) | **да** |
| `perf record -a` | низкий-средний (частота) | нет | да |
| `strace`/`ltrace` (ptrace) | **высокий** (~10×, стоп процесса) | нет | **нет** |

Тенденция очевидна: **eBPF** даёт низкий overhead **и** безопасность — поэтому он
вытеснил ручные kprobe-модули для трассировки. Самописный kprobe-модуль (§7) ценен
как **учебный** инструмент и когда нужно что-то за пределами BPF (но это редко).

### 16.2 perf_event_paranoid и kptr_restrict

Доступ к трассировке регулируется sysctl (важно для воспроизводимости и безопасности):

```sh
sysctl kernel.perf_event_paranoid     # -1..3: насколько perf/eBPF доступны без root
sysctl kernel.kptr_restrict           # 0/1/2: показывать ли адреса ядра (/proc/kallsyms)
sysctl kernel.unprivileged_bpf_disabled  # можно ли BPF без CAP_BPF
```

- `perf_event_paranoid <= 1` обычно нужен для `perf record` ядра и многих eBPF-tools;
- `kptr_restrict = 0` — чтобы `/proc/kallsyms` отдавал реальные адреса (иначе нули, и
  символы в профиле не разрешатся);
- права: трассировка ядра требует `root` или `CAP_BPF`+`CAP_PERFMON` (на свежих
  ядрах для eBPF выделены отдельные capability).

### 16.3 Heisenberg: трассировка меняет систему

Любая трассировка добавляет задержку и может **сдвинуть тайминги** — гонка может
исчезнуть под трассировкой или, наоборот, проявиться. Поэтому:
- сужай фильтрами до минимума (не трассируй «всё»);
- предпочитай агрегацию в ядре (bpftrace `hist`/maps) выводу каждого события;
- для тонких гонок — детерминированные инструменты (Ф4 §11 `rr` в userspace; в ядре
  — KCSAN, K2 §15).

### 16.4 Сквозной разбор: от симптома к корню

Инструменты по отдельности — словарь; реальная отладка ядра — связное предложение.
Пройдём типичный случай.

**Симптом.** После загрузки твоего драйвера система **периодически подлагивает** —
аудио заикается, growing джиттер. CPU не загружен на 100%. `dmesg` чист.

**Шаг 1. Где время — CPU или сон?** Снимаем оба профиля (Ф4 §17.5): CPU flame graph
(`profile -F 99 -af 10 > cpu.folded`) и off-CPU (`offcputime -f 10 > off.folded`).
CPU-профиль ровный, а off-CPU показывает широкое плато в стеке твоего драйвера через
`mutex_lock` → задачи **спят** на твоём мьютексе.

**Шаг 2. Кто держит лок и почему долго?** Вешаем bpftrace на вход/выход критической
секции (своя функция под локом):

```text
kprobe:mydrv_critical { @t[tid] = nsecs; }
kretprobe:mydrv_critical /@t[tid]/ { @held = hist(nsecs - @t[tid]); delete(@t[tid]); }
```

Гистограмма `@held` показывает «хвост» в десятки миллисекунд — секция иногда держит
лок непозволительно долго.

**Шаг 3. Что она делает в долгом случае?** function_graph на этой функции с фильтром:

```sh
echo mydrv_critical > set_graph_function
echo function_graph > current_tracer ; echo 1 > tracing_on
# ... воспроизвести лаг ...
cat trace | grep -A40 mydrv_critical    # видно вложенный вызов с '!': > 100 мкс
```

В графе видно: под локом вызывается аллокация большого буфера, которая под давлением
памяти уходит в reclaim (спит). **Корень:** долгая (и потенциально спящая) работа
**внутри** критической секции — антипаттерн K2.

**Шаг 4 (если бы упало).** Если бы вместо лага был oops/panic — `decode_stacktrace.sh`
(§14) по тексту oops, а при полном крахе — `vmcore` + `crash` (§15): `bt -a` показал
бы, какая задача держит лок, а какая ждёт.

**Фикс.** Вынести аллокацию/долгую работу **из-под** лока (захватывать лок только
вокруг собственно общих данных, K2). Регрессия: тот же bpftrace-гистограммой `@held`
в нагрузочном тесте — хвост должен исчезнуть.

**Мораль маршрута:** off-CPU указал *класс* (спим на локе), bpftrace-гистограмма —
*масштаб* (длинный хвост), function_graph — *причину* (что под локом), а kdump/`crash`
держался бы в резерве на случай полного краха. Ни один инструмент в одиночку не
закрыл бы задачу — и ни один не потребовал ни одного `printk`.

---

## 17. Инструменты, типичные ошибки, чеклист

### 17.1 Сводка инструментов

- **`tracefs`** (`/sys/kernel/tracing`) — ftrace вручную: `current_tracer`, `trace`,
  `set_ftrace_filter`, `events/`.
- **`trace-cmd`** / **KernelShark** — обёртка и GUI над ftrace.
- **`bpftrace`** — однострочники/скрипты на eBPF; основной ad-hoc инструмент.
- **`bcc`-tools** — готовые: `funccount`, `funclatency`, `offcputime`, `profile`,
  `opensnoop`, `biolatency`, ...
- **`perf`** — `probe`/`record`/`report`/`trace`/`annotate` по символам ядра.
- **FlameGraph** (Брендан Грегг) — `stackcollapse-perf.pl` + `flamegraph.pl`.
- **`bpftool`** — управление загруженными BPF-программами и maps.
- **`decode_stacktrace.sh`**, **`addr2line`**, **`faddr2line`** — декодирование oops.
- **`crash`**, **`makedumpfile`**, **`kexec`** — kdump/vmcore.
- **`dmesg`**, **`/proc/kallsyms`**, **`/proc/kcore`** — символы/лог/живая память ядра.

### 17.2 Галерея типичных ошибок

1. **`printk` на hot path** (или в kprobe-обработчике на горячей функции) →
   throughput коллапсирует, лог-флуд. Лечение: `trace_printk`/счётчики/bpftrace (§13).
2. **Сон в обработчике kprobe/ftrace** (`mutex`, `GFP_KERNEL`, `msleep`) →
   `scheduling while atomic` (§1.3, §7.4). Лечение: `GFP_ATOMIC`/per-CPU/без сна.
3. **Не сняли kprobe в `exit`** (`unregister_kprobe`) → после выгрузки кода
   breakpoint прыгает в освобождённую память → oops. Лечение: симметричный `exit`.
4. **kprobe на inline/удалённую функцию** → `register_kprobe` вернёт `-EINVAL`/
   `-ENOENT`; на другой версии ядра probe «отвалится». Лечение: tracepoint, где есть
   (§5.3); проверяй `available_filter_functions`/kallsyms.
5. **kretprobe `maxactive` мал** → `nmissed` растёт, часть возвратов потеряна.
   Лечение: поднять `maxactive` под параллелизм.
6. **ftrace без фильтра** на проде → пожарный шланг, нагрузка/потеря событий
   (overrun). Лечение: `set_ftrace_filter`/`set_graph_function` ПЕРЕД включением.
7. **Нет символов** (`kptr_restrict`/`paranoid`/нет debuginfo) → стеки из нулей,
   oops не декодируется. Лечение: sysctl (§16.2), debuginfo по build-id (§14.4).
8. **Обрыв стека** в flame graph → нет ORC/FP. Лечение: `CONFIG_UNWINDER_ORC`,
   `--call-graph dwarf`/fp (§12.3).
9. **kdump не сработал** — забыли `crashkernel=` или службу. Лечение: проверять
   `cat /sys/kernel/kexec_crash_loaded == 1` ЗАРАНЕЕ (§15.2).
10. **`trace_printk` оставлен в проде** → баннер-предупреждение, лишний буфер.
    Лечение: убрать после отладки; для постоянного — tracepoint/dynamic_debug.

### 17.3 Чеклист трассировки/инструментария

- [ ] Есть ли готовый **tracepoint** на нужное событие? (`ls events/`) — бери его, а
      не kprobe (§5.3).
- [ ] Обработчик kprobe/ftrace **не спит** (atomic context, §1.3, §7.4).
- [ ] kprobe/kretprobe **снимается** в `exit`; `maxactive` достаточен (§7, §17.2).
- [ ] ftrace **сужен фильтром** до включения (§3); buffer достаточен (overrun?).
- [ ] Для агрегации — **в ядре** (bpftrace `hist`/maps, hist-триггеры), не поток
      событий в userspace (§16.3).
- [ ] Символы доступны: `paranoid`/`kptr_restrict`/debuginfo по build-id (§16.2, §14.4).
- [ ] Flame graph: стеки целые (ORC/FP, §12.3); CPU **и** off-CPU сняты (§11–12).
- [ ] Для прода: предпочесть **eBPF** (верификатор, низкий overhead, §16.1).
- [ ] kdump настроен **заранее** (`crashkernel=`, служба, debuginfo) — проверь
      `kexec_crash_loaded` (§15.2).
- [ ] `trace_printk` — только временно, убран после отладки (§13.2).

### 17.4 Методология: с какого инструмента начинать

Не «какой трассировщик круче», а «какой вопрос задаю»:

```text
Вопрос                                  →  Инструмент (от простого к сложному)
───────────────────────────────────────────────────────────────────────────
"вызывается ли функция X и кем?"        →  ftrace function + set_ftrace_filter (§2–3)
"куда уходит время на пути в ядро?"     →  ftrace function_graph (§2.3)
"есть готовое событие на это?"          →  ls events/ → tracepoint (§5) → bpftrace
"сколько раз / какова латентность X?"   →  funccount / funclatency (bcc, §11)
"где CPU горячий в ядре?"               →  perf record -ag → flame graph (§12)
"медленно, но CPU не занят?"            →  offcputime → off-CPU flame graph (§12.4)
"кто держит лок / тормозит планировщик?"→  perf lock / perf sched (§8.4)
"нужна своя логика/фильтр в точке"      →  bpftrace-скрипт (§10) → libbpf (§9.5)
"постоянная наблюдаемость драйвера"     →  свой tracepoint (§5.5)
"ядро упало (oops)"                     →  decode_stacktrace / faddr2line (§14)
"ядро упало целиком (panic/hang)"       →  kdump → vmcore → crash (§15)
```

Два правила поверх таблицы: **(1)** начинай с самого дешёвого и встроенного (ftrace/
tracepoint), эскалируй к eBPF только когда нужна логика; **(2)** агрегируй **в ядре**
(hist/maps), а не лей поток событий в userspace (§16.3). И всегда помни: трассировка
меняет тайминги (§16.3) — снимай минимально необходимое.

---

## 18. Практика и самопроверка

### 18.0 Лаборатория: трассировка в QEMU

Как все K-упражнения, это **LKM** в QEMU (нельзя грузить на живой хост — баг =
паника). Модуль собирается как `cppmod.ko`, грузится `insmod`, тестируется и
выгружается `rmmod`; кнопка «⚙ Собрать и запустить в QEMU». Специфика трассировочных
тестов в гостевой busybox-среде:

- смонтируй tracefs: `mount -t tracefs nodev /sys/kernel/tracing` (и `proc` для
  `/proc`-вывода);
- **триггерь** трассируемую функцию из userspace детерминированно: kprobe на
  `do_sys_openat2` дёргается любым `cat`/открытием файла;
- читай свои счётчики через `/proc` (как K5/K6) или сообщения через
  `cat /sys/kernel/tracing/trace`;
- `dmesg` грепай на `BUG`/`Oops`/`scheduling while atomic` (обработчик в atomic!);
- ядро гостя должно иметь `CONFIG_KPROBES`/`CONFIG_FUNCTION_TRACER` (стандартно для
  отладочного ядра курса).

> Тонкость: целевой символ kprobe (`do_sys_openat2`) — это путь `openat(2)` на
> современных ядрах; он триггерится любым открытием файла, поэтому удобен для
> детерминированного теста без спец-нагрузки.

### 18.1 Практические задания (LKM в QEMU)

1. **`01-kprobe-count` — счётчик вызовов функции через kprobe.** Зарегистрировать
   `kprobe` на `do_sys_openat2` (символ — параметр модуля), в `pre_handler`
   инкрементировать **per-CPU** счётчик (K2 §9, atomic context!), отдать сумму через
   `/proc/k7_kprobe`. Проверка: счётчик растёт при открытии файлов. *(§6–7)*
2. **`02-kretprobe-latency` — латентность функции через kretprobe.** `entry_handler`
   засекает `ktime_get()` в `ri->data`, `handler` считает дельту и накапливает
   `count`/`last_ns`/`max_ns`; вывод через `/proc/k7_lat`. Проверка: после трафика
   `count > 0`, латентность ненулевая. *(§6.2, §7.3)*
3. **`03-trace-printk` — лог в ftrace-буфер вместо dmesg.** `/proc/k7_trace`: запись
   в него вызывает `trace_printk` с маркером (а не `printk`). Проверка: маркер виден
   в `/sys/kernel/tracing/trace`, но **не** во флуде `dmesg`. *(§13)*
4. **`04-kprobe-arg` — фильтр по аргументу функции.** kprobe на `do_sys_openat2`,
   через `regs_get_kernel_argument(regs, 0)` прочитать `dfd` и считать **только**
   открытия с `dfd == AT_FDCWD`; вывод через `/proc/k7_arg`. Проверка: счётчик
   растёт на `cat /path` (относительные открытия от cwd). *(§7.2)*

Все — LKM в QEMU: собираются как `cppmod.ko`, грузятся `insmod`, тест из userspace,
`dmesg` чист (нет `scheduling while atomic`). Кнопка «⚙ Собрать и запустить в QEMU».

### 18.2 Большое задание

Возьми **свой** драйвер из K1/K3 (или любой модуль с нетривиальными функциями).
(а) Инструментируй 2–3 его функции через kprobe/kretprobe из отдельного
трассировочного модуля — счётчики вызовов и гистограмму латентности (per-CPU,
`/proc`). (б) Напиши **bpftrace**-скрипт на kprobe тех же функций: `count()` по
процессам и `hist()` латентности. (в) Сними **flame graph** ядра под нагрузкой на
драйвер (`profile`/`perf record -ag` → `flamegraph.pl`). (г) Сравни overhead трёх
подходов и объясни, **почему** для прода ты бы выбрал eBPF (§16). Это прямой выход на
«инструментирую свои функции и снимаю flame graph» из критерия освоения.

### 18.3 Вопросы для самопроверки

1. Чем `function` отличается от `function_graph` в ftrace? Когда какой и почему у
   graph выше overhead?
2. Как устроен динамический ftrace: что делает `__fentry__` и зачем nop-патчинг?
   Почему выключенная трассировка стоит ≈ ноль?
3. tracepoint vs kprobe: кто их создаёт, что стабильнее и почему, когда выбрать
   каждый?
4. Как kprobe технически перехватывает функцию (`int3`)? Что такое оптимизированная
   kprobe и почему она дешевле?
5. Чем kretprobe отличается от kprobe? Как через неё измерить латентность функции
   ядра?
6. Что обязательно сделать в `exit` модуля с kprobe и почему? Что будет, если забыть?
7. В каком контексте исполняется обработчик kprobe/ftrace? Что из-за этого нельзя
   делать?
8. Почему eBPF безопаснее самописного kprobe-модуля? Что делает верификатор?
9. Что такое eBPF maps и зачем они, если программа «без состояния»? Назови 2–3 типа.
10. Что такое BTF/CO-RE и какую проблему bcc они решают?
11. `trace_printk` vs `printk`: куда пишет каждый, что дешевле, почему `trace_printk`
    не для прода?
12. Как декодировать `oops` без исходников: что даёт kallsyms, что — `addr2line`,
    зачем build-id?
13. Что нужно настроить **заранее**, чтобы после паники получить `vmcore`? Как
    проверить готовность kdump?
14. Что показывает flame graph ядра и как его снять (perf и eBPF путь)? Почему может
    оборваться стек?
15. Почему `perf_event_paranoid`/`kptr_restrict` влияют на то, увидишь ли ты символы
    и стеки ядра?

---

## 19. Банк вопросов

> Полные версии (с разбором) — в `quizzes/k7.json`. Карта тем:

### БАЗА
ftrace и tracefs; `function` vs `function_graph`; tracepoint; kprobe/kretprobe;
eBPF/bpftrace; `trace_printk` vs `printk`; kallsyms; kdump/vmcore; flame graph; NAPI/
контекст обработчика.

### МЕХАНИЗМЫ
function vs function_graph (вывод/overhead); kprobe vs tracepoint (стабильность,
когда что); как устроен динамический ftrace (`__fentry__`/nop); регистрация kprobe из
модуля и контекст обработчика; kretprobe и измерение латентности; eBPF maps и
доставка данных; bpftrace-структура (probe/filter/action, агрегаты); flame graph
ядра (perf и eBPF путь); фильтры ftrace (почему обязательны); `trace_printk` и ftrace
ring buffer.

### ЭКСПЕРТ
почему eBPF безопаснее/дешевле старых трейсеров (верификатор, JIT, CO-RE);
декодирование oops без исходников (kallsyms, build-id, `addr2line`,
`decode_stacktrace`); `trace_printk` vs `printk` — когда что и почему; panic → kdump →
vmcore → `crash` (что настроить заранее); оптимизированные kprobe и BPF-trampoline на
`__fentry__`; overhead/безопасность трассировщиков и `perf_event_paranoid`.

---

## 20. Глоссарий

- **ftrace** — встроенный в ядро трассировщик функций; управляется через `tracefs`.
- **tracefs** — ФС `/sys/kernel/tracing` (старый путь — `debug/tracing`), интерфейс
  ftrace.
- **`function` tracer** — плоский список вызовов функций ядра (функция ← вызыватель).
- **`function_graph`** — дерево вызовов с длительностью каждой функции.
- **`__fentry__` / mcount** — вставка в начале каждой функции (от `-pg`), точка
  входа трассировщика; патчится между `nop` и `call` на лету.
- **Динамический ftrace** (`CONFIG_DYNAMIC_FTRACE`) — nop-патчинг: платят только
  активные функции, выключенная трассировка ≈ бесплатна.
- **tracepoint** — статический именованный маркер в коде ядра с полями; стабилен
  (~ABI наблюдаемости).
- **trace event** — представление tracepoint/probe в `events/` с `format`/`filter`/
  `enable`.
- **hist trigger** — агрегация событий (гистограммы) в ядре, без userspace.
- **kprobe** — динамическая точка на произвольной инструкции ядра (через `int3`/
  optimized).
- **kretprobe** — перехват **возврата** функции (`entry_handler`+`handler`),
  измерение латентности/результата.
- **uprobe/uretprobe** — то же для функций userspace.
- **`pre_handler`/`post_handler`** — обработчики kprobe до/после оригинальной
  инструкции.
- **`maxactive`** — лимит одновременных «в полёте» kretprobe; превышение → `nmissed`.
- **`regs_get_kernel_argument`** — переносимое чтение n-го аргумента функции из
  `pt_regs`.
- **`regs_return_value`** — код возврата функции в kretprobe-обработчике.
- **optimized kprobe** (`CONFIG_OPTPROBES`) — замена `int3` на `jmp`/ftrace для
  скорости.
- **kprobe blacklist** — функции, на которые kprobe ставить нельзя (код kprobes,
  `noinstr`).
- **eBPF** — верифицируемый JIT-байткод в ядре; программируемая трассировка (и не
  только).
- **верификатор** — статически доказывает безопасность eBPF-программы до загрузки.
- **eBPF map** — типизированное хранилище состояния (hash/percpu-array/ringbuf/
  stack-trace).
- **ring buffer (BPF)** — канал доставки событий из eBPF в userspace.
- **BTF** — BPF Type Format: типы ядра, встроенные в `vmlinux`, основа CO-RE.
- **CO-RE** — Compile Once–Run Everywhere: один бинарь BPF работает на разных ядрах
  через BTF.
- **fentry/fexit** — дешёвый BPF-трамплин на вход/выход функции (через `__fentry__`).
- **bpftrace** — язык однострочников/скриптов поверх eBPF (`probe /filter/ { action }`).
- **bcc** — набор готовых eBPF-инструментов (`funccount`/`funclatency`/`offcputime`/...).
- **`profile`** — сэмплирующий профилировщик стеков (для flame graph).
- **flame graph** — визуализация профиля стеков (ширина = доля CPU-времени).
- **off-CPU анализ** — где задачи спят (стек в момент ухода с CPU + время).
- **ORC unwinder** (`CONFIG_UNWINDER_ORC`) — точная раскрутка стека ядра без frame
  pointer.
- **`trace_printk`** — дешёвый печать в ftrace ring buffer (отладка, не прод).
- **dynamic debug** — включение `pr_debug` на лету по файлу/функции без пересборки.
- **oops** — сообщение о некритичной ошибке ядра (дамп регистров/стека/`RIP`).
- **panic** — фатальная ошибка ядра; ведёт к остановке/kdump.
- **kallsyms** — встроенная таблица `адрес↔имя` символов ядра (`/proc/kallsyms`).
- **`addr2line` / `faddr2line`** — адрес/функция+offset → `файл:строка`.
- **`decode_stacktrace.sh`** — скрипт ядра, разрешающий весь стек oops в строки.
- **build-id** — хэш сборки в ELF; сопоставляет дамп с правильным `vmlinux`/debuginfo.
- **kexec** — загрузка нового ядра без BIOS-перезагрузки (основа kdump).
- **kdump** — снятие дампа памяти упавшего ядра через capture-ядро.
- **`crashkernel=`** — параметр загрузки: резерв памяти под capture-ядро (нужен заранее).
- **vmcore** — образ памяти ядра в момент паники (ELF/`ET_CORE`).
- **`crash`** — «gdb для ядра» поверх `vmcore`+`vmlinux` (`bt`/`ps`/`struct`/...).
- **`makedumpfile`** — фильтрация/сжатие vmcore (исключить нулевые/user-страницы).
- **`perf_event_paranoid`** — sysctl: насколько perf/eBPF доступны без root.
- **`kptr_restrict`** — sysctl: показывать ли реальные адреса ядра (`/proc/kallsyms`).
- **`trace-cmd` / KernelShark** — обёртка и GUI над ftrace.
- **`perf probe`** — создание kprobe/uprobe по имени функции/переменной через DWARF.

---

## 21. Что дальше

K7 закрывает Этап 2B (ядро ремесла): ты прошёл драйверы (K1), синхронизацию (K2),
отложенную работу (K3), память ядра (K4), VFS/procfs (K5), сетевой стек (K6) — и
теперь умеешь **наблюдать** их живьём: ftrace/tracepoints/kprobe, eBPF/bpftrace,
flame graph, декодирование oops, kdump. Это не «глава на прочитать» — это фон всей
дальнейшей работы в ядре: любой баг/перформанс-вопрос начинается с «повешу пробу /
сниму профиль / прочитаю дамп», а не с «добавлю `printk` и пересоберу».

Прямые связки:
- **Контрибьюция в ядро** (воркфлоу из трека): собери ядро, найди тривиальную
  проблему в staging-драйвере, оформи патч по `Documentation/process/`, отправь через
  `git send-email`. Трассировка — твой инструмент понять, что патч действительно
  чинит.
- **Спец. (a) Ядро и драйверы** — реальные подсистемы (input, v4l2/медиа — близко к
  томографии, USB, PCI), device model, upstream-качество. K7-инструменты —
  ежедневный фон отладки драйвера.
- **Спец. (c) Высокопроизводительные сети** — eBPF/XDP (K6 §13), bpftrace по сетевым
  событиям, flame graph горячего пути.
- **Назад к Ф4** — теперь ты видишь обе стороны: userspace-инструменты (perf, flame
  graph, core dump) и их ядровую изнанку (ftrace, kallsyms, vmcore).

> **Критерий готовности модуля:** ты инструментируешь свои функции ядра/драйвера
> через kprobe/ftrace, пишешь bpftrace-скрипт на kprobe своей функции, снимаешь flame
> graph ядра под нагрузкой, декодируешь стек oops без исходников и знаешь, что
> настроить заранее для `vmcore`. Тогда — Этап 2B пройден, выбирай специализацию.
