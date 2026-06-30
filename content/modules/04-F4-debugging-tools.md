# Модуль Ф4 — Инструменты отладки

## 0. Карта модуля

| | |
|---|---|
| **Время** | 10–15 ч |
| **Зачем** | Без отладочных инструментов системный программист слеп. GDB, Valgrind, AddressSanitizer, strace, perf — это ежедневные инструменты профессионала. Умение читать core dump, находить утечки памяти, профилировать CPU — обязательные навыки. |
| **Ресурсы** | `man gdb`, Valgrind manual, perf wiki, brendangregg.com |

**Шпаргалка: симптом → инструмент** (детали — в соответствующих разделах):

| Симптом | Первый инструмент | Раздел |
|---|---|---|
| Падает (SIGSEGV/SIGABRT) | core dump + GDB `bt`; ASan для точного места | §1.5, §2, §12 |
| Падает изредка / не воспроизводится | `rr record`/`replay`, core под нагрузкой | §11, §20 |
| Утечка памяти | ASan/LSan, Valgrind, `mtrace`/`mallinfo2`, heaptrack | §2, §3, §8, §13 |
| Порча кучи («double free», «corrupted») | ASan, `MALLOC_CHECK_=3`, Valgrind | §2, §13, §3 |
| Чтение неинициализированного | Valgrind memcheck или MSan (ASan не ловит) | §2.3, §3 |
| Гонка данных (многопоточность) | TSan, helgrind (ASan не ловит) | §2.3, §3.3 |
| Deadlock / зависло | GDB `thread apply all bt`, helgrind | §1.6, §10.1 |
| «Не открывает файл», странное поведение | strace (`-e trace=file`, inject) | §4 |
| Медленно (CPU) | perf `record`/`report`, flame graph | §6, §17 |
| Медленно, но CPU не занят | off-CPU анализ, strace на блокировке | §17.5, §4 |
| Плохо масштабируется по ядрам | `perf c2c` (false sharing), `perf mem` (NUMA) | §17.3, §17.4 |
| Крэш в проде, есть только адрес | `addr2line` + offset из maps, split debuginfo | §9, §18.4 |
| Лишние/неожиданные syscalls | strace `-k` (стек на вызов), `perf trace` | §4.2, §6.6 |
| Тестировать обработку ошибок (ENOMEM, EACCES) | strace inject (fault injection) | §4.1 |
| Баг в ядре/драйвере/embedded | ftrace, kgdb, QEMU `-s -S`, eBPF | §14, §18.3 |

---

## 1. GDB — основы

### 1.1 Запуск и базовые команды

Прежде чем запускать GDB, программу нужно скомпилировать с отладочной информацией. Без флага `-g` GDB покажет только адреса и дизассемблированный код — без имён переменных и строк файла.

> **Что такое DWARF.** Это стандартный **формат отладочной информации** (имя — шуточная пара к «ELF»), который `-g` встраивает в бинарник: таблицы «адрес ↔ строка исходника», типы, имена и расположение переменных (в каком регистре/на стеке для каждого диапазона PC), границы функций, данные для раскрутки стека (CFI, §19.3). Именно его читают GDB, `addr2line`, `perf`. Лежит в секциях `.debug_*` (`.debug_info`, `.debug_line`, ...); их можно вынести в отдельный файл символов (split debug info, §18.4).

```bash
gcc -g -O0 prog.c -o prog   # -g добавляет DWARF, -O0 отключает оптимизации
gcc -g3 prog.c -o prog       # -g3 включает макро-информацию
# -Og: оптимизации совместимые с отладкой (лучше чем -O0 для реального кода)
```

Разница между уровнями:
- `-g` / `-g2`: базовая DWARF-информация (имена, типы, строки)
- `-g3`: дополнительно — макроопределения через `#define`
- `-Og`: оптимизации не ломающие однозначное соответствие строк кода и инструкций; компилятор оставляет «ручки» для отладчика

Запуск GDB:

```
gdb ./prog                    # запустить программу
gdb ./prog core               # анализ core dump
gdb --args ./prog arg1 arg2  # с аргументами командной строки
```

Базовые команды управления исполнением:

```
(gdb) run [args]             # запустить программу (можно с аргументами)
(gdb) break main             # breakpoint на функции
(gdb) break file.c:42        # breakpoint на конкретной строке файла
(gdb) break *0x400abc        # breakpoint по абсолютному адресу
(gdb) info breakpoints       # список всех breakpoints
(gdb) delete 2               # удалить breakpoint #2
(gdb) disable 1              # временно отключить breakpoint #1
(gdb) enable 1               # включить обратно
(gdb) continue  (c)          # продолжить выполнение до следующего bp
(gdb) next      (n)          # следующая строка (не входить в вызываемые функции)
(gdb) step      (s)          # следующая строка (войти в функции)
(gdb) finish                 # выполнять до выхода из текущей функции
(gdb) until 55               # выполнять до строки 55 (полезно для выхода из циклов)
(gdb) return <value>         # принудительно вернуть значение из функции
(gdb) quit                   # выйти из GDB
```

Важное различие `next` и `step`: если текущая строка содержит вызов функции, `next` перешагнёт весь вызов целиком, `step` войдёт внутрь функции. В реальной отладке это принципиально — не входить в функции стандартной библиотеки.

### 1.2 Просмотр данных

```
(gdb) print x                # вывести значение переменной x
(gdb) print *ptr             # разыменовать указатель
(gdb) print arr[5]           # элемент массива
(gdb) print arr[0]@10        # 10 элементов массива начиная с 0
(gdb) print/x x             # вывести в шестнадцатеричном формате
(gdb) print/t x             # в двоичном формате
(gdb) print/d x             # в десятичном со знаком
(gdb) print/u x             # в десятичном без знака
(gdb) print/c x             # как символ ASCII
(gdb) print/f x             # как float
(gdb) display x             # автовывод x при каждой остановке программы
(gdb) undisplay 1           # отменить автовывод #1
(gdb) x/10xw 0x600000       # дамп памяти: 10 слов (word=4B) в hex
(gdb) x/10xg 0x600000       # дамп памяти: 10 giant (8B) в hex
(gdb) x/s 0x601020          # читать строку (null-terminated) по адресу
(gdb) x/20i $rip            # 20 инструкций начиная с IP
(gdb) info locals            # все локальные переменные текущего фрейма
(gdb) info args              # аргументы текущей функции
(gdb) backtrace (bt)         # стек вызовов (все фреймы)
(gdb) bt full                # стек + локальные переменные каждого фрейма
(gdb) frame 3                # переключиться на фрейм #3
(gdb) up / down              # перемещаться на фрейм выше/ниже
(gdb) list                   # показать исходный код вокруг текущей позиции
(gdb) list 40,60             # строки 40-60
(gdb) list func              # код вокруг функции func
```

Формат `x/NFS`: N — количество, F — формат (x hex, d decimal, u unsigned, f float, s string, i instruction), S — размер (b byte=1, h halfword=2, w word=4, g giant=8).

Пример полезного дампа памяти:

```
(gdb) x/16xb 0x7fffffffd000   # 16 байт в hex — хорошо видна структура
(gdb) x/4xg $rsp              # содержимое стека (верхние 4 записи)
```

### 1.3 Watchpoints

Watchpoints — точки наблюдения за памятью. В отличие от breakpoints (останавливают по адресу кода), watchpoints останавливают при изменении данных. Это единственный способ найти "кто изменил эту переменную?" без перечитывания всего кода.

```
(gdb) watch x               # остановиться при изменении значения x
(gdb) rwatch x              # остановиться при чтении x
(gdb) awatch x              # при чтении или записи
(gdb) watch -l x            # location watchpoint: следить за адресом, а не именем
(gdb) watch *(int*)0x601020 # watchpoint по абсолютному адресу
(gdb) info watchpoints      # список всех watchpoints
```

Аппаратные watchpoints (поддерживаемые процессором через debug registers DR0-DR3 на x86) работают с нулевым overhead. Программные — останавливают после каждой инструкции, на порядки медленнее. GDB выбирает аппаратные автоматически, но их количество ограничено (обычно 4 на x86).

### 1.4 Условные breakpoints и команды

```
(gdb) break loop.c:42 if i == 100       # остановить только когда i == 100
(gdb) break func if strcmp(s, "bad")==0  # вызов функции в условии
(gdb) condition 2 i > 50                # добавить условие к существующему bp #2
(gdb) condition 2                       # снять условие с bp #2

(gdb) commands 1           # задать команды при срабатывании breakpoint #1
> print i
> print arr[i]
> continue
> end
```

Блок `commands` с `continue` превращает breakpoint в автоматический трейсинг — программа не останавливается, просто выводит данные. Полезно для трассировки горячих путей без модификации кода.

Игнорирование первых N срабатываний:

```
(gdb) ignore 1 999          # игнорировать bp #1 первые 999 раз
```

### 1.5 Core dumps

Core dump — снимок состояния процесса в момент аварийного завершения. Содержит: регистры, стек, сегменты памяти с флагом `PT_LOAD`. Позволяет post-mortem анализ без повторного воспроизведения крэша.

```bash
# Включить core dumps (по умолчанию отключены)
ulimit -c unlimited

# systemd: настройка через sysctl
echo 'kernel.core_pattern = /var/cores/core.%e.%p.%t' > /etc/sysctl.d/core.conf
sysctl --system

# Запустить и получить core
./prog   # если упало, создаст core или core.PID
```

Анализ:

```
gdb ./prog core
(gdb) bt                   # где именно произошёл крэш
(gdb) bt full              # полный стек + локальные переменные
(gdb) info registers       # состояние всех регистров в момент крэша
(gdb) frame 0              # первый (ближайший к крэшу) фрейм
(gdb) print errno          # что вернул последний syscall
(gdb) info proc mappings   # карта памяти процесса
```

Частые причины крэша и признаки:
- `SIGSEGV` — обращение по невалидному адресу (NULL, за границы)
- `SIGABRT` — `abort()` или failed assertion
- `SIGFPE` — деление на ноль или overflow
- `SIGBUS` — MMAP-страница исчезла (обращение за конец укороченного файла) или невыровненный доступ **на архитектурах, требующих выравнивания** (SPARC, часть ARM-конфигураций). На x86-64 обычный невыровненный скалярный доступ аппаратно разрешён и **не** вызывает `SIGBUS` (хотя это и UB, который ловит UBSan); фолтят лишь отдельные SIMD-инструкции с требованием выравнивания

### 1.6 Многопоточная отладка

GDB по умолчанию останавливает все потоки при попадании в breakpoint (режим `all-stop`). Переключение между потоками:

```
(gdb) info threads              # список всех потоков с их состоянием
(gdb) thread 3                  # переключиться на поток 3
(gdb) thread apply all bt       # вывести backtrace всех потоков
(gdb) thread apply all bt full  # с локальными переменными
(gdb) thread apply 1 2 print x  # команда к конкретным потокам

(gdb) set scheduler-locking on   # заморозить остальные потоки пока выполняем step
(gdb) set scheduler-locking off  # вернуть нормальный режим
(gdb) set scheduler-locking step # замораживать только при step, не при continue
```

Отладка дедлоков — классическая задача:

```
# 1. Программа зависла. Ctrl+C в GDB — прервать.
(gdb) thread apply all bt    # смотрим где каждый поток ждёт

# Типичная картина дедлока:
# Thread 1: pthread_mutex_lock → ждёт mutex_A
# Thread 2: pthread_mutex_lock → ждёт mutex_B
# И при этом Thread 1 держит mutex_B, Thread 2 держит mutex_A
```

### 1.7 Отладка fork/exec и старт с первой инструкции

Отдельная боль — отладить **дочерний** процесс после `fork()` (мост к Ф3). По умолчанию GDB остаётся с родителем:

```
(gdb) set follow-fork-mode child    # после fork идти в потомка
(gdb) set follow-fork-mode parent   # (по умолчанию) оставаться в родителе
(gdb) set detach-on-fork off        # держать ОБА процесса под отладкой
(gdb) set follow-exec-mode new      # при execve переключиться на новый образ
(gdb) starti                        # остановиться на самой первой инструкции (до _start)
```

`starti` бесценен, когда баг в инициализации рантайма/динамического линкера — раньше `main`. `set detach-on-fork off` + `info inferiors` позволяет переключаться между родителем и потомком (`inferior 2`).

### 1.8 `.gdbinit` и удобства

Файл `~/.gdbinit` (или `./.gdbinit` в проекте) выполняется при старте — туда выносят настройки на каждый день:

```
set history save on              # сохранять историю команд между сессиями
set history size 100000
set print pretty on              # структуры — с отступами, по полю на строку
set print array on
set pagination off               # не останавливать длинный вывод на "---Type <return>"
set disassembly-flavor intel     # синтаксис Intel вместо AT&T
set confirm off                  # не переспрашивать на quit/delete

# свои команды:
define btall
  thread apply all backtrace
end
```

GDB для C++ автоматически подгружает **pretty-printers** libstdc++ (`std::vector`/`std::string`/`std::map` печатаются человекочитаемо, а не как сырые поля) — для трека это станет важно на Этапе 2A (C++). Проектный `.gdbinit` подхватывается только при `set auto-load safe-path` или явном `source`.

---

## 2. AddressSanitizer (ASan) и UBSan

AddressSanitizer — инструментатор компилятора, встроенный в GCC и Clang. Обнаруживает ошибки памяти с минимальным overhead по сравнению с Valgrind. Механизм: тень-память (shadow memory), каждые 8 байт реальной памяти описываются 1 байтом тени — при каждом обращении вставляется проверка тени.

```bash
gcc -fsanitize=address,undefined -g -O1 prog.c -o prog
# -O1 важно: делает код читаемее чем -O0, но сохраняет отладочную информацию
```

### 2.1 ASan — что обнаруживает

- **Heap buffer overflow**: выход за границы heap аллокации (как до начала, так и после конца)
- **Stack buffer overflow**: выход за границы массива на стеке
- **Use-after-free**: обращение к памяти после `free()` — один из самых опасных классов уязвимостей
- **Use-after-return**: доступ к локальной переменной через указатель после возврата функции
- **Use-after-scope**: доступ к переменной вне блока видимости
- **Double free**: вызов `free()` дважды на одном блоке
- **Memory leaks**: с `ASAN_OPTIONS=detect_leaks=1` (включено по умолчанию на Linux)

Пример вывода ASan при heap-buffer-overflow:

```
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000014
READ of size 4 at 0x602000000014 thread T0
    #0 0x401234 in main prog.c:5
    #1 0x7f1234 in __libc_start_main (/lib/libc.so.6+0x...)

0x602000000014 is located 0 bytes to the right of 40-byte region [0x602000000000,0x602000000028)
allocated by thread T0 here:
    #0 0x7f... in malloc (/usr/lib/libasan.so.8+0x...)
    #1 0x401200 in main prog.c:4
```

Как читать вывод:
- Тип ошибки и адрес
- Стек вызовов места ошибки (`READ of size 4 at`)
- Описание затронутого региона (`0 bytes to the right of 40-byte region`) — блок из 40 байт, обращение на 1 элемент int за его конец
- Стек вызовов места аллокации

Управление через переменные окружения:

```bash
ASAN_OPTIONS=detect_leaks=1:halt_on_error=0:log_path=/tmp/asan ./prog
# detect_leaks   — включить LeakSanitizer
# halt_on_error  — продолжать после первой ошибки (0) или нет (1, default)
# log_path       — писать отчёт в файл
```

### 2.2 UBSan — что обнаруживает

UndefinedBehaviorSanitizer обнаруживает undefined behavior языка C и C++ — поведение, которое "работает" в одних условиях и падает в других.

Категории проверок:

- **Signed integer overflow**: `INT_MAX + 1` — UB и в C, и в C++
- **Shift count out of range**: `x << 33` для 32-битного `x`, `x >> -1`
- **Division by zero**: `a / 0` для целых
- **Null pointer dereference**: через pointer-атрибуты (не все случаи)
- **Invalid enum value**: кастирование числа в enum, которого нет в перечислении
- **Misaligned pointer dereference**: `*(int*)odd_address`
- **VLA с неположительной длиной**: `int a[n]` при `n <= 0` (именно VLA, а `int arr[0]` — это уже константный zero-length массив, расширение GNU)

```bash
# Отдельные проверки (для понимания что именно включается):
-fsanitize=signed-integer-overflow
-fsanitize=unsigned-integer-overflow   # не в -fsanitize=undefined
-fsanitize=shift
-fsanitize=null
-fsanitize=alignment
-fsanitize=bounds                       # проверка границ VLA/array с фиксированным размером
-fsanitize=undefined                    # всё стандартное UB

# Остановка при первой ошибке:
UBSAN_OPTIONS=halt_on_error=1 ./prog
UBSAN_OPTIONS=print_stacktrace=1 ./prog  # показывать стек (нужно -g)
```

Типичная ошибка в реальном коде:

```c
// UB: signed overflow
int days_until = end_timestamp - start_timestamp;  // переполнение при разнице > 2^31

// UB: misaligned access
char buf[8];
int *p = (int*)(buf + 1);  // адрес не кратен 4
*p = 42;                    // UBSan: misaligned address

// UB: shift
uint32_t flags = 1 << 31;  // 1 имеет тип int (signed), shift на sign bit = UB
uint32_t flags = 1u << 31; // правильно: 1u unsigned
```

### 2.3 Другие санитайзеры

**ThreadSanitizer (-fsanitize=thread)**: обнаруживает data races между потоками. Механизм — **vector clocks** (векторные часы: каждому потоку — свой логический счётчик, по их сравнению TSan восстанавливает отношение «happens-before» между обращениями и видит, что два доступа к одной памяти **не упорядочены** ни блокировкой, ни иной синхронизацией → гонка) на каждый доступ к памяти. Overhead: ~5-15× по скорости, 5-10× по памяти. Несовместим с ASan (конфликт shadow memory).

```bash
gcc -fsanitize=thread -g -O1 prog.c -o prog -lpthread
TSAN_OPTIONS=history_size=7 ./prog  # больше истории = больше деталей гонки
```

**MemorySanitizer (-fsanitize=memory)**: обнаруживает использование неинициализированной памяти. Только Clang. Требует инструментации ВСЕХ зависимостей, включая libc (сложно).

```bash
clang -fsanitize=memory -g -O1 prog.c -o prog
MSAN_OPTIONS=poison_in_dtor=1 ./prog
```

**LeakSanitizer (-fsanitize=leak)**: утечки памяти без overhead ASan. На Linux можно запустить отдельно (без полного ASan overhead):

```bash
gcc -fsanitize=leak -g prog.c -o prog
LSAN_OPTIONS=verbosity=1:log_threads=1 ./prog
```

Сравнение:

| Инструмент | Что ловит | Overhead скорость | Overhead память |
|---|---|---|---|
| ASan | overflow, use-after-free, leaks | 2× | 2× |
| TSan | data races | 5-15× | 5-10× |
| MSan | uninit memory | 3× | 2× |
| UBSan | UB в C/C++ | <1.5× | минимум |
| LSan | только leaks | ~1× | минимум |

### 2.4 Тонкости санитайзеров для реального кода

Базовый `-fsanitize=address,undefined` — только начало. В реальных проектах нужны точечные настройки.

**Богатые `ASAN_OPTIONS`:**

```bash
ASAN_OPTIONS=\
detect_stack_use_after_return=1:\  # ловить use-after-return (стоит overhead)
detect_leaks=1:\
abort_on_error=1:\                 # abort() вместо exit → остаётся core dump
strict_string_checks=1:\           # строгая проверка strcpy/strlen и т.п.
check_initialization_order=1:\     # порядок инициализации глобалов (C++)
quarantine_size_mb=256 ./prog      # больше карантин → дольше держит freed-блоки для UAF
```

`abort_on_error=1` особенно важен в CI: ASan по умолчанию делает `exit(1)`, и core не создаётся; с `abort()` получаешь и отчёт ASan, и core с полным состоянием для последующего `gdb`.

**Точечное отключение инструментирования.** Иногда функция намеренно делает «грязную» работу (свой аллокатор, доступ к чужой памяти) — её исключают из проверок:

```c
__attribute__((no_sanitize("address")))
void *my_raw_pool_access(void *base, size_t off) { return (char*)base + off; }

__attribute__((no_sanitize("undefined")))
int intentional_wraparound(int x) { return x * 31 + 7; }  // хеш с осознанным переполнением
```

**Подавление известных срабатываний (suppressions)** — когда баг в сторонней библиотеке, который ты не чинишь:

```bash
# leaks.supp:
#   leak:libcrypto.so
#   leak:OPENSSL_init
ASAN_OPTIONS=suppressions=leaks.supp ./prog
LSAN_OPTIONS=suppressions=leaks.supp ./prog   # для утечек — отдельный канал
```

**UBSan: trap-режим для продакшена.** По умолчанию UBSan печатает диагностику и продолжает. Два полезных режима:

```bash
gcc -fsanitize=undefined -fno-sanitize-recover=all prog.c   # abort при ПЕРВОМ UB (для CI)
gcc -fsanitize=undefined -fsanitize-trap=undefined prog.c    # без рантайма: UB → SIGILL
```

`-fsanitize-trap` не тянет рантайм-библиотеку и просто вставляет `ud2` (недопустимая инструкция) в точке UB — крошечный overhead, годится включать даже в релиз как «жёсткий предохранитель»: UB превращается в немедленный управляемый крах вместо тихой порчи.

**Чего санитайзеры НЕ ловят (важно помнить):** ASan не видит гонок (нужен TSan) и чтения неинициализированного (нужен MSan/Valgrind); UBSan не видит ошибок памяти; TSan несовместим с ASan. Поэтому в зрелом проекте делают **несколько** CI-прогонов: `asan+ubsan`, отдельно `tsan`, периодически Valgrind на критичных путях.

---

## 3. Valgrind — анализ памяти

Valgrind — фреймворк динамического анализа бинарного кода. Работает иначе чем ASan: не требует перекомпиляции, инструментирует бинарный код на лету через JIT-компилятор. Значительно медленнее ASan, но работает с любым бинарником, включая закрытый код.

### 3.1 Memcheck (по умолчанию)

```bash
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --verbose \
         ./prog 2>&1 | tee valgrind.log
```

Ключевые опции:
- `--leak-check=full`: показать каждую утечку с полным стеком аллокации
- `--show-leak-kinds=all`: показывать все категории утечек
- `--track-origins=yes`: отслеживать источник uninit значений (дорого, но очень полезно)
- `--error-exitcode=1`: вернуть ненулевой код при ошибках (для CI)
- `--suppressions=file.supp`: подавить ложные срабатывания (для библиотек с "утечками" по дизайну)

Категории утечек в отчёте:

- **definitely lost**: нет ни одного указателя на этот блок — настоящая утечка
- **indirectly lost**: блок **достижим только через** уже потерянный (definitely lost) блок (например, узлы списка, на которые указывает лишь потерянная структура) — жив через потерянного «родителя», но из активной памяти недостижим; почини «родителя» — исчезнет и этот
- **possibly lost**: есть указатель, но он смотрит в середину блока (нестандартный аллокатор?)
- **still reachable**: есть указатель на начало блока, но `free()` не вызван до завершения программы

`still reachable` — не всегда проблема. Например, глобальные singleton-объекты или буферы, которые живут до конца программы. Ядро освободит память. Но при повторном использовании между тестами (в `fork` + exec паттерне) это важно.

Overhead: ~10-50× медленнее нативного исполнения. Зато находит то, что ASan не находит: использование неинициализированных значений через тени-биты (без `--track-origins` это буквально бесплатно).

Что Valgrind memcheck находит сверх ASan:
- Чтение неинициализированной памяти (stack или heap) — MSan это тоже делает, но только Clang
- Неинициализированные условия (branching on uninit)
- Невалидное использование syscall-аргументов

### 3.2 Callgrind — профилирование инструкций

```bash
valgrind --tool=callgrind ./prog
callgrind_annotate callgrind.out.$PID       # текстовый отчёт
callgrind_annotate --auto=yes callgrind.out.$PID  # с аннотацией кода
```

Callgrind считает количество инструкций CPU по функциям и строкам кода. В отличие от perf (sampling), это точный подсчёт без погрешности выборки. Overhead: программа выполняется в 20-100× медленнее.

Дополнительные опции:

```bash
valgrind --tool=callgrind --cache-sim=yes ./prog   # симулировать кэш (L1/L2)
valgrind --tool=callgrind --branch-sim=yes ./prog  # симулировать branch predictor
```

Визуализация в KCachegrind (graphical):

```bash
kcachegrind callgrind.out.$PID
```

KCachegrind показывает call graph, аннотированный код, самые дорогие функции. Отличный инструмент для алгоритмической оптимизации.

### 3.3 Helgrind — data races

```bash
valgrind --tool=helgrind ./prog
```

Helgrind обнаруживает data races и lock ordering violations (потенциальные дедлоки). Медленнее ThreadSanitizer, но находит больше потенциальных races, включая те где гонка ещё не реализовалась.

Особенность: Helgrind отслеживает порядок захвата мьютексов. Если Thread 1 берёт `A → B`, а Thread 2 берёт `B → A` — Helgrind предупредит о потенциальном дедлоке, даже если он ещё не произошёл.

### 3.4 Massif — heap profiler

```bash
valgrind --tool=massif ./prog
ms_print massif.out.$PID           # текстовый отчёт
valgrind --tool=massif --pages-as-heap=yes ./prog  # считать mmap как heap
```

Massif показывает динамику потребления heap памяти во времени: когда был пик, что его вызвало, дерево аллокаций. Полезно когда программа "постепенно пухнет" — не утечка, но растёт без причины.

### 3.5 Valgrind или ASan: что выбрать

Оба ищут ошибки памяти, но по-разному — это не дубликаты, а взаимодополнение:

| Критерий | ASan | Valgrind memcheck |
|---|---|---|
| Нужна перекомпиляция | да (`-fsanitize=address`) | нет (любой бинарник) |
| Overhead | ~2× | ~10–50× |
| Чтение неинициализированного | **не ловит** (нужен MSan) | **ловит** |
| Точность стека `free`/`malloc` | очень высокая | высокая |
| Stack/global overflow | ловит | ловит хуже (heap — сильная сторона) |
| Закрытый бинарник без исходников | нельзя | **можно** |
| Подходит для CI на каждый коммит | да (быстро) | скорее для ночных прогонов |

Практика: **ASan по умолчанию** в dev/CI (дёшево, точно), **Valgrind** — когда нет исходников, когда нужен поиск чтения неинициализированного без перехода на Clang/MSan, или как «второе мнение» на сложном баге. Запускать одновременно бессмысленно — ASan-бинарник под Valgrind работать корректно не будет.

Пример вывода `ms_print`:

```
    MB
18.22^                                                         #
     |                                                      #  #
     |                                                    ##   #
     |                                                 ###     #
     |                                               ##        #
     |                                           ####          #
     ...

99.90% (19,112,192B) (heap allocation functions)
->38.42% (7,340,032B): 0x402A3C: load_data (main.c:45)
->61.48% (11,772,160B): 0x401B2F: parse_input (parser.c:88)
```

---

## 4. strace — трассировка системных вызовов

strace перехватывает и отображает системные вызовы процесса. Механизм: `ptrace(PTRACE_SYSCALL)` — ядро останавливает процесс при каждом входе/выходе из syscall. Overhead существенный (~10×), но незаменимо для диагностики.

```bash
strace ./prog                        # трассировать с запуска
strace -p PID                        # прикрепиться к уже запущенному процессу
strace -e trace=read,write ./prog    # только указанные syscalls
strace -e trace=network ./prog       # только сетевые (connect, send, recv...)
strace -e trace=file ./prog          # только файловые (open, stat, unlink...)
strace -e trace=process ./prog       # процессные (fork, exec, wait...)
strace -e trace=signal ./prog        # сигналы
strace -e trace=ipc ./prog           # IPC (pipe, msgget, shmget...)
strace -c ./prog                     # только статистика по syscall-ам
strace -T ./prog                     # время выполнения каждого syscall
strace -t ./prog                     # timestamp для каждой строки
strace -tt ./prog                    # timestamp с микросекундами
strace -ttt ./prog                   # Unix timestamp
strace -f ./prog                     # следовать за fork/thread (все дочерние)
strace -ff -o trace ./prog           # каждый процесс/поток в отдельный файл
strace -o output.txt ./prog          # весь вывод в файл
strace -s 256 ./prog                 # показывать строки длиной до 256 байт (default 32)
strace -y ./prog                     # показывать пути к файлам для fd
strace -yy ./prog                    # показывать IP:port для сокетов тоже
```

> **Почему `strace -p PID` часто отвечает `Operation not permitted` (и как это лечить).**
> Все эти инструменты (strace, ltrace, `gdb -p`) работают через `ptrace(2)`, а к **чужому
> уже запущенному** процессу его пускают не всегда. На большинстве дистрибутивов включён
> модуль безопасности **Yama**, и `kernel.yama.ptrace_scope` по умолчанию `1` — «прицепиться
> можно только к своему потомку». Поэтому `strace ./prog` (ты сам запустил → потомок)
> работает, а `strace -p` к постороннему PID — нет. Варианты: запускать под `sudo` (root
> обходит ограничение), либо временно ослабить политику:
> ```bash
> cat /proc/sys/kernel/yama/ptrace_scope        # 0 нет огранич., 1 только потомки,
>                                               # 2 только root+CAP_SYS_PTRACE, 3 запрещено
> sudo sysctl kernel.yama.ptrace_scope=0        # разрешить attach к своим процессам
> ```
> Отдельно: процесс можно трассировать **только одним** ptrace-клиентом сразу — нельзя
> одновременно держать его под `gdb` и `strace`, и нельзя приаттачиться к процессу,
> который уже под отладчиком.

Пример вывода при запуске простой программы:

```
execve("./prog", ["./prog"], 0x7fff... /* 23 vars */) = 0
brk(NULL)                            = 0x5555556d6000
mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7f...
openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
fstat(3, {st_mode=S_IFREG|0644, st_size=204232, ...}) = 0
mmap(NULL, 204232, PROT_READ, MAP_PRIVATE, 3, 0) = 0x7f...
close(3)                             = 0
...
write(1, "hello\n", 6)               = 6
exit_group(0)                        = ?
+++ exited with 0 +++
```

Типичные задачи strace:

**"Почему программа не открывает файл?"**

```bash
strace -e trace=file ./prog 2>&1 | grep -E 'open|ENOENT|EACCES|EPERM'
# Видно точный путь который программа пытается открыть
# и системную ошибку: ENOENT (нет файла), EACCES (нет прав)
```

**"Что делает программа с сетью?"**

```bash
strace -e trace=network -s 512 ./prog
# Видны connect() с адресами, send/recv с первыми 512 байтами данных
```

**"Где программа тормозит?"**

```bash
strace -c ./prog       # итоговая статистика: время и число вызовов
strace -T ./prog 2>&1 | sort -t'<' -k2 -rn | head -20  # топ самых долгих
```

**"Почему программа зависла?"**

```bash
strace -p $PID         # прикрепиться → видим на каком syscall заблокирована
# Например: futex(0x..., FUTEX_WAIT...) — ждёт мьютекс
# read(5, ...) — ждёт данных с fd 5
# strace -yy -p $PID   # покажет что за fd 5 (socket IP:port?)
```

### 4.1 Инъекция ошибок: тестирование путей обработки

Мало кто знает, что strace умеет не только наблюдать, но и **вмешиваться** — подменять результат syscall. Это бесплатный fault injection для проверки кода обработки ошибок, которые в норме почти не воспроизвести:

```bash
# Заставить КАЖДЫЙ openat вернуть ENOENT — проверить, как код реагирует:
strace -e inject=openat:error=ENOENT ./prog

# Только на 3-й вызов (остальные настоящие):
strace -e inject=openat:error=ENOENT:when=3 ./prog

# Внести задержку 100мс на каждый read — смоделировать медленный диск/сеть:
strace -e inject=read:delay_enter=100000 ./prog

# Доставить сигнал на конкретном syscall:
strace -e inject=write:signal=SIGUSR1 ./prog
```

Это прямой способ проверить, что код действительно обрабатывает `ENOMEM`/`EACCES`/`EINTR`, а не игнорирует их (мост к Ф2: «проверяй возвращаемое значение»). В тестах так находят ветки, которые иначе никогда не исполняются.

### 4.2 Стек на каждом syscall

```bash
strace -k ./prog                # к каждому syscall — стек вызовов userspace
strace -k -e trace=openat ./prog | less
```

`-k` (нужен libunwind) печатает стек до каждого вызова — отвечает не только «какой syscall», но и «из какого места кода он сделан». Незаменимо, когда «лишний» `openat`/`stat` делает не твой код, а библиотека в глубине.

---

## 5. ltrace — трассировка библиотечных вызовов

ltrace работает аналогично strace, но перехватывает вызовы функций динамических библиотек (через PLT — Procedure Linkage Table), а не syscalls.

```bash
ltrace ./prog                  # трассировать все вызовы libc
ltrace -e malloc,free ./prog   # только malloc и free
ltrace -e 'malloc,free,realloc,calloc' ./prog
ltrace -c ./prog               # статистика: count и time
ltrace -l /lib/libpthread.so ./prog  # вызовы конкретной библиотеки
ltrace -f ./prog               # следовать за fork
ltrace -n 4 ./prog             # отступ 4 пробела для вложенных вызовов
```

Типичное использование — трассировка аллокаций:

```
malloc(40)                                       = 0x55a4c...
memset(0x55a4c..., '\0', 40)                    = 0x55a4c...
strdup("hello world")                            = 0x55a4c...
free(0x55a4c...)                                 = <void>
```

ltrace видит то, что strace не видит: вызовы `malloc`, `strcpy`, `printf` до того как они опустятся до syscall уровня. Но ltrace не видит inline-функции и статически слинкованные библиотеки.

---

## 6. perf — профилирование производительности

perf — стандартный профилировщик Linux на основе аппаратных счётчиков производительности (PMU — Performance Monitoring Unit). Два режима: счётчики (точный подсчёт событий) и sampling (статистическая выборка).

> **Если perf под обычным пользователем говорит «You may not have permission to collect
> [stats|samples]».** Доступ к PMU регулирует sysctl `kernel.perf_event_paranoid`: `3`/`2`
> (типичный дефолт) запрещают многое без root. Запускай под `sudo` либо ослабь:
> `sudo sysctl kernel.perf_event_paranoid=1` (≤1 хватает для `perf record`/стеков ядра,
> `-1` снимает почти все ограничения). Для **читаемых** символов ядра в профиле ещё нужен
> `kernel.kptr_restrict=0` (иначе адреса ядра — нули). То же ограничение действует на `rr`
> (§11) и eBPF-инструменты (§15).

### 6.1 perf stat — счётчики производительности

```bash
perf stat ./prog
```

Вывод:

```
 Performance counter stats for './prog':

       1,234.56 msec task-clock                #    0.987 CPUs utilized
          1,234      context-switches          #    0.999 K/sec
              0      cpu-migrations            #    0.000 /sec
            567      page-faults              #    0.459 K/sec
  4,567,890,123      cycles                   #    3.700 GHz
  3,456,789,012      instructions             #    0.76  insn per cycle (IPC)
    345,678,901      branches                 #  280.000 M/sec
     12,345,678      branch-misses            #    3.57% of all branches

       1.250 seconds time elapsed
```

Ключевые метрики:
- **IPC (instructions per cycle)**: эффективность использования CPU. 4.0 — идеал для современных суперскалярных CPU. Меньше 1.0 — серьёзная проблема (память? ветви?)
- **branch-misses%**: предсказание ветвлений. >5% — значительный overhead
- **page-faults**: обращения к странице, которой ещё нет в таблице страниц процесса. **minor** — без обращения к диску (страница уже в RAM: COW, zero-страница, или страница файла уже в page cache); **major** — потребовался диск (swap-in **или** первое чтение ещё не закэшированных страниц файла/`mmap`, в т.ч. demand-paging самого бинарника). Это **не** промахи TLB: те обрабатываются аппаратным page-walk'ом и считаются отдельными счётчиками `dTLB-load-misses`/`iTLB-load-misses`

Дополнительные счётчики:

```bash
perf stat -e cache-misses,cache-references,instructions,cycles ./prog
perf stat -e L1-dcache-loads,L1-dcache-load-misses,LLC-loads,LLC-load-misses ./prog
perf stat -e cpu-cycles,cpu-clock,task-clock,page-faults ./prog
```

Детальный анализ кэш-промахов:

```bash
perf stat -e L1-dcache-load-misses,L2-dcache-load-misses,LLC-load-misses ./prog
# L1: быстрейший (~4 цикла), L2 (~12 цикла), LLC (~40 цикла), RAM (~200 цикла)
# Если LLC-load-misses высок — данные не помещаются в кэш → data locality проблема
```

### 6.2 perf record/report — sampling профиль

```bash
perf record -g ./prog          # запись с call graph (-g для стека вызовов)
perf record -F 99 -g -p $PID  # 99 Hz sampling, прикрепиться к процессу
perf record -F 999 -g ./prog  # выше частота = точнее профиль, больше overhead
perf report                    # интерактивный TUI отчёт (навигация стрелками)
perf report --stdio            # текстовый отчёт (для скриптов, CI)
perf report --sort comm,dso,symbol  # сортировка: команда, библиотека, символ
```

В TUI `perf report`:
- `Enter` на функции — развернуть call graph
- `a` — аннотировать (показать код с процентами)
- `d` — фильтровать по DSO (динамической библиотеке)
- `t` — фильтровать по потоку
- `g` — переключить режим отображения call graph

Почему 99 Hz, а не 100 Hz: избегать резонанса с таймерами 100 Hz систем (HZ=100), которые могут искажать выборку.

### 6.3 perf top — профиль в реальном времени

```bash
perf top                           # общесистемный профиль (все процессы)
perf top -p $PID                   # конкретный процесс
perf top -g                        # с call graph
perf top --sort comm,symbol        # сортировать по команде и символу
```

perf top — аналог `top` для функций, а не процессов. Видно горячие функции в реальном времени без записи файлов. Полезно для быстрой диагностики "что сейчас нагружает CPU?".

### 6.4 Flame graphs (Brendan Gregg)

Flame graph — визуализация профиля CPU: ширина каждого прямоугольника пропорциональна времени CPU в этой функции. Горизонтальная ось — доля времени (не время), вертикальная — стек вызовов.

```bash
# Записать профиль
perf record -F 99 -a -g -- sleep 30

# Экспортировать стеки
perf script > perf.stacks

# Сгенерировать SVG (FlameGraph от Brendan Gregg)
git clone https://github.com/brendangregg/FlameGraph
./FlameGraph/stackcollapse-perf.pl perf.stacks | \
    ./FlameGraph/flamegraph.pl > flame.svg

# Открыть в браузере (интерактивно — клик для zoom)
firefox flame.svg
```

Как читать flame graph:
- Ширина блока = % времени CPU в этой функции (включая потомков)
- Высота = глубина стека вызовов
- Плоские "плато" наверху — горячие листовые функции, там нужно оптимизировать
- Широкое основание без плато — время размазано по многим функциям (хорошо)

Варианты flame graphs:
- **CPU flame graph**: где тратится время CPU
- **Off-CPU flame graph**: где процесс блокируется (I/O, locks)
- **Memory flame graph**: где аллоцируется память

### 6.5 perf для специфических событий

```bash
# Аппаратные события (PMU)
perf stat -e cpu/event=0xd1,umask=0x10/ ./prog   # raw hardware event

# Cache misses на конкретных операциях
perf stat -e cache-misses:u ./prog   # только userspace
perf stat -e cache-misses:k ./prog   # только kernel

# TLB misses
perf stat -e dTLB-loads,dTLB-load-misses,iTLB-loads,iTLB-load-misses ./prog

# Список доступных событий
perf list hardware
perf list software
perf list cache
perf list tracepoint
```

### 6.6 perf trace и top-down анализ

`perf trace` — это «strace на стероидах»: тот же перехват syscalls, но через perf-инфраструктуру, с **меньшим overhead** и возможностью трассировать систему целиком, не привязываясь к ptrace одного процесса:

```bash
perf trace ./prog                 # syscalls + длительности, как strace -T
perf trace -p $PID                # прицепиться к процессу
perf trace -a sleep 5             # вся система за 5 секунд
perf trace -e openat,read,write ./prog
```

Для микроархитектурного анализа на поддерживающих CPU есть **top-down** методология — она сразу классифицирует, чем «связан» код, вместо угадывания по отдельным счётчикам:

```bash
perf stat -d ./prog               # детальные счётчики (кэши L1/LLC, TLB)
perf stat --topdown ./prog        # разбивка: Frontend Bound / Backend Bound /
                                  # Bad Speculation / Retiring
```

Top-down отвечает на вопрос «почему низкий IPC» одним взглядом: высокий **Backend Bound** → упёрлись в память/исполнительные порты; высокий **Bad Speculation** → промахи предсказания ветвлений; высокий **Frontend Bound** → не успевает декодироваться/подгружаться код. Это направляет оптимизацию точнее, чем десяток разрозненных `-e cache-misses`.

---

## 7. GDB + отладка без исходников

Иногда исходников нет: закрытая библиотека, stripped бинарник, ядро. GDB продолжает работать на уровне машинных инструкций.

```
(gdb) disassemble main             # дизассемблировать функцию main
(gdb) disassemble 0x400abc         # дизассемблировать по адресу
(gdb) disassemble /r main         # показать hex опкоды + дизассемблирование
(gdb) set disassembly-flavor intel # синтаксис Intel (vs AT&T по умолчанию)
(gdb) info registers               # все регистры general purpose + флаги
(gdb) info all-registers           # все регистры включая FPU, SSE
(gdb) info register rip            # конкретный регистр
(gdb) p $rax                       # значение регистра через print
(gdb) x/20i $rip                  # 20 инструкций начиная с текущей позиции
(gdb) stepi (si)                  # один шаг на машинную инструкцию
(gdb) nexti (ni)                  # один шаг, не заходить в call
```

Разбор неизвестной структуры:

```
# Дамп памяти как 8-байтовые слова (указатели)
(gdb) x/20xg 0x55555678          # 20 × 8-байтных слов

# Если знаем смещения поля — можем назначить тип
(gdb) set $obj = (struct known_type*)0x55555678
(gdb) print $obj->field_name

# Или кастовать по ходу
(gdb) print *(int*)(0x55555678 + 16)   # поле на смещении 16
```

Python в GDB:

```python
# ~/.gdbinit или внутри GDB:
python
import gdb

class PrintAll(gdb.Command):
    def __init__(self):
        super().__init__("print-all", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        frame = gdb.selected_frame()
        block = frame.block()
        for sym in block:
            if sym.is_variable:
                val = frame.read_var(sym)
                print(f"{sym.name} = {val}")

PrintAll()
end
```

GDB Python scripting даёт возможность автоматизировать анализ: обходить структуры данных, проверять инварианты, генерировать отчёты — без ручного ввода команд.

### 7.1 Отладка stripped бинарников

```bash
# Проверить наличие символов
file ./prog        # "stripped" или нет
nm ./prog          # список символов (пусто если stripped)
nm -D ./prog       # динамические символы (обычно есть даже после strip)
readelf -s ./prog  # полный список из ELF символьных таблиц
```

Работа со stripped бинарником в GDB:

```
# Нет символов — работаем с адресами
(gdb) info proc mappings          # карта памяти — где загружены какие сегменты
(gdb) x/100i 0x401000            # дизассемблировать с начала .text

# Имена из PLT (imported functions) обычно есть
(gdb) break malloc                # поставить bp на malloc — работает через PLT
(gdb) info sharedlibrary          # список загруженных SO
```

---

## 8. Отладка утечек памяти без Valgrind

В продакшн-среде Valgrind недоступен. Альтернативные методы:

```bash
# LSAN через ASAN
ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 ./prog

# Мониторинг через /proc
while true; do
    cat /proc/$PID/status | grep -E 'VmRSS|VmPeak|VmSize'
    sleep 1
done
# VmRSS растёт → утечка (или легитимный рост)
```

Через `mallinfo2` (glibc):

```c
#include <malloc.h>

struct mallinfo2 mi = mallinfo2();
printf("heap total:    %zu bytes\n", mi.arena);     // total heap
printf("heap in use:   %zu bytes\n", mi.uordblks);  // allocated
printf("heap free:     %zu bytes\n", mi.fordblks);  // free in heap
printf("mmap allocs:   %zu\n", mi.hblks);           // mmap() allocations
printf("mmap total:    %zu bytes\n", mi.hblkhd);    // total mmap size
```

Heaptrack (KDE утилита, намного легче Valgrind):

```bash
heaptrack ./prog
heaptrack_print heaptrack.prog.$PID.gz
# Показывает: пик памяти, топ функций по аллокациям, flamegraph аллокаций
```

LeakSanitizer отдельно (без full ASan):

```bash
gcc -fsanitize=leak -g prog.c -o prog
LSAN_OPTIONS=verbosity=1:log_threads=1:report_objects=1 ./prog
```

Tcmalloc heap profiler (Google):

```bash
# Слинковать с tcmalloc
gcc prog.c -o prog -ltcmalloc

HEAPPROFILE=/tmp/heap ./prog
pprof --pdf ./prog /tmp/heap.0001.heap > profile.pdf
```

---

## 9. addr2line и objdump для анализа crash

Получение file:line из crash-адреса — стандартная задача при анализе production-крэша.

```bash
# addr2line — основной инструмент
addr2line -e ./prog -f 0x4005a3
# Вывод:
# main
# /home/user/prog.c:42

# С флагом -i — inlined функции тоже разворачиваются
addr2line -e ./prog -f -i 0x4005a3

# Несколько адресов за раз
addr2line -e ./prog 0x4005a3 0x400612 0x400789

# С offset из shared library (нужно знать базовый адрес из /proc/PID/maps)
addr2line -e /lib/x86_64-linux-gnu/libc.so.6 0x89abc
```

Как получить правильный адрес из crash лога при ASLR:

```bash
# /proc/PID/maps показывает базовые адреса при жизни процесса
# Для PIE (Position Independent Executable):
cat /proc/$PID/maps | grep prog
# 555555554000-555555556000 r-xp 00000000 ... /path/to/prog

# base = 0x555555554000
# crash addr = 0x5555555549a3
# offset = 0x5555555549a3 - 0x555555554000 = 0x9a3

addr2line -e ./prog -f 0x9a3   # использовать offset, не абсолютный адрес
```

objdump для дизассемблирования:

```bash
objdump -d -M intel ./prog > prog.asm            # весь .text в Intel синтаксисе
objdump -d -M intel ./prog | grep -A 20 '<main>' # только функция main
objdump -S ./prog > prog.asm                      # перемешать с исходным кодом (нужен -g)
objdump -x ./prog                                 # заголовки ELF + секции
objdump -R ./prog                                 # relocation таблицы (PLT)

# Посмотреть конкретную секцию
objdump -s --section=.data ./prog               # дамп секции данных
objdump -s --section=.rodata ./prog             # строковые константы
```

readelf — более детальный анализ ELF:

```bash
readelf -h ./prog          # ELF header
readelf -S ./prog          # таблица секций
readelf -l ./prog          # program headers (segments)
readelf -d ./prog          # dynamic section (импорты)
readelf --debug-dump=info ./prog  # DWARF информация
```

---

## 10. GDB: продвинутые приёмы

Базовых `break`/`next`/`print` хватает на 80% сессий, но оставшиеся 20% — самые тяжёлые баги — требуют инструментов, о которых редко знают.

### 10.1 Catchpoints — останов на событии, а не на коде

Breakpoint ловит исполнение адреса, watchpoint — изменение данных. Catchpoint ловит **событие**: системный вызов, сигнал, загрузку библиотеки, исключение C++.

```
(gdb) catch syscall openat     # останов на каждом openat (вход и выход)
(gdb) catch syscall            # на любом syscall
(gdb) catch signal SIGSEGV     # перехватить до того, как сигнал убьёт процесс
(gdb) catch signal             # любой сигнал
(gdb) catch load libssl.so     # момент dlopen/загрузки конкретной библиотеки
(gdb) catch throw              # C++: бросок исключения (до раскрутки стека!)
(gdb) catch catch              # C++: перехват исключения
```

`catch syscall` бесценен, когда программа делает «что-то не то» с файлами/сетью, но ты не знаешь, в каком месте кода. `catch throw` ловит исключение **в точке броска**, пока стек ещё цел, — в отличие от breakpoint на обработчике, куда ты попадаешь уже после раскрутки, потеряв контекст.

Особый трюк с сигналами: по умолчанию GDB пробрасывает сигнал программе. Управление политикой:

```
(gdb) info handle SIGSEGV      # как GDB обрабатывает сигнал
(gdb) handle SIGSEGV stop print nopass   # останавливаться, печатать, НЕ пробрасывать
(gdb) handle SIGUSR1 nostop noprint pass # игнорировать «рабочий» сигнал приложения
```

`nopass` на `SIGSEGV` позволяет осмотреть состояние в момент падения и при желании «починить» причину (изменить указатель) и продолжить.

### 10.2 dprintf и tbreak — трассировка без правки кода

`dprintf` ставит точку, которая **печатает и продолжает** — встроенный «printf-дебаг» без перекомпиляции:

```
(gdb) dprintf process.c:88, "id=%d state=%d\n", req->id, req->state
(gdb) tbreak main              # одноразовый breakpoint (снимается после первого срабатывания)
(gdb) info dprintf             # список dprintf-точек
```

Это лучше, чем расставлять `printf` в коде: не нужна пересборка, точки снимаются мгновенно, формат — как у обычного `printf`. Связка `dprintf` + `set logging on` пишет трассу в файл.

### 10.3 Convenience-переменные, история значений, вызовы

```
(gdb) print $rax               # регистр как переменная
(gdb) set $i = 0               # своя переменная сессии
(gdb) print $i++               # инкремент — удобно листать массивы: p arr[$i++]
(gdb) print $                  # последнее напечатанное значение
(gdb) print $$                 # предпоследнее
(gdb) print $_exitcode         # код выхода (после завершения)
(gdb) call func(arg)           # вызвать функцию отлаживаемой программы
(gdb) print strlen(name)       # вызов прямо в выражении
```

Вызов функций (`call`/`print func()`) выполняется **в контексте отлаживаемого процесса** — можно дёргать его аллокаторы, логгеры, валидаторы инвариантов. Осторожно: если вызванная функция сама упадёт или возьмёт мьютекс, который уже держит остановленный поток, — сессия повиснет.

### 10.4 TUI — текстовый интерфейс

```
(gdb) layout src               # окно исходника + командная строка
(gdb) layout asm               # дизассемблер
(gdb) layout regs              # регистры сверху
(gdb) layout split             # исходник + asm одновременно
(gdb) tui enable               # то же, что Ctrl-X A
```

В TUI видно текущую строку, breakpoints на полях, регистры обновляются на каждом шаге. `Ctrl-L` перерисовывает экран, если вывод программы «попортил» рамки.

### 10.5 Reverse debugging — шаг назад во времени

GDB умеет писать журнал исполнения и **отматывать назад**. Это встроено (на x86-64), отдельный инструмент не нужен:

```
(gdb) record                   # начать запись (process record/replay)
(gdb) reverse-step    (rs)     # шаг назад по строкам
(gdb) reverse-next    (rn)     # шаг назад, не входя в функции
(gdb) reverse-continue (rc)    # выполнять назад до предыдущего breakpoint
(gdb) reverse-stepi            # один шаг назад по инструкции
(gdb) record stop              # остановить запись
```

Классический сценарий: переменная неожиданно стала мусором. Ставим на неё `watch`, затем `reverse-continue` — GDB останавливается в момент **последней записи** в неё, идя назад. Это прямой ответ на вопрос «кто это испортил?», без перезапусков.

Ограничения встроенного `record`: огромный overhead и потолок по объёму журнала (`set record full insn-number-max`), не записывает большинство «нестандартных» инструкций. Для серьёзного reverse-debugging есть `rr` (§11).

### 10.6 Checkpoints — ветвление состояния

```
(gdb) checkpoint               # сохранить снимок процесса (fork под капотом)
(gdb) info checkpoints
(gdb) restart 1                # вернуться к снимку #1
```

Позволяет «попробовать» путь исполнения, а потом откатиться к точке ветвления — дешевле, чем перезапуск долго стартующей программы.

---

## 11. rr — детерминированная запись и воспроизведение

`rr` (Mozilla) — «time-travel debugger»: записывает выполнение программы один раз, а затем **детерминированно** воспроизводит его сколько угодно раз, с полным reverse-debugging поверх обычного GDB. Главное достоинство — ловит **неустойчивые (флапающие) баги**: записал один раз, когда «выстрелило», — дальше отлаживаешь сколько надо на одной и той же записи.

```bash
rr record ./prog arg1 arg2     # записать один прогон
rr replay                      # воспроизвести в gdb-подобном интерфейсе
rr replay -p PID               # если в записи было несколько процессов
rr ps                          # список процессов в записи
```

Внутри `rr replay` доступны все обратные команды GDB (`reverse-cont`, `reverse-step`) — и они **быстрые и точные**, потому что воспроизведение детерминировано (rr сериализует потоки на один логический CPU и записывает все недетерминированные входы: результаты syscalls, сигналы, точки переключения потоков).

Канонический рецепт «кто испортил память»:

```
(rr) continue                  # дойти до проявления бага (мусор в X)
(rr) watch -l obj->field       # watchpoint по адресу
(rr) reverse-continue          # отмотать до последней записи в field
# rr останавливается ровно на инструкции, записавшей мусор
```

Требования: `rr` использует аппаратные счётчики PMU, поэтому нужен реальный CPU (или VM с проброшенным PMU) и обычно `perf_event_paranoid <= 1`. На многих CPU требуется отключить аппаратный prefetch-специфичный счётчик — `rr` сам диагностирует совместимость командой `rr record` при первом запуске.

`rr` vs встроенный `record` GDB: `rr` на порядки быстрее, переживает многопоточность и многопроцессность, хранит компактную запись на диске. Это инструмент выбора для «невоспроизводимых» багов.

---

## 12. Core dumps глубже: core_pattern, coredumpctl, gcore

### 12.1 Куда падает core

Куда ядро пишет core и как его именует, задаёт `/proc/sys/kernel/core_pattern`:

```bash
cat /proc/sys/kernel/core_pattern
# вариант 1 (файл): /var/cores/core.%e.%p.%t
# вариант 2 (pipe): |/usr/lib/systemd/systemd-coredump %P %u %g %s %t %c %h
```

Спецификаторы шаблона: `%e` — имя исполняемого, `%p` — PID, `%t` — время (Unix), `%s` — номер сигнала, `%u`/`%g` — UID/GID, `%h` — hostname, `%P` — глобальный PID. Если шаблон начинается с `|`, ядро **передаёт core по конвейеру** указанной программе (так работает `systemd-coredump`).

```bash
ulimit -c unlimited                       # снять лимит размера core (по умолчанию 0!)
echo '/var/cores/core.%e.%p.%t' | sudo tee /proc/sys/kernel/core_pattern
# постоянно — через sysctl:
echo 'kernel.core_pattern=/var/cores/core.%e.%p.%t' | sudo tee /etc/sysctl.d/50-core.conf
sudo sysctl --system
```

Частая ловушка: `ulimit -c` по умолчанию `0` → core не создаётся вообще; и `core_pattern` глобальный, а лимит — на процесс.

**Важное исключение (актуально для systemd-систем):** если `core_pattern` начинается с `|` (передача дампа в программу — как `|/usr/lib/systemd/systemd-coredump`), ядро **игнорирует `RLIMIT_CORE`**. `man 5 core` прямо: *«RLIMIT_CORE ... is not enforced for core dumps that are piped to a program»* и *«RLIMIT_CORE will be ignored if the system is configured to pipe core dumps»*. То есть обработчик запускается **даже при `ulimit -c 0`**, а сохранять дамп или нет решает уже сам **обработчик** (для systemd — по `coredump.conf`/`system.conf`), а не шелл-лимит процесса. Вывод: на современной systemd-системе `ulimit -c 0` **не** отключает сбор краш-дампов — смотри `coredumpctl` (§12.2), а не только `ulimit`.

### 12.2 systemd-coredump и coredumpctl

На systemd-дистрибутивах core уходит в журнал, и доступ к нему — через `coredumpctl`:

```bash
coredumpctl list                 # все собранные core'ы
coredumpctl list myprog          # по имени
coredumpctl info 12345           # метаданные краша (сигнал, версия, командная строка)
coredumpctl debug myprog         # открыть последний core в GDB одной командой
coredumpctl dump myprog -o core  # выгрузить core в файл
```

`coredumpctl debug` сам подтянет нужный бинарник и (если установлены) debuginfo-символы — удобнее, чем вручную искать пару «бинарник + core».

### 12.3 gcore — core без убийства процесса

Иногда процесс **завис** (не упал), и его надо вскрыть, не прерывая работу надолго:

```bash
gcore -o /tmp/snapshot $PID       # снять core с живого процесса
gdb ./prog /tmp/snapshot.$PID     # анализировать снимок офлайн
```

`gcore` ненадолго останавливает процесс, дампит память и отпускает — сервис почти не замечает. Дальше зависший снимок разбираешь спокойно в офлайне (`thread apply all bt`), пока сервис продолжает жить.

### 12.4 Что внутри core и как читать

Core — это ELF-файл типа `ET_CORE`: набор сегментов `PT_LOAD` (снимки памяти) + `PT_NOTE` (регистры всех потоков, `siginfo`, маппинги, `auxv`).

```
gdb ./prog core
(gdb) bt                          # стек на момент падения
(gdb) info threads                # все потоки в снимке
(gdb) thread apply all bt         # стек каждого потока
(gdb) info registers              # регистры (включая rip — где упали)
(gdb) info signal                 # каким сигналом убито
(gdb) p $_siginfo                 # детали siginfo (адрес обращения при SIGSEGV)
(gdb) info proc mappings          # карта памяти из PT_NOTE
```

При `SIGSEGV` поле `$_siginfo._sifields._sigfault.si_addr` — это **адрес, по которому обратились**. Если он `0x0` — разыменование NULL; если похож на «затёртый» указатель — порча памяти. Чтобы символы в core совпали с бинарником, нужен **тот же** бинарник, что собрал core (по `build-id`: `readelf -n` у обоих должен совпасть).

---

## 13. Отладка кучи: glibc malloc под микроскопом

ASan и Valgrind — внешние. Но у самого glibc есть встроенные проверки и переключатели, доступные **без перекомпиляции**.

### 13.1 Переменные окружения glibc

```bash
MALLOC_CHECK_=3 ./prog     # включить проверки целостности кучи
# 0 = молча игнорировать; 1 = печатать диагностику; 2 = abort();
# 3 = печатать + abort() (рекомендуется для отладки)

MALLOC_PERTURB_=42 ./prog  # свежевыделенную память забивать байтом ~42 (комплемент),
                           # освобождённую — байтом 42 (так задаёт glibc, не наоборот!)
# выявляет чтение неинициализированного и use-after-free через "мусорные" значения
```

`MALLOC_CHECK_` подменяет аллокатор на версию с дополнительными проверками: ловит часть double-free, переполнений и записи в освобождённый блок — дешевле ASan, но и слабее.

### 13.2 Встроенные детекторы glibc (всегда включены)

Современный glibc печатает диагностику и вызывает `abort()` при типичных порчах кучи — это те самые знакомые сообщения:

```
free(): invalid pointer
free(): double free detected in tcache 2
malloc(): corrupted top size
malloc(): unsorted double linked list corrupted
```

С glibc **2.26** (2017) появился **tcache** (per-thread cache мелких чанков) — но **без проверок безопасности**, из-за чего эксплуатация кучи (tcache poisoning) стала тривиальной. Проверку double-free «в tcache» и поле-**ключ** (`tcache_entry.key`) для её срабатывания добавили лишь полтора года спустя — в **glibc 2.29** (2019). Сообщение `double free detected in tcache 2` означает: блок уже лежит в кэше освобождённых и его освобождают повторно.

### 13.3 mtrace — встроенный трекер аллокаций

```c
#include <mcheck.h>
int main(void) {
    mtrace();          // начать запись malloc/free (пишет в файл из MALLOC_TRACE)
    /* ... работа ... */
    muntrace();        // остановить
}
```

```bash
MALLOC_TRACE=/tmp/mt.log ./prog
mtrace ./prog /tmp/mt.log     # разобрать лог: покажет несбалансированные аллокации
# "Memory not freed: ... 0x.... 0x20 at /path/prog.c:42"
```

`mtrace` — лёгкий способ найти утечку с точностью до строки без ASan/Valgrind, если можно вставить две строки в `main`.

### 13.4 Статистика кучи в рантайме

```c
#include <malloc.h>
malloc_stats();               // печать сводки в stderr: in-use, total, mmap
malloc_info(0, stdout);       // подробный XML по аренам (для парсинга)
```

`malloc_info` выдаёт XML с разбивкой по аренам и bin'ам — удобно для долгоживущих сервисов, чтобы понять, фрагментация это или настоящая утечка (растёт `<total type="rest">` при стабильном in-use → фрагментация).

### 13.5 Разобранный пример: double-free глазами glibc

```c
#include <stdlib.h>
int main(void) {
    char *p = malloc(32);
    free(p);
    free(p);          // double free — p уже в tcache
    return 0;
}
```

Даже **без** санитайзеров glibc поймает это сам — благодаря ключу tcache (сам tcache — с 2.26, а вот проверка по ключу — с **glibc 2.29**):

```
free(): double free detected in tcache 2
Aborted (core dumped)
```

Что произошло: первый `free` положил чанк в tcache-список (bin индекс 2 — для размеров ~32 байт) и записал в него «ключ» tcache. Второй `free` увидел, что чанк уже содержит этот ключ → диагностировал повторное освобождение и вызвал `abort()` (отсюда `SIGABRT` и core). Дальше — обычный разбор:

```
gdb ./prog core
(gdb) bt                       # увидим abort ← __libc_message ← _int_free ← main
(gdb) frame 4                  # подняться до своего кадра (main)
```

Сравните инструменты на этом же баге: glibc-детектор — бесплатный, но даёт лишь факт и текущий стек; **ASan** добавит три стека (malloc/первый free/второй free) — кто и где владел блоком; **Valgrind** найдёт то же на не перекомпилированном бинарнике. Для CI достаточно ASan; glibc-детектор — «последняя линия» в продакшене, где санитайзеров нет.

---

## 14. ftrace — трассировка ядра из userspace

Когда тормозит или зависает не твой код, а **путь в ядре** (syscall, драйвер, планировщик), нужен трассировщик ядра. `ftrace` встроен в ядро и управляется через файлы в `/sys/kernel/tracing` (старый путь — `/sys/kernel/debug/tracing`).

```bash
cd /sys/kernel/tracing
cat available_tracers              # function, function_graph, wakeup, ...
echo function_graph > current_tracer
echo do_sys_openat2 > set_graph_function   # ограничить трассу одной функцией
echo 1 > tracing_on; cat trace_pipe; echo 0 > tracing_on
```

`function_graph` рисует дерево вызовов ядра с временем на каждой функции — видно, **где именно** в ядре теряется время:

```
 3) + 21.847 us   |  do_sys_openat2() {
 3)   2.119 us    |    getname();
 3) + 15.231 us   |    path_openat() {
 3) + 11.402 us   |      link_path_walk();   # ← здесь основная задержка
 3)   1.231 us    |    }
 3) }
```

Практичнее обёртка `trace-cmd` (и GUI `KernelShark`):

```bash
trace-cmd record -p function_graph -g do_sys_openat2 ./prog
trace-cmd report | less
```

ftrace почти бесплатен (статически пропатчленные точки в ядре), поэтому годится и под нагрузкой. Для произвольных мест — `kprobe` (динамические точки на любой функции ядра), что подводит нас к eBPF.

---

## 15. eBPF и bpftrace — динамическая трассировка нового поколения

eBPF — виртуальная машина внутри ядра: твои мини-программы цепляются к kprobe/uprobe/tracepoint и **безопасно** (верификатор не пускает программу, способную уронить ядро) собирают данные с минимальным overhead. Это современная замена связке strace+ltrace+ftrace, особенно в продакшене.

### 15.1 bpftrace — однострочники

```bash
# Кто открывает файлы (имя процесса + путь):
bpftrace -e 'tracepoint:syscalls:sys_enter_openat { printf("%s %s\n", comm, str(args->filename)); }'

# Гистограмма размеров read():
bpftrace -e 'tracepoint:syscalls:sys_exit_read /args->ret>0/ { @ = hist(args->ret); }'

# Сколько раз каждый процесс звал malloc (uprobe в libc):
bpftrace -e 'uprobe:/lib/x86_64-linux-gnu/libc.so.6:malloc { @[comm] = count(); }'

# Латентность блочного I/O:
bpftrace -e 'tracepoint:block:block_rq_issue { @start[args->dev] = nsecs; }'
```

`comm` — имя процесса, `args->` — аргументы tracepoint/probe, `@` — агрегирующая карта (`count()`, `hist()`, `avg()`). Вывод печатается при выходе по `Ctrl-C`.

### 15.2 Готовые инструменты bcc/bpftrace

Набор `bcc-tools`/`bpftrace` даёт десятки готовых утилит (Brendan Gregg):

| Инструмент | Что показывает |
|---|---|
| `execsnoop` | каждый `execve` в системе (что и кто запускает) |
| `opensnoop` | каждый `open`/`openat` с путём и результатом |
| `biolatency` | гистограмма латентности дискового I/O |
| `tcplife` | время жизни и объём каждого TCP-соединения |
| `profile` | сэмплирующий профилировщик стеков (для flame graph) |
| `offcputime` | где процесс **спал** (off-CPU анализ) |
| `funccount` | счётчик вызовов любой функции ядра/процесса |

`offcputime` отвечает на вопрос, который не берут perf и flame graph CPU: «программа не грузит CPU, но медленная — где она блокируется?». eBPF записывает стек в момент ухода со CPU и время сна.

Требования: ядро 4.x+ с включённым BPF, права (`CAP_BPF`/`CAP_SYS_ADMIN` или root). В отличие от `ptrace`-инструментов (strace/ltrace), eBPF не останавливает процесс на каждом событии — overhead на порядки ниже.

### 15.3 bpftrace, bcc или libbpf — что выбрать

| Подход | Когда |
|---|---|
| `bpftrace` однострочники | разовая диагностика «здесь и сейчас», ad-hoc вопросы |
| `bcc`-инструменты (готовые) | типовые задачи — есть `execsnoop`/`opensnoop`/`biolatency` под рукой |
| `libbpf` + CO-RE (C) | свой production-инструмент: компилируется один раз, переносится между ядрами (CO-RE = Compile Once, Run Everywhere), грузится без рантайм-компилятора |

Для повседневной отладки хватает `bpftrace`/`bcc`. `libbpf` — это уже разработка собственного трассировщика (мост в трек ядра K и в наблюдаемость продакшена), где важны минимальный footprint и переносимость BTF/CO-RE. Эволюция здесь: от `ptrace` (strace) → к статическому ftrace → к программируемому eBPF; каждый шаг — ниже overhead и выше гибкость.

---

## 16. Статический анализ и защитная компиляция

Динамические инструменты (ASan/Valgrind/TSan) ловят баг, **только если он исполнился** на данном входе. Статический анализ ищет дефекты не запуская код — дополняет, а не заменяет санитайзеры.

### 16.1 Встроенный анализатор GCC: -fanalyzer

```bash
gcc -fanalyzer -O2 prog.c -o prog
```

`-fanalyzer` (GCC 10+) символически обходит пути исполнения и находит: разыменование NULL, double-free, use-after-free, утечки, использование неинициализированного, выход за границы. Пример вывода на разыменовании NULL:

```
warning: dereference of NULL 'p' [CWE-476] [-Wanalyzer-null-dereference]
```

В скобках `[CWE-476]` — это номер класса дефекта в **CWE** (Common Weakness Enumeration — общий каталог типов уязвимостей/слабостей кода; `476` = разыменование NULL). Удобно для отчётов и связи с безопасностью. Каждое предупреждение снабжено **путём** (interprocedural trace), как анализатор пришёл к дефекту, — это читается как мини-доказательство.

### 16.2 clang-tidy, cppcheck, scan-build

```bash
clang-tidy prog.c -checks='*' -- -std=c17        # огромный набор проверок + автофиксы
cppcheck --enable=all --inconclusive prog.c       # независимый анализатор, без сборки
scan-build gcc -c prog.c                           # Clang Static Analyzer + HTML-отчёт
```

`clang-tidy` умеет не только находить, но и **чинить** (`-fix`) — стилистика, модернизация, типичные баги. `cppcheck` не требует корректной сборки (полезно на чужом коде). `scan-build` строит HTML с подсветкой пути дефекта.

### 16.3 Защитная компиляция (compile-time + runtime барьеры)

Часть багов дешевле **предотвратить** флагами, чем ловить:

```bash
gcc -O2 -Wall -Wextra -Wconversion -Wsign-conversion \
    -D_FORTIFY_SOURCE=3 \
    -fstack-protector-strong \
    -fstack-clash-protection \
    -Wformat=2 -Werror=format-security \
    prog.c -o prog
```

| Флаг | Что даёт |
|---|---|
| `-D_FORTIFY_SOURCE=2/3` | проверки переполнения в `memcpy`/`strcpy`/`sprintf` (нужен `-O1+`); уровень 3 (glibc 2.34+) — точнее, через `__builtin_dynamic_object_size` |
| `-fstack-protector-strong` | canary на стеке → ловит классический stack smashing |
| `-fstack-clash-protection` | защита от «перепрыгивания» guard-страницы большим VLA/alloca |
| `-Wformat=2` | строгая проверка форматных строк (мост к format-string атакам из Ф1) |

Эти флаги дают рантайм-падение с понятным сообщением (`*** stack smashing detected ***`, `*** buffer overflow detected ***`) вместо тихой порчи — то есть превращают UB в управляемый отказ.

### 16.4 Статический и динамический анализ — почему нужны оба

Это не конкуренты, а разные «срезы» одного и того же:

- **Статический** (`-fanalyzer`, clang-tidy, cppcheck) видит **все пути** в коде, не запуская его, — найдёт баг на ветке, которая в тестах никогда не исполнилась. Цена — ложные срабатывания (анализатор не знает рантайм-инвариантов) и неспособность рассуждать о сложном межпроцедурном состоянии.
- **Динамический** (ASan/UBSan/TSan/Valgrind) видит **только исполненные** пути, зато без ложных срабатываний: если сработало — баг реален, с точным стеком и состоянием. Цена — баг найдётся, лишь если данный вход его задел.

Зрелый конвейер совмещает: статический анализ на каждый коммит (дёшево, ловит «очевидное» рано) + санитайзеры в тестах (`asan+ubsan` и отдельно `tsan`) + защитные флаги в релизе (`_FORTIFY_SOURCE`, stack protector) как последний предохранитель. Один слой пропускает то, что ловит другой.

---

## 17. perf глубже: память, false sharing, off-CPU

`perf stat`/`record` из §6 — вершина айсберга. Тяжёлые перформанс-баги (промахи кэша, NUMA, блокировки) требуют специализированных режимов.

### 17.1 perf record и виды call graph

Чтобы стек вызовов в профиле был верным, нужен способ его размотать. У perf три механизма:

```bash
perf record --call-graph fp ./prog      # по frame pointer — быстро, но нужен -fno-omit-frame-pointer
perf record --call-graph dwarf ./prog   # по DWARF CFI — точно даже без FP, но дамп стека дорог
perf record --call-graph lbr ./prog     # Last Branch Record — аппаратный, дёшево, ограничен по глубине
```

Подвох по умолчанию: **апстрим**-GCC на `-O2` (x86-64) **выкидывает frame pointer** (`-fomit-frame-pointer` включён). Тогда `--call-graph fp` даёт обрубленные стеки. Лечение: либо собрать продакшн с `-fno-omit-frame-pointer` (стоит ~1% производительности, но возвращает читаемые профили и flame graph'ы), либо использовать `dwarf`/`lbr`.

> **Тренд (актуально):** именно из-за этой проблемы с профилированием современные дистрибутивы **возвращают** frame pointer: **Fedora 38+** (2023) и **Ubuntu 24.04** собирают свои пакеты с `-fno-omit-frame-pointer` по умолчанию. То есть на свежей системе системные библиотеки уже могут быть с FP. Но **дефолт самого GCC** на `-O2` не менялся — для **своих** сборок флаг `-fno-omit-frame-pointer` всё равно ставь явно, не полагайся на компилятор.

### 17.2 perf annotate — горячая строка и инструкция

```bash
perf record -g ./prog
perf annotate -s hot_function     # исходник+asm с % на каждой инструкции
```

`perf annotate` показывает, **на какой именно инструкции** копится время. Часто «горячая» строка — это не вычисление, а `mov` из памяти, который ждёт кэш-промах: процент висит на загрузке, а не на арифметике рядом. Это прямой указатель на проблему data locality.

### 17.3 perf mem — кто и куда обращается к памяти

```bash
perf mem record ./prog            # семплировать загрузки/выгрузки с уровнем памяти
perf mem report                   # где обращения попадают: L1/L2/LLC/RAM/remote-NUMA
```

`perf mem` отвечает на вопрос «откуда реально читаются данные»: если много обращений идёт в `Remote RAM` (другой NUMA-узел) — это десятки лишних наносекунд на доступ, и лечится привязкой памяти/потоков к узлу (`numactl`, `mbind`).

> **NUMA** (Non-Uniform Memory Access) — на многосокетных (и некоторых многочиплетных) машинах память физически разбита на **узлы**, по одному рядом с каждым набором ядер. Доступ к «своему» (local) узлу быстрый, к «чужому» (remote) — медленнее, потому что идёт через межсокетную шину. Поэтому где **физически** лежат данные относительно потока, который их читает, влияет на скорость — `perf mem` это и показывает, а `numactl`/`mbind` позволяют прибить память и потоки к одному узлу.

### 17.4 perf c2c — детектор false sharing

```bash
perf c2c record ./prog
perf c2c report --stdio
```

`perf c2c` (cache-to-cache) специально ищет **строки кэша, которые «пинг-понгуют» между ядрами**, — это и есть подпись false sharing (§ из C1). В отчёте видно: адрес строки кэша, смещения внутри неё, какие функции/потоки конкурируют. Это единственный инструмент, прямо показывающий false sharing на готовом бинарнике, без догадок.

### 17.5 Off-CPU анализ — где программа спит

CPU-профиль (perf/flame graph) показывает, где тратится **процессор**. Но если программа медленная из-за ожидания (блокировки, I/O, сон на condvar) — на CPU-профиле этого нет: спящая программа не семплируется. Off-CPU анализ ловит обратное:

```bash
perf sched record ./prog ; perf sched latency     # задержки планировщика
# или через eBPF (точнее и дешевле):
offcputime-bpfcc -p $PID 30                        # стеки в момент ухода со CPU + время сна
```

Правило: **CPU flame graph + off-CPU flame graph вместе** покрывают всё время жизни потока — «считал» и «ждал». Без второго легко часами оптимизировать функцию, которая и так не на критическом пути, пока программа спит на мьютексе.

---

## 18. Удалённая и кросс-отладка

Не всё отлаживается на локальной машине: embedded-плата, контейнер, ядро, процесс на чужом хосте.

### 18.1 gdbserver — отладчик по сети/последовательному порту

`gdbserver` запускает (или цепляется к) программе на целевой машине, а GDB-клиент с символами работает на твоей рабочей станции. Это основа отладки embedded и контейнеров.

```bash
# На цели (например, ARM-плата или контейнер):
gdbserver :2345 ./prog arg1           # слушать TCP-порт 2345
gdbserver --attach :2345 $PID         # прицепиться к работающему процессу

# На рабочей станции (с тем же бинарником и символами):
gdb ./prog
(gdb) target remote 192.168.1.50:2345
(gdb) continue
```

Для кросс-архитектуры на хосте берут `gdb-multiarch` (или `arm-none-eabi-gdb`) и задают `set architecture`/`set sysroot`. Символы и исходники — на хосте, на цели достаточно лёгкого `gdbserver`.

### 18.2 Отладка процесса в контейнере

Процесс контейнера виден с хоста под своим (хостовым) PID. Отладить можно двумя путями:

```bash
# 1) С хоста по хостовому PID (символы берём из образа контейнера):
sudo gdb -p $(pgrep -f myservice)

# 2) Войти в namespaces контейнера и работать «изнутри»:
sudo nsenter -t $PID -a gdb -p $PID    # -a: все namespaces цели
```

Тонкость: пути к исполняемому и библиотекам внутри контейнера отличаются от хостовых — указывай `set sysroot /proc/$PID/root` (корень mnt-namespace цели через `/proc`), тогда GDB найдёт правильные `.so` и символы.

### 18.3 Отладка ядра: kgdb и QEMU

```bash
# QEMU: ждать подключения GDB на старте гостя
qemu-system-x86_64 -s -S -kernel bzImage ...
#   -s  = открыть gdbstub на :1234
#   -S  = заморозить до команды continue от GDB

# На хосте:
gdb vmlinux
(gdb) target remote :1234
(gdb) hbreak start_kernel       # аппаратный bp (память ещё не настроена для soft-bp)
(gdb) continue
```

Для «живого» ядра на железе — `kgdb` (отладчик в ядре) поверх `kgdboc` (kgdb over console, последовательный порт): параметры ядра `kgdboc=ttyS0,115200 kgdbwait`, затем `target remote /dev/ttyS0` с хоста. Это прямой мост в трек ядра (K-модули) и embedded (EL-модули).

### 18.4 Раздельные символы (split debug info)

В продакшен идёт stripped-бинарник (меньше, не выдаёт внутренности), а символы хранятся отдельно и подгружаются при разборе краша:

```bash
objcopy --only-keep-debug prog prog.debug      # вынуть отладочную инфу
objcopy --strip-debug prog                     # очистить бинарник
objcopy --add-gnu-debuglink=prog.debug prog    # связать обратно по имени
```

GDB найдёт `prog.debug` автоматически — по `.gnu_debuglink` или по `build-id` в `/usr/lib/debug/.build-id/xx/yyyy.debug`. Совпадение гарантирует **build-id** (`readelf -n`): символы и бинарник из разных сборок не «склеятся» по ошибке. Так устроены debuginfo-пакеты дистрибутивов (`debuginfod` отдаёт их по сети автоматически).

---

## 19. Отладка оптимизированного кода

Продакшн собирается с `-O2`/`-O3`, и отладчик начинает «врать»: переменные исчезают, строки прыгают, стек обрывается. Это не баг GDB — это следствие оптимизаций, и с ним надо уметь работать, потому что воспроизводить баг на `-O0` часто невозможно (он там не проявляется).

### 19.1 «value optimized out»

```
(gdb) print count
$1 = <optimized out>
```

Переменная не «потеряна» — компилятор держит её часть жизни в регистре, а в точке останова она уже мертва (её диапазон жизни закончился) или ещё не материализована. Что делать:

- посмотреть **регистры** (`info registers`, `info locals` иногда покажет, что переменная в `rbx`);
- сместиться по времени: `next`/`reverse-next` к точке, где значение ещё/уже живо;
- глянуть DWARF location: `info address count` скажет, где переменная находится в текущем PC (регистр, стек, вычисляемое выражение);
- собрать критический модуль с `-Og` или `-O2 -fvar-tracking-assignments` (включено по умолчанию на `-O2 -g`) — это улучшает доступность переменных без потери оптимизаций.

### 19.2 Прыгающая текущая строка и инлайнинг

На `-O2` инструкции разных строк перемешаны (instruction scheduling), поэтому `step` «скачет». А заинлайненные функции не имеют своего кадра стека:

```
(gdb) bt
#0  parse_token (...) at parser.c:88
#1  parse_line (...) at parser.c:140      # inline-функция показана как отдельный кадр
(gdb) info frame                          # видно "inlined into"
```

GDB **реконструирует** инлайн-кадры из DWARF (`DW_TAG_inlined_subroutine`), поэтому backtrace остаётся осмысленным — но `finish` из инлайн-кадра ведёт себя не как из настоящего вызова. Команда `set step-mode on` не даёт GDB проскакивать строки без line-info.

### 19.3 Почему стек обрывается и как чинить

Обрыв `bt` на `-O2` обычно означает отсутствие информации для раскрутки:

- нет frame pointer (`-fomit-frame-pointer` по умолчанию) → раскрутка по FP невозможна; спасает `.eh_frame` (**CFI** — Call Frame Information: таблицы «как для данного PC восстановить адрес возврата и регистры вызывающего»), который GDB использует и так, но в «голых» функциях (asm, PLT) его может не быть;
- для надёжного `bt` под профайлером — `-fno-omit-frame-pointer` (см. §17.1);
- для C++ — `-fasynchronous-unwind-tables` (включено по умолчанию) держит `.eh_frame` корректным на любом PC.

### 19.4 Минимальный продакшн-рецепт «отлаживаемо, но быстро»

```bash
gcc -O2 -g -fno-omit-frame-pointer -fasynchronous-unwind-tables prog.c -o prog
objcopy --only-keep-debug prog prog.debug && objcopy --strip-debug prog
objcopy --add-gnu-debuglink=prog.debug prog
```

Итог: бинарник быстрый и без «внутренностей», но по core + `prog.debug` восстанавливается полный стек с именами. Это и есть то, как собирают сервисы, которые потом реально удаётся отладить по продакшн-крэшу.

---

## 20. Сквозной разбор: от симптома к корню

Инструменты по отдельности — это словарь. Реальная отладка — связное предложение. Пройдём типичный продакшн-баг целиком.

**Симптом.** Многопоточный сервер изредка (раз в несколько часов) падает с `SIGSEGV`; на тесте не воспроизводится. В логах — ничего осмысленного.

**Шаг 1. Поймать core.** Включаем `ulimit -c unlimited` и `core_pattern`, ждём падения, забираем core (`coredumpctl debug` или `gdb ./srv core`).

```
(gdb) bt
#0  0x... in handle_request (req=0x0) at server.c:142   # req == NULL
(gdb) p $_siginfo._sifields._sigfault.si_addr
$1 = (void *) 0x8                                        # обращение по смещению 8 от NULL
```

Падение — разыменование почти-NULL (`req->state`, где `state` на смещении 8). Но **почему** `req` стал NULL — стек этого не говорит: его обнулил кто-то раньше.

**Шаг 2. Гипотеза: гонка/UAF.** «Изредка, под нагрузкой, только многопоточно» — классическая подпись data race или use-after-free. Проверяем оба инструментами.

```bash
# Гонки:
gcc -fsanitize=thread -O1 -g server.c -o srv_tsan -lpthread
# Память:
gcc -fsanitize=address -O1 -g server.c -o srv_asan -lpthread
```

Под нагрузочным тестом `TSan` выдаёт:

```
WARNING: ThreadSanitizer: data race (pid=...)
  Write of size 8 at 0x... by thread T3:
    #0 worker_free  server.c:201      # поток освобождает req и пишет NULL в слот
  Previous read of size 8 by thread T7:
    #0 handle_request server.c:142     # другой поток читает тот же слот
```

**Шаг 3. Подтвердить детерминированно (rr).** Чтобы увидеть точный порядок, записываем падение под `rr` (один «выстрел» достаточно):

```
rr record ./srv_asan
rr replay
(rr) continue                 # до краша
(rr) watch -l slot[i]         # следим за слотом
(rr) reverse-continue         # отматываем до записи NULL
# останавливаемся ровно на worker_free: server.c:201
```

**Корень.** Один поток освобождает запрос и зануляет слот в общей таблице **без удержания мьютекса**, который читатель берёт перед доступом. Окно между «другой поток прочитал указатель» и «этот занулил/освободил» и даёт UAF→NULL-деref.

**Фикс и регрессия.** Заводим блокировку слота (или atomic-владение/refcount). Закрепляем тестом под `TSan` в CI: гонка должна исчезнуть из отчёта. ASan/Valgrind в CI как ASan не ловит гонки — поэтому именно TSan становится воротами регрессии (см. правило в `MODULE_AUTHORING.md §4.2`).

**Мораль маршрута:** core dump дал *где упало*, TSan — *класс бага*, rr — *точный причинно-следственный порядок*, а защитный CI-прогон — гарантию, что не вернётся. Ни один инструмент в одиночку не закрыл бы задачу.

---

## 21. Практика и самопроверка

### 21.1 Практические задания

1. **ASan-охота** (`01-asan-hunt`) — программа с тремя посаженными багами (heap-overflow, OOB-чтение, use-after-free); собрать под ASan, прочитать три отчёта, исправить так, чтобы прогон был чистым. *(§2)*
2. **GDB: логические ошибки без краша** (`02-gdb-logic`) — программа не падает, но даёт неверный результат; через breakpoints/watchpoints/conditional breakpoints найти и устранить логический дефект. *(§1, §10)*
3. **Охота на утечку без Valgrind** (`03-leak-hunt`) — найти и устранить утечку памяти, опираясь на LSan (`-fsanitize=leak`)/`mtrace`/`mallinfo2`; добиться нулевого баланса аллокаций. *(§8, §13)*
4. **UB-чистка под UBSan** (`04-ubsan-clean`) — арифметика с переполнениями/сдвигами/выравниванием; переписать так, чтобы прогон под `-fsanitize=undefined` был чист на всех граничных входах. *(§2.2, §16)*

### 21.2 Вопросы для самопроверки

<details>
<summary><strong>1. Почему -O0 нежелательно использовать для профилирования perf?</strong></summary>

`-O0` генерирует код, в котором каждая переменная помещается в стек и выгружается обратно при каждом обращении. Такой код тратит огромную долю времени на load/store операции, которых нет в реальном коде с оптимизациями. Профиль `-O0` показывает не узкие места алгоритма, а артефакты неоптимизированного кода. Профилировать нужно с теми же флагами что и продакшн (обычно `-O2` или `-O3`), с добавлением `-g` для символов: `gcc -O2 -g prog.c -o prog`. `-Og` — компромисс: оптимизации не ломающие отладку.

</details>

<details>
<summary><strong>2. Почему ASan и TSan нельзя использовать одновременно?</strong></summary>

Оба инструмента используют технику shadow memory — область памяти, где каждый байт реальной памяти отображается на несколько бит/байт теней. ASan использует 1/8 адресного пространства (каждые 8 байт → 1 байт тени). TSan использует другую схему shadow memory для векторных часов. Эти схемы конфликтуют — не могут сосуществовать в одном адресном пространстве. Выбор: если ищем memory corruption → ASan; если ищем data races → TSan.

</details>

<details>
<summary><strong>3. Что такое shadow memory в ASan и почему нельзя устанавливать RLIMIT_AS?</strong></summary>

ASan резервирует огромный регион виртуального адресного пространства для shadow memory: на 64-bit системах это 16 TB виртуального адреса. Shadow memory — непрерывный массив, индексируемый адресом реальной памяти, сдвинутым на 3 бита (потому что 8 байт → 1 байт тени). При каждом доступе к памяти вставленный ASan-код вычисляет адрес тени и проверяет её. RLIMIT_AS ограничивает виртуальное адресное пространство: если лимит меньше чем резервируемые ASan ~16 TB, процесс упадёт при инициализации ASan. Поэтому с ASan нельзя использовать `ulimit -v`.

</details>

<details>
<summary><strong>4. Как определить утечку памяти если Valgrind недоступен?</strong></summary>

Несколько методов: (1) `/proc/$PID/status` — мониторить `VmRSS` каждые N секунд, постоянный рост = утечка. (2) `ASAN_OPTIONS=detect_leaks=1` — если программа скомпилирована с ASan. (3) `gcc -fsanitize=leak` — только LSan, маленький overhead. (4) `mallinfo2()` в коде — сравнивать `uordblks` в разные моменты. (5) Heaptrack — легче Valgrind, работает на бинарниках без перекомпиляции. (6) `mtrace()` из glibc — встроенный трекер аллокаций (устаревший).

</details>

<details>
<summary><strong>5. Как strace помогает найти проблему с правами доступа к файлу?</strong></summary>

`strace -e trace=file ./prog 2>&1 | grep -E 'ENOENT|EACCES|EPERM'` покажет точный путь, который программа пытается открыть, и ошибку. Например: `openat(AT_FDCWD, "/etc/myapp/config.conf", O_RDONLY) = -1 EACCES (Permission denied)`. Это мгновенно отвечает на вопросы: (а) по какому точному пути открывается файл (программа может искать не там где думаете), (б) какова именно ошибка — ENOENT (файл не существует) vs EACCES (нет прав) vs EPERM (запрещено политикой). Без strace приходится добавлять printf в код или гадать.

</details>

<details>
<summary><strong>6. Что такое false sharing и как perf помогает его обнаружить?</strong></summary>

False sharing: два потока работают с разными переменными, но переменные находятся в одной cache line (64 байта). Запись одним потоком инвалидирует cache line другого, даже если тот работает со своей переменной. Симптом: плохой scaling при многопоточности, высокий LLC-load-misses. Обнаружение через perf: `perf stat -e cache-misses,cache-references` — высокий процент промахов при, казалось бы, локальном доступе. `perf c2c record ./prog && perf c2c report` — специально для cache-to-cache transfer analysis. Лечение: выравнивание данных на границу cache line (`__attribute__((aligned(64)))`) или padding.

</details>

<details>
<summary><strong>7. Как отладить multithread deadlock через GDB?</strong></summary>

(1) Запустить программу в GDB и ждать зависания. (2) `Ctrl+C` — прервать в GDB. (3) `thread apply all bt` — вывести backtrace всех потоков. (4) Найти потоки в `pthread_mutex_lock` или `futex` — это потоки ожидающие блокировку. (5) По стеку определить какой мьютекс ждёт каждый поток. (6) Найти поток, который держит этот мьютекс (через `info threads` + `thread N` + `info locals`). (7) Построить граф ожидания: Thread 1 ждёт M1 (держит поток 2), Thread 2 ждёт M2 (держит поток 1) → цикл = дедлок.

</details>

<details>
<summary><strong>8. Что значит "still reachable" в отчёте Valgrind?</strong></summary>

"Still reachable" означает: память не была `free()`-освобождена до завершения программы, но на неё есть живой указатель (не потеряна). Это не всегда ошибка. Примеры легитимного "still reachable": глобальные кэши, singleton-объекты, открытые файлы в glibc (stdio буферы). Это ошибка когда: тест-фреймворк запускает несколько тестов в одном процессе и "утечки" одного теста влияют на следующий; программа работает как daemon и "утечки" накапливаются. Отличить от настоящей утечки ("definitely lost"): definitely lost — нет ни одного указателя на блок, программа потеряла доступ к памяти навсегда.

</details>

<details>
<summary><strong>9. Как читать flame graph?</strong></summary>

Ось X: процент времени CPU (не время в порядке выполнения). Ось Y: глубина стека вызовов. Каждый прямоугольник — функция. Ширина прямоугольника = суммарное время в этой функции (включая вызываемые ею функции). Цвет — случайный (для читаемости), не несёт семантики. Что искать: (1) Широкие плато наверху — "горячие" листовые функции, там реально тратится CPU; (2) Узкие башни — глубокий стек без "горячих" листьев (алгоритмическая проблема или I/O ожидание); (3) Кликнуть на функцию — zoom показывает только её call tree. Плохой признак: одна функция занимает 40%+ ширины на верхних уровнях.

</details>

<details>
<summary><strong>10. Когда выбрать strace, а когда perf?</strong></summary>

strace: диагностика поведения программы — "что она делает, с какими файлами/сетью/процессами работает, какие ошибки получает". Вопросы типа "почему не открывается файл", "что за сетевые соединения", "на чём заблокирована". Работает с любым бинарником без перекомпиляции. Overhead ~10× — не для продакшн нагрузки.

perf: диагностика производительности — "где тратится CPU время, какие функции горячие, cache misses, branch mispredictions". Вопросы типа "почему медленно", "что нагружает CPU". Минимальный overhead (~1-5%), можно использовать на продакшн. Нужны символы для читаемого отчёта.

</details>

<details>
<summary><strong>11. Чем rr лучше обычного перезапуска под GDB для флапающего бага?</strong></summary>

`rr record` фиксирует **один** недетерминированный прогон (включая результаты syscalls, доставку сигналов, порядок переключения потоков) в компактную запись. Дальше `rr replay` воспроизводит **ровно тот же** прогон сколько угодно раз — баг всегда на месте. В обычном GDB каждый `run` — новый недетерминированный прогон, и редкий баг может не повториться часами. Сверху rr даёт быстрый и точный reverse-debugging: `reverse-continue` к последней записи в испорченную переменную находит виновника без догадок. Это инструмент выбора для гонок и «раз в сутки» крэшей.

</details>

<details>
<summary><strong>12. Как perf c2c обнаруживает false sharing, чего не покажет обычный perf stat?</strong></summary>

`perf stat -e cache-misses` скажет лишь, что промахов много, но не *почему*. `perf c2c` (cache-to-cache) семплирует когерентность кэша и находит **строки кэша, которые передаются между ядрами** (HITM — hit-modified в чужом кэше). В отчёте видно конкретный адрес строки, смещения переменных внутри неё и какие функции/потоки конкурируют. Если две независимые переменные двух потоков лежат в одной 64-байтной строке — это и есть false sharing, и c2c покажет его прямо, без догадок. Лечение: развести данные по строкам (`alignas(64)`/padding).

</details>

<details>
<summary><strong>13. Почему на продакшн-бинарнике с -O2 стек в GDB обрывается, и как этого избежать?</strong></summary>

На `-O2` GCC по умолчанию убирает frame pointer (`-fomit-frame-pointer`), поэтому раскрутка по FP невозможна. GDB опирается на `.eh_frame` (DWARF CFI), который обычно есть, но в «голых» функциях (рукописный asm, PLT-заглушки) его может не быть → обрыв. Профайлеры с `--call-graph fp` страдают сильнее. Лечение: собирать с `-fno-omit-frame-pointer` (~1% потери скорости, зато надёжный backtrace и flame graph) и/или `-fasynchronous-unwind-tables`; для perf — `--call-graph dwarf`/`lbr`. Символы можно хранить отдельно (`objcopy --only-keep-debug` + `--add-gnu-debuglink`), связь гарантирует build-id.

</details>

<details>
<summary><strong>14. Зачем нужен off-CPU анализ, если есть flame graph CPU?</strong></summary>

CPU flame graph показывает, где тратится **процессор**, но спящий поток (ждёт мьютекс, I/O, condvar) не семплируется — на CPU-профиле его «нет». Если программа медленная из-за ожидания, а не вычислений, CPU-профиль будет обманчиво «чистым». Off-CPU анализ (через eBPF `offcputime`, либо `perf sched`) записывает стек в момент ухода потока со CPU и время сна — показывает, *где и сколько* программа блокируется. CPU + off-CPU вместе покрывают всё время жизни потока. Без второго легко оптимизировать функцию, которая не на критическом пути, пока узкое место — блокировка.

</details>

---

## 22. Банк вопросов

### БАЗА (8 MCQ)

1. **Какой флаг gcc добавляет отладочную информацию (DWARF)?**
   - `-d`
   - `-g`
   - `-debug`
   - `-dwarf`

   **Ответ: `-g`**. Добавляет DWARF отладочную информацию в бинарник. `-g3` добавляет также информацию о макросах.

2. **Что обнаруживает AddressSanitizer?**
   - Только утечки памяти
   - Только переполнения буферов на heap
   - heap/stack overflow, use-after-free, double free, memory leaks
   - Только data races между потоками

   **Ответ: heap/stack overflow, use-after-free, double free, memory leaks**. ASan — широкий инструмент для ошибок памяти.

3. **Что показывает strace?**
   - Вызовы функций стандартной библиотеки
   - Системные вызовы и их аргументы/результаты
   - Инструкции CPU
   - Обращения к памяти

   **Ответ: системные вызовы и их аргументы/результаты**. Каждую строку вывода strace — это один syscall с аргументами и возвращаемым значением.

4. **Как посмотреть backtrace (стек вызовов) в GDB?**
   - `stack`
   - `call-stack`
   - `backtrace` или `bt`
   - `frames`

   **Ответ: `backtrace` или `bt`**. Обе формы работают.

5. **Что такое core dump?**
   - Дамп содержимого CPU кэша
   - Снимок состояния процесса в момент аварийного завершения
   - Лог системных вызовов до краша
   - Образ исполняемого файла

   **Ответ: снимок состояния процесса**. Содержит регистры, стек и сегменты памяти.

6. **Для чего используется `perf stat`?**
   - Записывает профиль для последующего анализа
   - Показывает статистику аппаратных счётчиков (cycles, instructions, cache-misses) за время выполнения
   - Строит flame graph
   - Трассирует системные вызовы

   **Ответ: статистика аппаратных счётчиков**. Один запуск — итоговая статистика за всё время выполнения.

7. **Почему Valgrind Memcheck медленнее чем ASan?**
   - Valgrind работает в режиме ядра
   - Valgrind инструментирует бинарный код через JIT без перекомпиляции; ASan — лёгкий runtime, вставленный компилятором
   - Valgrind проверяет больше типов ошибок
   - Valgrind использует 100% одного ядра всегда

   **Ответ: Valgrind использует JIT-инструментацию бинарного кода** — ~10-50× overhead vs ~2× у ASan.

8. **Что такое watchpoint в GDB?**
   - Breakpoint на вызов конкретной функции
   - Точка наблюдения, останавливающая программу при изменении (или чтении) данных по адресу/переменной
   - Таймер срабатывающий через N миллисекунд
   - Мониторинг системных вызовов внутри GDB

   **Ответ: точка наблюдения за данными**. В отличие от breakpoint (по коду), watchpoint срабатывает при обращении к памяти.

9. **Что делает `rr record` / `rr replay`?**
   - Записывает только системные вызовы, как strace
   - Детерминированно записывает прогон и воспроизводит его сколько угодно раз с reverse-debugging
   - Профилирует CPU как perf
   - Снимает core dump живого процесса

   **Ответ: детерминированная запись и воспроизведение**. Главное применение — флапающие/невоспроизводимые баги: записал один «выстрел» — отлаживаешь сколько надо.

10. **Что нужно, чтобы при падении создавался core dump?**
   - Только `-g` при компиляции
   - Ненулевой `ulimit -c` И настроенный `kernel.core_pattern`
   - Запуск под root
   - Флаг `--core` у программы

   **Ответ: `ulimit -c` (≠0) и `core_pattern`**. По умолчанию `ulimit -c` равен 0 — самый частый «почему нет core».

11. **Какой инструмент с минимальным overhead покажет, какие файлы открывают процессы во всей системе?**
   - `strace` на каждом процессе
   - `bpftrace`/`opensnoop` (eBPF на tracepoint `sys_enter_openat`)
   - `valgrind`
   - `addr2line`

   **Ответ: eBPF (`opensnoop`/`bpftrace`)**. В отличие от `strace` (ptrace, останавливает процесс), eBPF собирает события в ядре без остановки и с низким overhead.

12. **Что делает `MALLOC_CHECK_=3`?**
   - Отключает malloc для отладки
   - Включает проверки целостности кучи glibc: печать диагностики + `abort()` при порче
   - Ограничивает размер кучи
   - Принудительно использует mmap для всех аллокаций

   **Ответ: проверки целостности кучи glibc**. Дешевле ASan: ловит часть double-free/переполнений без перекомпиляции.

---

### МЕХАНИЗМЫ (11 self_grade)

1. **Как найти use-after-free баг без запуска в отладчике, только с ASan?**

   Скомпилировать с `-fsanitize=address -g -O1`, запустить. При первом use-after-free ASan выведет: адрес обращения, стек вызовов места ошибки, описание блока ("freed 40-byte region"), стек вызовов места `free()` и стек вызовов места `malloc()`. По этим трём стекам восстанавливается полная история блока: кто создал, кто освободил, кто использует после освобождения. `ASAN_OPTIONS=halt_on_error=0` позволит найти несколько ошибок за один запуск.

2. **Объясни как strace используется для диагностики "Permission denied" при открытии файла**

   `strace -e trace=file ./prog 2>&1` покажет каждый `openat()` с точным путём и возвращаемой ошибкой. Пример: `openat(AT_FDCWD, "/var/lib/myapp/data.db", O_RDWR|O_CREAT, 0666) = -1 EACCES (Permission denied)`. Из этого: (а) точный путь — программа ищет не там где думали? (б) EACCES или ENOENT — нет прав или нет файла? (в) флаги открытия — возможно O_RDWR когда файл read-only? Дополнительно `strace -yy` покажет разрешённые пути для существующих fd.

3. **Как настроить GDB для отладки многопоточного deadlock?**

   Запустить `gdb ./prog`, `run`. После зависания — `Ctrl+C`. Команды: `thread apply all bt` — увидеть все потоки и где каждый заблокирован. Потоки в `futex()` ждут мьютекс. `set scheduler-locking on` — заморозить остальные потоки при переключении. `thread N` + `info locals` — посмотреть какой мьютекс ждёт поток N. Построить граф ожидания: цикл в графе = deadlock. Альтернатива: `valgrind --tool=helgrind` или `TSAN` обнаружат lock ordering violations превентивно.

4. **Как perf record/report помогает найти горячий код?**

   `perf record -F 99 -g ./prog` — sampling 99 раз/сек, запись call graph. `perf report` — TUI показывает функции отсортированные по % CPU. Войти в функцию `Enter` → развернуть call graph (кто вызывает эту функцию, кого она вызывает). `a` — аннотировать: показать исходный код (или assembler если нет отладочной информации) с процентами времени на каждой строке. Строки с наибольшим % — точки оптимизации. Flame graph — альтернативный вид того же профиля.

5. **Как читать вывод Valgrind memcheck для утечки памяти?**

   Секция "LEAK SUMMARY" в конце: `definitely lost: N bytes in M blocks` — настоящие утечки. Для каждой: стек аллокации (кто вызвал malloc). Раздел "HEAP SUMMARY" показывает общий баланс. Команда `--leak-check=full --show-leak-kinds=all` разворачивает каждую утечку с полным стеком. Искать в стеке первую вашу функцию (не malloc/calloc) — это место аллокации которая никогда не была освобождена. `--track-origins=yes` помогает для uninit memory.

6. **Как использовать addr2line для расшифровки адреса из crash лога?**

   `addr2line -e ./prog -f -i 0xADDRESS`. Для PIE-бинарников нужен offset, не абсолютный адрес: из `/proc/$PID/maps` берём базовый адрес загрузки, вычитаем из crash address. Для shared libraries: аналогично — адрес относительно базы SO. `-f` показывает имя функции, `-i` разворачивает inlined функции. Если выводит `?:?` — нет отладочной информации (нужен `-g` при компиляции).

7. **Какая разница между `-O0` и `-Og` при отладке?**

   `-O0`: никаких оптимизаций. Каждая переменная в стеке, каждое выражение вычисляется отдельно. Легко отлаживать, но код нереально медленный — профиль не соответствует реальности. `-Og`: оптимизации "совместимые с отладкой" (GCC 4.8+) — это, по документации gcc, **все флаги `-O1`, кроме нескольких, мешающих отладке**. То есть лёгкая оптимизация всё же делается (в т.ч. инлайнинг мелких функций), но компилятор сохраняет вменяемое соответствие строк, держит значения переменных доступными отладчику и не применяет агрессивный реордеринг. Результат: быстрее `-O0`, а отладчик не врёт о текущей строке. Рекомендован вместо `-O0` для разработки.

8. **Как установить conditional breakpoint в GDB срабатывающий на определённое значение переменной?**

   `break file.c:42 if x == 100` — простой случай. `break func if strcmp(buf, "trigger") == 0` — вызов функции в условии (GDB вызывает её в контексте отлаживаемой программы). `condition 3 ptr != NULL && *ptr > 0` — сложное условие к существующему bp #3. Если условие вычисляется медленно (каждое срабатывание = проверка) — лучше `ignore N count` чтобы пропустить первые N срабатываний, или сузить breakpoint до конкретного call-site.

9. **Как с помощью reverse-debugging найти, кто испортил переменную?**

   Дойти до точки, где переменная уже содержит мусор (`continue` до краша/ассерта). Поставить аппаратный watchpoint на её адрес: `watch -l obj->field`. Выполнить `reverse-continue` — отладчик пойдёт **назад** и остановится на последней инструкции, **записавшей** значение. Это прямой ответ на «кто это сделал», без перезапусков и догадок. Встроенный `record` GDB работает на коротких отрезках; для длинных/многопоточных прогонов — `rr replay`, где обратное выполнение детерминировано и быстро.

10. **Как устроена раздельная отладочная информация (split debug info) и зачем build-id?**

   Из бинарника выносят DWARF: `objcopy --only-keep-debug prog prog.debug`, затем `objcopy --strip-debug prog` и `objcopy --add-gnu-debuglink=prog.debug prog`. В продакшн идёт лёгкий stripped-бинарник, символы хранятся отдельно. GDB находит `prog.debug` по `.gnu_debuglink` или по **build-id** — хэшу содержимого (`readelf -n`), который записан и в бинарник, и в файл символов. Build-id гарантирует, что символы соответствуют **именно этой** сборке: файлы из разных сборок не «склеятся» по ошибке. На этом построены debuginfo-пакеты и `debuginfod`.

11. **Как отладить процесс, работающий внутри контейнера?**

   Процесс контейнера виден с хоста под хостовым PID. Вариант 1: с хоста `gdb -p $(pgrep -f svc)`, символы — из образа. Вариант 2: войти в namespaces цели — `nsenter -t $PID -a gdb -p $PID`. Ключевая тонкость: пути к `.so`/исполняемому внутри контейнера отличаются, поэтому в GDB задают `set sysroot /proc/$PID/root` (корень mnt-namespace цели через `/proc`), и тогда символы и библиотеки находятся корректно. Аналогично для удалённой/embedded цели работает `gdbserver` + `target remote`.

---

### ЭКСПЕРТ (7 self_grade)

1. **Как построить flame graph для продакшн системы без остановки сервиса?**

   `perf record -F 99 -p $PID -g -- sleep 30` — прикрепиться к живому процессу на 30 секунд. Overhead ~1-2% CPU — приемлемо для большинства сервисов. Затем: `perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg`. Для Java/JVM: `-XX:+PreserveFramePointer` + `perf-map-agent`. Для Node.js: `node --perf-basic-prof`. Альтернатива с меньшим overhead: eBPF-based профилировщики (bcc `profile.py`, `py-spy`, `rbspy`) — работают без ptrace, работают с любым языком. Важно: иметь `/proc/sys/kernel/perf_event_paranoid <= 1` или запускать с root/CAP_SYS_ADMIN.

2. **Как отладить программу которая крашится только под нагрузкой в production?**

   Стратегия многоуровневая: (1) Core dumps: `ulimit -c unlimited` + `kernel.core_pattern` → анализ `gdb ./prog core` после факта. (2) ASan/TSan в staging с нагрузкой: если воспроизводится. (3) `rr` (Mozilla Record & Replay): `rr record ./prog` + `rr replay` — детерминированное воспроизведение с GDB. (4) `ASAN_OPTIONS=detect_leaks=0:abort_on_error=1` с core dumps включенными — ASan с abort() при первой ошибке → core dump с полным состоянием. (5) Если нужен live attach без остановки: `gdb -p $PID` + `set pagination off` + `bt` + `detach` — быстрый снимок стека без kill. (6) `perf record` под нагрузкой для временного профиля.

3. **Как использовать GDB Python scripting для автоматизации анализа?**

   GDB имеет встроенный Python интерпретатор. `python gdb.execute("bt")` — выполнить GDB команду. `gdb.selected_frame()` — текущий фрейм. `frame.read_var("x")` — прочитать переменную. `gdb.parse_and_eval("expr")` — вычислить выражение. Применения: (а) Обход linked list: `pretty-printer` класс наследует `gdb.ValuePrinter` — автоматически красивый вывод структур. (б) Batch-анализ: скрипт проходит по всем потокам, печатает интересующие переменные. (в) Condition с Python: `break func` + `commands` + `python if gdb.parse_and_eval("x") > 100: gdb.execute("bt")`. Подключение: `source ~/gdb_helpers.py` или через `.gdbinit`.

4. **Как ASan shadow memory реализована технически?**

   64-bit: виртуальное адресное пространство разделено. Shadow region = `(real_address >> 3) + SHADOW_OFFSET`. Для каждых 8 байт реальной памяти — 1 байт тени. Значение байта тени: 0 = все 8 байт доступны; N (1-7) = только первые N байт доступны; отрицательное = весь 8-байтный блок недоступен (разные значения для разных причин: heap redzones, stack redzones, freed memory, global redzones). При каждом load/store компилятор вставляет: `char shadow = *(char*)((addr >> 3) + SHADOW_OFFSET); if (shadow != 0) report_error();`. Overhead 2× по памяти (shadow), ~2× по скорости (проверка). ASan перехватывает malloc/free: аллокации окружены "red zones" (помечены как недоступные в тени), free() помечает блок как недоступный (use-after-free detection).

5. **Как использовать perf probe для добавления кастомных tracepoints без перекомпиляции?**

   `perf probe` добавляет dynamic tracepoints к живому бинарнику или ядру без перекомпиляции (через uprobes/kprobes). Примеры: `perf probe -x ./prog malloc` — tracepoint на вызов malloc (uprobe). `perf probe -x ./prog malloc%return` — tracepoint на возврат из malloc. `perf probe --add 'main.c:42 x'` — tracepoint на строку 42 с записью переменной `x` (нужны символы). После добавления: `perf record -e probe_prog:malloc_entry -g ./prog` — записать события. `perf script` — вывести события. `perf probe --del '*'` — удалить все probe. Для ядра: `perf probe --add 'tcp_sendmsg bytes'` — отслеживать параметр bytes в tcp_sendmsg без перекомпиляции ядра. Это альтернатива eBPF-инструментам, работающая через uprobes/kprobes Linux perf subsystem (сам `perf probe` использует не eBPF-байткод, а механизм динамических точек ядра).

6. **Как найти false sharing и NUMA-проблемы доступа к памяти инструментами perf?**

   `perf c2c record ./prog && perf c2c report` (cache-to-cache) находит строки кэша, которые «пинг-понгуют» между ядрами (HITM — попадание в модифицированную строку чужого кэша) — прямая подпись false sharing; в отчёте виден адрес строки, смещения переменных и конкурирующие потоки. `perf mem record/report` показывает **уровень** каждого обращения (L1/L2/LLC/Local RAM/Remote RAM): много `Remote RAM` → данные читаются с чужого NUMA-узла (десятки лишних нс). Лечение false sharing — `alignas(64)`/padding; NUMA — привязка памяти и потоков к узлу (`numactl`, `mbind`, `set_mempolicy`). Это связка с C1 (модель памяти) и C6 (производительность).

7. **Как отладить ядро Linux под QEMU и на железе (kgdb)?**

   Под QEMU: запустить гостя с `-s -S` (`-s` открывает gdbstub на `:1234`, `-S` морозит до команды от GDB), на хосте `gdb vmlinux` + `target remote :1234`, ставить `hbreak start_kernel` (аппаратный bp — память для soft-bp ещё не готова), `continue`. На реальном железе — `kgdb` поверх `kgdboc` (kgdb over console): параметры ядра `kgdboc=ttyS0,115200 kgdbwait`, ядро ждёт подключения на последовательном порту, с хоста `target remote /dev/ttyS0`. Нужен `vmlinux` с символами (не сжатый `bzImage`). Это прямой мост в треки K (модули ядра) и EL (embedded), где отладка через `printk` дорога.

---

## 23. Что дальше

Ф4 закрывает Этап 1 (Фундамент): ты владеешь языком C на уровне UB и типов (Ф1), контрактом syscalls и `errno`/`EINTR` (Ф2), моделью процессов/потоков/памяти/ELF (Ф3) — и теперь умеешь **видеть**, что код делает на самом деле: GDB и reverse-debugging, ASan/UBSan/TSan, Valgrind, strace/ltrace, perf и flame graph'ы, eBPF, анализ core dump.

Эти инструменты — не «глава, которую прочитал и забыл», а ежедневный фон всего, что дальше. Прямые связки:

- **C1 (Конкурентность и модель памяти)** — следующий модуль и начало Этапа 2A. Здесь критичны **TSan** (единственные ворота против гонок — ASan их не ловит), **helgrind**, **perf c2c** (false sharing из §17.4) и reverse-debugging/`rr` для невоспроизводимых гонок. Сквозной разбор из §20 — ровно про баг уровня C1.
- **C2 (epoll/io_uring)** и **C5 (демоны)** — `strace -f`, `perf`, off-CPU анализ (§17.5), отладка по core от живого сервиса (`gcore`, `coredumpctl`).
- **C6 (Производительность)** — `perf stat/record/mem/c2c`, flame graph'ы, аннотация горячих инструкций как основной рабочий цикл.
- **K1–K3 (ядро)** и **EL1–EL7 (embedded)** — `ftrace`/`trace-cmd`, `kgdb`, отладка ядра под QEMU (`-s -S`), кросс-`gdbserver` (§18) становятся основными инструментами, потому что `printf`-отладка в ядре дорога и опасна.

Правило, которое стоит унести из модуля: **сначала измерь/посмотри, потом меняй**. Большинство «таинственных» багов перестают быть таинственными, как только подключён правильный инструмент из этого модуля — а гадание по исходнику без инструментов и есть главный пожиратель времени.
