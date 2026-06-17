# Модуль C5 — Проектирование демонов и сервисов

> Этап 2A, Userspace-ремесло. Метка трека: *(новое)*. Сервер из C2/C4 — это ещё не
> сервис. Сервис должен **корректно запускаться** (и не падать, если зависимый
> компонент чуть медленнее), **переживать рестарты без потери запросов и без
> простоя порта**, **сообщать о своём здоровье** (а не молча зависать), **корректно
> останавливаться** и **жить под супервизором** (systemd), который его перезапустит,
> ограничит в правах и соберёт логи. Этот модуль — про то, как из «программы,
> которая слушает порт» сделать **боевой systemd-сервис**: `Type=notify`, socket
> activation, watchdog, graceful drain, zero-downtime reload. Если ты уверенно
> объясняешь, почему классический double-fork **вредит** под systemd, и можешь
> нарисовать путь слушающего сокета от `.socket`-юнита до `accept` в твоём процессе
> — проматывай к самопроверке.
>
> **Язык — двуязычный.** Основной — **C++** (RAII над дескрипторами, обёртки), рядом
> — **C-эквивалент** и **конфиг** (unit-файлы — это не код, но часть контракта
> сервиса). Сами интерфейсы (`sd_notify`, `sd_listen_fds`) — это **протокол поверх
> сисколлов** (датаграмма в UNIX-сокет, переменные окружения, наследование fd),
> одинаковый из C и C++; мы реализуем его руками, чтобы увидеть, что «магия systemd»
> — это пара переменных окружения и соглашение о номерах дескрипторов.
>
> **Опирается на C2, C3, C4.** event-loop и graceful shutdown — из C4; передача
> дескрипторов (`SCM_RIGHTS`) и UNIX-сокеты — из C3; неблокирующий сервер — из C2.
> Здесь всё это «упаковывается» в сервис.

**Читать к модулю:**

- **systemd docs:** `systemd.service(5)` (типы сервисов, `Type=notify`),
  `systemd.socket(5)` (socket activation), `sd_notify(3)`, `sd_listen_fds(3)`,
  `systemd.exec(5)` (sandbox-опции), `systemd.kill(5)` (как убивают),
  `daemon(7)` (каноничный гид: классическая vs «new-style» демонизация).
- **`man`:** `sd-daemon(3)`, `systemctl(1)`, `journalctl(1)`, `sd_journal_print(3)`,
  `cgroups(7)`, `credentials(7)`.
- **Контекст:** Lennart Poettering, «systemd for Administrators» (серия статей) —
  зачем new-style демоны, socket activation, cgroup-трекинг.

---

## 0. Карта модуля

| Раздел | О чём | Зачем системщику |
|--------|-------|------------------|
| 1 | Что такое демон | Базовая модель фонового процесса |
| 2 | Классическая демонизация (double-fork) | Откуда взялась и что она делает |
| 3 | Почему под systemd демонизировать НЕ надо | Главный сдвиг мышления |
| 4 | systemd: юниты, цели, зависимости | Среда, в которой живёт сервис |
| 5 | Типы сервисов: simple/exec/forking/oneshot/notify/dbus | Контракт готовности |
| 6 | `Type=notify` и протокол `sd_notify` | Сервис сам говорит «я готов» |
| 7 | Socket activation | Сокет переживает рестарт; нет `ECONNREFUSED` |
| 8 | Watchdog | Поймать «жив, но завис» |
| 9 | PID-файлы vs cgroup-трекинг | Почему systemd выбрал cgroups |
| 10 | Журналирование через journald | stdout → структурированный журнал |
| 11 | Graceful shutdown и reload (drain) | Не потерять in-flight |
| 12 | Zero-downtime reload: передача сокета | Рестарт без простоя |
| 13 | FDSTORE — хранилище дескрипторов | Пережить рестарт с состоянием |
| 14 | Sandboxing юнита: least privilege | Права через конфиг, не код |
| 15 | Restart-политики и flapping | Самовосстановление без штормов |
| 16 | Сборка: сервер C2/C4 → сервис | Всё вместе |
| 17 | Инструменты и отладка | systemctl/journalctl/analyze |
| 18 | Практика и самопроверка | Закрепление |
| 19–21 | Банк вопросов, глоссарий, что дальше | — |

**Время на модуль:** 30–45 часов (с упражнениями и развёртыванием юнита).

**Что значит «освоено» (из трека):** *проектируешь сервис с zero-downtime reload и
без потери in-flight запросов.* Не «написал unit-файл», а понимаешь, почему сокет
надо отделить от процесса (socket activation), как дренировать соединения при
остановке, как не зависнуть незаметно (watchdog) и как передать слушающий сокет
новому экземпляру без простоя.

---

## 1. Что такое демон

### 1.1 Определение

**Демон** (daemon) — это процесс, который работает **в фоне**, без управляющего
терминала, обычно долго (часто всё время работы системы), обслуживая запросы:
`sshd`, `nginx`, `crond`, `systemd-journald`, `dockerd`. Имя — от «daemon» из
греческой мифологии (полезный незримый дух), а не «demon»; традиционно к имени
добавляют `d` (`sshd`, `httpd`).

Ключевые свойства демона:
- **нет управляющего терминала** — он не привязан к сессии пользователя и не умрёт,
  когда тот выйдет (иначе получил бы `SIGHUP` при закрытии терминала);
- **работает в фоне** — запуск не «держит» шелл;
- **переживает выход родителя** — его «усыновляет» `init`/systemd (PID 1);
- **управляем сигналами/сокетом** — `SIGTERM` остановить, `SIGHUP` перечитать
  конфиг (C4), команды через UNIX-сокет (C3).

### 1.2 Демон vs обычная программа

| | Обычная программа | Демон |
|--|-------------------|-------|
| Терминал | привязана к tty | **нет** управляющего tty |
| Время жизни | пока работает пользователь | долго/постоянно |
| stdin/stdout | терминал | `/dev/null` / журнал |
| Родитель | шелл | `init`/systemd (PID 1) |
| Управление | Ctrl+C, клавиатура | сигналы, сокет, `systemctl` |

### 1.3 Две эпохи демонов

Исторически демон **сам** отрывался от терминала через ритуал double-fork (§2).
Сегодня под systemd он этого **не делает** — наоборот, работает **в переднем плане**,
а отрыв, фон, перезапуск, логи и права берёт на себя супервизор (§3). Понимать надо
**обе** модели: старую — чтобы читать legacy и собеседоваться, новую — чтобы писать
сервисы сегодня.

### 1.4 Обязанности боевого сервиса (карта модуля в одном списке)

Сервер из C2 «слушает порт». Сервис **дополнительно** обязан:

1. **Сообщить готовность** — не принимать за «готов» факт запуска (`Type=notify`,
   §6).
2. **Корректно остановиться** — drain in-flight по `SIGTERM`, не оборвать (§11).
3. **Перечитать конфиг на лету** — `SIGHUP`/reload без рестарта (§11.3).
4. **Не зависнуть незаметно** — watchdog (§8).
5. **Пережить рестарт без простоя** — сокет отдельно от процесса (§7, §12).
6. **Логировать наблюдаемо** — структурно в журнал (§10).
7. **Работать с минимумом прав** — sandbox (§14).
8. **Самовосстанавливаться, но не флапать** — restart-политика (§15).
9. **Не плодить зомби и сирот** — reaping/cgroup-трекинг (§9, Ф3).

Каждый пункт — отдельный раздел модуля. «Боевой сервис» = сервер C2 + все девять.
Именно этого не хватает «программе, которая слушает порт», чтобы её можно было
**эксплуатировать**.

---

## 2. Классическая демонизация (double-fork)

Прежде чем «почему не надо», разберём «как делали» — это каноничный ритуал из
`daemon(7)`, и его до сих пор много в legacy.

### 2.1 Ритуал по шагам

```c
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

void daemonize_classic(void) {
    /* 1) Первый fork: родитель выходит → мы не лидер группы, шелл вернул управление */
    pid_t pid = fork();
    if (pid < 0) _exit(1);
    if (pid > 0) _exit(0);              /* родитель завершается */

    /* 2) setsid: новая сессия → отвязались от управляющего терминала, мы лидер сессии */
    if (setsid() < 0) _exit(1);

    /* 3) Второй fork: теперь мы НЕ лидер сессии → НИКОГДА не получим терминал */
    pid = fork();
    if (pid < 0) _exit(1);
    if (pid > 0) _exit(0);

    /* 4) chdir("/"): не держим cwd (иначе нельзя размонтировать ФС) */
    if (chdir("/") < 0) _exit(1);

    /* 5) umask(0): не зависеть от unmask родителя при создании файлов */
    umask(0);

    /* 6) Закрыть/перенаправить stdin/stdout/stderr в /dev/null */
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }
    /* (7) опционально: записать PID-файл, открыть syslog, сбросить привилегии */
}
```

### 2.2 Зачем каждый шаг

- **Первый `fork` + `setsid`** — выйти из группы процессов шелла и создать **новую
  сессию** без управляющего терминала. Лидер сессии, однако, **может** получить
  терминал, открыв tty-устройство.
- **Второй `fork`** — стать **не**-лидером сессии, чтобы случайное открытие tty
  **не** дало нам управляющий терминал (иначе `SIGHUP`/`SIGINT` с того терминала
  убивали бы демон). Это и есть «double-fork».
- **`chdir("/")`** — не держать рабочий каталог (иначе его ФС нельзя размонтировать,
  пока демон жив).
- **`umask(0)`** — предсказуемые права создаваемых файлов, не зависящие от родителя.
- **Перенаправление stdio в `/dev/null`** — у демона нет терминала; чтение/запись в
  «висящий» fd терминала — ошибка. (Это разрывало связь с логами — отсюда
  PID-файлы и syslog в старой модели.)

### 2.3 Сопутствующий зоопарк старой модели

Классический демон тащил за собой:
- **PID-файл** (`/run/myapp.pid`) — записать свой PID, чтобы `init`-скрипт мог его
  потом найти и послать сигнал. Источник гонок и «протухших» файлов (§9).
- **syslog** — раз stdout ушёл в `/dev/null`, логи слали в `syslogd` через
  `openlog`/`syslog`.
- **ручной перезапуск** — если демон падал, его никто не поднимал (или это делал
  громоздкий init-скрипт/`monit`).
- **ручной сброс привилегий** — `setuid`/`setgid`/`chroot` руками в коде.

Всё это — **ровно** те задачи, которые systemd забрал себе (§3).

### 2.4 Классический демон целиком: сигналы и PID-файл

Полная классическая модель добавляет к §2.1 обработку сигналов и PID-файл — чтобы
init-скрипт мог управлять демоном:

```c
static volatile sig_atomic_t g_stop = 0, g_reload = 0;
static void on_term(int){ g_stop = 1; }       // C4: минимальный обработчик — только флаг
static void on_hup (int){ g_reload = 1; }

int classic_main(void) {
    daemonize_classic();                       /* §2.1: double-fork и т.д. */

    /* PID-файл: записать свой PID атомарно (O_CREAT|O_EXCL — защита от гонки) */
    int pf = open("/run/myapp.pid", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (pf >= 0) { char b[16]; int n = snprintf(b, sizeof b, "%d\n", getpid());
                   write(pf, b, (size_t)n); close(pf); }

    openlog("myapp", LOG_PID, LOG_DAEMON);     /* раз stdout в /dev/null — логи в syslog */
    struct sigaction sa{}; sa.sa_flags = SA_RESTART;
    sa.sa_handler = on_term; sigaction(SIGTERM, &sa, nullptr);
    sa.sa_handler = on_hup;  sigaction(SIGHUP,  &sa, nullptr);

    while (!g_stop) {
        if (g_reload) { reload_config(); g_reload = 0; syslog(LOG_INFO, "reloaded"); }
        do_work();
    }
    unlink("/run/myapp.pid");                   /* убрать PID-файл (если успеем — а если SIGKILL?) */
    closelog();
    return 0;
}
```

Обрати внимание на хрупкости, которые и убил systemd: `unlink` PID-файла **не**
выполнится при `SIGKILL`/крэше → файл **протухнет** (§9); логи ушли в `syslog`, а
не в журнал юнита; перезапуск при падении никто не делает. SysV-init управлял этим
через скрипт `/etc/init.d/myapp` (`start`/`stop`/`restart`/`status`), который читал
PID-файл и слал сигналы — громоздко и полно гонок. systemd заменил весь этот слой
декларативным юнитом.

---

## 3. Почему под systemd демонизировать НЕ надо

Главный сдвиг мышления модуля. **New-style демон (`daemon(7)`) не делает double-fork
вообще** — он работает в переднем плане, а всё перечисленное в §2.3 отдаёт супервизору.

### 3.1 systemd делает это лучше и надёжнее

| Задача классического демона | Кто делает под systemd |
|-----------------------------|------------------------|
| Отрыв от терминала, фон | systemd запускает без tty, в своём cgroup |
| Перезапуск при падении | `Restart=on-failure` (надёжно, с backoff) |
| PID-файл и поиск процессов | **cgroup-трекинг** — знает все процессы точно (§9) |
| Логи (syslog/файлы) | **journald** ловит stdout/stderr (§10) |
| Сброс привилегий, chroot | `User=`, `ProtectSystem=`, seccomp в юните (§14) |
| Готовность («я поднялся») | `Type=notify` + `sd_notify` (§6) |
| Сокет до старта | **socket activation** (§7) |
| Зависел? | **watchdog** (§8) |

### 3.2 Double-fork под systemd — активно ВРЕДИТ

Если сервис типа `simple`/`notify` сделает `fork` и родитель выйдет, systemd
решит, что **главный процесс завершился**, — и либо посчитает сервис упавшим, либо
«потеряет» настоящий рабочий процесс (хотя cgroup-трекинг его удержит, состояние
сервиса будет неверным). Для форкающихся демонов есть `Type=forking` + `PIDFile=`,
но это **legacy-режим**, который systemd поддерживает ради совместимости и **не
рекомендует** для нового кода.

> **Правило new-style демона:** **не** делай `fork`/`setsid`/`/dev/null`-ритуал. Работай
> в переднем плане, пиши логи в **stdout/stderr**, сообщи готовность через
> `sd_notify(READY=1)`, реагируй на `SIGTERM`. Всё остальное — забота unit-файла.
> Это и проще (меньше кода), и надёжнее (нет гонок PID-файла, нет потерянных
> процессов).

### 3.3 Но знать классику обязательно

- **Контейнеры без systemd** (минимальные образы) иногда требуют «тонкого» PID 1
  или ручной демонизации (хотя чаще берут `tini`/`dumb-init` как init).
- **Legacy-системы** (старый SysV-init, embedded без systemd) — там double-fork
  ещё живой.
- **Собеседования** — «зачем второй fork?» — классический вопрос (ответ §2.2).

### 3.4 New-style демон: минимальный шаблон

Контраст с §2.1: new-style демон — это **обычная** программа переднего плана. Весь
«ритуал» исчезает:

```cpp
int newstyle_main() {
    // НЕТ fork/setsid/chdir/umask/dev-null — работаем в переднем плане.
    install_signal_handling();          // signalfd: SIGTERM/SIGHUP (C4)
    int lfd = take_socket_or_bind();     // sd_listen_fds (упр. 01) ИЛИ bind как fallback
    warm_up();                           // прогрев кэша, подключение к БД
    my_sd_notify("READY=1\n");           // упр. 02: ТЕПЕРЬ готов
    run_event_loop();                    // C2/C4: epoll; логи в stderr (§10)
    my_sd_notify("STOPPING=1\n");        // graceful по SIGTERM (§11)
    return 0;                            // systemd увидит выход и (если надо) перезапустит
}
```

| | Классический (§2) | New-style (§3) |
|--|-------------------|----------------|
| double-fork/setsid | да | **нет** |
| stdin/out/err | `/dev/null` | переднеплановые stdout/stderr → журнал |
| фон/перезапуск | сам/init-скрипт | systemd |
| PID-файл | да | **нет** (cgroup, §9) |
| логи | syslog/файлы | stdout/stderr → journald (§10) |
| готовность | неявно | `sd_notify(READY=1)` |
| строк кода демонизации | десятки | **ноль** |

Это и есть сдвиг: меньше кода, нет гонок, надёжный надзор. Тот же бинарь запускается
и из shell (для отладки — логи прямо в терминал), и под systemd (логи в журнал) —
без изменений.

---

## 4. systemd: юниты, цели, зависимости

systemd — это **система инициализации** (PID 1) и **менеджер сервисов** Linux. Он
запускает, останавливает, перезапускает и **отслеживает** сервисы, разрешает
зависимости и параллелит загрузку.

### 4.1 Юнит-файлы

Всё, чем управляет systemd, описано **юнитами** (units) — текстовыми файлами:

| Тип юнита | Расширение | Что описывает |
|-----------|-----------|---------------|
| service | `.service` | сервис/демон (главный для нас) |
| socket | `.socket` | слушающий сокет (socket activation, §7) |
| timer | `.timer` | таймер (замена cron) |
| target | `.target` | группа юнитов / точка синхронизации (≈ runlevel) |
| mount/automount | `.mount` | точки монтирования |
| path | `.path` | реакция на изменение пути (через inotify, C4) |

Расположение: `/etc/systemd/system/` (админ, приоритет), `/usr/lib/systemd/system/`
(пакеты), `~/.config/systemd/user/` (user-сервисы).

### 4.2 Минимальный `.service`

```ini
# /etc/systemd/system/myapp.service
[Unit]
Description=My application server
After=network-online.target
Wants=network-online.target

[Service]
Type=notify
ExecStart=/usr/local/bin/myapp --config /etc/myapp/config
ExecReload=/bin/kill -HUP $MAINPID
Restart=on-failure
RestartSec=2
User=myapp
Group=myapp

[Install]
WantedBy=multi-user.target
```

- **`[Unit]`** — метаданные и зависимости (`After=`, `Requires=`, `Wants=`).
- **`[Service]`** — как запускать (`ExecStart=`), тип готовности (`Type=`), политика
  перезапуска, права.
- **`[Install]`** — куда «прицепить» при `systemctl enable` (`WantedBy=`).

### 4.3 Зависимости: ordering vs requirement

Два **разных** измерения, которые путают:

- **Порядок** (`After=`/`Before=`) — *когда* запускать относительно другого юнита.
  **Не** означает «требуется».
- **Требование** (`Requires=`/`Wants=`/`BindsTo=`) — *нужен* ли другой юнит.
  - `Wants=` — мягкая зависимость (запусти, но переживём отказ);
  - `Requires=` — жёсткая (если тот упал при старте — мы тоже не стартуем);
  - `BindsTo=` — ещё жёстче (если тот остановился **в любой момент** — и мы).

Типичная ошибка: написать `Requires=foo` без `After=foo` — systemd запустит оба
**параллельно**, и порядка нет. Нужны **оба**: `Requires=foo` + `After=foo`.

### 4.4 Цели (targets) ≈ runlevels

`target` — точка синхронизации/группа. Ключевые: `multi-user.target` (обычный
сервер без графики), `graphical.target`, `network-online.target` (сеть поднята).
`WantedBy=multi-user.target` в `[Install]` означает «при `enable` запускать меня в
многопользовательском режиме».

### 4.5 Шаблонные юниты и инстансы

Часто нужно **много** экземпляров одного сервиса с параметром — по одному на порт,
пользователя, устройство. Для этого — **шаблонные юниты** (`name@.service`):
`%i` подставляет «инстанс».

```ini
# myapp@.service — шаблон (обрати внимание на @ в имени)
[Service]
Type=notify
ExecStart=/usr/local/bin/myapp --shard %i      # %i = то, что после @
# %i — инстанс как есть; %I — с unescape; %n — полное имя юнита
```

```sh
systemctl start myapp@0.service   # запустит с --shard 0
systemctl start myapp@1.service   # независимый экземпляр, --shard 1
systemctl enable myapp@{0,1,2,3}.service   # 4 шарда
```

Спецификаторы: `%i`/`%I` (инстанс), `%n` (имя юнита), `%H` (hostname), `%u`/`%U`
(имя/UID пользователя), `%t` (runtime-каталог, `/run`). Шаблоны + socket activation
(`Accept=yes` запускает `name@<номер-соединения>.service`) — основа inetd-стиля.

### 4.6 Таймеры: замена cron

systemd-таймеры (`.timer`) запускают сервис по расписанию — современная замена cron,
интегрированная в журнал и зависимости:

```ini
# backup.timer
[Timer]
OnCalendar=*-*-* 02:00:00        # каждый день в 02:00 (как cron, но читаемо)
# OnBootSec=5min                 # через 5 мин после загрузки
# OnUnitActiveSec=1h             # каждый час после последнего запуска
Persistent=true                  # «догнать» пропущенный запуск (если машина спала)
[Install]
WantedBy=timers.target
```

```ini
# backup.service — Type=oneshot, запускается таймером того же имени
[Service]
Type=oneshot
ExecStart=/usr/local/bin/backup
```

Преимущества над cron: логи в журнале (`journalctl -u backup`), зависимости
(`After=`), `Persistent=` (догнать пропуск), рандомизация (`RandomizedDelaySec=`
против штормов), и тот же sandbox (§14). `systemctl list-timers` показывает все
таймеры и время следующего запуска.

### 4.7 Параметры и окружение

```ini
[Service]
Environment=LOG_LEVEL=info PORT=8080            # переменные окружения сервиса
EnvironmentFile=/etc/myapp/env                  # или из файла (KEY=VALUE построчно)
ExecStart=/usr/local/bin/myapp --port ${PORT}   # подстановка
```

Это чище, чем хардкодить параметры: эксплуатация меняет `EnvironmentFile` и делает
`systemctl restart`, не трогая бинарь. Секреты, однако, лучше не в `Environment=`
(видны в `systemctl show` и `/proc/<pid>/environ`) — для них systemd credentials
(§14.5).

### 4.8 User-сервисы и lingering

systemd управляет не только системными сервисами (PID 1), но и **пользовательскими** —
свой инстанс systemd на каждого вошедшего пользователя:

```sh
systemctl --user start myapp        # юнит из ~/.config/systemd/user/
systemctl --user enable myapp
loginctl enable-linger alice        # сервисы alice работают даже когда она НЕ в системе
```

User-сервисы (`--user`) полезны для пер-пользовательских демонов (агенты,
синхронизаторы) без рута. По умолчанию они **останавливаются**, когда пользователь
выходит; **`enable-linger`** оставляет их жить постоянно. Те же `Type=notify`,
socket activation, sandbox — но в пользовательском пространстве, под UID
пользователя, без `/etc`. Для серверных сервисов используют системные юниты, но
знать про user-инстанс полезно (CI-агенты, desktop-демоны).

---

## 5. Типы сервисов: контракт готовности

`Type=` определяет, **когда systemd считает сервис «запустившимся»** (готовым). Это
важно: зависимые юниты (`After=`/`Requires=`) ждут именно этого момента. Ошибка в
типе — гонки старта.

### 5.1 Таблица типов

| `Type=` | «Готов», когда… | Когда использовать |
|---------|-----------------|--------------------|
| `simple` | сразу после `fork`/`exec` (готовность **не** проверяется) | простые сервисы; есть гонка готовности |
| `exec` | `exec` главного процесса **успешно** выполнен (v240+) | лучше `simple`: ловит ошибку запуска |
| `notify` | сервис прислал **`READY=1`** через `sd_notify` | **сервисы с инициализацией** (рекомендуется) |
| `forking` | родитель форкнул рабочий процесс и **вышел** | legacy-демоны с PID-файлом |
| `oneshot` | процесс **завершился** (с успехом) | скрипты, разовые задачи; `RemainAfterExit=` |
| `dbus` | сервис занял **имя на D-Bus** | D-Bus-сервисы |
| `idle` | как `simple`, но отложен до «затишья» загрузки | косметика вывода на загрузке |

### 5.2 Главная проблема `simple`: гонка готовности

С `Type=simple` systemd считает сервис готовым **в момент `exec`** — то есть до
того, как сервис открыл сокет, прогрел кэш, подключился к БД. Зависимые юниты
(`After=myapp`) стартуют **слишком рано** и получают `ECONNREFUSED`:

```
Type=simple:  exec myapp ──готов!──> запускается зависимый юнит
                  │  (а myapp ещё bind()'ит сокет — отказ!)
                  ▼
              bind/listen/прогрев...
```

`Type=notify` закрывает это: «готов» = сервис **сам** прислал `READY=1`, когда
**действительно** готов (§6):

```
Type=notify:  exec myapp ──> bind/listen/прогрев ──> sd_notify(READY=1) ──готов!──> зависимый
```

### 5.3 Почему `forking` — legacy

`Type=forking` повторяет классический double-fork: ExecStart форкает рабочий
процесс, родитель выходит, systemd по выходу родителя считает сервис запущенным (и
ищет настоящий PID в `PIDFile=`). Минусы: нужен PID-файл (гонки, §9), нет точной
готовности, лишний код демонизации. Для нового сервиса — `notify` или `exec`, не
`forking`.

### 5.4 Lifecycle-хуки: `ExecStartPre`/`ExecStop`/`ExecStopPost`

`ExecStart=` — не единственная команда; вокруг неё есть **хуки** жизненного цикла:

```ini
[Service]
Type=notify
ExecStartPre=/usr/local/bin/myapp --check-config   # до старта: валидация (провал → не стартуем)
ExecStartPre=-/usr/bin/mkdir -p /run/myapp          # '-' = игнорировать ошибку
ExecStart=/usr/local/bin/myapp
ExecStartPost=/usr/local/bin/register-in-discovery  # после готовности: регистрация в service discovery
ExecReload=/bin/kill -HUP $MAINPID                  # перезагрузка
ExecStop=/usr/local/bin/myapp-ctl drain             # команда graceful-остановки (опц.)
ExecStopPost=/usr/local/bin/deregister              # ВСЕГДА после остановки (даже при крэше): уборка
```

- **`ExecStartPre=`** — подготовка (валидация конфига, создание каталогов). Провал
  (без `-`) **отменяет** старт — лучше упасть здесь, чем с битым конфигом в проде.
- **`ExecStartPost=`** — после того, как сервис готов (для `notify` — после
  `READY=1`): регистрация в discovery, прогрев.
- **`ExecStop=`** — если задан, systemd зовёт его **вместо** простого `SIGTERM`
  (но обычно сигнала достаточно, и `ExecStop` не нужен).
- **`ExecStopPost=`** — выполняется **всегда** после остановки, **в том числе при
  падении**, — место для гарантированной уборки (дерегистрация, удаление runtime-файлов).

Эти хуки выносят логику развёртывания из кода сервиса в декларативный юнит:
валидация конфига, регистрация/дерегистрация в балансировщике — не дело бинаря.

### 5.5 D-Bus-активация (`Type=dbus`)

`Type=dbus` — сервис «готов», когда **занял имя на D-Bus** (системной шине IPC,
поверх UNIX-сокетов, C3). Юнит указывает `BusName=org.example.MyApp`; systemd может
**активировать** сервис **по требованию** — когда кто-то впервые обращается к этому
имени на шине (как socket activation, но триггер — D-Bus-запрос). Так стартуют многие
системные сервисы (NetworkManager, logind). Для сетевых серверов это не нужно — там
socket activation; но знать про D-Bus-активацию полезно: это тот же принцип «запуск
по обращению», только через шину.

---

## 6. `Type=notify` и протокол `sd_notify`

Упражнение `02-sd-notify`. `Type=notify` — рекомендуемый способ интеграции:
сервис **сам сообщает** systemd о своём состоянии через простой текстовый протокол.

### 6.1 Как это работает

systemd передаёт сервису переменную окружения **`$NOTIFY_SOCKET`** — путь к
датаграммному `AF_UNIX`-сокету. Сервис шлёт туда **датаграмму** с телом из строк
`KEY=VALUE`, разделённых `\n`. Это и есть `sd_notify`.

```
сервис ── sendmsg(NOTIFY_SOCKET, "READY=1\nSTATUS=accepting\n") ──> systemd (PID 1)
                                                                     помечает сервис готовым
```

### 6.2 Ключевые сообщения

| Сообщение | Смысл |
|-----------|-------|
| `READY=1` | «я полностью готов» (для `Type=notify` — завершает старт) |
| `RELOADING=1` + `MONOTONIC_USEC=<n>` | «начал перечитывать конфиг» (с v253 — обязательно с меткой времени) |
| `STOPPING=1` | «начал штатную остановку» (не считать падением) |
| `STATUS=<текст>` | человекочитаемый статус в `systemctl status` |
| `ERRNO=<n>` | код ошибки при сбое старта |
| `WATCHDOG=1` | «я жив» (heartbeat watchdog, §8) |
| `MAINPID=<pid>` | сообщить настоящий главный PID (если форкался) |
| `FDSTORE=1` | передать дескриптор на хранение systemd (§13) |

### 6.3 Построение тела (упражнение `02-sd-notify`)

В упражнении ты строишь **тело** этих сообщений по канону — это и есть суть
протокола (отправка — обычный `sendmsg` в `$NOTIFY_SOCKET`):

```cpp
// "READY=1\n" + (если status задан) "STATUS=<status>\n":
int notify_ready(char* buf, int cap, const char* status);
// "RELOADING=1\nMONOTONIC_USEC=<mono_usec>\n":
int notify_reloading(char* buf, int cap, unsigned long long mono_usec);
int notify_stopping(char* buf, int cap);   // "STOPPING=1\n"
int notify_watchdog(char* buf, int cap);   // "WATCHDOG=1\n"
```

Полная отправка (что прячет libsystemd внутри `sd_notify`):

```cpp
// Упрощённо: открыть датаграммный сокет на $NOTIFY_SOCKET и послать тело.
int my_sd_notify(const char* state) {
    const char* path = getenv("NOTIFY_SOCKET");
    if (!path) return 0;                       // не под systemd-notify — no-op
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    sockaddr_un addr{}; addr.sun_family = AF_UNIX;
    // путь либо обычный, либо абстрактный (начинается с '@' → первый байт '\0', C3 §3.2)
    if (path[0] == '@') { addr.sun_path[0] = '\0'; strncpy(addr.sun_path + 1, path + 1, sizeof(addr.sun_path) - 2); }
    else                {                          strncpy(addr.sun_path,     path,     sizeof(addr.sun_path) - 1); }
    ssize_t n = sendto(fd, state, strlen(state), MSG_NOSIGNAL,
                       (sockaddr*)&addr, sizeof addr);
    close(fd);
    return n < 0 ? -1 : 1;
}
// Использование:
my_sd_notify("READY=1\nSTATUS=accepting connections\n");
```

### 6.4 `NotifyAccess=` — кто может слать

По умолчанию (`NotifyAccess=main`) systemd принимает уведомления **только** от
главного процесса сервиса. Если уведомлять должен дочерний/вспомогательный —
поставь `NotifyAccess=all` (или `exec`). Иначе твой `READY=1` из «не того» процесса
**проигнорируется**, и `Type=notify`-сервис «зависнет на старте» до таймаута
(`TimeoutStartSec`). Это частая грабля.

### 6.5 Жизненный цикл уведомлений в реальном сервисе

```cpp
int main() {
    init_resources();                 // bind/listen, прогрев кэша, подключение к БД
    my_sd_notify("READY=1\nSTATUS=accepting\n");   // ТЕПЕРЬ systemd считает старт завершённым
    run_event_loop();                 // C2/C4: epoll + signalfd + timerfd
    // при SIGHUP:
    //   my_sd_notify("RELOADING=1\nMONOTONIC_USEC=<now>\n"); reload(); my_sd_notify("READY=1\n");
    // при SIGTERM:
    my_sd_notify("STOPPING=1\nSTATUS=draining\n");  // C4 graceful (§11)
    drain_and_exit();
}
```

`STATUS=` особенно ценен в эксплуатации: `systemctl status myapp` покажет «accepting
/ draining / 12 active conns» — живая картина без лазанья в логи.

### 6.6 Абстрактный сокет и отправка с дескриптором

`$NOTIFY_SOCKET` обычно **абстрактный** (начинается с `@` → первый байт пути `\0`,
C3 §3.2): нет файла в ФС, исчезает сам. Полная отправка с **дескриптором** (для
`FDSTORE`, §13) — это `sendmsg` с `SCM_RIGHTS` (C3 §3.4), а тело идёт в `iov`:

```cpp
int sd_notify_with_fd(const char* state, int fd_to_store) {
    const char* path = getenv("NOTIFY_SOCKET");
    if (!path) return 0;
    int s = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    sockaddr_un a{}; a.sun_family = AF_UNIX;
    if (path[0] == '@') { a.sun_path[0] = '\0'; memcpy(a.sun_path + 1, path + 1, strlen(path + 1)); }
    else                  memcpy(a.sun_path, path, strlen(path));

    iovec iov{ (void*)state, strlen(state) };
    char cbuf[CMSG_SPACE(sizeof(int))]{};
    msghdr msg{};
    msg.msg_name = &a; msg.msg_namelen = sizeof a;
    msg.msg_iov = &iov; msg.msg_iovlen = 1;
    if (fd_to_store >= 0) {                       // приложить fd через SCM_RIGHTS
        msg.msg_control = cbuf; msg.msg_controllen = sizeof cbuf;
        cmsghdr* c = CMSG_FIRSTHDR(&msg);
        c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(c), &fd_to_store, sizeof(int));
    }
    ssize_t n = sendmsg(s, &msg, MSG_NOSIGNAL);
    close(s);
    return n < 0 ? -1 : 1;
}
// sd_notify_with_fd("FDSTORE=1\nFDNAME=conn\n", conn_fd);  // отдать соединение systemd
```

`MSG_NOSIGNAL` — чтобы запись в «исчезнувший» notify-сокет не прислала `SIGPIPE`
(C3). Это вся «магия» `sd_notify_with_fds` из libsystemd.

### 6.7 Liveness vs readiness: два разных вопроса

Из мира оркестраторов пришло важное разделение, прямо отображающееся на systemd:

- **Readiness** (готовность) — «можно ли слать мне трафик **сейчас**?». В systemd
  это `READY=1` (старт завершён) и `STATUS=` (текущая фаза). При перегрузке/reload
  сервис может быть «жив, но не готов».
- **Liveness** (живость) — «жив ли я **вообще** или завис?». В systemd это
  **watchdog** (§8): не прислал `WATCHDOG=1` → считаю мёртвым, перезапускаю.

Путать их опасно: liveness-проверка, которая дёргает зависимость (БД), **перезапустит**
здоровый сервис из-за временной недоступности БД (каскадный сбой). Правило: liveness
проверяет **только себя** (event-loop крутится?), readiness — может учитывать
зависимости. Это ровно k8s `livenessProbe` vs `readinessProbe` (§12.6).

### 6.8 Edge-cases протокола

- **`NOTIFY_SOCKET` не задан** — сервис запущен **не** под `Type=notify` (или вне
  systemd). `sd_notify` обязан стать **no-op** (вернуть 0), а не упасть, — иначе
  бинарь нельзя запустить вручную для отладки. Проверка `getenv("NOTIFY_SOCKET")`
  в начале (§6.3) — обязательна.
- **Долгий старт: `EXTEND_TIMEOUT_USEC=`.** Если прогрев дольше `TimeoutStartSec`,
  не увеличивай таймаут вслепую — шли `sd_notify("EXTEND_TIMEOUT_USEC=10000000")`
  периодически: «я ещё жив и работаю, дай ещё 10 c». Так systemd отличает «медленно,
  но прогрессирует» от «завис». То же на остановке (`STOPPING` + `EXTEND_TIMEOUT`).
- **`MAINPID=`** — если главный процесс сменился (форкнулся настоящий рабочий), шли
  `MAINPID=<pid>`, чтобы systemd следил за **ним** (нужно `NotifyAccess`).
- **`RELOADING` без `MONOTONIC_USEC`** на systemd ≥ v253 → reload «не завершается»
  (systemd ждёт `READY=1`, но не знает, что reload реально начался). Всегда пара
  `RELOADING=1\nMONOTONIC_USEC=<now>\n` (упр. 02).
- **Уведомление — best-effort.** Датаграмма может потеряться (буфер сокета полон);
  libsystemd это в основном игнорирует. Для критичного старта это ок: systemd всё
  равно подождёт до таймаута, а не зависнет навсегда.

---

## 7. Socket activation

Упражнение `01-listen-fds`. Socket activation — одна из главных идей systemd:
**слушающий сокет открывает systemd**, а не сам сервис.

### 7.1 Как это работает

`.socket`-юнит описывает, что слушать; systemd делает `socket`+`bind`+`listen`
**сам**, и при первом подключении (или сразу при загрузке) запускает сервис,
**передав ему готовый сокет по наследству** на дескрипторах `3, 4, …`:

```ini
# /etc/systemd/system/myapp.socket
[Socket]
ListenStream=8080
FileDescriptorName=http       # имя для sd_listen_fds_with_names
Backlog=1024

[Install]
WantedBy=sockets.target
```

```ini
# myapp.service — БЕЗ bind/listen в коде, сокет придёт по наследству
[Service]
Type=notify
ExecStart=/usr/local/bin/myapp
```

```
systemd:  socket()+bind(8080)+listen()  ──ждёт подключения──>
   клиент стучится ──> systemd запускает myapp, передаёт fd 3 = слушающий сокет
   myapp: sd_listen_fds() → 1; работает с fd 3 как с listen-сокетом (НЕ делает bind!)
```

### 7.2 Протокол передачи (что разбирает `01-listen-fds`)

Сервис узнаёт о переданных сокетах из **переменных окружения**:

- **`LISTEN_PID`** — PID, которому предназначены дескрипторы. Сервис **обязан**
  сверить с `getpid()` (переменные могли «утечь» в потомка — нельзя применять чужое).
- **`LISTEN_FDS`** — **число** переданных fd. Они занимают `3 .. 3 + N − 1`
  (`SD_LISTEN_FDS_START = 3`).
- **`LISTEN_FDNAMES`** — имена через `:` (из `FileDescriptorName=`), по одному на fd:
  `"http:metrics"` → fd 3 = http, fd 4 = metrics.

```cpp
// Ровно это ты реализуешь в 01-listen-fds (аналог sd_listen_fds):
int my_listen_fds(int my_pid, const char* listen_pid, const char* listen_fds);
int my_listen_fd_by_name(int my_pid, const char* listen_pid, const char* listen_fds,
                         const char* listen_fdnames, const char* name);
```

Логика: `LISTEN_PID` пуст или ≠ `my_pid` → 0 (не нам); иначе вернуть `LISTEN_FDS`.
По имени — найти индекс в `LISTEN_FDNAMES`, вернуть `3 + индекс`. Сверка PID — не
формальность: без неё процесс примет за «свои» чужие дескрипторы.

### 7.3 Зачем это (четыре больших выигрыша)

1. **Нет `ECONNREFUSED` при рестарте.** Сокет открыт **до** старта сервиса и
   переживает его рестарт. Клиенты, постучавшиеся в момент рестарта, ждут в
   **backlog** ядра, а не получают отказ. Это фундамент zero-downtime (§12).
2. **Параллельная загрузка.** systemd открывает **все** сокеты сразу, поэтому
   сервисы, зависящие друг от друга через сокеты, стартуют **параллельно** —
   подключение просто буферизуется, пока адресат поднимается. Ускоряет загрузку.
3. **Запуск по требованию (on-demand).** Сервис можно запускать **только** при
   первом подключении (как `inetd`), экономя ресурсы.
4. **Privilege separation.** systemd (root) делает `bind` на привилегированный порт
   (< 1024) или к защищённому пути, а сервис работает под `User=myapp` **без рута** —
   ему достался уже готовый сокет.

### 7.4 `Accept=yes` vs `Accept=no`

- **`Accept=no`** (по умолчанию) — сервису передаётся **слушающий** сокет; он сам
  делает `accept` в цикле (как в C2). Один экземпляр сервиса. **Норма для серверов.**
- **`Accept=yes`** — inetd-стиль: systemd сам делает `accept` и запускает **отдельный
  экземпляр** сервиса на **каждое** соединение (передаёт **уже принятый** сокет).
  Просто, но дорого (процесс на соединение) — для редких/тяжёлых соединений.

### 7.5 Socket activation vs ручная передача fd

В C3 ты передавал дескрипторы через `SCM_RIGHTS` руками. Socket activation — это
**тот же** механизм наследования fd, но сокетом владеет **systemd** (переживающий
рестарты сервиса), а не сам сервис. Поэтому он надёжнее для zero-downtime: даже если
сервис целиком упал и перезапустился, сокет (и backlog клиентов) **не потерян**.

### 7.6 Не только TCP: datagram, FIFO, несколько сокетов

`.socket`-юнит активирует не только потоковые сокеты:

```ini
[Socket]
ListenStream=8080            # TCP/UNIX stream
ListenStream=/run/myapp.sock # UNIX-сокет (путь) — локальный API (C3)
ListenDatagram=9000          # UDP/UNIX datagram
ListenFIFO=/run/myapp.fifo   # именованный канал (C3)
ListenStream=[::]:443        # IPv6
```

Один `.socket` может слушать **несколько** адресов — все придут сервису как
`LISTEN_FDS=N` подряд с fd 3, 4, … По именам (`FileDescriptorName=`, через
`LISTEN_FDNAMES`) сервис разбирает, какой fd какой (упр. 01, `my_listen_fd_by_name`).
Так один процесс обслуживает и публичный TCP, и локальный UNIX-сокет администрирования
— оба от systemd, оба без `bind` в коде.

### 7.7 Локальное тестирование без установки юнита

Отлаживать `sd_listen_fds` (упр. 01) можно **без** systemd — утилитой
`systemd-socket-activate`, которая имитирует активатор:

```sh
# Откроет сокет на 8080 и запустит ./myapp, передав его как fd 3 с LISTEN_FDS=1:
systemd-socket-activate -l 8080 --fdname=http ./myapp
# В другом терминале: curl localhost:8080 → активирует и проверит твой sd_listen_fds.
```

Так же проверяется fallback: запусти `./myapp` **без** активатора — `LISTEN_FDS` не
задан, твой `my_listen_fds` вернёт 0, и код пойдёт по ветке `make_listener` (сам
`bind`). Сервис обязан работать **обоими** способами — под systemd и при ручном
запуске.

---

## 8. Watchdog

Упражнение `03-watchdog`. Сервис может **не упасть, но зависнуть** — event-loop
застрял, дедлок, бесконечный цикл. Снаружи процесс «жив», а запросы не обслуживает —
худшая авария. Watchdog systemd ловит именно это.

### 8.1 Как работает

В юните: `WatchdogSec=30s`. systemd передаёт сервису **`WATCHDOG_USEC`** (период в
микросекундах) и **`WATCHDOG_PID`**. Сервис обязан **периодически** слать
`sd_notify("WATCHDOG=1")`. Не прислал в срок → systemd считает сервис повисшим и
действует по `Restart=` (обычно убивает и перезапускает).

```ini
[Service]
Type=notify
WatchdogSec=30s
Restart=on-watchdog          # перезапустить при срабатывании watchdog
ExecStart=/usr/local/bin/myapp
```

### 8.2 Канонический интервал — половина периода

Сервис шлёт `WATCHDOG=1` **каждые `WATCHDOG_USEC / 2`**, а не раз в полный период.
Половина — запас на джиттер планировщика/сети: даже пропустив один пинг из-за
всплеска нагрузки, успеешь до дедлайна. Это логика пейсера из упражнения:

```cpp
// 03-watchdog: enabled ⇔ WATCHDOG_USEC>0 и (WATCHDOG_PID пуст или == нашему);
//              interval = WATCHDOG_USEC/2; should_ping: первый раз — сразу, далее — раз в interval.
Wd* wd = wd_create(getpid(), getenv("WATCHDOG_USEC"), getenv("WATCHDOG_PID"));
// в event-loop на каждой итерации (или по timerfd с периодом interval, C4):
if (wd_should_ping(wd, now_usec())) my_sd_notify("WATCHDOG=1\n");
```

### 8.3 Куда вешать пинг в реальном loop

Важно: пинг должен идти **из основного event-loop**, чтобы он отражал **реальную
живость** обработки. Если повесить пинг на отдельный поток, который пингует всегда —
watchdog бесполезен (зависший loop не заметят). Правильно — пинговать **там же**, где
крутится обработка: по `timerfd` с периодом `interval` (C4) или проверкой на каждой
итерации `epoll_wait`. Тогда зависший обработчик **не успеет** пингнуть, и systemd
перезапустит сервис.

### 8.4 Аппаратный watchdog

systemd умеет связать software-watchdog сервисов с **аппаратным** watchdog'ом
системы (`RuntimeWatchdogSec=` в `/etc/systemd/system.conf`): если зависнет сам
systemd/ядро — железо перезагрузит машину. Так строят надёжные встраиваемые/
серверные системы: сервис → systemd → железо, цепочка «живости» до самого низа.

### 8.5 Что делает systemd при срабатывании watchdog

Пропущенный дедлайн watchdog — это для systemd **аварийное** завершение сервиса
(не штатное). Дальше:

- **`WatchdogSignal=`** — каким сигналом убить повисший сервис (по умолчанию
  `SIGABRT` — он даёт **core dump**, Ф4, чтобы можно было разобрать, **где** завис).
- затем действует **`Restart=`**: `on-watchdog` или `on-failure`/`always`
  перезапустят сервис; с `StartLimitBurst` (§15) — без бесконечного флапа.

```ini
[Service]
WatchdogSec=30s
WatchdogSignal=SIGABRT        # core dump повисшего — для post-mortem
Restart=on-watchdog
```

Тонкость про **самопроверку**: пинг `WATCHDOG=1` должен подтверждать, что сервис
делает **полезную** работу, а не просто «таймер тикает». Поэтому продвинутый
watchdog пингует только если **прошли** ключевые этапы цикла (обработали события,
очередь не застряла). Иначе зависший в `epoll_wait` навсегда (но «живой») loop будет
исправно пинговать — и watchdog бесполезен. Связывай пинг с **прогрессом**, а не с
самим фактом итерации.

---

## 9. PID-файлы vs cgroup-трекинг

Один из вопросов трека: почему systemd отказался от PID-файлов в пользу cgroups.

### 9.1 Проблема PID-файлов

Классический способ «найти процессы сервиса» — PID-файл (`/run/myapp.pid` с числом).
Его беды:
- **Гонка создания/чтения** — между стартом процесса, записью PID-файла и его
  чтением init-скриптом есть окна.
- **Протухание (stale).** Процесс упал/убит `SIGKILL` — PID-файл **остался**. Хуже:
  PID **переиспользован** другим процессом → init-скрипт пошлёт сигнал **не тому**
  (C3/C4: та же гонка переиспользования PID, что решает `pidfd`).
- **Неполнота.** PID-файл знает про **один** процесс. А сервис мог нафоркать
  воркеров/потомков — их PID-файл не отслеживает, и при остановке часть процессов
  **утечёт** (зомби/осиротевшие).

### 9.2 Решение systemd: cgroup на сервис

systemd помещает **все** процессы сервиса в свою **control group** (cgroup) —
иерархическую группу процессов ядра. Cgroup — это **надёжная, отслеживаемая ядром**
принадлежность: какой бы процесс сервис ни форкнул, он **остаётся** в cgroup
сервиса (потомки наследуют cgroup). Следствия:

- systemd **точно** знает **все** процессы сервиса (`systemctl status` показывает
  дерево) — без PID-файла и без гадания;
- остановка убивает **весь** cgroup (`KillMode=control-group` по умолчанию) — ни
  один потомок не утечёт;
- нет «протухшего» состояния: cgroup существует ровно пока есть процессы;
- бонусом — **лимиты ресурсов** на сервис (CPU, память, I/O) через тот же cgroup
  (`MemoryMax=`, `CPUQuota=`).

```
PID-файл:   /run/myapp.pid = 1234   ← один PID, может протухнуть, не видит детей
cgroup:     myapp.service/
              ├─ 1234 myapp (главный)
              ├─ 1235 myapp (воркер)
              └─ 1236 myapp (воркер)   ← ВСЕ процессы, отслежены ядром, убьются вместе
```

### 9.3 `KillMode` — как останавливают

`KillMode=` управляет остановкой:
- `control-group` (по умолчанию) — `SIGTERM` **всем** процессам cgroup, потом
  `SIGKILL` по таймауту;
- `mixed` — `SIGTERM` главному, `SIGKILL` всем (полезно, если главный сам управляет
  потомками);
- `process` — только главному (легко оставить сирот — осторожно).

Вывод: cgroup-трекинг устранил целый класс багов PID-файлов (протухание,
неполнота, гонки переиспользования) и дал лимиты ресурсов «бесплатно». Поэтому
new-style сервис **не пишет PID-файл** вообще.

### 9.4 cgroups v2 под капотом

**cgroup** (control group) — механизм ядра для группировки процессов и применения
к группе **контроллеров** (ограничителей/учётчиков ресурсов). В **cgroups v2**
(единая иерархия, вытеснившая v1) это дерево каталогов в `/sys/fs/cgroup/`:

```
/sys/fs/cgroup/
  system.slice/
    myapp.service/
      cgroup.procs        ← PID всех процессов сервиса (ядро ведёт само)
      memory.max          ← лимит памяти
      memory.current      ← текущее потребление
      cpu.max             ← квота CPU
      io.max              ← лимит block I/O
      cgroup.events       ← populated 0/1 (есть ли живые процессы)
```

Ключевые факты:
- Процесс **всегда** в ровно одной cgroup; потомок **наследует** cgroup родителя
  (вот почему `fork` не «убегает» из-под трекинга, §9.2).
- Контроллеры (`memory`, `cpu`, `io`, `pids`) **включаются** на поддереве через
  `cgroup.subtree_control`. Лимит ниже по дереву не может превысить лимит выше
  (иерархичность).
- `cgroup.events` → `populated` даёт systemd точный сигнал «сервис опустел» (все
  процессы вышли) — без опроса PID.

systemd организует cgroup-дерево в **slices** (`.slice`): `system.slice` (системные
сервисы), `user.slice` (сессии пользователей), `machine.slice` (контейнеры/VM). Это
позволяет задавать лимиты на **группу** сервисов, а не только на один.

### 9.5 Лимиты ресурсов на сервис

Раз сервис уже в cgroup, лимиты — одна строка юнита (через те же файлы cgroup):

```ini
[Service]
MemoryMax=512M           # OOM-kill при превышении (memory.max)
MemoryHigh=400M          # мягкий порог: троттлинг до OOM (memory.high)
CPUQuota=200%            # не более 2 ядер (cpu.max)
CPUWeight=100            # доля CPU при конкуренции (относительный вес)
TasksMax=4096            # лимит числа процессов/потоков (pids.max) — анти-fork-бомба
IOReadBandwidthMax=/dev/sda 50M   # лимит чтения с диска
```

`MemoryMax` надёжнее, чем `RLIMIT_AS` в коде: это **на весь сервис** (все процессы),
и превышение даёт предсказуемый OOM-kill **внутри** cgroup сервиса, не задевая
систему. `TasksMax` — дешёвая защита от fork-бомбы в сервисе. Всё это — тот же
cgroup-механизм, что используют Docker/Kubernetes для лимитов контейнеров (§14.6).

### 9.6 Делегирование cgroup

Если сервис сам управляет под-процессами с ресурсными лимитами (контейнерный
рантайм, менеджер воркеров), systemd может **делегировать** ему поддерево cgroup
(`Delegate=yes`) — тогда сервис создаёт свои под-cgroup внутри своей и сам ими
рулит, не конфликтуя с systemd. Так работают `docker`/`podman`/`systemd-nspawn`
под systemd.

---

## 10. Журналирование через journald

### 10.1 Просто пиши в stdout/stderr

Под systemd сервис **не** открывает свои лог-файлы и **не** ходит в syslog — он
просто пишет в **stdout/stderr**, а **journald** их ловит и кладёт в
структурированный журнал. Никакой ротации, никаких прав на `/var/log` — это забота
journald.

```cpp
// Под systemd это попадёт в журнал сервиса (journalctl -u myapp):
std::fprintf(stderr, "connection from %s accepted\n", peer);
std::printf("processed %d requests\n", count);
```

`journalctl -u myapp -f` — живой хвост; `journalctl -u myapp --since "1 hour ago"` —
история; журнал индексирован по юниту, времени, PID, приоритету — без `grep` по
файлам.

### 10.2 Уровни важности: префикс `<N>`

journald понимает **syslog-уровни** через префикс `<N>` в начале строки (если в
юните `SyslogLevelPrefix=yes`, по умолчанию включено):

```cpp
// Уровни (syslog): 0 emerg .. 3 err, 4 warning, 6 info, 7 debug
std::fprintf(stderr, "<3>fatal: cannot bind socket\n");   // ERR — красным в journalctl
std::fprintf(stderr, "<4>config reloaded with warnings\n"); // WARNING
std::printf("<6>accepting connections\n");                  // INFO
```

`journalctl -p err -u myapp` отфильтрует только ошибки — потому что уровень
**структурно** известен журналу, а не «зашит» в текст.

### 10.3 Структурированные поля (`sd_journal_send`)

Для богатых записей — `sd_journal_send` (libsystemd) с произвольными полями
`KEY=VALUE`:

```cpp
// sd_journal_send("MESSAGE=request failed", "PRIORITY=3",
//                 "REQUEST_ID=%s", id, "PEER=%s", peer, "LATENCY_MS=%d", ms, NULL);
```

Поля попадают в журнал как индексируемые: `journalctl REQUEST_ID=abc123` найдёт все
записи по запросу. Это «structured logging» из коробки — без своего JSON-логгера.

### 10.4 Почему не свои файлы

- **Ротация/диск** — journald сам ограничивает размер, ротирует, чистит (`SystemMaxUse=`).
- **Единый формат** — все сервисы в одном журнале, коррелируются по времени.
- **Права** — не нужен доступ к `/var/log`, не нужен `logrotate`.
- **Надёжность при крэше** — последние строки не теряются в буфере stdio твоего
  процесса (хотя для критичных — `setvbuf(stderr, ..., _IONBF)` или `fflush`).

> **Грабли:** stdout **буферизуется** (построчно на терминале, **блоками** при пайпе
> в journald). Под systemd твой `printf` может «зависнуть» в буфере и не появиться в
> журнале до флаша/выхода. Лечение: `setvbuf(stdout, NULL, _IOLBF, 0)` (line-buffered)
> или `std::endl`/`fflush`, либо писать в `stderr` (часто небуферизован).

### 10.5 journald под капотом

- **Транспорт.** journald даёт сервисам сокет `/run/systemd/journal/stdout` (туда
  направлены их stdout/stderr) и `/dev/log` (совместимость с классическим syslog),
  и `/run/systemd/journal/socket` для `sd_journal_send`. Сообщения — это набор
  бинарных полей `KEY=VALUE`.
- **Persistent vs volatile.** По умолчанию журнал в `/run/log/journal` (tmpfs,
  **теряется** при перезагрузке). Создай `/var/log/journal` (`Storage=persistent` в
  `journald.conf`) — журнал переживёт ребут. Частая причина «после ребута логов
  нет».
- **Rate limiting.** journald **отбрасывает** лог при флуде (`RateLimitIntervalSec`/
  `RateLimitBurst`) — сервис, спамящий тысячи строк/с, увидит «Suppressed N
  messages». Не полагайся на журнал как на надёжную очередь событий.
- **Forward.** journald может пересылать в классический syslog (`ForwardToSyslog=`),
  на консоль, в kmsg — для интеграции со старым стеком сбора логов.
- **Корреляция.** Каждая запись несёт автоматически `_PID`, `_UID`, `_SYSTEMD_UNIT`,
  `_BOOT_ID`, `_HOSTNAME`, `__REALTIME_TIMESTAMP` — поэтому `journalctl -u myapp
  _PID=1234` или фильтр по загрузке (`-b`) работают без парсинга текста.

### 10.6 `sd_journal_send` — структурно из кода

Для богатых записей — поля произвольной формы, индексируемые журналом:

```cpp
// sd_journal_send(
//   "MESSAGE=request failed",          // обязательное человекочитаемое
//   "PRIORITY=%d", 3,                  // syslog-уровень (err)
//   "MESSAGE_ID=" SD_ID128_..,         // стабильный UUID типа события (для аналитики)
//   "REQUEST_ID=%s", req_id,           // свои поля — индексируются
//   "PEER=%s", peer,
//   "LATENCY_MS=%d", ms,
//   NULL);
```

`MESSAGE_ID` (128-битный UUID конкретного **типа** события) — мощная штука: позволяет
`journalctl MESSAGE_ID=<uuid>` найти **все** случаи этого события по всем сервисам и
вешать на него документацию/алерты. Это «structured logging» уровня инфраструктуры —
без своего JSON-логгера и ELK-парсинга текста. Минус — зависимость от libsystemd;
для портируемости многие пишут поля в JSON в stdout, а journald индексирует строку.

### 10.7 Практики логирования сервиса

- **Не логируй на горячем пути по строке на запрос** — это и нагрузка, и
  rate-limit journald (§10.5) отбросит часть. Агрегируй (счётчики, периодическая
  сводка по `timerfd`, C4) или семплируй.
- **Уровни осмысленно:** `<3>`(err) — то, что требует внимания человека; `<4>`
  (warning) — аномалии без сбоя; `<6>`(info) — ключевые события (старт, reload,
  shutdown); `<7>`(debug) — только под `LogLevelMax=debug`. Шум на `info` прячет
  важное.
- **Никаких секретов в логах** — пароли, токены, тела запросов с персональными
  данными. Журнал читается шире, чем кажется.
- **Идемпотентные сообщения старта/остановки:** «accepting on :8080», «reloaded
  config», «draining N connections», «stopped» — по ним эксплуатация читает
  жизненный цикл без доступа к коду.
- **Корреляция:** клади `REQUEST_ID`/трейс-идентификатор в поля (§10.6) — тогда
  один запрос прослеживается через все строки. Это вход в распределённую
  трассировку (выходит за рамки модуля, но журнал — её фундамент).
- **Флаш на критичном:** перед `abort()`/выходом по фатальной ошибке — `fflush`
  (или небуферизованный `stderr`), иначе последние строки (самые важные!) потеряются
  в буфере stdio (§10.4).

Главный принцип: журнал сервиса — это **операционный интерфейс**, а не свалка
`printf`-ов. По нему дежурный инженер в 3 часа ночи должен понять, что происходит, —
проектируй сообщения под это.

---

## 11. Graceful shutdown и reload (drain)

Упражнение `04-drain-tracker`. Это «не потерять in-flight запросы» из трека —
синтез C4 (signalfd, shutdown-FSM) и учёта активных запросов.

### 11.1 Как systemd останавливает

`systemctl stop myapp` (или рестарт): systemd шлёт **`SIGTERM`** (`KillSignal=`,
по умолчанию SIGTERM) **всем** процессам cgroup, ждёт **`TimeoutStopSec`** (по
умолчанию 90 c), и если сервис не вышел — шлёт **`SIGKILL`**. Сервис обязан
**успеть** корректно остановиться в это окно.

```
systemctl stop → SIGTERM ──(TimeoutStopSec)──> если жив → SIGKILL
сервис:          drain in-flight, ответить, exit  ← успеть здесь
```

### 11.2 Что делает сервис по `SIGTERM` (drain)

Корректная остановка (через `signalfd` из C4):
1. `sd_notify("STOPPING=1")` — «начал штатную остановку».
2. **Перестать принимать** новые соединения (снять listen-сокет с `epoll`, C4 §18).
3. **Доделать активные** (in-flight) — дописать ответы.
4. Когда активных не осталось → **выйти** (или по дедлайну `timerfd` — форс).

Учёт in-flight — ровно упражнение `04-drain-tracker`:

```cpp
// admit() при приёме запроса (active++, только в RUNNING), finish() при завершении;
// begin_shutdown() → DRAINING (новые admit отклоняются); STOPPED когда active==0; timeout() форс.
if (drain_admit(d)) { handle_request(); drain_finish(d); }  // обычный запрос
// по SIGTERM:
drain_begin_shutdown(d);            // перестали принимать, доделываем
// когда drain_state(d)==STOPPED:   все доделаны (или TIMEOUT) → выходим
```

**Отказ новых в DRAINING** — это снятие listen-сокета с epoll (или ответ `503
Connection: close`). **Переход в STOPPED по `active==0`** — гарантия, что мы отдали
**все** ответы: ни один in-flight не потерян. **Дедлайн** (`timerfd` + `drain_timeout`)
— страховка от зависших запросов: не ждём вечно.

### 11.3 Graceful **reload** (`SIGHUP`) vs **shutdown** (`SIGTERM`)

- **Reload** (`SIGHUP`, через `ExecReload=/bin/kill -HUP $MAINPID`) — перечитать
  конфиг **без** остановки: сервис продолжает обслуживать, просто применяет новые
  настройки. Без обрыва соединений. Шлёт `RELOADING=1` → `READY=1` вокруг.
- **Shutdown** (`SIGTERM`) — drain и выход (§11.2).

Разница: reload **сохраняет** соединения и процесс; shutdown их **сливает** и
выходит. Оба — graceful (не рвут in-flight), но reload не прекращает приём.

### 11.4 Согласование с `TimeoutStopSec`

Дедлайн drain в сервисе (`timerfd`) должен быть **меньше** `TimeoutStopSec` юнита,
иначе systemd пошлёт `SIGKILL` **раньше**, чем твой graceful успеет, — и часть
in-flight всё же оборвётся. Правило: `внутренний дедлайн < TimeoutStopSec`. Например,
drain-дедлайн 25 c при `TimeoutStopSec=30s`.

### 11.5 Lame-duck: стратегии слива соединений

«Перестать принимать» можно по-разному, и выбор влияет на корректность:

- **Снять listen-сокет с epoll** (C4 §18) — новые `accept` просто не происходят;
  уже принятые соединения дослуживаются. Простейший и обычно правильный способ для
  socket activation: сокет остаётся у systemd, новые подключения ждут в backlog
  (их подхватит новый экземпляр).
- **`shutdown(listen_fd, SHUT_RD)` / `close`** — активнее отказывать; но при socket
  activation закрывать **чужой** (systemd) сокет нельзя.
- **Lame-duck для HTTP keep-alive:** на соединениях, которые держатся (keep-alive),
  при drain'е добавлять в ответ `Connection: close` — клиент не пошлёт следующий
  запрос в это соединение, и оно естественно закроется. Без этого keep-alive-клиент
  будет «висеть» до таймаута, удерживая drain.
- **Дедлайн (`timerfd`)** — поверх всего: зависшие/медленные соединения **форсированно**
  закрыть по дедлайну (упр. 04, `drain_timeout`), чтобы не ждать вечно.

Полный протокол остановки (соединяя §11.2, §12.6, §12.7):

```
SIGTERM → STOPPING=1 → /healthz отдаёт 503 (LB выводит из ротации, §12.7)
        → перестать accept (снять listen с epoll)
        → keep-alive ответы с Connection: close (lame-duck)
        → ждать active==0 (drain-трекер) ИЛИ дедлайн < TimeoutStopSec
        → exit 0
```

Это и есть «не потерять in-flight» во всей полноте: внешний трафик уведён, новые не
принимаются, старые доделаны, зависшие закрыты по дедлайну.

### 11.6 Настройка сигналов остановки

Какой сигнал systemd шлёт на остановку — настраивается, и это важно согласовать с
тем, что слушает сервис (C4):

```ini
[Service]
KillSignal=SIGTERM          # основной сигнал остановки (по умолчанию SIGTERM)
RestartKillSignal=SIGTERM   # сигнал при рестарте (можно отличать от stop)
FinalKillSignal=SIGKILL     # добивающий по TimeoutStopSec (по умолчанию SIGKILL)
SendSIGHUP=no               # слать ли дополнительно SIGHUP при остановке
SendSIGKILL=yes             # слать ли финальный SIGKILL вообще
```

Некоторые серверы исторически используют **нестандартные** сигналы для управления
(nginx: `SIGQUIT` — graceful, `SIGWINCH` — закрыть воркеров, `SIGUSR2` — обновить
бинарь). Для них `KillSignal=` подгоняют под ожидания приложения. Но для своего
сервиса проще всего: **`SIGTERM` = graceful drain** (§11.2), `SIGHUP` = reload
(§11.3), и не выдумывать — это то, чего ожидают и systemd, и операторы по умолчанию.

`KillMode=` (§9.3) определяет, **кому** из cgroup уйдёт сигнал; `KillSignal=` —
**какой**. Вместе они задают весь протокол остановки на стороне systemd, а сервис
обязан на `KillSignal` корректно среагировать (drain), уложившись в
`TimeoutStopSec` до `FinalKillSignal`.

---

## 12. Zero-downtime reload: передача сокета

Вопрос ЭКСПЕРТ трека: как обновить сервис **без простоя** и **без потери in-flight**.
Это сложнее, чем graceful shutdown: нужно, чтобы **новый** экземпляр принимал, пока
**старый** дренирует.

### 12.1 Проблема

Наивный рестарт = stop (drain + exit) → start. Между exit старого и listen нового
есть **окно**, когда порт **никто не слушает** → `ECONNREFUSED`. Zero-downtime
требует, чтобы слушающий сокет **не закрывался** ни на миг.

### 12.2 Подход А: socket activation (рекомендуется)

Сокет принадлежит **systemd** (§7), а не сервису. При рестарте сервиса сокет
**остаётся открытым** у systemd; новый экземпляр получает **тот же** сокет по
наследству. Старый дренирует свои соединения и выходит; новый уже принимает на **том
же** сокете. Порт не «падает», backlog клиентов сохранён ядром.

```
старый myapp (drain in-flight) ──exit──┐
socket (у systemd) ────────────────────┼──> новый myapp (тот же fd 3, accept)
                                        ┘   порт открыт ВСЁ время
```

Это самый чистый zero-downtime: тебе почти ничего не надо в коде — просто бери сокет
из `sd_listen_fds` (упр. 01), а не делай `bind` сам.

### 12.3 Подход Б: `SO_REUSEPORT`

Несколько экземпляров **независимо** делают `bind` на тот же порт с `SO_REUSEPORT`
(C2 §8.2) — ядро распределяет входящие между ними. Деплой: поднять **новый**
экземпляр (он забиндился рядом), дать **старому** команду drain и выйти. В переходный
момент порт слушают оба — простоя нет. Минус: нужна внешняя координация (кто когда
выходит), и распределение по соединениям, а не по сокету.

### 12.4 Подход В: master/worker с передачей fd (nginx-стиль)

Классика nginx/unicorn: **master** держит listen-сокет, форкает **worker**'ов. При
reload master форкает **новых** worker'ов (наследуют сокет), шлёт **старым** «graceful
quit» — те дослуживают и выходят. Сокет всё время у master'а. Передача нового бинаря
— через `exec` master'а с наследованием fd (или `SCM_RIGHTS`, C3). Гибко, но весь
оркестр пишешь сам.

### 12.5 Сводка

| Подход | Кто держит сокет | Кода | Когда |
|--------|------------------|------|-------|
| **Socket activation** | systemd | минимум | под systemd — **дефолт** |
| **SO_REUSEPORT** | каждый экземпляр | средне | без systemd, K8s rolling |
| **master/worker + fd** | master-процесс | много | nginx-класс, полный контроль |

Во всех трёх ключ один: **слушающий сокет переживает смену рабочих процессов**.
Различие — кто его держит. Под systemd — socket activation, и большая часть работы
сделана за тебя.

### 12.6 Параллель с Kubernetes

Та же модель, что у systemd, повторяется в Kubernetes — полезно видеть соответствие:

| systemd | Kubernetes | Смысл |
|---------|-----------|-------|
| `SIGTERM` от `systemctl stop` | `SIGTERM` от kubelet | сигнал «начни остановку» |
| `TimeoutStopSec=` | `terminationGracePeriodSeconds` | окно до `SIGKILL` |
| `Type=notify`/`READY=1` | `readinessProbe` | «можно слать трафик» |
| watchdog | `livenessProbe` | «жив или завис» (§6.7) |
| socket activation | Service/Endpoints + rolling update | сокет/трафик переживает смену подов |
| `ExecStop=`/drain | `preStop` hook | дать доделать перед `SIGTERM` |

Важнейший общий паттерн **graceful drain в оркестраторе**: при выводе пода из
ротации есть **гонка** — балансировщик ещё шлёт трафик, пока узнаёт об удалении
endpoint. Поэтому правильная остановка: (1) пометить readiness=false (перестать
получать **новый** трафик), (2) подождать, пока балансировщик это увидит (preStop
`sleep`), (3) drain in-flight (упр. 04), (4) выйти. Без шага (2) последние запросы
прилетят в уже закрывающийся под → 5xx. Это та же логика, что drain-трекер, но с
учётом задержки распространения в балансировщике.

### 12.7 Интеграция с балансировщиком: health check

Балансировщик/прокси (nginx, HAProxy, облачный LB) опрашивает **health-эндпоинт**
сервиса (`GET /healthz`). Для zero-downtime сервис должен на старте drain'а
**возвращать `503`** на `/healthz` (readiness=false) — балансировщик выведет его из
ротации **до** того, как сервис перестанет принимать. Последовательность:
`/healthz → 503` → LB убрал из ротации (несколько секунд) → drain in-flight → exit.
Так связываются readiness (§6.7), drain (§11) и внешний трафик в единый
zero-downtime-протокол.

---

## 13. FDSTORE — хранилище дескрипторов

Иногда нужно пережить рестарт **с состоянием** — не только слушающий сокет, но и
**активные** соединения или прогретые дескрипторы. systemd даёт **fd store**.

### 13.1 Как работает

Сервис шлёт `sd_notify("FDSTORE=1")` **вместе** с дескриптором (через `SCM_RIGHTS`,
C3) — systemd **хранит** этот fd у себя. При рестарте сервиса сохранённые fd
**возвращаются** ему как обычные переданные дескрипторы (через `sd_listen_fds`, §7,
с именами из `FDNAME=`). Так сервис может, например, сохранить **установленные
соединения** через рестарт.

```ini
[Service]
FileDescriptorStoreMax=64        # сколько fd systemd готов хранить за сервис
```

```cpp
// сохранить соединение перед рестартом:
//   sd_notify_with_fds("FDSTORE=1\nFDNAME=conn", &conn_fd, 1);
// после рестарта получить обратно через sd_listen_fds_with_names (по имени "conn").
```

### 13.2 Когда нужно

- **Сохранить активные соединения** через апдейт бинаря (редко, сложно — нужно
  сериализовать состояние сессии).
- **Дорогие дескрипторы** (открытые устройства, прогретые mmap), которые жалко
  переоткрывать.

Это продвинутая техника; для большинства сервисов хватает socket activation (§12.2)
— слушающий сокет переживает рестарт, а in-flight дренируются. FDSTORE — для случаев,
когда нельзя терять **сами** соединения.

---

## 14. Sandboxing юнита: least privilege

Сервис должен иметь **минимум** прав. systemd даёт это **декларативно** в юните —
без единой строки кода. Это надёжнее ручного `setuid`/`chroot` (которые легко
написать с ошибкой).

### 14.1 Пользователь и базовая изоляция

```ini
[Service]
User=myapp
Group=myapp
DynamicUser=yes              # systemd создаёт временного пользователя на время работы
NoNewPrivileges=yes          # запрет повышения привилегий (setuid-бинари бессильны)
```

`DynamicUser=yes` — мощно: пользователь существует **только** пока сервис работает,
файлы создаются с его UID, никаких «вечных» сервисных аккаунтов.

### 14.2 Изоляция файловой системы

```ini
ProtectSystem=strict         # вся ФС read-only, кроме явно разрешённого
ProtectHome=yes              # /home, /root, /run/user недоступны
PrivateTmp=yes               # свой /tmp (изолирован от других, C3 namespaces)
ReadWritePaths=/var/lib/myapp # что можно писать
StateDirectory=myapp         # systemd создаст /var/lib/myapp с нужными правами
```

### 14.3 Ограничение привилегий и сисколлов

```ini
CapabilityBoundingSet=CAP_NET_BIND_SERVICE   # только биндить порты <1024, остальное снято
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX  # какие сокеты можно
SystemCallFilter=@system-service             # seccomp-allowlist (Ф3/контейнеры)
SystemCallFilter=~@privileged @mount         # снять опасные классы сисколлов
MemoryDenyWriteExecute=yes                   # W^X: нет страниц rwx (анти-эксплойт)
RestrictNamespaces=yes
LockPersonality=yes
```

Это — те же механизмы, что у контейнеров (namespaces, seccomp, capabilities из
Ф3/C3), но включаемые **строкой в юните**. Принцип наименьших привилегий через
конфиг: сервис, которому нужен только TCP и запись в один каталог, не должен иметь
доступа ни к чему ещё.

### 14.4 Проверка sandbox'а

`systemd-analyze security myapp.service` ставит сервису **оценку безопасности**
(exposure level) и перечисляет, какие защиты включены/выключены, — отличный чеклист
для ужесточения юнита.

### 14.5 systemd credentials — секреты без переменных окружения

Секреты (ключи, пароли, токены) **нельзя** класть в `Environment=` — они видны в
`systemctl show` и `/proc/<pid>/environ` любому, кто может читать. systemd даёт
**credentials**: секрет подаётся сервису как файл в приватном tmpfs-каталоге
`$CREDENTIALS_DIRECTORY`, доступном **только** этому сервису:

```ini
[Service]
LoadCredential=db-password:/etc/myapp/db.secret    # из файла
# SetCredential=api-key:literalvalue               # прямо в юните (для не-секретного)
# LoadCredentialEncrypted=token:/etc/myapp/token.enc  # зашифрованный (TPM/host key)
```

```cpp
// В коде: читаем секрет из $CREDENTIALS_DIRECTORY/db-password
const char* dir = getenv("CREDENTIALS_DIRECTORY");
// open(dir + "/db-password") — файл в tmpfs, виден только нам, исчезает при остановке
```

Преимущества: секрет **не** в окружении, **не** в образе/репозитории, недоступен
другим процессам и подчищается при остановке. `LoadCredentialEncrypted` шифрует
секрет ключом хоста/TPM — он бесполезен на другой машине. Это правильная замена
«пароль в `Environment=`» и самодельным секрет-файлам.

### 14.6 Сервис vs контейнер: одни механизмы

Песочница юнита (§14) и контейнер (Docker/Podman) стоят на **одних и тех же**
примитивах ядра (Ф3, C3): namespaces (`PrivateTmp`/`ProtectSystem` = mount ns,
`PrivateNetwork` = net ns), cgroups (лимиты, §9.5), seccomp (`SystemCallFilter`),
capabilities (`CapabilityBoundingSet`). Разница — в **упаковке**: контейнер несёт
свою ФС-образ, сервис использует ФС хоста. По изоляции хорошо настроенный
systemd-юнит (`DynamicUser` + `ProtectSystem=strict` + `SystemCallFilter` +
`PrivateNetwork`) **не уступает** контейнеру. Поэтому «нужен ли Docker для одного
сервиса?» часто имеет ответ «нет — достаточно жёсткого юнита». Понимание, что под
капотом это те же namespaces/cgroups/seccomp, снимает магию с обоих.

---

## 15. Restart-политики и flapping

### 15.1 Когда перезапускать

```ini
Restart=on-failure        # перезапуск при ненулевом коде/сигнале/watchdog (НЕ при штатном exit 0)
RestartSec=2              # пауза перед перезапуском
```

Варианты `Restart=`: `no` (никогда), `on-success`, `on-failure` (типичный выбор),
`on-watchdog` (только по watchdog, §8), `on-abnormal`, `always` (даже при чистом
выходе — для сервисов, которые «должны жить всегда»).

### 15.2 Защита от штормов (flapping)

Если сервис падает сразу после старта, бесконечный рестарт = шторм. systemd ограничивает:

```ini
StartLimitIntervalSec=60     # окно
StartLimitBurst=5            # не больше 5 стартов за окно; иначе сервис → failed
```

Превысил — systemd **перестаёт** перезапускать и помечает сервис `failed`
(вмешается человек/алерт). Это спасает систему от «крутящегося» битого сервиса,
жгущего CPU и заваливающего журнал.

### 15.3 Связь с watchdog и graceful

Полная картина самовосстановления: сервис **штатно** дренирует по `SIGTERM` (§11),
**перезапускается** при падении (`Restart=on-failure`), и **перезапускается при
зависании** (`WatchdogSec` + `Restart=on-watchdog`, §8), но **не флапает** бесконечно
(`StartLimitBurst`). Это и есть «боевой» сервис: переживает сбои, но не маскирует
системную поломку.

### 15.4 Альтернативные супервизоры и почему systemd

systemd — не единственный супервизор; полезно знать ландшафт:

| Супервизор | Подход | Где встречается |
|-----------|--------|-----------------|
| **systemd** | cgroup-трекинг, socket activation, sd_notify, sandbox | дефолт в большинстве дистрибутивов |
| **runit / s6** | минимализм, процесс-надзор, `./run`-скрипты | embedded, Void Linux, контейнеры (s6-overlay) |
| **supervisord** | Python, конфиг, без cgroup | приложения, легаси |
| **Docker/Kubernetes** | контейнерный рантайм + оркестратор | облако, микросервисы |
| **SysV init** | shell-скрипты, PID-файлы | очень старые системы |

systemd победил на серверах/десктопах за счёт cgroup-трекинга (§9), socket activation
(§7), декларативной песочницы (§14) и параллельной загрузки. Минималисты (runit/s6)
живут в embedded и контейнерах, где systemd «тяжёл». В контейнерах часто **вообще
нет** супервизора внутри — один процесс как PID 1, а надзор/рестарт делает
оркестратор снаружи (§12.6). Но протокол `sd_notify`/socket activation поддерживают
и другие (s6 умеет readiness-нотификацию) — это де-факто стандарт.

> **Грабли PID 1 в контейнере:** процесс с PID 1 **не** получает дефолтных действий
> сигналов и **обязан** сам пожинать зомби (Ф3) — иначе они копятся. Поэтому в
> контейнерах ставят тонкий init (`tini`, `dumb-init`) как PID 1, который reap'ает
> детей и пробрасывает сигналы. Это та же тема reaping из Ф3/C4, всплывшая в
> контейнерном контексте.

---

## 16. Сборка: сервер C2/C4 → сервис

Соберём всё: event-loop из C2/C4 превращается в полноценный systemd-сервис.

### 16.1 Код сервиса (скелет)

```cpp
int main() {
    // 1) Сокет — из socket activation (упр. 01), НЕ bind сами:
    int lfd;
    int nfds = my_listen_fds(getpid(), getenv("LISTEN_PID"), getenv("LISTEN_FDS"));
    if (nfds >= 1) lfd = SD_LISTEN_FDS_START;        // fd 3 — от systemd
    else           lfd = make_listener(8080);         // fallback: сами (для запуска вне systemd)

    // 2) Event-loop из C4: epoll + signalfd(SIGTERM/SIGHUP) + timerfd:
    int ep = setup_epoll(lfd);
    int sfd = setup_signalfd();                        // C4 §7
    Wd* wd = wd_create(getpid(), getenv("WATCHDOG_USEC"), getenv("WATCHDOG_PID")); // упр. 03
    Drain drain;                                       // упр. 04

    // 3) Готов!
    my_sd_notify("READY=1\nSTATUS=accepting\n");       // упр. 02; Type=notify

    for (;;) {
        int n = epoll_wait(ep, evs, MAXEV, /*timeout под watchdog*/ wd_poll_ms(wd));
        if (n < 0 && errno == EINTR) continue;
        if (wd_should_ping(wd, now_usec())) my_sd_notify("WATCHDOG=1\n");  // §8
        for (int i = 0; i < n; ++i) dispatch(evs[i], drain);              // sfd/lfd/conn
        if (drain_state(&drain) == STOPPED) break;     // graceful завершён (§11)
    }
    my_sd_notify("STOPPING=1\n");
    return 0;
}
// при SIGTERM (из signalfd): my_sd_notify("STOPPING=1\n"); drain_begin_shutdown(&drain);
//                            снять lfd с epoll; вооружить timerfd на drain-дедлайн (< TimeoutStopSec)
// при SIGHUP:                my_sd_notify("RELOADING=1\nMONOTONIC_USEC=..\n"); reload(); my_sd_notify("READY=1\n");
```

### 16.2 Юнит-файлы

```ini
# myapp.socket — сокет открывает systemd (zero-downtime, §7, §12)
[Socket]
ListenStream=8080
FileDescriptorName=http
[Install]
WantedBy=sockets.target
```

```ini
# myapp.service
[Unit]
Description=My app
After=network-online.target
Wants=network-online.target
[Service]
Type=notify                       # §6: ждём READY=1
ExecStart=/usr/local/bin/myapp
ExecReload=/bin/kill -HUP $MAINPID
WatchdogSec=30s                   # §8
Restart=on-failure                # §15
RestartSec=2
TimeoutStopSec=30s                # §11.4 (drain-дедлайн в коде < этого)
# sandbox (§14):
User=myapp
DynamicUser=yes
ProtectSystem=strict
PrivateTmp=yes
NoNewPrivileges=yes
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
[Install]
WantedBy=multi-user.target
```

### 16.3 Развёртывание

```sh
sudo cp myapp /usr/local/bin/
sudo cp myapp.service myapp.socket /etc/systemd/system/
sudo systemctl daemon-reload          # перечитать юниты
sudo systemctl enable --now myapp.socket   # включить сокет (он запустит сервис по запросу)
systemctl status myapp                # увидеть STATUS= из sd_notify, дерево cgroup
journalctl -u myapp -f                # логи (§10)
sudo systemctl reload myapp           # SIGHUP — hot reload
sudo systemctl restart myapp          # zero-downtime, если через socket (§12)
```

### 16.4 Как сходятся все упражнения

- `01-listen-fds` — сокет приходит от systemd (socket activation, §7, §16.1).
- `02-sd-notify` — `READY=1`/`RELOADING`/`STOPPING`/`WATCHDOG=1` (§6, §8, §11).
- `03-watchdog` — пейсер `WATCHDOG=1` в loop (§8).
- `04-drain-tracker` — учёт in-flight для graceful drain (§11).

Все четыре — кирпичи одного сервиса из §16.1, поверх event-loop из C4. Капстоун
собирает их в боевой сервис.

### 16.5 План тестирования сервиса

Сервис проверяется не только юнит-тестами логики (упражнения), но и **поведением
под supervisor'ом**. Минимальный план приёмки:

| Свойство | Как проверить | Ожидаемо |
|----------|---------------|----------|
| Готовность | `systemctl start myapp; systemctl is-active` | `active` только после `READY=1` |
| Статус | `systemctl status myapp` | видно `STATUS=` из `sd_notify` и cgroup-дерево |
| Логи | `journalctl -u myapp` | строки сервиса с уровнями `<N>` |
| Reload | `systemctl reload myapp` под нагрузкой | конфиг применён, **0** обрывов |
| Graceful stop | `systemctl stop myapp` под нагрузкой (`wrk`) | in-flight доделаны, **0** 5xx |
| Дедлайн | сервис «завис» в обработчике > `TimeoutStopSec` | `SIGKILL` (видно в журнале) |
| Watchdog | искусственный `sleep(WatchdogSec*2)` в loop | systemd убил+перезапустил |
| Zero-downtime | `systemctl restart` под нагрузкой (socket activation) | **0** `ECONNREFUSED` |
| Flapping | сервис падает на старте | после `StartLimitBurst` → `failed`, не крутится |
| Sandbox | `systemd-analyze security myapp` | приличная оценка (exposure) |

Особенно важна проверка **zero-downtime**: запусти `wrk -d30s` и в середине сделай
`systemctl restart myapp` — счётчик ошибок `wrk` должен остаться **нулём** (сокет
держит systemd, backlog не потерян). Если есть обрывы — значит сервис делает `bind`
сам, а не берёт сокет из `sd_listen_fds` (§7, §12.2).

---

## 17. Инструменты и отладка

- **`systemctl`** — управление: `status` (состояние + cgroup-дерево + последние
  логи + `STATUS=`), `start/stop/restart/reload`, `enable/disable`, `cat myapp`
  (показать юнит), `show myapp` (все свойства), `list-units --failed`.
- **`journalctl`** — логи: `-u myapp` (по юниту), `-f` (хвост), `-p err` (по
  уровню), `--since`, `-o json` (структурно), `-b` (текущая загрузка), `_PID=`/
  поля (§10.3).
- **`systemd-analyze`** — `blame` (что тормозит загрузку), `critical-chain`,
  `security myapp` (оценка sandbox, §14.4), `verify myapp.service` (синтаксис юнита).
- **`systemd-run`** — запустить команду как временный сервис (отладить юнит-опции
  без файла): `systemd-run --property=WatchdogSec=5s -- /usr/local/bin/myapp`.
- **`systemctl show -p MainPID,ActiveState,SubState myapp`** — машиночитаемые
  свойства (для скриптов/мониторинга).
- **`busctl`** — посмотреть D-Bus (если `Type=dbus`).
- **`strace -f -p $(systemctl show -p MainPID --value myapp)`** — трассировать
  живой сервис (видеть `sendmsg` в NOTIFY_SOCKET, `accept` на fd 3).
- **`/run/systemd/notify`**, `$NOTIFY_SOCKET` — проверить, куда слать уведомления.
- **`systemd-socket-activate`** — тестовый активатор: запустить сервис под socket
  activation **вручную** (без установки юнита): `systemd-socket-activate -l 8080
  ./myapp` — отлаживать `sd_listen_fds` (упр. 01) локально.

### 17.1 Чеклист ревью сервиса

**Готовность и тип**
- [ ] `Type=notify` (или `exec`), не голый `simple` с гонкой готовности (§5.2).
- [ ] `sd_notify(READY=1)` шлётся **после** реальной готовности (bind/прогрев) (§6.5).
- [ ] `NotifyAccess=` верный, если уведомляет не главный процесс (§6.4).

**Демонизация**
- [ ] **Нет** double-fork/`setsid`/перенаправления в `/dev/null` (§3.2).
- [ ] **Нет** своего PID-файла (cgroup-трекинг, §9).
- [ ] Логи в **stdout/stderr** с уровнями `<N>`, не в свои файлы (§10).

**Сокет и zero-downtime**
- [ ] Сокет — из `sd_listen_fds` (socket activation), есть fallback на `bind` для
      запуска вне systemd (§7, §16.1).
- [ ] Сверка `LISTEN_PID == getpid()` (§7.2).

**Остановка**
- [ ] `SIGTERM` → drain in-flight (упр. 04), не обрыв; `sd_notify(STOPPING=1)` (§11).
- [ ] Внутренний drain-дедлайн **< `TimeoutStopSec`** (§11.4).
- [ ] `SIGHUP`/`ExecReload` → reload без обрыва соединений (§11.3).

**Здоровье и устойчивость**
- [ ] `WATCHDOG=1` пингуется **из основного loop** (упр. 03), `WatchdogSec=` задан (§8).
- [ ] `Restart=on-failure` + `StartLimitBurst` (нет флапа) (§15).

**Безопасность**
- [ ] `User=`/`DynamicUser=`, `NoNewPrivileges=`, `ProtectSystem=`, узкий
      `CapabilityBoundingSet=` (§14); проверено `systemd-analyze security`.

### 17.2 Галерея типичных багов

1. **`Type=simple` для сервиса с инициализацией** → зависимые стартуют до
   готовности, `ECONNREFUSED`. *Лечение:* `Type=notify` + `READY=1` (§5.2, §6).
2. **`READY=1` из неглавного процесса при `NotifyAccess=main`** → старт «зависает»
   до `TimeoutStartSec`. *Лечение:* `NotifyAccess=all`/`exec` (§6.4).
3. **Double-fork под `Type=simple`** → systemd считает главный процесс завершённым,
   путаница состояния. *Лечение:* не демонизировать (§3.2).
4. **Свой PID-файл + `Type=simple`** → протухание, неполнота, гонки. *Лечение:*
   убрать, довериться cgroup (§9).
5. **Логи в свой файл/буферизованный stdout** → не видны в `journalctl` / зависают
   в буфере. *Лечение:* stdout/stderr + line-buffering (§10.4).
6. **Watchdog-пинг из отдельного «всегда живого» потока** → зависший loop не
   детектится. *Лечение:* пинговать из основного loop (§8.3).
7. **Drain-дедлайн ≥ `TimeoutStopSec`** → `SIGKILL` раньше graceful, обрыв
   in-flight. *Лечение:* внутренний дедлайн < `TimeoutStopSec` (§11.4).
8. **`bind` в коде вместо `sd_listen_fds`** → теряется zero-downtime, окно
   `ECONNREFUSED` при рестарте. *Лечение:* брать сокет от systemd (§7, §12.2).
9. **Не сверил `LISTEN_PID`** → принял чужие дескрипторы. *Лечение:* сверка с
   `getpid()` (§7.2).
10. **`Restart=always` без `StartLimitBurst`** → шторм рестартов битого сервиса.
    *Лечение:* лимит флапа (§15.2).

### 17.3 Разбор: «сервис висит на старте» (пошаговая отладка)

Симптом: `systemctl start myapp` **не возвращается** и в итоге падает по таймауту
`Job for myapp.service failed ... timeout`. Сервис, однако, в логах «работает».
Это **классика `Type=notify`**: systemd ждёт `READY=1`, которого не получил.

Пошагово:

```sh
systemctl status myapp           # SubState=start (а не running) → ждёт готовности
journalctl -u myapp -b           # сервис пишет «accepting», но Active: activating
systemctl show -p Type,NotifyAccess myapp   # Type=notify, NotifyAccess=main
```

Типичные причины и проверка:
1. **Сервис не шлёт `READY=1`** вообще (забыл/условие не сработало). Проверка:
   `strace -f -e trace=sendto,sendmsg -p <pid>` — есть ли запись в `$NOTIFY_SOCKET`?
2. **`NOTIFY_SOCKET` не виден** в коде, потому что `Type` не `notify` (тогда
   переменная не задаётся) — сверь `Type=notify` в юните.
3. **Шлёт из неглавного процесса** при `NotifyAccess=main` → systemd игнорирует
   (§6.4). Лечение: `NotifyAccess=all`/`exec` или слать из главного.
4. **Шлёт `READY=1` слишком поздно** (после долгого прогрева > `TimeoutStartSec`).
   Лечение: увеличить `TimeoutStartSec=` или слать `READY=1` раньше + `STATUS=`
   о прогреве; на долгий старт можно слать `EXTEND_TIMEOUT_USEC=` (продлить окно).

Этот разбор — почему `Type=notify` требует дисциплины, и почему `sd_notify` (упр.
02) — не «лишний вызов», а **контракт** запуска. Симметрично «висит на **остановке**»
обычно = `SIGKILL` по `TimeoutStopSec`, потому что drain не уложился (§11.4).

---

## 18. Практика и самопроверка

### 18.1 Практические задания (в редакторе курса)

1. **`01-listen-fds`** — socket activation: разбор `LISTEN_PID`/`LISTEN_FDS`/
   `LISTEN_FDNAMES` (аналог `sd_listen_fds`); сверка PID, дескрипторы с `3`, поиск
   по имени. *(§7)*
2. **`02-sd-notify`** — построение тела сообщений `sd_notify`: `READY=1`(+`STATUS`),
   `RELOADING=1`+`MONOTONIC_USEC`, `STOPPING=1`, `WATCHDOG=1`; `-1` при тесном
   буфере. *(§6)*
3. **`03-watchdog`** — пейсер watchdog: разбор `WATCHDOG_USEC`/`WATCHDOG_PID`,
   интервал = период/2, «пора ли пингнуть» по инъектируемому времени. *(§8)*
4. **`04-drain-tracker`** — graceful drain: учёт in-flight (`admit`/`finish`),
   переход RUNNING→DRAINING→STOPPED, отказ новых при остановке, форс по дедлайну. *(§11)*

После реализации — собери сервис из §16, поставь юниты (`.service` + `.socket`),
прогони `systemctl status`/`journalctl`/`systemctl reload`/`restart` и убедись:
готовность через `READY=1`, reload без обрыва, drain при stop, сокет переживает
рестарт. Серверный автопрогон проверяет логику упражнений под ASan/UBSan.

### 18.2 Большое задание (из трека)

Возьми свой epoll/io_uring-сервер (C2) и преврати его в **боевой systemd-сервис**:
(1) `Type=notify` с `READY=1` после готовности (упр. 02); (2) socket activation —
сокет из `sd_listen_fds` (упр. 01) + `.socket`-юнит, с fallback на `bind`; (3)
graceful shutdown по `SIGTERM` через drain-трекер (упр. 04) с дедлайном <
`TimeoutStopSec`; (4) `SIGHUP`/`ExecReload` — reload конфига без обрыва; (5) watchdog
(упр. 03) с `WatchdogSec=`; (6) sandbox в юните (`DynamicUser`, `ProtectSystem`,
узкие capabilities). Продемонстрируй **zero-downtime**: под нагрузкой (`wrk`) сделай
`systemctl restart` и покажи, что **нет** ни одного `ECONNREFUSED`/обрыва (сокет
держит systemd). Это прямой кирпич капстоуна.

### 18.3 Вопросы для самопроверки

1. Что такое демон и чем он отличается от обычной программы?
2. Зачем в классической демонизации **второй** `fork`? Что делает `setsid`?
3. Почему под systemd демонизировать (double-fork) **не надо** и даже вредно?
4. Чем `Type=notify` отличается от `Type=simple`? Какую гонку закрывает?
5. Как устроен протокол `sd_notify`? Назови 4 сообщения и их смысл.
6. Что такое socket activation? Назови 4 выигрыша.
7. Какие три переменные окружения несут переданные сокеты и зачем сверять
   `LISTEN_PID`?
8. Зачем watchdog, если сервис не падает? Почему пинговать раз в **половину**
   периода и **из основного loop**?
9. Почему systemd выбрал cgroup-трекинг вместо PID-файлов? Три проблемы PID-файлов.
10. Как сервис должен логировать под systemd и зачем префикс `<N>`?
11. Опиши graceful shutdown по `SIGTERM`: шаги drain. Чем reload (`SIGHUP`)
    отличается от shutdown?
12. Почему внутренний drain-дедлайн должен быть **меньше** `TimeoutStopSec`?
13. Как сделать zero-downtime reload? Три подхода и кто держит сокет в каждом.
14. Что такое FDSTORE и когда он нужен?
15. Назови пять sandbox-опций юнита и от чего каждая защищает.
16. Что такое flapping и как `StartLimitBurst` его гасит?
17. Чем `Requires=` отличается от `After=`? Почему нужны оба?
18. Нарисуй путь слушающего сокета от `.socket`-юнита до `accept` в сервисе.
19. Как проверить безопасность юнита и кто такой `systemd-analyze security`?
20. Как отлаживать socket activation локально без установки юнита?

---

## 19. Банк вопросов

> Полные версии (варианты + разборы) — в `quizzes/c5.json`. Ниже — карта тем.

### БАЗА (термины — мгновенно)
1. Что такое демон.
2. Что такое systemd-юнит и какие бывают типы.
3. `Type=simple` vs `Type=notify` — в чём разница.
4. Что такое socket activation в одном предложении.
5. Что делает `sd_notify(READY=1)`.
6. Что такое watchdog сервиса.
7. Почему логи под systemd идут в stdout/stderr.
8. Что такое graceful shutdown / drain.

### МЕХАНИЗМЫ (как и почему работает)
1. Классический double-fork: шаги и зачем второй fork; почему под systemd не нужен.
2. Socket activation: какие env-переменные, сверка PID, fd с 3, выигрыши.
3. Протокол `sd_notify`: тело сообщений, READY/RELOADING/STOPPING/WATCHDOG.
4. Watchdog: WATCHDOG_USEC, интервал/2, пинг из основного loop.
5. Graceful drain: учёт in-flight, DRAINING, STOPPED, дедлайн vs TimeoutStopSec.
6. PID-файлы vs cgroup-трекинг: три проблемы PID-файлов, что даёт cgroup.
7. Reload (SIGHUP) vs shutdown (SIGTERM): что сохраняется, что сливается.
8. Журналирование: stdout→journald, уровни `<N>`, структурированные поля.

### ЭКСПЕРТ (рассуждение)
1. Zero-downtime reload: как передать слушающий сокет новому процессу (3 подхода).
2. Watchdog: как не зависнуть незаметно; почему пинг из loop, а не из отдельного потока.
3. Как не потерять in-flight при рестарте (drain + сокет у systemd + дедлайн).
4. Sandbox юнита как least privilege: capabilities/seccomp/namespaces через конфиг.
5. FDSTORE: пережить рестарт с состоянием/соединениями.

### ЗАДАНИЯ (по одному на упражнение)
1. `01-listen-fds` 2. `02-sd-notify` 3. `03-watchdog` 4. `04-drain-tracker`

---

## 20. Глоссарий

*(Акронимы раскрыты: английская расшифровка + короткое пояснение.)*

- **Демон (daemon)** — фоновый долгоживущий процесс без управляющего терминала.
- **systemd** — система инициализации (PID 1) и менеджер сервисов Linux.
- **Unit (юнит)** — описание управляемого объекта (`.service`, `.socket`, `.timer`,
  `.target`).
- **PID** — Process ID (идентификатор процесса).
- **PID 1** — первый процесс (init/systemd), «усыновляет» осиротевшие процессы.
- **tty** — teletype (управляющий терминал); у демона его нет.
- **double-fork** — классический ритуал демонизации (fork→setsid→fork) для отрыва
  от терминала.
- **`Type=simple`** — сервис «готов» сразу после exec (нет проверки готовности).
- **`Type=notify`** — сервис «готов», когда прислал `READY=1` через `sd_notify`.
- **`Type=forking`** — legacy: ExecStart форкает рабочий процесс и выходит (с PID-файлом).
- **`Type=oneshot`** — разовая задача/скрипт, «готов» по завершении.
- **`sd_notify`** — протокол уведомления systemd через датаграмму в `$NOTIFY_SOCKET`
  (`READY=1`, `RELOADING=1`, `STOPPING=1`, `WATCHDOG=1`, `STATUS=`).
- **`NOTIFY_SOCKET`** — переменная окружения с путём к notify-сокету.
- **MONOTONIC_USEC** — метка `CLOCK_MONOTONIC` в микросекундах (нужна для `RELOADING=1`).
- **Socket activation** — systemd сам открывает слушающий сокет и передаёт его
  сервису по наследству (fd с `SD_LISTEN_FDS_START=3`).
- **`sd_listen_fds`** — функция, читающая `LISTEN_PID`/`LISTEN_FDS`/`LISTEN_FDNAMES`
  и возвращающая число переданных дескрипторов.
- **`LISTEN_PID`/`LISTEN_FDS`/`LISTEN_FDNAMES`** — env переданных сокетов: кому,
  сколько, имена.
- **`SD_LISTEN_FDS_START`** — номер первого переданного дескриптора (3).
- **`Accept=yes/no`** — inetd-стиль (экземпляр на соединение) vs один экземпляр со
  слушающим сокетом.
- **Watchdog** — механизм «я жив»: сервис шлёт `WATCHDOG=1` каждые `WATCHDOG_USEC/2`,
  иначе systemd перезапускает.
- **`WATCHDOG_USEC`/`WATCHDOG_PID`** — env: период watchdog (мкс) и кому он адресован.
- **`WatchdogSec=`** — опция юнита, задающая период watchdog.
- **cgroup** — control group (контрольная группа): иерархическая группа процессов
  ядра; systemd трекает все процессы сервиса в его cgroup.
- **`KillMode=`** — как останавливать (`control-group` = всем процессам cgroup).
- **`KillSignal=`** — сигнал остановки (по умолчанию `SIGTERM`).
- **journald** — сервис журналирования systemd; ловит stdout/stderr сервисов.
- **`journalctl`** — клиент журнала (`-u`, `-f`, `-p`, поля).
- **syslog-уровни** — приоритеты `0..7` (`<N>`-префикс): emerg..debug; 3=err, 6=info.
- **Structured logging** — лог с индексируемыми полями `KEY=VALUE` (`sd_journal_send`).
- **Drain (слив)** — доделать активные (in-flight) запросы, не принимая новые.
- **In-flight** — запросы, уже принятые и ещё не завершённые.
- **Graceful shutdown** — корректная остановка: drain + выход, без обрыва.
- **Graceful reload** — перечитать конфиг (`SIGHUP`) без остановки и обрыва соединений.
- **`TimeoutStopSec=`** — сколько systemd ждёт после `SIGTERM` до `SIGKILL`.
- **`ExecReload=`** — команда перезагрузки (часто `kill -HUP $MAINPID`).
- **Zero-downtime reload** — обновление без простоя порта и потери in-flight.
- **`SO_REUSEPORT`** — несколько сокетов на один порт, ядро балансирует (C2);
  подход к zero-downtime.
- **FDSTORE** — хранилище дескрипторов в systemd: `FDSTORE=1` + `SCM_RIGHTS` →
  systemd держит fd через рестарт.
- **`FileDescriptorStoreMax=`** — сколько fd systemd готов хранить за сервис.
- **`SCM_RIGHTS`** — control message для передачи дескриптора через UNIX-сокет (C3).
- **`Restart=`** — политика перезапуска (`on-failure`/`always`/`on-watchdog`/…).
- **Flapping** — частые рестарты падающего сервиса; гасится `StartLimitBurst`.
- **`StartLimitIntervalSec`/`StartLimitBurst`** — окно и лимит стартов против флапа.
- **`DynamicUser=`** — временный пользователь на время работы сервиса.
- **`ProtectSystem=`/`ProtectHome=`/`PrivateTmp=`** — sandbox ФС в юните.
- **`NoNewPrivileges=`** — запрет повышения привилегий (setuid бессилен).
- **`CapabilityBoundingSet=`** — Linux capabilities (привилегии-кусочки), оставленные
  сервису.
- **`SystemCallFilter=`** — seccomp-фильтр сисколлов в юните.
- **seccomp** — secure computing mode: фильтр разрешённых сисколлов (Ф3/контейнеры).
- **`Requires=`/`Wants=`/`BindsTo=`** — жёсткая/мягкая/связанная зависимость юнитов.
- **`After=`/`Before=`** — порядок запуска (не требование!).
- **target** — группа/точка синхронизации юнитов (≈ runlevel).
- **`systemd-analyze security`** — оценка sandbox-настроек юнита.
- **`MAINPID`** — главный PID сервиса (подставляется в `ExecReload=` и т.п.).
- **fd** — file descriptor (файловый дескриптор).
- **ECONNREFUSED** — «соединение отклонено»: порт никто не слушает (то, чего избегает
  socket activation).
- **SysV init** — старая система инициализации на shell-скриптах (`/etc/init.d/`),
  предшественник systemd.
- **slice (`.slice`)** — узел дерева cgroup для группы сервисов (`system.slice`,
  `user.slice`).
- **Delegate=** — отдать сервису поддерево cgroup в управление (контейнерные рантаймы).
- **liveness / readiness** — «жив ли вообще» (watchdog) vs «можно ли слать трафик»
  (`READY=1`/health).
- **lame-duck** — фаза, когда сервис ещё обслуживает старые соединения, но не
  принимает новые.
- **EXTEND_TIMEOUT_USEC** — `sd_notify`-сообщение «дай ещё времени» при долгом
  старте/остановке.
- **OOM** — Out Of Memory: нехватка памяти; `MemoryMax` даёт OOM-kill внутри cgroup.
- **TPM** — Trusted Platform Module: аппаратный крипточип; шифрует
  `LoadCredentialEncrypted`.
- **LB** — Load Balancer (балансировщик нагрузки); выводит сервис из ротации по
  health-check.
- **UUID** — Universally Unique Identifier; формат `MESSAGE_ID` события в журнале.
- **lingering** — режим, при котором user-сервисы работают после выхода пользователя.
- **template unit (`name@.service`)** — шаблон для множества инстансов (`%i`).
- **tini / dumb-init** — тонкий init для PID 1 в контейнере (reaping + проброс
  сигналов).

---

## 21. Что дальше

C5 превратил сервер в боевой сервис. Это во многом **финал userspace-ремесла**:
C2 (event-loop) → C3 (IPC) → C4 (сигналы/таймеры) → C5 (сервис). Дальше:

- **C6 (производительность системного кода):** профилируешь свой сервис (`perf`),
  находишь узкое место, показываешь zero-copy (`sendfile`/`splice`, C3) и
  syscall-batching (`io_uring`, C2); cache-friendly layout. От метрики к причине.
- **Капстоун этапа 2A:** высокопроизводительный сервис «epoll → io_uring» как
  systemd-юнит с `Type=notify`, socket activation, graceful shutdown и
  zero-downtime — ровно сборка из §16, доведённая до прода.
- **K-этап (ядро):** обратная сторона — cgroups изнутри (`kernel/cgroup/`), как ядро
  передаёт сокеты по наследству, как устроен notify-сокет на уровне `AF_UNIX`
  датаграмм (C3 — обратная сторона уже знакома).

### Мини-проект для закрепления

Доведи сервис из §16 до прод-уровня и **разверни** его: (1) `.service` + `.socket` с
`Type=notify`, `WatchdogSec`, sandbox; (2) `READY=1` после готовности, `WATCHDOG=1`
из loop, `STOPPING=1` при остановке (упр. 02, 03); (3) graceful drain по `SIGTERM`
(упр. 04) с дедлайном < `TimeoutStopSec`; (4) socket activation (упр. 01) для
zero-downtime; (5) `systemctl reload` (SIGHUP) без обрыва. Проверь: под нагрузкой
`wrk` сделай `systemctl restart` — **ноль** `ECONNREFUSED`; `journalctl` показывает
твои логи с уровнями; `systemctl status` показывает `STATUS=` и cgroup-дерево;
`systemd-analyze security` даёт приличную оценку; зависание (искусственный
`sleep(100)` в loop) ловится watchdog'ом и перезапускается. Это микромодель реального
прод-сервиса и прямой выход на капстоун.

> **Критерий готовности модуля:** ты реализовал `sd_listen_fds`, протокол
> `sd_notify`, watchdog-пейсер и drain-трекер; превратил epoll-сервер в systemd-сервис
> с `Type=notify`, socket activation, watchdog и graceful drain; можешь объяснить,
> почему double-fork вреден под systemd, почему cgroup лучше PID-файла, и как
> добиться zero-downtime reload без потери in-flight. Тогда — вперёд в C6.
