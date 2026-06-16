# Модуль Ф4 — Инструменты отладки

## 0. Карта модуля

| | |
|---|---|
| **Время** | 10–15 ч |
| **Зачем** | Без отладочных инструментов системный программист слеп. GDB, Valgrind, AddressSanitizer, strace, perf — это ежедневные инструменты профессионала. Умение читать core dump, находить утечки памяти, профилировать CPU — обязательные навыки. |
| **Ресурсы** | `man gdb`, Valgrind manual, perf wiki, brendangregg.com |

---

## 1. GDB — основы

### 1.1 Запуск и базовые команды

Прежде чем запускать GDB, программу нужно скомпилировать с отладочной информацией. Без флага `-g` GDB покажет только адреса и дизассемблированный код — без имён переменных и строк файла.

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
- `SIGBUS` — невыровненный доступ, или MMAP-страница исчезла (truncated file)

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
    #0 0x7f... in malloc (/usr/lib/libasan.so.5+0x...)
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

- **Signed integer overflow**: `INT_MAX + 1` — UB в C, но не в C++ (нет, тоже UB)
- **Shift count out of range**: `x << 33` для 32-битного `x`, `x >> -1`
- **Division by zero**: `a / 0` для целых
- **Null pointer dereference**: через pointer-атрибуты (не все случаи)
- **Invalid enum value**: кастирование числа в enum, которого нет в перечислении
- **Misaligned pointer dereference**: `*(int*)odd_address`
- **VLA с нулевым размером**: `int arr[0]`
- **Integer promotion overflow**: беззнаковые операции с promoted знаковыми типами

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

**ThreadSanitizer (-fsanitize=thread)**: обнаруживает data races между потоками. Механизм — vector clocks на каждый доступ к памяти. Overhead: ~5-15× по скорости, 5-10× по памяти. Несовместим с ASan (конфликт shadow memory).

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
- **indirectly lost**: блок недостижим через directly lost блок (например, список внутри lost структуры)
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
- **page-faults**: TLB misses и подкачка страниц

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

## 10. Практика и самопроверка

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

---

## 11. Банк вопросов

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

---

### МЕХАНИЗМЫ (8 self_grade)

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

   `-O0`: никаких оптимизаций. Каждая переменная в стеке, каждое выражение вычисляется отдельно. Легко отлаживать, но код нереально медленный — профиль не соответствует реальности. `-Og`: оптимизации "совместимые с отладкой" (GCC 4.8+). Компилятор применяет оптимизации не нарушающие однозначность строк кода: не инлайнит функции (если только не просит), не удаляет "ненужные" переменные, не переупорядочивает сильно. Результат: поведение ближе к `-O2`, но отладчик не врёт о текущей строке. Рекомендован вместо `-O0` для разработки.

8. **Как установить conditional breakpoint в GDB срабатывающий на определённое значение переменной?**

   `break file.c:42 if x == 100` — простой случай. `break func if strcmp(buf, "trigger") == 0` — вызов функции в условии (GDB вызывает её в контексте отлаживаемой программы). `condition 3 ptr != NULL && *ptr > 0` — сложное условие к существующему bp #3. Если условие вычисляется медленно (каждое срабатывание = проверка) — лучше `ignore N count` чтобы пропустить первые N срабатываний, или сузить breakpoint до конкретного call-site.

---

### ЭКСПЕРТ (5 self_grade)

1. **Как построить flame graph для продакшн системы без остановки сервиса?**

   `perf record -F 99 -p $PID -g -- sleep 30` — прикрепиться к живому процессу на 30 секунд. Overhead ~1-2% CPU — приемлемо для большинства сервисов. Затем: `perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg`. Для Java/JVM: `-XX:+PreserveFramePointer` + `perf-map-agent`. Для Node.js: `node --perf-basic-prof`. Альтернатива с меньшим overhead: eBPF-based профилировщики (bcc `profile.py`, `py-spy`, `rbspy`) — работают без ptrace, работают с любым языком. Важно: иметь `/proc/sys/kernel/perf_event_paranoid <= 1` или запускать с root/CAP_SYS_ADMIN.

2. **Как отладить программу которая крашится только под нагрузкой в production?**

   Стратегия многоуровневая: (1) Core dumps: `ulimit -c unlimited` + `kernel.core_pattern` → анализ `gdb ./prog core` после факта. (2) ASan/TSan в staging с нагрузкой: если воспроизводится. (3) `rr` (Mozilla Record & Replay): `rr record ./prog` + `rr replay` — детерминированное воспроизведение с GDB. (4) `ASAN_OPTIONS=detect_leaks=0:abort_on_error=1` с core dumps включенными — ASan с abort() при первой ошибке → core dump с полным состоянием. (5) Если нужен live attach без остановки: `gdb -p $PID` + `set pagination off` + `bt` + `detach` — быстрый снимок стека без kill. (6) `perf record` под нагрузкой для временного профиля.

3. **Как использовать GDB Python scripting для автоматизации анализа?**

   GDB имеет встроенный Python интерпретатор. `python gdb.execute("bt")` — выполнить GDB команду. `gdb.selected_frame()` — текущий фрейм. `frame.read_var("x")` — прочитать переменную. `gdb.parse_and_eval("expr")` — вычислить выражение. Применения: (а) Обход linked list: `pretty-printer` класс наследует `gdb.ValuePrinter` — автоматически красивый вывод структур. (б) Batch-анализ: скрипт проходит по всем потокам, печатает интересующие переменные. (в) Condition с Python: `break func` + `commands` + `python if gdb.parse_and_eval("x") > 100: gdb.execute("bt")`. Подключение: `source ~/gdb_helpers.py` или через `.gdbinit`.

4. **Как ASan shadow memory реализована технически?**

   64-bit: виртуальное адресное пространство разделено. Shadow region = `real_address >> 3 + SHADOW_OFFSET`. Для каждых 8 байт реальной памяти — 1 байт тени. Значение байта тени: 0 = все 8 байт доступны; N (1-7) = только первые N байт доступны; отрицательное = весь 8-байтный блок недоступен (разные значения для разных причин: heap redzones, stack redzones, freed memory, global redzones). При каждом load/store компилятор вставляет: `char shadow = *(char*)(addr >> 3 + SHADOW_OFFSET); if (shadow != 0) report_error();`. Overhead 2× по памяти (shadow), ~2× по скорости (проверка). ASan перехватывает malloc/free: аллокации окружены "red zones" (помечены как недоступные в тени), free() помечает блок как недоступный (use-after-free detection).

5. **Как использовать perf probe для добавления кастомных tracepoints без перекомпиляции?**

   `perf probe` добавляет dynamic tracepoints к живому бинарнику или ядру без перекомпиляции (через uprobes/kprobes). Примеры: `perf probe -x ./prog malloc` — tracepoint на вызов malloc (uprobe). `perf probe -x ./prog malloc%return` — tracepoint на возврат из malloc. `perf probe --add 'main.c:42 x'` — tracepoint на строку 42 с записью переменной `x` (нужны символы). После добавления: `perf record -e probe_prog:malloc_entry -g ./prog` — записать события. `perf script` — вывести события. `perf probe --del '*'` — удалить все probe. Для ядра: `perf probe --add 'tcp_sendmsg bytes'` — отслеживать параметр bytes в tcp_sendmsg без перекомпиляции ядра. Это eBPF-альтернатива через Linux perf subsystem.
