# Модуль Ф3 — Процессы, потоки, память, ELF

> Этап 1, Фундамент. Этот модуль — сердцевина системного программирования под Linux: всё, что запускает код, изолирует данные и разделяет ресурсы между единицами исполнения. `fork`/`exec`/`wait` — это не просто API, это контракт операционной системы. Без его точного понимания нельзя написать ни корректный демон, ни нормальный шелл, ни трассировщик. ELF-формат — язык, на котором компилятор разговаривает с загрузчиком и отладчиком. pthreads — стандарт, который лежит под любой многопоточной userspace-программой на Linux.

---

## 0. Карта модуля

| | |
|---|---|
| **Время (честно)** | 15–20 ч. Теория — 5–6ч, практика fork/exec/wait — 4ч, ELF инструменты — 2ч, pthreads + atomics — 4–5ч. |
| **Зачем** | fork/exec/wait — основа любого системного программирования; понимание виртуальной памяти и ELF-формата необходимо для отладки, профилирования, написания системных утилит; pthreads — фундамент многопоточных приложений |
| **Главная книга** | Michael Kerrisk, **«The Linux Programming Interface»** (TLPI) — главы 24–31 (процессы), 29–33 (потоки), 48–50 (память). |
| **Справочник** | `man 2 clone`, `man 7 pthreads`, `man 2 fork`, `man 2 execve`, `man 2 waitpid`, `man 2 mmap`, `man 5 elf`. |
| **Инструменты** | `strace -f`, `readelf`, `objdump`, `nm`, `ldd`, `size`, `/proc/self/maps`, `/proc/self/smaps`, `valgrind --tool=helgrind`, `gcc -fsanitize=thread`. |

**Точная карта чтения:**

| Тема | TLPI (главы) | man | Зачем именно это |
|---|---|---|---|
| Анатомия процесса | гл. 6 | `man 5 proc`, `man 2 getpid` | Что хранит ядро для каждого процесса |
| fork/exec/wait | гл. 24–27 | `man 2 fork`, `man 2 execve`, `man 2 waitpid` | Полный контракт создания и ожидания |
| Виртуальная память | гл. 49–50 | `man 2 mmap`, `man 2 mlock` | Как адресное пространство отображается на физику |
| ELF | — | `man 5 elf` | Структура исполняемого файла |
| pthreads | гл. 29–33 | `man 7 pthreads`, `man 3 pthread_create` | POSIX threading API |
| Atomics C11 | — | C11 §5.1.2.4 / cppreference `memory_order` | Модель памяти и безопасность без мьютексов |
| clone/namespaces | гл. 28, Linux-специфика | `man 2 clone`, `man 7 namespaces` | Основа контейнеров |

---

## 1. Анатомия процесса Linux

### 1.1 Что такое процесс

Процесс — это **экземпляр программы в исполнении**: виртуальное адресное пространство + ресурсы ядра (таблица файловых дескрипторов, таблица обработчиков сигналов, информация о пользователе/группе) + одно или несколько ядер исполнения (потоков). На уровне ядра процесс описывается структурой `task_struct` (ядро Linux) — то же самое, что описывает поток; разница только в том, что разделяется между экземплярами.

Ядро хранит для каждого процесса:
- идентификаторы (PID, PPID, PGID, SID, UID, GID, ...)
- таблицу виртуальных адресных областей (VMA list / `mm_struct`)
- таблицу открытых файлов (`files_struct`)
- маску сигналов и таблицу обработчиков
- учётную информацию (CPU time, limits — `rlimit`)
- namespace-принадлежность (PID ns, net ns, mnt ns, ...)

### 1.2 Идентификаторы

```c
#include <sys/types.h>
#include <unistd.h>

pid_t pid   = getpid();   /* Process ID — уникален среди живых процессов */
pid_t ppid  = getppid();  /* Parent PID */
pid_t pgid  = getpgrp();  /* Process Group ID — группа связанных процессов */
pid_t sid   = getsid(0);  /* Session ID — для управления терминалом */

uid_t uid   = getuid();   /* Real UID — кто запустил процесс */
uid_t euid  = geteuid();  /* Effective UID — определяет права доступа */
gid_t gid   = getgid();
gid_t egid  = getegid();
```

**Зачем нужны реальный и эффективный UID?** Программы с setuid-битом (например `/usr/bin/passwd`) запускаются с `EUID=0` (root), но `UID` остаётся UID пользователя. Это позволяет отличить "кто запустил" от "с чьими правами работает".

**Process Group и Session** — иерархия для управления заданиями (job control) в шелле:
- Pipeline `cat file | grep foo | wc -l` — все три процесса в одной process group
- `SIGINT` при нажатии `^C` доставляется всей process group
- Session — набор process groups, привязанный к одному управляющему терминалу

### 1.3 Состояния процесса

Состояние процесса видно в `/proc/<pid>/status` поле `State:` и в выводе `ps`:

| Символ | Название | Когда |
|--------|----------|-------|
| `R` | Running / Runnable | Выполняется или ждёт CPU в очереди планировщика |
| `S` | Sleeping (interruptible) | Ждёт событие (I/O, сигнал, timer) — может быть прерван сигналом |
| `D` | Disk sleep (uninterruptible) | Ждёт I/O — **нельзя** прервать даже SIGKILL; типично при проблемах с диском/NFS |
| `Z` | Zombie | Завершился, ждёт `wait()` от родителя |
| `T` | Stopped | Остановлен `SIGSTOP`/`SIGTSTP` (или ptrace) |
| `X` | Dead | Мёртв, не должен быть виден — мгновенное состояние |
| `t` | Traced | Остановлен трассировщиком (ptrace) |
| `I` | Idle kernel thread | Поток ядра без работы |

Состояние `D` — практически важное: процесс в этом состоянии **не реагирует на SIGKILL** пока не завершится I/O операция. Это нормально для кратких I/O; если процесс застрял в D надолго — признак проблем с блочным устройством или NFS-зависания.

### 1.4 /proc/self/ — самоописание процесса

`/proc/self` — симлинк на `/proc/<текущий PID>`. Ключевые файлы:

```bash
/proc/self/status      # Общая информация: Name, Pid, PPid, State, UID/GID, VmRSS, ...
/proc/self/maps        # Виртуальные адресные области (VMA)
/proc/self/smaps       # Детальная статистика каждого VMA (RSS, PSS, ...)
/proc/self/fd/         # Открытые файловые дескрипторы (симлинки)
/proc/self/fdinfo/     # Подробности каждого fd (позиция, флаги)
/proc/self/cmdline     # Аргументы командной строки (\0-разделители)
/proc/self/environ     # Переменные окружения (\0-разделители)
/proc/self/exe         # Симлинк на исполняемый файл
/proc/self/cwd         # Симлинк на текущий рабочий каталог
/proc/self/stat        # Машиночитаемая статистика (используют ps, top)
/proc/self/ns/         # Namespace файловые дескрипторы
```

```c
/* Прочитать символическую ссылку /proc/self/exe */
#include <unistd.h>
char path[4096];
ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
if (n > 0) {
    path[n] = '\0';
    printf("executable: %s\n", path);
}
```

---

## 2. fork() — создание процесса

### 2.1 Полная семантика fork()

`fork()` создаёт точную копию текущего процесса. Точную — но с нюансами:

**Что наследуется (копируется):**
- Виртуальное адресное пространство (через Copy-on-Write — см. ниже)
- Таблица открытых fd (те же описания open file; смещения разделяются!)
- Маска сигналов (`sigprocmask`) и обработчики сигналов
- Текущий рабочий каталог (`cwd`) и корень (`/`)
- `umask`, rlimits
- Переменные окружения
- Группа процессов (PGID) и сессия (SID)

**Что НЕ наследуется (сбрасывается/становится уникальным):**
- PID (дочерний получает новый уникальный PID)
- Pending signals (множество незадоставленных сигналов очищается у потомка)
- Файловые блокировки (классические `fcntl` locks — они привязаны к паре (процесс, inode), и потомок их не наследует)
- Таймеры (`setitimer`, POSIX timers — сбрасываются у потомка)
- Очереди async I/O (aio — не завершаются у потомка)

**После fork() оба процесса продолжают выполнение с одной точки** — следующей инструкции после `fork()`. Разделяются по возвращаемому значению: `0` в дочернем, `PID потомка` в родительском, `-1` при ошибке (только в родительском).

### 2.2 Copy-on-Write (COW)

Наивная реализация `fork()` скопировала бы всё адресное пространство сразу — для процесса с 1 ГБ heap это катастрофа, особенно если после `fork()` сразу вызывается `exec()`.

Linux реализует **Copy-on-Write**: при `fork()` страницы родителя и потомка отмечаются как read-only в таблицах страниц обоих процессов. При первой записи в любую из этих страниц ядро:
1. Перехватывает page fault (попытка записи в read-only страницу)
2. Создаёт физическую копию страницы
3. Обновляет таблицу страниц нарушителя — теперь он пишет в свою копию
4. Снимает read-only с оригинала у другого процесса (если он единственный владелец)

**Следствия COW:**
- `fork()` быстрый — данные не копируются; накладные расходы пропорциональны размеру таблиц страниц (числу маппингов), а не объёму данных
- Реальное копирование происходит лениво при записи
- Если после `fork()` вызвать `exec()` — большинство страниц никогда не копируется (именно поэтому fork+exec эффективны)
- Память (RSS) родителя и потомка физически общая до момента записи — важно для мониторинга

### 2.3 Базовый паттерн fork/wait

```c
#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

int main(void)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    } else if (pid == 0) {
        /* Дочерний процесс */
        printf("child: PID=%d, PPID=%d\n", (int)getpid(), (int)getppid());
        /* Использовать _exit(), не exit() — не флашим stdio-буферы родителя */
        _exit(42);
    }

    /* Родительский процесс */
    int status;
    pid_t waited = waitpid(pid, &status, 0);
    if (waited < 0) {
        perror("waitpid");
        exit(1);
    }

    if (WIFEXITED(status)) {
        printf("child exited normally, code=%d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        printf("child killed by signal %d\n", WTERMSIG(status));
    }

    return 0;
}
```

**Почему `_exit()` в потомке, а не `exit()`?** `exit()` вызывает `atexit()`-обработчики и `fflush()` на всех stdio-буферах. После `fork()` потомок унаследовал те же буферы с теми же незаписанными данными. Если потомок вызовет `exit()` — он флашнет родительские данные (и данные выведутся дважды). `_exit()` напрямую вызывает `exit_group` syscall без лишней работы.

### 2.4 Zombie-процессы

Когда дочерний процесс завершается, ядро **не удаляет его запись из таблицы процессов сразу**. Запись остаётся в состоянии Zombie (`Z`) до тех пор, пока родитель не вызовет `wait()`/`waitpid()` и не заберёт статус завершения. Только после этого ресурсы PID освобождаются полностью.

Zombie не потребляет CPU или RAM (ресурсы уже освобождены), но занимает **слот в таблице процессов** и **PID**. Сервер, порождающий тысячи потомков без `wait()`, постепенно исчерпает лимит PID.

**Три способа избежать zombie:**

1. **Явный `waitpid()`** — блокирующий или с `WNOHANG` в цикле
2. **SIGCHLD handler** — асинхронный сбор завершившихся потомков:

```c
#include <signal.h>
#include <errno.h>

static void sigchld_handler(int sig)
{
    (void)sig;
    int saved_errno = errno;  /* waitpid может изменить errno */
    /*
     * Цикл с WNOHANG — собрать ВСЕХ завершившихся.
     * Один SIGCHLD может прийти для нескольких завершившихся потомков.
     */
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    errno = saved_errno;  /* восстановить errno для прерванного syscall */
}

/* При инициализации: */
struct sigaction sa = {0};
sa.sa_handler = sigchld_handler;
sa.sa_flags   = SA_RESTART | SA_NOCLDSTOP;  /* SA_NOCLDSTOP: не получать сигнал при SIGSTOP */
sigemptyset(&sa.sa_mask);
sigaction(SIGCHLD, &sa, NULL);
```

3. **`SA_NOCLDWAIT`** или **`signal(SIGCHLD, SIG_IGN)`** — сказать ядру явно игнорировать завершение потомков. Тогда ядро автоматически пожинает зомби без `wait()`. POSIX-2008 гарантирует это поведение.

### 2.5 Double fork — создание настоящего daemon

Daemon-процесс должен:
- Не иметь управляющего терминала (иначе получит SIGHUP при отключении)
- Не быть лидером сессии (иначе может получить терминал при открытии `tty`)
- Работать независимо от родителя

```c
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>

void daemonize(void)
{
    pid_t pid;

    /* Шаг 1: первый fork — родитель выходит */
    pid = fork();
    if (pid < 0) { perror("fork1"); exit(1); }
    if (pid > 0) { exit(0); }  /* родитель завершается */

    /* Теперь мы потомок — создаём новую сессию */
    if (setsid() < 0) { perror("setsid"); exit(1); }
    /* setsid(): мы становимся лидером новой сессии без терминала */

    /* Шаг 2: второй fork — лидер сессии выходит */
    pid = fork();
    if (pid < 0) { perror("fork2"); exit(1); }
    if (pid > 0) { exit(0); }  /* лидер сессии завершается */

    /*
     * Внук: не является лидером сессии → не может СЛУЧАЙНО захватить
     * управляющий терминал, открыв реальное tty-устройство
     * (напр. /dev/tty1, /dev/pts/N) БЕЗ флага O_NOCTTY.
     * (Заметь: /dev/tty — это синоним УЖЕ существующего упр. терминала;
     *  получить через него НОВЫЙ терминал нельзя — без терминала open
     *  на /dev/tty вернёт ENXIO. Захват идёт через открытие физического
     *  tty, и только лидером сессии.) Настоящий daemon.
     */

    /* Закрыть stdin/stdout/stderr, перенаправить в /dev/null */
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO) close(devnull);
    }

    /* Сменить рабочий каталог — не держать смонтированные ФС */
    chdir("/");

    /* Установить umask — не зависеть от umask родителя */
    umask(0);
}
```

**Почему второй fork?** После `setsid()` мы становимся лидером новой сессии. Лидер сессии **может** получить управляющий терминал при первом `open()` на tty-устройство (поведение зависит от ОС). Второй `fork()` гарантирует, что daemon — не лидер сессии и терминал ему не грозит никогда.

### 2.6 vfork() — устаревший артефакт

`vfork()` исторически создавался для оптимизации `fork()` + `exec()` до появления COW: потомок разделял адресное пространство родителя (без копирования), родитель блокировался до `exec()` или `_exit()` потомка.

**Никогда не использовать в новом коде.** Причины:
- Поведение крайне ограничено (нельзя вызывать почти ничего до `exec()`)
- COW делает `fork()` достаточно быстрым
- `posix_spawn()` — современная замена ручной связки fork+exec (новый процесс **создаётся**, но без копирования адресного пространства родителя — см. §3.5)

---

## 3. exec() — замена образа процесса

### 3.1 Системный вызов execve()

```c
#include <unistd.h>

int execve(const char *pathname, char *const argv[], char *const envp[]);
```

`execve()` заменяет текущий образ процесса новым исполняемым файлом. PID, PPID, PGID, SID — сохраняются. Открытые fd (без FD_CLOEXEC) — переходят к новой программе. Потоки — уничтожаются все, кроме вызвавшего.

При успешном `execve()` функция **не возвращается** — управление передаётся точке входа нового образа. Возврат означает ошибку.

### 3.2 Семейство exec-функций (обёртки libc)

```c
/* Все — обёртки над execve() */

/* l = list аргументов, NULL-терминированный список в параметрах */
int execl(const char *path, const char *arg, .../* NULL */);
int execlp(const char *file, const char *arg, .../* NULL */);  /* PATH поиск */
int execle(const char *path, const char *arg, .../* NULL, char *const envp[] */);

/* v = vector (массив argv) */
int execv(const char *path, char *const argv[]);
int execvp(const char *file, char *const argv[]);    /* PATH поиск */
int execvpe(const char *file, char *const argv[], char *const envp[]); /* GNU */
```

Мнемоника суффиксов: `l` = list, `v` = vector, `p` = PATH поиск, `e` = custom environ.

```c
/* Пример: запустить /bin/ls -la /tmp с текущим environ */
char *const args[] = { "ls", "-la", "/tmp", NULL };
execv("/bin/ls", args);
/* Если мы здесь — exec провалился */
perror("execv");
exit(127);  /* 127 = command not found (соглашение shell) */
```

### 3.3 Что происходит при exec()

**Сбрасывается (заменяется новым образом):**
- Виртуальная память: старые сегменты unmapped, загружаются сегменты нового ELF
- Обработчики сигналов → `SIG_DFL` (кроме `SIG_IGN` — они сохраняются)
- Маппинги mmap (включая heap и stack)
- Потоки: все уничтожаются кроме вызвавшего exec

**Сохраняется:**
- PID, PPID, PGID, SID
- Открытые fd (без `FD_CLOEXEC`)
- Реальные UID/GID (эффективные могут измениться через setuid-бит)
- Маска сигналов **и** множество pending-сигналов (оба переживают `exec` — в отличие от `fork`, где у потомка pending очищаются)
- Текущий каталог, umask, rlimits

### 3.4 FD_CLOEXEC — обязателен для любого fd в долгоживущем процессе

```c
/* При open() — атомарно: */
int fd = open("file.txt", O_RDONLY | O_CLOEXEC);

/* Или явно через fcntl: */
int flags = fcntl(fd, F_GETFD);
fcntl(fd, F_SETFD, flags | FD_CLOEXEC);

/* Или при создании socket/pipe: */
int sock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
int pfd[2];
pipe2(pfd, O_CLOEXEC);  /* GNU extension */
```

**Почему `O_CLOEXEC` при `open()`, а не отдельный `fcntl()` потом?** В многопоточной программе между `open()` и `fcntl()` другой поток может вызвать `fork()` + `exec()` — fd утечёт в дочерний процесс. `O_CLOEXEC` устанавливает флаг **атомарно** при создании fd.

### 3.5 posix_spawn() — fork+exec без fork()

В среде с жёсткими требованиями к RT-latency или при использовании valgrind (где `fork()` медленный), `posix_spawn()` предоставляет удобный интерфейс:

> **Что значит «без `fork()`».** `posix_spawn` **всё равно создаёт новый процесс** (у
> потомка свой PID) — иначе и быть не может: запуск программы = новый процесс. «Без
> `fork()`» означает, что **тебе** не нужно вручную писать связку `fork`+`exec` и
> обрабатывать промежуточное состояние, **и** что glibc реализует это эффективно: под
> капотом — `clone(CLONE_VM | CLONE_VFORK)` (как `vfork`), то есть **без копирования
> таблиц страниц** родителя (нет COW-дублирования адресного пространства). Родитель
> приостанавливается, пока потомок не вызовет `execve`/`_exit`. Так что выигрыш — в
> отсутствии копирования памяти и ручной обвязки, а не в «запуске без процесса».

```c
#include <spawn.h>

posix_spawnattr_t attr;
posix_spawn_file_actions_t fa;
posix_spawnattr_init(&attr);
posix_spawn_file_actions_init(&fa);

/* Закрыть stdin в дочернем процессе */
posix_spawn_file_actions_addclose(&fa, STDIN_FILENO);

char *const argv[] = { "ls", "-la", NULL };
pid_t child;
posix_spawn(&child, "/bin/ls", &fa, &attr, argv, environ);

posix_spawnattr_destroy(&attr);
posix_spawn_file_actions_destroy(&fa);
waitpid(child, NULL, 0);
```

---

## 4. wait() и waitpid() — синхронизация с потомком

### 4.1 Интерфейс waitpid()

```c
#include <sys/wait.h>

pid_t waitpid(pid_t pid, int *status, int options);
```

**Первый аргумент `pid`:**
| Значение | Смысл |
|----------|-------|
| `-1` | Любой потомок (эквивалент `wait()`) |
| `> 0` | Конкретный дочерний PID |
| `0` | Любой потомок из той же process group что и вызывающий |
| `< -1` | Любой потомок с `PGID == abs(pid)` |

**Опции `options`:**
| Флаг | Смысл |
|------|-------|
| `WNOHANG` | Не блокировать; вернуть 0 если нет завершившихся |
| `WUNTRACED` | Также сообщать об остановленных потомках (SIGSTOP) |
| `WCONTINUED` | Также сообщать о продолжении (SIGCONT) |

**Макросы для разбора статуса:**
```c
if (WIFEXITED(status))   { int code = WEXITSTATUS(status);  }  /* нормальный выход */
if (WIFSIGNALED(status)) { int sig  = WTERMSIG(status);     }  /* убит сигналом */
if (WIFSTOPPED(status))  { int sig  = WSTOPSIG(status);     }  /* остановлен */
if (WIFCONTINUED(status)){ /* продолжил после SIGCONT */    }

/* Только если WIFSIGNALED: */
#ifdef WCOREDUMP
if (WCOREDUMP(status))   { /* core dump создан */ }
#endif
```

### 4.2 Ожидание нескольких потомков

```c
/* Запустить N потомков и дождаться всех */
#define N 5
pid_t children[N];

for (int i = 0; i < N; i++) {
    children[i] = fork();
    if (children[i] == 0) {
        /* потомок i */
        _exit(i + 1);
    }
}

/* Родитель: ждать в любом порядке */
for (int i = 0; i < N; i++) {
    int status;
    pid_t w = waitpid(-1, &status, 0);  /* -1 = любой потомок */
    if (w < 0) { perror("waitpid"); break; }
    printf("child %d exited with code %d\n",
           (int)w, WEXITSTATUS(status));
}
```

### 4.3 waitid() — расширенный интерфейс

```c
#include <sys/wait.h>

int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);
```

`waitid()` заполняет `siginfo_t` с детальной информацией: `si_pid`, `si_uid`, `si_status`, `si_code` (`CLD_EXITED`, `CLD_KILLED`, `CLD_DUMPED`, `CLD_STOPPED`, `CLD_CONTINUED`). Более богатый интерфейс чем `waitpid()`.

Главное отличие в **вызове**: вместо «магического» числа в первом аргументе `waitid` разводит «кого ждать» на два явных параметра — `idtype` (**что** означает `id`) и сам `id`, — а **что именно считать событием**, ты задаёшь флагами в `options` (в отличие от `waitpid`, где «завершение» подразумевается). Поэтому **`WEXITED` обязателен** — без него `waitid` не заберёт завершившегося потомка:

```c
#include <sys/wait.h>

siginfo_t info = {0};            // обнулить: при отсутствии потомков si_pid останется 0

// idtype:  P_PID → ждать id;  P_PGID → группу id;  P_ALL → любого (id игнорируется)
// options: WEXITED ОБЯЗАТЕЛЕН для завершений; |WSTOPPED|WCONTINUED|WNOHANG — по желанию
if (waitid(P_ALL, 0, &info, WEXITED) == -1) {
    perror("waitid");
} else {
    printf("PID %d, ", info.si_pid);
    switch (info.si_code) {                       // КАК умер — прямо в si_code,
        case CLD_EXITED:                          // не нужны макросы WIF*
            printf("exited, code=%d\n", info.si_status);   // здесь si_status = код выхода
            break;
        case CLD_KILLED:
            printf("killed by signal %d\n", info.si_status); // здесь si_status = номер сигнала
            break;
        case CLD_DUMPED:
            printf("killed by signal %d (core dumped)\n", info.si_status);
            break;
    }
}
```

Обрати внимание: `si_status` — **полиморфен**, его смысл (код выхода или номер сигнала) задаётся `si_code`. Это и есть выигрыш `waitid` над `waitpid`: `CLD_DUMPED` отличается от `CLD_KILLED` напрямую (а в `waitpid` пришлось бы городить `WIFSIGNALED` + `WCOREDUMP`), и через `WNOWAIT` в `options` можно **подсмотреть** статус, **не** забирая зомби (запись останется для следующего `wait`).

---

## 5. Виртуальная память Linux

### 5.1 Адресное пространство процесса

Типичная раскладка на **x86-64** (48-bit virtual address space, 4-level page tables):

```
0xFFFFFFFFFFFFFFFF
        ...        ← недоступная «дыра» (канонические адреса)
0xFFFF800000000000 ← начало kernel space (недоступно в user mode)
        ...
0x00007FFFFFFFFFFF ← верхняя граница user space

    [stack]              ← растёт вниз; начало случайное (ASLR)
        ↓
        ... (зазор)
        ↑
    [mmap регион]        ← разделяемые библиотеки, анонимные маппинги
                           растёт вниз от случайного адреса (ASLR)

    [heap]               ← растёт вверх через brk()/sbrk()
                           начинается после BSS

    [BSS]                ← неинициализированные глобальные переменные
    [.data]              ← инициализированные глобальные переменные
    [.text / .rodata]    ← код и константы (read-only, +exec для .text)

0x0000555555554000      ← типичная база PIE-исполняемого (случайная при ASLR)
0x0000000000400000      ← типичная база non-PIE ELF

0x0000000000000000      ← NULL (недоступно, SIGSEGV)
```

На **AArch64** (ARM64): схожая структура, но kernel space с 0xFFFF000000000000 при 48-bit VA.

**ASLR (Address Space Layout Randomization)** — ядро рандомизирует базовые адреса stack, mmap-региона и (для PIE-бинарников) самого исполняемого файла. Цель — усложнить эксплуатацию уязвимостей типа buffer overflow. Управление: `/proc/sys/kernel/randomize_va_space` (0=off, 1=stack+mmap, 2=+heap).

### 5.2 /proc/self/maps — карта памяти процесса

```
address-range            perms offset   dev   inode  pathname
7f8a12345000-7f8a1256b000 r-xp 00000000 08:01 131074 /usr/lib/x86_64-linux-gnu/libc.so.6
7f8a1256b000-7f8a1276a000 ---p 00226000 08:01 131074 /usr/lib/x86_64-linux-gnu/libc.so.6
7f8a1276a000-7f8a12770000 r--p 00225000 08:01 131074 /usr/lib/x86_64-linux-gnu/libc.so.6
7f8a12770000-7f8a12772000 rw-p 0022b000 08:01 131074 /usr/lib/x86_64-linux-gnu/libc.so.6
7f8a12772000-7f8a1277e000 rw-p 00000000 00:00 0      (anonymous)
7ffd23456000-7ffd23477000 rw-p 00000000 00:00 0      [stack]
7ffd235b1000-7ffd235b5000 r--p 00000000 00:00 0      [vvar]
7ffd235b5000-7ffd235b7000 r-xp 00000000 00:00 0      [vdso]
```

**Расшифровка полей permissions:**
- `r` read, `w` write, `x` execute, `p` private (COW), `s` shared
- `---p`: guard page между сегментами libc (защита от buffer overrun в одной секции)
- `[vvar]`: данные vDSO (clock, auxv)
- `[vdso]`: код vDSO — «библиотека» от ядра для быстрых syscalls без ring switch

### 5.3 Страничная память, TLB и иерархия таблиц

**Страница** — минимальная единица управления памятью. На x86-64: обычно 4 KiB, huge pages — 2 MiB (transparent huge pages, THP) или 1 GiB (статически).

**Иерархия таблиц страниц на x86-64 (4-level):**
```
CR3 → PML4 (Page-Map Level-4, 512 записей)
       → PDPT (Page-Directory-Pointer, 512 записей)
          → PD (Page-Directory, 512 записей)
             → PT (Page-Table, 512 записей)
                → physical page (4 KiB)
```
Каждый уровень занимает одну физическую страницу (4 KiB = 512 × 8-байтных записей). Итого до 4 × memory access на трансляцию.

**TLB (Translation Lookaside Buffer)** — кэш аппаратных трансляций virtual→physical. При попадании трансляция происходит за 1 цикл (вместо 4 обращений к памяти). При переключении контекста (смена CR3) TLB сбрасывается (TLB flush) — одна из причин, почему переключения дороги. Тегирование записей TLB идентификатором адресного пространства — ASID (Address Space ID) на ARM, PCID (Process-Context ID) на x86 — позволяет избежать полного flush (в том числе смягчает стоимость **KPTI** — Kernel Page-Table Isolation, разделения таблиц страниц ядра и userspace как митигации Meltdown — на x86).

### 5.4 Детальная статистика: /proc/self/smaps

`smaps` (в отличие от `maps`) показывает для каждой VMA:
```
7f8a12345000-7f8a1256b000 r-xp ...
Size:               2264 kB      ← общий размер VMA
KernelPageSize:        4 kB
MMUPageSize:           4 kB
Rss:                1832 kB      ← реально в RAM прямо сейчас
Pss:                 143 kB      ← RSS / количество процессов разделяющих эти страницы
Shared_Clean:       1832 kB      ← разделяемые немодифицированные страницы
Shared_Dirty:          0 kB
Private_Clean:         0 kB
Private_Dirty:         0 kB      ← приватные модифицированные (COW произошёл)
Referenced:         1832 kB      ← было доступлено с последнего сброса счётчика
Anonymous:             0 kB
LazyFree:              0 kB
AnonHugePages:         0 kB
ShmemPmdMapped:        0 kB
Shared_Hugetlb:        0 kB
Private_Hugetlb:       0 kB
Swap:                  0 kB
SwapPss:               0 kB
Locked:                0 kB
```

**RSS vs PSS:** Если libc загружена в 100 процессов, её физические страницы разделяются. RSS считает их за каждым процессом полностью; PSS делит пропорционально — реальное давление на память.

### 5.5 mlock() — фиксация страниц в RAM

```c
#include <sys/mman.h>

/* Зафиксировать конкретный диапазон */
mlock(addr, len);

/* Зафиксировать ВСЮ память процесса (текущую и будущую) */
mlockall(MCL_CURRENT | MCL_FUTURE);

/* MCL_CURRENT: все уже маппированные страницы
   MCL_FUTURE:  все страницы, которые будут маппированы позже */
```

Зачем нужно RT-приложениям и системному ПО:
- Swapping добавляет непредсказуемую латентность (десятки миллисекунд)
- Audio/video обработка, real-time системы управления, финансовый HFT
- Требует `CAP_IPC_LOCK` или достаточного `RLIMIT_MEMLOCK`

### 5.6 mincore() — какие страницы реально в RAM

```c
#include <sys/mman.h>

/* Узнать какие страницы диапазона [addr, addr+len) находятся в RAM */
size_t pages = (len + 4095) / 4096;
unsigned char vec[pages];
mincore(addr, len, vec);
/* vec[i] & 1 == 1 → страница i присутствует в RAM (не выгружена) */
```

Практическое применение: `vmtouch` (утилита управления page cache), warm-up маппинга перед RT-задачей, диагностика.

### 5.7 Malloc под капотом: glibc ptmalloc2

`malloc()` — не системный вызов, а функция libc поверх `brk()`/`mmap()`:

**Для малых аллокаций (< M_MMAP_THRESHOLD, обычно 128 KiB):**
- Ядро heap (sbrk/brk расширяет BSS вверх)
- Внутри — кэши свободных чанков по размеру («bins»). Порядок, в котором их смотрят при `malloc`:
  - **`tcache`** (thread-local cache, **glibc ≥ 2.26**, 2017) — **первый и самый быстрый** уровень: per-thread кэш на **64 размерных класса** (мелкие аллокации, на 64-bit примерно до 1 КиБ), до **7** чанков в каждом классе по умолчанию (tunable `glibc.malloc.tcache_count`). Берётся/возвращается **без блокировки арены** → почти весь поток мелких `malloc`/`free` обслуживается здесь, не доходя до fastbins. (Современный must-know: tcache и определяет производительность многопоточных мелких аллокаций, и лежит в основе многих heap-эксплойтов.)
  - **fastbins** (на 64-bit — чанки 16..128 байт, кратные 16; потолок `global_max_fast`) — следующий уровень, тоже без коалесинга, LIFO.
  - **smallbins**, **largebins** — для бóльших размеров (smallbins — точные размеры, largebins — диапазоны), с коалесингом соседей.
- Каждый поток имеет арену (arena) для уменьшения contention между потоками
- `free()` возвращает в bin, объединяет соседние свободные чанки (coalescing)
- В ОС память обычно **не возвращается** — heap может только расти через `brk()`

**Для больших аллокаций (≥ M_MMAP_THRESHOLD):**
- `mmap(MAP_ANONYMOUS|MAP_PRIVATE)` — каждая аллокация независимый маппинг
- `free()` вызывает `munmap()` — память **немедленно возвращается ОС**

**Именно поэтому `free()` не всегда уменьшает RSS:** маленькие аллокации возвращаются в heap ptmalloc2, а не ОС. Для возврата малых блоков нужен `malloc_trim()` или использовать `jemalloc`/`tcmalloc`.

```c
#include <malloc.h>
/* Настройка поведения: */
mallopt(M_MMAP_THRESHOLD, 64 * 1024);  /* изменить порог mmap */
mallopt(M_TRIM_THRESHOLD, 128 * 1024); /* порог возврата памяти ОС */
malloc_trim(0);                          /* попытаться вернуть свободную heap-память */
```

---

## 6. ELF формат

### 6.1 Что такое ELF и зачем его знать системному программисту

ELF (Executable and Linkable Format) — стандартный бинарный формат на Linux, FreeBSD, Solaris и большинстве UNIX-систем. Компилятор производит ELF, линкер потребляет ELF, загрузчик ядра интерпретирует ELF, отладчик читает ELF. Без понимания ELF нельзя:
- Разобраться в ошибках линкера
- Понять как работает динамическая линковка
- Написать собственный загрузчик (для embedded без ОС)
- Эффективно использовать отладчик
- Анализировать безопасность бинарников

### 6.2 Структура ELF файла

ELF состоит из трёх основных частей:

```
┌─────────────────────────────────┐ offset 0
│         ELF Header (64 байта)   │
│  Magic: \x7FELF                 │
│  Class: ELFCLASS32/64           │
│  Data: ELFDATA2LSB/2MSB         │
│  Type: ET_EXEC/ET_DYN/ET_REL    │
│  Machine: EM_X86_64 (62)        │
│  Entry point address            │
│  PHT offset, PHT entry count    │
│  SHT offset, SHT entry count    │
├─────────────────────────────────┤
│    Program Header Table (PHT)   │
│  Описывает сегменты для         │
│  загрузчика (ядра/ld.so)        │
├─────────────────────────────────┤
│         Содержимое файла        │
│  (.text, .rodata, .data, ...)   │
├─────────────────────────────────┤
│    Section Header Table (SHT)   │
│  Описывает секции для линкера   │
│  и отладчика                    │
└─────────────────────────────────┘
```

**Ключевое различие:**
- **Segments (сегменты)** = Program Headers = что загружать в память (runtime view)
- **Sections (секции)** = Section Headers = где что находится для линкера (link-time view)
- Stripped бинарник: SHT удалена, PHT остаётся — исполнение работает, отладка затруднена

### 6.3 Program Headers (сегменты)

| Тип | Назначение |
|-----|------------|
| `PT_LOAD` | Загрузить в память: rx-сегмент (код), rw-сегмент (данные) |
| `PT_DYNAMIC` | Таблица `.dynamic` для `ld.so` |
| `PT_INTERP` | Путь к динамическому линкеру (`/lib64/ld-linux-x86-64.so.2`) |
| `PT_PHDR` | Сам PHT (чтобы `ld.so` мог найти себя в памяти) |
| `PT_GNU_STACK` | Защита: `p_flags` без `PF_X` → NX (No-eXecute, запрет исполнения) на стеке — нельзя выполнить код, записанный в стек |
| `PT_GNU_RELRO` | Read-only after relocation: GOT, `.init_array` делаются RO после загрузки |
| `PT_TLS` | Thread-Local Storage template |
| `PT_NOTE` | Метаданные (build-id, ABI note) |

### 6.4 Section Headers (секции)

| Секция | Содержимое | Флаги |
|--------|-----------|-------|
| `.text` | Исполняемый код | `SHF_ALLOC + SHF_EXECINSTR` |
| `.rodata` | Строковые литералы, константы | `SHF_ALLOC` |
| `.data` | Инициализированные глобальные переменные | `SHF_ALLOC + SHF_WRITE` |
| `.bss` | Неинициализированные глобальные (в файле — нулевой размер) | `SHF_ALLOC + SHF_WRITE` |
| `.got` | Global Offset Table — адреса символов для PIC | `SHF_ALLOC + SHF_WRITE` |
| `.got.plt` | GOT-слоты для PLT (lazy binding) | `SHF_ALLOC + SHF_WRITE` |
| `.plt` | Procedure Linkage Table — заглушки для lazy binding | `SHF_ALLOC + SHF_EXECINSTR` |
| `.symtab` | Полная таблица символов (только если не stripped) | — |
| `.dynsym` | Экспортируемые/импортируемые символы (всегда) | `SHF_ALLOC` |
| `.dynstr` | Строки для `.dynsym` | `SHF_ALLOC` |
| `.rela.dyn` | Записи релокации для `.data`/`.got` | — |
| `.rela.plt` | Записи релокации для `.plt` | — |
| `.debug_info` | DWARF отладочная информация (типы, переменные) | — |
| `.debug_line` | DWARF: маппинг адрес → исходная строка | — |
| `.eh_frame` | Frame unwind информация (backtrace, C++ exceptions) | `SHF_ALLOC` |
| `.init_array` | Массив указателей на конструкторы (вызываются до `main()`) | `SHF_ALLOC + SHF_WRITE` |
| `.fini_array` | Массив указателей на деструкторы (вызываются после `main()`) | `SHF_ALLOC + SHF_WRITE` |
| `.note.gnu.build-id` | SHA1/MD5 хэш бинарника (для debuginfo поиска) | `SHF_ALLOC` |

**Почему `.bss` не занимает места в файле?** Ядро знает, что `.bss` — нулевые байты. При загрузке ELF ядро просто создаёт анонимный mapping нужного размера с нулями (zero-fill — это то, что даёт `mmap(MAP_ANONYMOUS)`). Писать нули в файл было бы бессмысленно.

### 6.5 Инструменты анализа ELF

```bash
# ELF заголовок
readelf -h /bin/ls
file /bin/ls        # быстрая сводка

# Секции (sections)
readelf -S /bin/ls
objdump -h /bin/ls
size /bin/ls        # размеры text/data/bss

# Сегменты (program headers)
readelf -l /bin/ls

# Символы
nm /lib/x86_64-linux-gnu/libc.so.6 | head -20
nm -D /bin/ls           # только dynamic (экспортированные)
nm --undefined-only /bin/ls  # undefined: что импортируется

# Дизассемблирование
objdump -d /bin/ls | less
objdump -d -S main.c    # с исходным кодом (нужна компиляция с -g)
# Современная альтернатива:
llvm-objdump -d --source /bin/ls

# Строки в бинарнике
strings -t x /bin/ls    # с шестнадцатеричным смещением

# Динамические зависимости
ldd /bin/ls
readelf -d /bin/ls | grep NEEDED

# Динамическая таблица
readelf -d /bin/ls

# DWARF отладочная информация
readelf --debug-dump=info main.elf | less

# Build ID
readelf -n /bin/ls | grep "Build ID"

# Релокации
readelf -r /bin/ls
```

### 6.6 Динамическая линковка: PLT/GOT и lazy binding

Когда ваш код вызывает `printf()`, компилятор не знает адрес `printf` во время компиляции (он в libc, загружаемой динамически). Используется механизм PLT/GOT:

**PLT (Procedure Linkage Table)** — секция `.plt`, содержит заглушки по одной на каждый импортируемый символ.

**GOT (Global Offset Table)** — секция `.got.plt`, содержит слоты (8 байт каждый) для реальных адресов символов.

**Lazy binding — пошагово:**

```
Первый вызов printf():
    call printf@plt           ; перейти на PLT-заглушку printf
    jmp *printf@got.plt       ; прыжок через GOT[printf] →
                              ;   GOT[printf] содержит адрес PLT+6 (следующей инструкции)
    push <индекс printf>      ; поместить индекс в стек
    jmp PLT[0]                ; прыжок на первый элемент PLT (resolver)
    ; PLT[0]: jmp *GOT[2]     ;   GOT[2] = адрес _dl_runtime_resolve()
    ; _dl_runtime_resolve():
    ;   найти printf в libc по SONAME (имя библиотеки из .dynamic, напр. libc.so.6) + индексу
    ;   записать реальный адрес printf в GOT[printf]
    ;   прыгнуть на printf в libc

Второй и последующие вызовы:
    call printf@plt
    jmp *printf@got.plt       ; GOT[printf] теперь = реальный printf в libc
                              ; → прямой прыжок без resolver
```

**RELRO (Relocation Read-Only)** — защита от GOT overwrite атак:
- `PARTIAL RELRO`: `.got` (не `.got.plt`) делается read-only после загрузки
- `FULL RELRO`: форсирует `LD_BIND_NOW` (все символы резолвятся при загрузке), затем всё GOT делается read-only

```bash
gcc -Wl,-z,relro,-z,now main.c -o main  # FULL RELRO
checksec --file=main                      # проверить защиты бинарника
```

### 6.7 Position Independent Code (PIC/PIE)

**PIC (Position Independent Code)** — код, который работает независимо от адреса загрузки. Обязателен для shared libraries (`.so`), так как они загружаются по разным адресам в разных процессах.

**PIE (Position Independent Executable)** — PIC для исполняемого файла. Обязателен для эффективного ASLR: без PIE `.text` сегмент загружается по фиксированному адресу (например `0x400000`), делая атаки предсказуемыми.

```bash
gcc -fPIC -shared lib.c -o libfoo.so           # shared library (всегда PIC)
gcc -fPIE -pie main.c -o main                  # PIE executable (ASLR для .text)
gcc -no-pie -fno-pie main.c -o main            # non-PIE (фиксированный адрес)

# Проверить:
readelf -h main | grep Type
# ET_DYN → PIE (динамически перемещаемый)
# ET_EXEC → non-PIE (фиксированный адрес)
```

**Как PIC обращается к глобальным данным?** Через GOT: компилятор генерирует `mov rax, [rip+offset_to_got_entry]` — относительное смещение к GOT постоянно независимо от адреса загрузки. `ld.so` при загрузке патчит GOT нужными адресами.

---

## 7. POSIX потоки (pthreads)

### 7.1 Модель потоков Linux

В Linux **нет принципиального различия** между процессом и потоком на уровне ядра. Оба представлены структурой `task_struct`. Потоки (threads) — это задачи (tasks), разделяющие адресное пространство и таблицу fd с другими задачами той же группы.

POSIX thread = Linux task с флагами `CLONE_VM | CLONE_FILES | CLONE_FS | CLONE_SIGHAND | CLONE_THREAD`.

Каждый поток имеет:
- Отдельный стек (по умолчанию 8 MiB, задаётся через `pthread_attr_setstacksize`)
- Свой TID (Thread ID, `gettid()`) — отличается от PID в многопоточном процессе
- Свою маску сигналов
- Свой набор `errno` (TLS-переменная в glibc)

### 7.2 Создание и завершение потока

```c
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>   /* strerror() */

/* Тип функции потока: принимает void*, возвращает void* */
void *worker(void *arg)
{
    int id = (int)(intptr_t)arg;
    printf("thread %d: tid=%lu\n", id, (unsigned long)pthread_self());
    /* Возвращаемое значение передаётся через pthread_join() */
    return (void *)(intptr_t)(id * id);
}

int main(void)
{
    const int N = 4;
    pthread_t tids[N];

    for (int i = 0; i < N; i++) {
        int rc = pthread_create(&tids[i], NULL, worker, (void *)(intptr_t)i);
        if (rc != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(rc));
            exit(1);
        }
    }

    for (int i = 0; i < N; i++) {
        void *retval;
        int rc = pthread_join(tids[i], &retval);
        if (rc != 0) { fprintf(stderr, "pthread_join: %s\n", strerror(rc)); }
        printf("thread %d result: %ld\n", i, (intptr_t)retval);
    }

    return 0;
}
```

Разбор вызовов:
- **`pthread_create(&tid, attr, start_routine, arg)`** — `&tid` (сюда запишется идентификатор нового потока, тип `pthread_t`), `attr` (атрибуты потока или `NULL` = по умолчанию, см. ниже), `start_routine` (функция `void *(*)(void *)` — тело потока), `arg` (единственный аргумент, передаваемый в неё). Поток стартует **сразу** и бежит параллельно.
- **`pthread_join(tid, &retval)`** — дождаться завершения потока `tid` и забрать его результат: `void*`, который функция-тело вернула (или передала в `pthread_exit`), кладётся в `retval`. Аналог `wait()` для потоков; без `join` (и без `DETACHED`) завершившийся поток оставляет «зомби»-ресурсы.

> **Важно: pthread-функции возвращают код ошибки НАПРЯМУЮ, а не через `errno`/`-1`.** В
> отличие от syscall-обёрток из §2 (`-1` + `errno`), почти все `pthread_*` возвращают **`0`
> при успехе** и **номер ошибки** (`EAGAIN`, `EINVAL`, `EDEADLK`, ...) как **возвращаемое
> значение**. Поэтому здесь `int rc = pthread_create(...); if (rc != 0) ... strerror(rc)` —
> а **не** `errno`. Глобальный `errno` они, как правило, не трогают. Путать эти два контракта
> — классическая ошибка. (Исключение — немногие функции вроде `pthread_setname_np`, но базовые
> `create`/`join`/`mutex_lock`/`cond_wait` — все по схеме «вернул код».)

**Атрибуты потока:**
```c
pthread_attr_t attr;
pthread_attr_init(&attr);

/* Размер стека (минимум PTHREAD_STACK_MIN): */
pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);  /* 2 MiB */

/* Detached state: нет join, ресурсы освобождаются автоматически */
pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

/* Scheduling policy (требует привилегий для SCHED_FIFO/RR): */
pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
struct sched_param sp = { .sched_priority = 50 };
pthread_attr_setschedparam(&attr, &sp);
pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

pthread_create(&tid, &attr, worker, arg);
pthread_attr_destroy(&attr);
```

**Завершение потока:**
- `return value;` из функции потока — нормальное завершение
- `pthread_exit(value)` — явное завершение из любого места
- `pthread_cancel(tid)` — запрос на отмену (поток должен иметь cancellation points)
- Если главный поток возвращается из `main()` → `exit()` → все потоки убиваются

### 7.3 Mutex — взаимное исключение

```c
#include <pthread.h>

/* Статическая инициализация (для глобальных/статических mutex): */
pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

/* Динамическая инициализация: */
pthread_mutex_t mu;
pthread_mutexattr_t attr;
pthread_mutexattr_init(&attr);
pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
pthread_mutex_init(&mu, &attr);
pthread_mutexattr_destroy(&attr);

/* Использование: */
pthread_mutex_lock(&mu);
/* --- критическая секция --- */
pthread_mutex_unlock(&mu);

/* Неблокирующий вариант: */
if (pthread_mutex_trylock(&mu) == 0) {
    /* mutex захвачен */
    pthread_mutex_unlock(&mu);
} /* else: EBUSY — уже захвачен */

/* С таймаутом (POSIX.1-2008): */
struct timespec deadline;
clock_gettime(CLOCK_REALTIME, &deadline);
deadline.tv_sec += 1;  /* ждать не более 1 секунды */
int rc = pthread_mutex_timedlock(&mu, &deadline);
/* rc == ETIMEDOUT → таймаут */

pthread_mutex_destroy(&mu);
```

> **Это функции libc, а не syscalls — и в этом их сила.** `pthread_mutex_*`,
> `pthread_cond_*`, `pthread_rwlock_*` реализованы в glibc/NPTL поверх **одного**
> системного вызова — `futex(2)` (fast userspace mutex). Ключевое: в **неконкурентном**
> случае (лок свободен) `pthread_mutex_lock` — это просто атомарный CAS **в userspace**,
> **без** перехода в ядро; `futex(2)` зовётся **только при контеншне** — когда поток
> надо реально усыпить или разбудить. Поэтому «горячий» неоспариваемый лок почти
> бесплатен, а в `strace` ты увидишь `futex` лишь под нагрузкой. (Так же `pthread_create`
> — обёртка над `clone(2)`, §8.) Сам `futex` напрямую почти никогда не вызывают вручную:
> это «ассемблер» синхронизации, на котором стоят все высокоуровневые примитивы.

**Типы mutex:**

| Тип | Поведение при повторном lock() в том же потоке |
|-----|----------------------------------------------|
| `PTHREAD_MUTEX_NORMAL` | Deadlock (поток ждёт сам себя вечно) |
| `PTHREAD_MUTEX_ERRORCHECK` | Возвращает `EDEADLK` |
| `PTHREAD_MUTEX_RECURSIVE` | Успех; внутренний счётчик; нужно столько же unlock() |
| `PTHREAD_MUTEX_DEFAULT` | Implementation-defined (обычно как NORMAL) |

**Правила избежания deadlock:**
1. Всегда захватывать несколько mutex в одном и том же порядке во всём коде
2. Удерживать mutex как можно меньше времени — только во время доступа к данным
3. Никогда не вызывать функции, которые захватывают тот же mutex, внутри критической секции (reentrancy)

### 7.4 Condition Variable — ожидание условия

Condition variable позволяет потоку ждать выполнения условия, атомарно освобождая mutex на время ожидания.

```c
#include <pthread.h>

pthread_mutex_t mu   = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  cond = PTHREAD_COND_INITIALIZER;

/* Общие данные: */
#define MAX_QUEUE 100
int queue[MAX_QUEUE];
int head = 0, tail = 0;   /* позиции FIFO-кольца: пишем в tail, читаем из head */
int queue_size = 0;        /* число элементов в очереди */

/* ПРОИЗВОДИТЕЛЬ (producer): */
void *producer(void *arg)
{
    (void)arg;
    for (int i = 0; i < 1000; i++) {
        pthread_mutex_lock(&mu);

        while (queue_size >= MAX_QUEUE)  /* ждём пока есть место */
            pthread_cond_wait(&cond, &mu);

        queue[tail] = i;                    /* положить в ХВОСТ (FIFO) */
        tail = (tail + 1) % MAX_QUEUE;
        queue_size++;
        pthread_cond_signal(&cond);  /* разбудить одного потребителя */

        pthread_mutex_unlock(&mu);
    }
    return NULL;
}

/* ПОТРЕБИТЕЛЬ (consumer): */
void *consumer(void *arg)
{
    (void)arg;
    while (1) {
        pthread_mutex_lock(&mu);

        while (queue_size == 0)  /* ВСЕГДА while, не if */
            pthread_cond_wait(&cond, &mu);
        /*
         * pthread_cond_wait():
         *   1. Атомарно освобождает mutex
         *   2. Блокируется на condvar
         *   3. При probуждении: атомарно захватывает mutex обратно
         *   4. Возвращает с удержанным mutex
         *
         * while вместо if — защита от:
         *   - spurious wakeup (POSIX разрешает пробуждение без signal)
         *   - stolen wakeup (другой поток забрал элемент до нас)
         */

        int val = queue[head];              /* взять из ГОЛОВЫ (FIFO, не LIFO!) */
        head = (head + 1) % MAX_QUEUE;
        queue_size--;
        pthread_cond_signal(&cond);  /* разбудить производителя если он ждёт */

        pthread_mutex_unlock(&mu);
        printf("consumed: %d\n", val);
    }
    return NULL;
}
```

> **Важная оговорка к примеру выше.** Здесь и producer, и consumer ждут на **одном** condvar, но разных условий («есть место» / «есть элемент»), и используют `pthread_cond_signal`. Это корректно **только для схемы один producer + один consumer**. При нескольких producer'ах и/или consumer'ах `signal` может разбудить «не того» (например, второго producer'а вместо ожидающего consumer'а) → потерянное пробуждение → deadlock. Общий случай решается **двумя** отдельными condvar (`not_full` и `not_empty`) — как в задаче про пул потоков ниже (§МЕХАНИЗМЫ-2) — либо заменой `signal` на `broadcast`.

**`pthread_cond_signal()` vs `pthread_cond_broadcast()`:**
- `signal`: разбудить **один** ждущий поток (какой именно — не определено)
- `broadcast`: разбудить **все** ждущие потоки

Правило: если разные потоки ждут разных условий на одном condvar → использовать `broadcast`. Если условие одинаково для всех и одна операция удовлетворяет только одного → `signal`.

### 7.5 rwlock — читатели-писатели

```c
pthread_rwlock_t rwl = PTHREAD_RWLOCK_INITIALIZER;

/* Несколько читателей одновременно: */
pthread_rwlock_rdlock(&rwl);
/* читаем данные */
pthread_rwlock_unlock(&rwl);

/* Один писатель — исключает всех: */
pthread_rwlock_wrlock(&rwl);
/* изменяем данные */
pthread_rwlock_unlock(&rwl);
```

Внимание: rwlock может страдать от writer starvation (поток-писатель ждёт бесконечно пока есть читатели). Glibc-реализация имеет preference-флаги, но поведение зависит от реализации.

### 7.6 Thread-Local Storage (TLS)

```c
/* GNU расширение — компилируется в TLS прямо в бинарник, быстро: */
__thread int per_thread_counter = 0;

/* C11 стандарт — то же самое: */
_Thread_local int per_thread_counter = 0;

/* POSIX pthread_key: динамические TLS ключи */
pthread_key_t key;

void destructor(void *val)
{
    free(val);  /* вызывается при завершении потока если val != NULL */
}

/* Создать ключ один раз (через pthread_once): */
pthread_key_create(&key, destructor);

/* В каждом потоке: */
void *data = pthread_getspecific(key);
if (data == NULL) {
    data = malloc(sizeof(MyState));
    pthread_setspecific(key, data);
}
```

`errno` в glibc — это именно `__thread int errno` (TLS-переменная). Без TLS не было бы MT-безопасного errno.

### 7.7 Атомарные операции C11

```c
#include <stdatomic.h>

/* Типы: atomic_int, atomic_long, atomic_size_t, atomic_uintptr_t, ... */
atomic_int counter = 0;   /* ATOMIC_VAR_INIT устарел в C17 и удалён в C23 —
                             современные компиляторы (gnu23 по умолчанию) его не знают;
                             просто инициализируй значением */

/* Чтение и запись: */
int val = atomic_load(&counter);
atomic_store(&counter, 42);

/* Атомарные RMW (read-modify-write): */
int old = atomic_fetch_add(&counter, 1);   /* counter++, возвращает старое */
int old = atomic_fetch_sub(&counter, 1);   /* counter-- */
int old = atomic_fetch_or(&flags, 0x01);   /* flags |= 0x01 */
int old = atomic_fetch_and(&flags, ~0x01); /* flags &= ~0x01 */

/* CAS (Compare-And-Swap) — основа lock-free структур: */
int expected = 5;
bool ok = atomic_compare_exchange_strong(&counter, &expected, 10);
/*
 * Если counter == expected (5):
 *   counter = 10, expected не изменяется → return true
 * Иначе:
 *   counter не изменяется, expected = текущее значение counter → return false
 *
 * _weak версия может ложно вернуть false (spurious failure) — использовать в цикле:
 */
do {
    expected = atomic_load(&counter);
} while (!atomic_compare_exchange_weak(&counter, &expected, expected + 1));
```

### 7.8 Memory ordering — порядок видимости

По умолчанию все C11 atomic операции используют `memory_order_seq_cst` (sequentially consistent) — самый строгий, самый медленный. На x86-64 разница минимальна (модель памяти **TSO** — Total Store Order: процессор почти не переупорядочивает обращения к памяти, кроме «store раньше последующего load»), на ARM/POWER (слабые модели, переупорядочивают агрессивно) — существенна.

```c
/* Явная передача данных: acquire/release семантика */
atomic_int flag = 0;
int data = 0;

/* Поток 1 (публикатор): */
data = 42;                                           /* запись до флага */
atomic_store_explicit(&flag, 1, memory_order_release);
/*
 * release: все предыдущие записи (data=42) видны
 * потоку, который сделает acquire на этом же атомике
 */

/* Поток 2 (потребитель): */
while (atomic_load_explicit(&flag, memory_order_acquire) == 0)
    ;  /* спин */
/*
 * acquire: после чтения флага гарантировано
 * видим все записи до release в потоке 1
 */
printf("%d\n", data);  /* гарантированно 42 */
```

**Когда что использовать:**

| Ordering | Использование |
|----------|---------------|
| `relaxed` | Статистика, счётчики без синхронизации; порядок не важен |
| `acquire` | Чтение флага/указателя: "заберу данные, которые кто-то опубликовал" |
| `release` | Публикация: "данные готовы, устанавливаю флаг" |
| `acq_rel` | RMW операции в обеих ролях (CAS в lock-free структурах) |
| `seq_cst` | Дефолт; когда нужен глобальный порядок между несколькими атомиками |

### 7.9 Типичные ошибки многопоточности

**Data race — гонка данных:**
```c
/* ОШИБКА: два потока без синхронизации */
int counter = 0;
void *thread_fn(void *arg) { counter++; return NULL; }  /* UB! */

/*
 * counter++ = load + add + store (три инструкции)
 * Без атомики или mutex — не атомарно
 * C11: data race → undefined behavior
 * Компилятор может: переупорядочить, кэшировать в регистре, оптимизировать
 */
```

**Deadlock:**
```c
/* ОШИБКА: A ждёт B, B ждёт A */
void transfer(Account *from, Account *to, int amount)
{
    pthread_mutex_lock(&from->mu);   /* поток 1 захватывает A */
    pthread_mutex_lock(&to->mu);     /* поток 1 ждёт B; поток 2 уже держит B, ждёт A */
    from->balance -= amount;
    to->balance += amount;
    pthread_mutex_unlock(&to->mu);
    pthread_mutex_unlock(&from->mu);
}

/* РЕШЕНИЕ: всегда в одном порядке (по адресу или ID): */
void transfer_safe(Account *from, Account *to, int amount)
{
    Account *first  = (from < to) ? from : to;
    Account *second = (from < to) ? to   : from;
    pthread_mutex_lock(&first->mu);
    pthread_mutex_lock(&second->mu);
    from->balance -= amount;
    to->balance += amount;
    pthread_mutex_unlock(&second->mu);
    pthread_mutex_unlock(&first->mu);
}
```

**Priority Inversion:**
Высокоприоритетный поток H ждёт mutex, удерживаемый низкоприоритетным потоком L. Если средний поток M вытесняет L (и тем самым мешает освободить mutex) — H фактически заблокирован потоком M через L. Решение: Priority Inheritance Mutex (`PTHREAD_PRIO_INHERIT`).

**False Sharing — ложное разделение:**
```c
/* ОШИБКА: два потока на разных полях, но в одном cache line */
struct {
    atomic_int counter_a;   /* 4 байта */
    atomic_int counter_b;   /* 4 байта */
    /* оба в одном cache line (64 байта) */
} stats;

/* Поток 1: stats.counter_a++
   Поток 2: stats.counter_b++
   → cache line invalidation между ядрами → драматическое замедление */

/* РЕШЕНИЕ: выравнивание по cache line: */
struct {
    atomic_int counter_a;
    char _pad_a[64 - sizeof(atomic_int)];
    atomic_int counter_b;
    char _pad_b[64 - sizeof(atomic_int)];
} stats;
/* или: */
alignas(64) atomic_int counter_a;
alignas(64) atomic_int counter_b;
```

**Spurious wakeup:**
```c
/* ОШИБКА: */
pthread_mutex_lock(&mu);
if (queue_empty)                         /* if — ошибка! */
    pthread_cond_wait(&cond, &mu);
process(dequeue());

/* ПРАВИЛЬНО: */
pthread_mutex_lock(&mu);
while (queue_empty)                      /* while — всегда */
    pthread_cond_wait(&cond, &mu);
process(dequeue());
```

POSIX явно разрешает `pthread_cond_wait()` вернуться без `signal()`/`broadcast()` — spurious wakeup. Причина: некоторые архитектуры делают реализацию проще если допускать spurious wakeup. Код обязан проверять условие в цикле.

---

## 8. clone() — под капотом fork и pthread

### 8.1 Системный вызов clone()

`clone()` — Linux-специфичный системный вызов, лежащий в основе как `fork()`, так и `pthread_create()`. Ядро не делает различий — и то, и другое создаёт новый `task_struct`.

```c
#define _GNU_SOURCE
#include <sched.h>

/*
 * Сигнатура для userspace; реальный syscall отличается
 * (ядро принимает аргументы иначе)
 */
int clone(int (*fn)(void *arg), void *child_stack,
          int flags, void *arg,
          pid_t *parent_tidptr,
          void *tls,
          pid_t *child_tidptr);
```

Флаги `clone()` определяют, что разделяется между родителем и потомком:

| Флаг | Что разделяется |
|------|-----------------|
| `CLONE_VM` | Виртуальная память (страницы разделяются, не копируются) |
| `CLONE_FS` | Файловая система: cwd, root, umask |
| `CLONE_FILES` | Таблица файловых дескрипторов |
| `CLONE_SIGHAND` | Таблица обработчиков сигналов |
| `CLONE_THREAD` | Группа потоков: TGID = PID родителя; kill(PID) убивает группу |
| `CLONE_SYSVSEM` | SysV семафоры (undo-списки) |
| `CLONE_SETTLS` | Установить новый TLS (fs:base регистр) |
| `CLONE_PARENT_SETTID` | Записать TID в адрес родителя |
| `CLONE_CHILD_CLEARTID` | Очистить TID в адресе потомка при завершении (futex wake) |
| `CLONE_NEWPID` | Новое PID-пространство имён |
| `CLONE_NEWNET` | Новое сетевое пространство имён |
| `CLONE_NEWNS` | Новое mount-пространство имён |
| `CLONE_NEWUTS` | Новые hostname/domainname |
| `CLONE_NEWUSER` | Новое пространство имён UID/GID |
| `CLONE_NEWCGROUP` | Новое cgroup-пространство имён |

**fork() = clone(SIGCHLD)** — без `CLONE_VM`, `CLONE_THREAD`, `CLONE_FILES`, ...; создаёт независимый процесс с COW-копией адресного пространства.

**pthread_create() ≈ clone(CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM | CLONE_SETTLS | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID)**

### 8.2 clone3() — современный интерфейс

Linux 5.3+ добавил `clone3()` с расширяемой структурой аргументов:

```c
#include <linux/sched.h>
#include <sys/syscall.h>

struct clone_args args = {
    .flags       = CLONE_NEWPID | CLONE_NEWNET | CLONE_PIDFD,
    .exit_signal = SIGCHLD,
    .pidfd       = (uint64_t)(uintptr_t)&pidfd,  /* сюда ядро запишет pidfd */
};
pid_t child = (pid_t)syscall(SYS_clone3, &args, sizeof(args));
```

`pidfd` — файловый дескриптор, привязанный к процессу (не PID, который может быть повторно использован). Позволяет `poll()` на завершение процесса, `pidfd_send_signal()` без race по PID.

---

## 9. Пространства имён (namespaces) — основа контейнеров

### 9.1 Что такое namespace

Linux namespace — механизм изоляции, позволяющий группе процессов видеть отличный от глобального вид ресурсов. Ключевые типы:

| Namespace | Что изолирует | CLONE_* флаг |
|-----------|--------------|--------------|
| PID | Дерево процессов; внутри — своя нумерация с PID 1 | `CLONE_NEWPID` |
| Net | Сетевые интерфейсы, маршруты, iptables, сокеты | `CLONE_NEWNET` |
| Mount | Точки монтирования; `pivot_root` для корня контейнера | `CLONE_NEWNS` |
| UTS | `hostname` и `domainname` (Unix Timesharing System) | `CLONE_NEWUTS` |
| User | Маппинг UID/GID: root внутри контейнера = обычный пользователь снаружи | `CLONE_NEWUSER` |
| IPC | SysV IPC, POSIX очереди сообщений | `CLONE_NEWIPC` |
| Cgroup | Иерархия cgroup | `CLONE_NEWCGROUP` |
| Time | Монотонные часы (Linux 5.6+) | `CLONE_NEWTIME` |

### 9.2 Docker = namespaces + cgroups + seccomp

```
Docker container:
  CLONE_NEWPID   → ps видит только свои процессы
  CLONE_NEWNET   → свой eth0, lo, iptables
  CLONE_NEWNS    → pivot_root на образ контейнера; overlay ФС
  CLONE_NEWUTS   → свой hostname
  CLONE_NEWUSER  → root внутри = uid=1000 снаружи (rootless)
  CLONE_NEWIPC   → изолированные очереди сообщений
  cgroups        → лимиты CPU, RAM, блочного I/O
  seccomp-bpf    → whitelist разрешённых syscalls
```

### 9.3 Практика с unshare

```bash
# Создать изолированное окружение без root (user namespace + PID)
unshare --pid --fork --mount-proc --user --map-root-user bash

# Теперь внутри:
ps aux          # видит только свои процессы
id              # uid=0(root) — но это маппинг!
hostname        # тот же что снаружи (нет --uts)

# PID namespace: первый процесс получает PID 1
echo $$         # 1

# Подключиться к namespace существующего контейнера (nsenter):
nsenter --pid=/proc/$(docker inspect -f '{{.State.Pid}}' mycontainer)/ns/pid bash
```

### 9.4 /proc/PID/ns/ — файлы namespace

```bash
ls -la /proc/self/ns/
# lrwxrwxrwx ... cgroup -> cgroup:[4026531835]
# lrwxrwxrwx ... ipc -> ipc:[4026531839]
# lrwxrwxrwx ... mnt -> mnt:[4026531840]
# lrwxrwxrwx ... net -> net:[4026531992]
# lrwxrwxrwx ... pid -> pid:[4026531836]
# lrwxrwxrwx ... user -> user:[4026531837]
# lrwxrwxrwx ... uts -> uts:[4026531838]
```

Число в квадратных скобках — inode namespace. Если у двух процессов один inode → они в одном namespace.

```c
/* setns() — присоединиться к чужому namespace */
int nsfd = open("/proc/12345/ns/net", O_RDONLY);
setns(nsfd, CLONE_NEWNET);
close(nsfd);
/* теперь этот процесс работает в сетевом namespace PID 12345 */
```

---

## 10. Практика и самопроверка

### 10.1 Практические задания

**Задание 1: fork-бомба с контролем**
Написать функцию `fork_n(int n)`, которая создаёт ровно N дочерних процессов (не рекурсивно), каждый спит `i*10ms` и завершается с кодом `i+1`. Родитель собирает статусы и печатает в порядке завершения.

**Задание 2: Анализ ELF**
```bash
gcc -O2 -fPIE -pie -o prog prog.c
readelf -l prog       # найти PT_INTERP, PT_GNU_STACK
nm -D prog            # что экспортируется?
readelf -r prog       # релокации
objdump -d prog | grep -A5 '<main>'
```
Ответить: какой interpreter, есть ли исполняемый стек, сколько PLT-заглушек.

**Задание 3: Thread sanitizer**
```bash
gcc -fsanitize=thread -O1 -g racy.c -o racy -lpthread
./racy
# ThreadSanitizer покажет точный race: файл, строку, потоки
```

**Задание 4: /proc анализ**
```c
/* Написать функцию, парсящую /proc/self/maps и возвращающую
   суммарный RSS через /proc/self/smaps.
   Сравнить с значением из /proc/self/status (VmRSS). */
```

### 10.2 Вопросы для самопроверки

<details>
<summary><b>1. Что копируется при fork() и что разделяется? Что такое COW?</b></summary>

**Копируется** (точнее — создаётся COW-копия): виртуальное адресное пространство, таблица страниц (отмечается read-only), таблица fd (но указывает на те же open file descriptions!), signal dispositions, маска сигналов, cwd, umask.

**Разделяется** (общие ресурсы): физические страницы памяти (до момента записи), open file descriptions (включая смещение в файле), inode файлов.

**COW (Copy-on-Write)**: при записи в страницу происходит page fault, ядро создаёт приватную копию страницы для нарушителя. До записи — оба процесса физически читают одно и то же.
</details>

<details>
<summary><b>2. Почему нельзя вызывать большинство функций libc в обработчике SIGCHLD?</b></summary>

Обработчик сигнала прерывает основной поток в **произвольной точке**. Если в этот момент выполнялась non-async-signal-safe функция (например, `malloc`, `printf`, `strerror`), и обработчик вызывает ту же функцию — возможен **deadlock** (если функция использует внутренний mutex) или **heap corruption** (если функция работала с внутренними структурами).

Безопасны только функции из `man 7 signal-safety` (async-signal-safe): `write()`, `waitpid()`, `errno` присваивание, `_exit()`, `read()` и ещё несколько десятков.

В обработчике SIGCHLD: `waitpid(-1, NULL, WNOHANG)` — безопасно. Сохранить/восстановить `errno` — обязательно.
</details>

<details>
<summary><b>3. Зачем нужен double fork для daemon?</b></summary>

После `fork()` + `setsid()` мы становимся **лидером новой сессии**. Лидер сессии может получить управляющий терминал при первом `open()` на tty-устройство (если ОС поддерживает это поведение — BSD-семантика). Второй `fork()` создаёт потомка, который **не является** лидером сессии (у него есть SID, но PID≠SID), и поэтому никогда не получит управляющий терминал случайно.

Также: первый `fork()` позволяет родителю завершиться, освобождая шелл; второй гарантирует что shell не будет ждать сигнал от daemon (т.к. первый `fork()` уже вернул управление шеллу).
</details>

<details>
<summary><b>4. Что такое zombie процесс и как его избежать?</b></summary>

**Zombie** — завершившийся процесс, чья запись в таблице процессов ещё не считана родителем через `wait()`. Ядро обязано сохранить PID и статус завершения до момента, когда родитель его заберёт.

**Избежать:**
1. Вызывать `waitpid(-1, NULL, WNOHANG)` периодически или в SIGCHLD handler
2. Установить обработчик SIGCHLD с `waitpid(-1, NULL, WNOHANG)` в цикле
3. `signal(SIGCHLD, SIG_IGN)` — ядро автоматически пожинает завершившихся потомков
4. Double fork: потомок немедленно форкает внука и завершается → внук усыновляется init

Zombie не потребляет CPU/RAM, но держит PID. Тысячи zombie → исчерпание PID namespace.
</details>

<details>
<summary><b>5. Почему malloc не всегда возвращает память ОС при вызове free()?</b></summary>

glibc `malloc` использует два механизма:
1. **brk/sbrk** для малых аллокаций (< MMAP_THRESHOLD ≈ 128KB): расширяет heap вверх. `free()` возвращает чанк в bin ptmalloc2 — heap **не уменьшается**. ОС не видит возврата памяти.
2. **mmap(MAP_ANONYMOUS)** для больших аллокаций: `free()` вызывает `munmap()` — память **сразу возвращается ОС**.

Причина: `brk()` — единая граница heap, нельзя вернуть ОС фрагмент из середины. Можно только уменьшить с конца (если конец свободен), что делает `malloc_trim()`. Альтернатива — `jemalloc`/`tcmalloc` с более агрессивным возвратом.
</details>

<details>
<summary><b>6. Что такое PLT и GOT? Как работает lazy binding?</b></summary>

**GOT (Global Offset Table)** — таблица в `.got.plt`, содержащая 8-байтные слоты для реальных адресов импортируемых символов. При загрузке — заполнена адресами PLT stub'ов.

**PLT (Procedure Linkage Table)** — секция `.plt` с заглушками: каждый stub делает `jmp *GOT[symbol]`. Если GOT не заполнен — прыгает на resolver (`_dl_runtime_resolve`).

**Lazy binding**: при первом вызове `foo@plt` → GOT указывает обратно на PLT+6 → передаёт управление `_dl_runtime_resolve` → находит реальный адрес → пишет в GOT → прыгает туда. Второй вызов: PLT → GOT → реальная функция напрямую.

`LD_BIND_NOW=1` или `FULL RELRO` — резолвирует все символы при загрузке, GOT делается RO.
</details>

<details>
<summary><b>7. Почему condition variable ВСЕГДА проверяется в while, а не в if?</b></summary>

Две причины:
1. **Spurious wakeup**: POSIX явно разрешает `pthread_cond_wait()` вернуться без сигнала. Стандарт не объясняет почему — это упрощает реализацию на некоторых архитектурах (futex).
2. **Stolen wakeup**: между моментом пробуждения и захватом mutex другой поток уже мог изменить условие (например, забрать элемент из очереди). `while` перепроверяет — если условие всё ещё не выполнено, ждём снова.

Паттерн: `while (!condition) pthread_cond_wait(&cv, &mu);`
</details>

<details>
<summary><b>8. Что такое false sharing и как его избежать?</b></summary>

**False sharing** — ситуация когда два потока записывают в **разные** переменные, которые находятся в **одном cache line** (типично 64 байта на x86). Аппаратная когерентность кэша (MESI-протокол) вынуждена инвалидировать весь cache line при любой записи — даже в несмежные байты.

Результат: потоки "мешают" друг другу без логической гонки данных. Производительность падает в 10–50× по сравнению с отсутствием sharing.

**Решение:**
```c
alignas(64) atomic_int counter_a;  /* в своём cache line */
alignas(64) atomic_int counter_b;  /* в своём cache line */
```
Или структура с padding до 64 байт. Диагностика: `perf stat -e cache-misses`, `valgrind --tool=cachegrind`.
</details>

<details>
<summary><b>9. Чем clone() отличается от fork() на уровне ядра?</b></summary>

На уровне ядра **нет отличия** — `fork()` реализован как `clone(SIGCHLD)`. Оба создают новый `task_struct` через `copy_process()`. Разница в флагах:

- `fork()`: без `CLONE_VM` → COW-копия адресного пространства; без `CLONE_FILES` → копия таблицы fd; без `CLONE_THREAD` → новая group (PID≠TGID)
- `pthread_create()`: с `CLONE_VM | CLONE_FILES | CLONE_THREAD | ...` → всё разделяется; TGID = PID родителя

Планировщик Linux **не знает** "потоки" и "процессы" — только задачи (`task_struct`). Разница лишь в том, что разделяется между задачами.
</details>

<details>
<summary><b>10. Как работает ASLR и почему PIE обязателен для его эффективности?</b></summary>

**ASLR (Address Space Layout Randomization)**: при каждом запуске ядро рандомизирует базовые адреса: stack (несколько бит), mmap-регион (heap и библиотеки). Цель — сделать невозможным hardcoded адреса в exploit-коде.

**Проблема без PIE**: для non-PIE (ET_EXEC) `.text` и `.data` сегменты загружаются по **фиксированному адресу** (например `0x400000`). Только stack и mmap рандомизируются. Атакующий знает адрес любой функции/гаджета ROP в `.text`.

**С PIE (ET_DYN)**: весь бинарник — позиционно-независимый. Ядро выбирает случайный базовый адрес для загрузки. Теперь рандомизируется и `.text` бинарника. Комбинация PIE + ASLR + RELRO + NX стек — минимальный набор защит для production-кода.

Проверить: `cat /proc/sys/kernel/randomize_va_space` (должен быть 2).
</details>

---

## 11. Банк вопросов

### БАЗА (8 MCQ)

1. Что вернёт `fork()` **дочернему** процессу при успехе?
   - A) PID родительского процесса
   - B) PID дочернего процесса
   - C) 0
   - D) -1
   - **Ответ: C)** — в дочернем всегда 0; PID потомка возвращается только родителю.

2. Что такое zombie-процесс?
   - A) Процесс с 100% CPU без видимой причины
   - B) Процесс, потерявший родителя (усыновлён init)
   - C) Завершившийся процесс, чья запись в таблице не считана родителем через wait()
   - D) Процесс, заблокированный в D-состоянии навсегда
   - **Ответ: C)**

3. Какой системный вызов лежит в основе `pthread_create()`?
   - A) `fork()`
   - B) `vfork()`
   - C) `clone()`
   - D) `spawn()`
   - **Ответ: C)** — с флагами CLONE_VM|CLONE_THREAD|...

4. Что такое ASLR?
   - A) Механизм lazy binding для shared libraries
   - B) Рандомизация адресов стека, heap и mmap при каждом запуске
   - C) Аппаратный механизм защиты страниц (NX bit)
   - D) Алгоритм планировщика Linux
   - **Ответ: B)**

5. Для чего нужен флаг `FD_CLOEXEC`?
   - A) Закрыть fd при вызове close()
   - B) Автоматически закрыть fd при execve(), предотвратить наследование
   - C) Предотвратить dup() этого fd
   - D) Запретить fork() пока fd открыт
   - **Ответ: B)**

6. Что такое `.bss` секция в ELF?
   - A) Код запуска (startup code)
   - B) Инициализированные глобальные переменные
   - C) Неинициализированные глобальные переменные — в файле нулевого размера, нули генерирует загрузчик
   - D) Таблица символов для отладчика
   - **Ответ: C)**

7. Зачем нужен `__thread` / `_Thread_local`?
   - A) Сделать переменную видимой только в текущей единице трансляции
   - B) Дать каждому потоку свою копию переменной (Thread-Local Storage)
   - C) Защитить переменную от одновременного доступа (аналог mutex)
   - D) Разместить переменную в read-only секции
   - **Ответ: B)**

8. Что делает `pthread_cond_broadcast()` в отличие от `pthread_cond_signal()`?
   - A) Гарантирует пробуждение конкретного потока по TID
   - B) Пробуждает только один поток с наивысшим приоритетом
   - C) Пробуждает ВСЕ потоки, ожидающие на данной condition variable
   - D) Отменяет ожидание без пробуждения (возвращает ECANCELED)
   - **Ответ: C)**

---

### МЕХАНИЗМЫ (8 self_grade)

1. **Объясни полный жизненный цикл дочернего процесса от `fork()` до `waitpid()`**. Что происходит с ресурсами на каждом этапе? Когда именно освобождается PID?

   *Подсказка*: fork → COW → выполнение → exit/_exit → zombie → waitpid → освобождение PID.

   *Эталон*: `fork()`: создаёт COW-копию адресного пространства (страницы shared, RO), копирует таблицу fd (ссылки на те же OFD), копирует signal dispositions. Потомок выполняется. `exit()` / `_exit()`: освобождает память (munmap всех VMA), закрывает fd (если rc_fd=0), отправляет SIGCHLD родителю, переходит в Z-состояние. PID и статус завершения сохраняются в `task_struct` (зомби). `waitpid()`: родитель читает статус из `task_struct` зомби, после чего ядро окончательно освобождает `task_struct` и PID возвращается в пул.

2. **Как реализовать пул потоков** с очередью задач, используя mutex + condvar? Опиши структуры данных и логику worker/submit.

   *Подсказка*: кольцевой буфер задач + mutex + condvar "есть задача" + condvar "есть место" + флаг shutdown.

   *Эталон*: Структура: `task_queue` (массив функций/аргументов, head/tail/count), `pthread_mutex_t mu`, `pthread_cond_t not_empty` (для воркеров), `pthread_cond_t not_full` (для submit), `int shutdown`. `worker()`: `lock(mu); while(!shutdown && empty) wait(not_empty, mu); если shutdown && empty → unlock, return; взять задачу, signal(not_full); unlock; выполнить задачу`. `submit(task)`: `lock; while(full && !shutdown) wait(not_full, mu); добавить; signal(not_empty); unlock`. Завершение: `lock; shutdown=1; broadcast(not_empty); unlock; for each worker: join`.

3. **Объясни PLT/GOT механизм lazy binding шаг за шагом**. Что происходит при первом и втором вызове `printf()`? Как RELRO защищает от атак?

   *Подсказка*: GOT изначально содержит адрес PLT+6; resolver патчит GOT; RELRO делает GOT read-only.

   *Эталон*: В `.plt`: `printf@plt: jmp *printf@got.plt`. Изначально `GOT[printf]` = адрес `PLT+6` (следующей инструкции в stub). Первый вызов: PLT → GOT → PLT+6 → `push index → jmp PLT[0] → _dl_runtime_resolve() → находит printf в libc → записывает адрес в GOT → прыгает на printf`. Второй вызов: PLT → GOT → реальный printf. RELRO: PARTIAL RELRO — `.got` (не `.got.plt`) становится RO; FULL RELRO = `LD_BIND_NOW` (все GOT заполняются при загрузке) + весь GOT становится RO. Атаки GOT-overwrite: перезаписать `GOT[free]` адресом `system`, затем `free("/bin/sh")` → shell. FULL RELRO делает GOT read-only → write → SIGSEGV.

4. **Как прочитать карту памяти процесса через `/proc` и что означает каждое поле?** Напиши код, парсящий `/proc/self/maps` и выводящий суммарный RSS.

   *Подсказка*: `/proc/self/maps` — одна VMA в строке; RSS из `/proc/self/smaps`.

   *Эталон*: `/proc/self/maps` формат: `start-end perms offset dev ino name`. `perms`: r/w/x/- и p(rivate)/s(hared). Для RSS нужен `/proc/self/smaps` — там поле `Rss:` для каждой VMA. Парсинг: `FILE *f = fopen("/proc/self/smaps", "r")`; читать построчно; если строка начинается с "Rss:", парсить число и суммировать. Альтернатива: `/proc/self/status` поле `VmRSS` — суммарный RSS без разбивки по VMA.

5. **Почему data race — это UB в C11?** Что может сделать компилятор с кодом, имеющим гонку? Покажи конкретный пример оптимизации, ломающей racy код.

   *Подсказка*: компилятор предполагает single-threaded семантику; может кэшировать в регистре; может переупорядочить.

   *Эталон*: C11 §5.1.2.4: data race — UB. Компилятор предполагает что любой non-atomic, non-volatile доступ видим только текущему потоку. Пример: `while (!stop) { work(); }` — компилятор читает `stop` один раз в регистр, если не видит другого writer'а в этом потоке → бесконечный цикл даже если другой поток установил `stop=1`. Другой пример: `x = 1; y = 2;` — компилятор может переупорядочить; без memory barrier другой поток может увидеть `y=2` до `x=1`. Решение: `atomic_store_explicit(&stop, 1, memory_order_relaxed)` или `volatile sig_atomic_t stop` (только для signal handler).

6. **Как безопасно передать данные из родительского в дочерний через fork?** Какие ловушки при передаче указателей? Как передать данные в другом направлении (от потомка к родителю)?

   *Подсказка*: данные в heap доступны обоим (COW); указатели валидны сразу после fork; для обратной передачи — pipe или shared memory.

   *Эталон*: Сразу после `fork()` дочерний видит всё адресное пространство родителя (COW-копию). Указатели на heap валидны. Ловушки: (1) если родитель после fork модифицирует данные — COW создаст приватную копию, дочерний не увидит изменений; (2) если в данных есть fd — оба процесса имеют копии fd (разделяют OFD!); (3) mutex в shared данных: если родитель держал mutex в момент fork → в дочернем mutex навсегда locked (pthread_atfork). Дочерний → родитель: pipe (до fork), `wait()+WEXITSTATUS` (только 8 бит), `mmap(MAP_SHARED|MAP_ANONYMOUS)` до fork (обе стороны пишут в одну physical page), SysV shared memory, POSIX shm.

7. **Что такое memory ordering в C11 atomics и когда нужен `memory_order_acquire/release`?** Как они работают на x86-64 и на ARM?

   *Подсказка*: x86 имеет TSO (Total Store Order) — по умолчанию strong; ARM = weak; acquire/release создают happens-before.

   *Эталон*: `memory_order_release` при store: все предыдущие stores и loads в программном порядке видны до этого store (no reorder past this store). `memory_order_acquire` при load: все последующие loads и stores выполняются после этого load. Вместе создают happens-before: всё написанное до release видно после acquire на том же атомике. На x86-64 (TSO): store→load может переупорядочиться, но не store→store или load→load. `release` store на x86 — обычная инструкция MOV. `acquire` load — обычный MOV. Накладных расходов нет. На ARM (weak ordering): `release` = `stlr` (store-release), `acquire` = `ldar` (load-acquire). Без этих инструкций возможно любое переупорядочение. Потому acquire/release на ARM дороже чем relaxed.

8. **Как `clone()` используется для создания контейнеров?** Какой минимальный набор namespace флагов нужен для изолированного контейнера? Что такое `pivot_root`?

   *Подсказка*: CLONE_NEWPID + CLONE_NEWNET + CLONE_NEWNS + CLONE_NEWUTS + user mapping.

   *Эталон*: Минимум для настоящей изоляции: `CLONE_NEWPID` (свои PID), `CLONE_NEWNET` (свой lo, нет доступа к хостовым интерфейсам), `CLONE_NEWNS` (свои mount points), `CLONE_NEWUTS` (свой hostname), `CLONE_NEWIPC`. `CLONE_NEWUSER` для rootless: маппинг UID 0 внутри → UID 1000 снаружи (uid_map, gid_map в /proc). `pivot_root(new_root, put_old)` — меняет корень ФС контейнера на образ (overlayfs поверх базового образа). В отличие от `chroot()` — меняет root для всего mount namespace, а не только для процесса; и старый корень перемещается в put_old (можно umount). Порядок: `clone(CLONE_NEWNS|...) → mount overlayfs → pivot_root → umount старый root → exec /sbin/init`.

---

### ЭКСПЕРТ (5 self_grade)

1. **Как реализовать lock-free стек на CAS атомиках?** Покажи реализацию push/pop. Что такое ABA-проблема и как её решить?

   *Подсказка*: CAS на head: compare old head, set new node. ABA: указатель вернулся к тому же значению, но data изменились — версионный счётчик (tagged pointer).

   *Эталон*: Lock-free стек: `typedef struct Node { int val; struct Node *next; } Node; atomic_uintptr_t head;`. Push: `new->next = atomic_load(&head); do { ... } while (!CAS_weak(&head, &old_head, new))`. Pop: аналогично CAS на head→next. ABA: поток A читает head=X, вытесняется; поток B: pop(X), push(Y), push(X) → head=X снова. Поток A: CAS(head==X → new) успевает, но X.next уже другой → heap corruption. Решение: tagged pointer — упаковать версию в старшие биты указателя (ABA count). На 64-bit Linux адреса 48-bit → старшие 16 бит для счётчика. Или `__int128` CAS (cmpxchg16b на x86), или hazard pointers (безопаснее но сложнее), или SMR (Safe Memory Reclamation).

2. **Как работает DWARF отладочная информация и как gdb находит нужный stack frame?**

   *Подсказка*: .eh_frame — CFA (Canonical Frame Address) expressions; DWARF CFI — правила восстановления регистров для каждого PC.

   *Эталон*: DWARF CFI (Call Frame Information) в `.eh_frame`: для каждого диапазона PC описывает как вычислить CFA (обычно `rsp + offset` или `rbp + offset`) и как восстановить сохранённые регистры. `gdb backtrace`: читает PC из текущего стека, находит запись CFI для этого PC, вычисляет CFA, читает возвратный адрес (RA) = CFA − 8, повторяет для следующего frame. Без `.eh_frame` (stripped): gdb использует эвристику по frame pointer (rbp). DWARF types в `.debug_info`: DW_TAG_variable, DW_TAG_subprogram с DW_AT_type, DW_AT_location (DWARF expression для вычисления адреса переменной). DW_AT_location может быть DW_OP_reg (в регистре) или DW_OP_fbreg offset (на стеке) — меняется по ходу функции (live ranges).

3. **Объясни priority inversion и mutex priority inheritance.** Когда это критично? Как Linux поддерживает PI-mutex?

   *Подсказка*: H ждёт mutex у L; M вытесняет L; H фактически ждёт M. PI: L временно получает приоритет H.

   *Эталон*: Классический сценарий (Mars Pathfinder, 1997): Low (L) держит mutex, High (H) ждёт его, Medium (M) не связан с mutex но вытесняет L → L не может освободить mutex → H ждёт вечно несмотря на высокий приоритет. Priority Inheritance (PI): когда H блокируется на mutex у L, ядро временно поднимает приоритет L до приоритета H. Как только L освобождает mutex — приоритет L возвращается. Linux: `PTHREAD_MUTEX_PROTOCOL` + `PTHREAD_PRIO_INHERIT` в `pthread_mutexattr_setprotocol()`. Реализовано через futex с FUTEX_LOCK_PI. Альтернатива: Priority Ceiling — mutex имеет заданный ceil-приоритет, любой захватывающий его поток временно получает этот приоритет. Менее динамично, но проще.

4. **Как реализовать свой аллокатор памяти поверх `mmap()`?**

   *Подсказка*: `mmap(MAP_ANONYMOUS)` сразу большой chunk; внутри — freelist или bump allocator; alignment, header перед каждым блоком.

   *Эталон*: Простой bump allocator: `mmap(NULL, POOL_SIZE, PROT_RW, MAP_ANON|MAP_PRIVATE, -1, 0)` — получить pool. `base + offset` — текущая позиция; при alloc(size): выравнять offset, bump, вернуть. Нет free (arena allocator). Для free: перед каждым блоком хранить header `{size_t size; uint32_t magic}`. `free(ptr)`: читать header по `ptr - sizeof(header)`, добавить в freelist. Realloc: alloc нового + memcpy + free старого. Реальные аллокаторы: segregated size classes (jemalloc), slab (ядро Linux: kmem_cache), TLSF (O(1) RT-аллокатор). Важно: alignment (всегда `_Alignof(max_align_t)` = 16 на x86-64), guard pages (`mprotect(PROT_NONE)` вокруг пула для детектирования overflow).

5. **Что такое memory-mapped files и почему они быстрее `read()`/`write()` для больших файлов?**

   *Подсказка*: нет копирования в userspace буфер; page cache — единственная копия; но mmap имеет ограничения.

   *Эталон*: `read(fd, buf, n)`: ядро копирует данные из page cache в userspace буфер — **два экземпляра данных** в памяти. `mmap(fd)`: виртуальные страницы процесса отображаются **напрямую на page cache**. Нет копирования. Для чтения: page fault → ядро загружает страницу в cache → маппинг указывает туда же. Для записи (`MAP_SHARED`): запись напрямую в page cache → `msync`/умирание процесса → файл обновляется. Почему быстрее: (1) нет memcpy → меньше bandwidth (2) случайный доступ — не нужно lseek; (3) ОС может агрессивно prefetch. Почему не всегда лучше: (1) маппинг 4 ГиБ файла на 32-bit → нет адресов; (2) страница файла исчезла (ftruncate) → SIGBUS; (3) sequential read → readahead read() может быть быстрее из-за меньшего TLB trashing; (4) pipe и socket → mmap неприменим. Правило: mmap для случайного доступа к большим файлам; read() для последовательного I/O и streaming.

