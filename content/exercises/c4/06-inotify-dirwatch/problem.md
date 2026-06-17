# Задание: inotify — точная модель каталога через поток событий

`inotify` даёт наблюдение за файловой системой **через дескриптор** (события как
`EPOLLIN`, встраивается в event-loop, C2/C4). Задача — не «вызвать inotify», а
поддерживать **точную модель** содержимого каталога (множество имён), разбирая
поток событий create / delete / rename. Это то, что делают сборщики (watch-режим),
синхронизаторы, hot-reload демоны.

## Что реализовать

```cpp
int  init(const char* path);   // inotify_init1 + inotify_add_watch
void process(int timeout_ms);  // дождаться событий, вычитать все, обновить модель
int  contains(const char* name) const;   // есть ли имя в модели
int  count() const;                       // размер модели
```

### `init`
```cpp
fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
if (fd_ < 0) return -1;
int wd = inotify_add_watch(fd_, path,
            IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
return wd < 0 ? -1 : 0;
```

### `process` — две тонкости
```cpp
struct pollfd p{ fd_, POLLIN, 0 };
poll(&p, 1, timeout_ms);                     // подождать первое событие
char buf[8192] __attribute__((aligned(__alignof__(struct inotify_event))));
for (;;) {
    ssize_t n = read(fd_, buf, sizeof buf);
    if (n <= 0) { if (n < 0 && errno == EINTR) continue; break; }  // EAGAIN → всё
    for (char* ptr = buf; ptr < buf + n; ) {
        auto* e = (struct inotify_event*)ptr;
        if (e->len > 0) {                         // есть имя
            std::string name(e->name);            // ВНИМАНИЕ: e->name может быть с '\0'-паддингом
            if (e->mask & (IN_CREATE | IN_MOVED_TO))    model_.insert(name);
            if (e->mask & (IN_DELETE | IN_MOVED_FROM))  model_.erase(name);
        }
        ptr += sizeof(struct inotify_event) + e->len;   // СЛЕДУЮЩЕЕ событие
    }
}
```

**Тонкость 1 — переменная длина.** События идут в буфере подряд, каждое —
`struct inotify_event` + имя длиной `e->len` (с `\0`-паддингом до выравнивания).
Шаг до следующего — `sizeof(inotify_event) + e->len`. Неправильный шаг = каша.

**Тонкость 2 — rename = два события.** Переименование **внутри** каталога
приходит парой `IN_MOVED_FROM` (старое имя) + `IN_MOVED_TO` (новое). Для модели
достаточно: `MOVED_FROM` → убрать, `MOVED_TO` → добавить. (Связать их в «rename»
можно по полю `cookie` — нужно для перемещений МЕЖДУ каталогами; для модели одного
каталога не обязательно.)

## Грабли inotify (знать)

- **Не рекурсивно:** watch на каталог НЕ покрывает подкаталоги; для дерева — watch
  на каждый подкаталог + отслеживать создание новых (отдельная задача).
- **Очередь переполняется:** при буре событий возможен `IN_Q_OVERFLOW` — модель
  рассинхронизируется, надо пересканировать каталог. Продакшн это обрабатывает.
- **`fanotify`** — мощнее (события на всю ФС/монтирование, может блокировать
  доступ для антивирусов), но требует `CAP_SYS_ADMIN`; `inotify` — для обычного
  слежения за путями.

## Проверка

Автопрогон (ASan/UBSan) на реальном temp-каталоге: `create a.txt` → модель `{a}`;
ещё два create → `{a,b,c}`; `rename a→z` → `{z,b,c}` (a убран, z добавлен);
`delete b` → `{z,c}`; без изменений модель стабильна. Парсинг событий переменной
длины и обработка rename-пары — суть задания.
