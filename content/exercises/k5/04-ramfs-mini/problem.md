# Задание: простейшая in-memory ФС k5fs (амбициозное)

Это «амбициозная» часть задания трека — заглянуть под капот всех файловых систем.
Ты регистрируешь собственный **тип ФС** `k5fs`, который не работает с диском
(in-memory, `nodev`), и при монтировании заполняешь **суперблок** (`super_block`):
создаёшь корневой **inode**-каталог и привязываешь его к корневому **dentry**.

Современный путь монтирования в ядре (после перехода на **fs_context**, ~5.x):

```text
mount(2) → init_fs_context() → fc->ops->get_tree() → get_tree_nodev() → fill_super()
```

То есть `mount` не вызывает твою функцию напрямую: ты лишь подставляешь свои операции
контекста (`fs_context_operations.get_tree`), а в `get_tree` зовёшь готовый
`get_tree_nodev`, передавая ему свою `fill_super`.

## Что реализовать (в `fill_super`)

1. Заполнить поля суперблока: `s_magic`, `s_blocksize`/`s_blocksize_bits` (`PAGE_SIZE`/
   `PAGE_SHIFT`), `s_op = &k5fs_sops`, `s_time_gran = 1`.
2. Создать корневой inode: `new_inode(sb)`; задать `i_ino`, `i_mode = S_IFDIR | 0755`,
   `simple_inode_init_ts(inode)` (инициализация времён — поля `i_atime/i_mtime/i_ctime`
   в новых ядрах задаются только через accessor-ы), `i_op = &simple_dir_inode_operations`,
   `i_fop = &simple_dir_operations`, `set_nlink(inode, 2)`.
3. Привязать корень: `sb->s_root = d_make_root(inode); if (!sb->s_root) return -ENOMEM;`
   затем `return 0;`.

Обвязка (`init_fs_context`, `get_tree`, регистрация типа) уже дана.

## Ключевые API

- **`register_filesystem` / `unregister_filesystem`** — (де)регистрация типа ФС;
  после регистрации `k5fs` появляется в `/proc/filesystems`.
- **`init_fs_context`** (поле `file_system_type`) — точка входа монтирования; ставит
  `fc->ops`.
- **`get_tree_nodev(fc, fill_super)`** — выделить анонимный суперблок и позвать твою
  `fill_super`. Замена устаревшему `mount_nodev`.
- **`new_inode(sb)`**, **`set_nlink`**, **`simple_inode_init_ts`** — создание и
  инициализация inode.
- **`d_make_root(inode)`** — корневой dentry из корневого inode.
- **`simple_dir_operations` / `simple_dir_inode_operations` / `simple_statfs`** — готовые
  библиотечные операции (libfs) для каталога.
- **`kill_anon_super`** (поле `kill_sb`) — снос анонимного суперблока при размонтировании.

## Почему так

`get_tree_nodev` + `kill_anon_super` — каноничная пара для ФС без блочного устройства
(только в памяти, один анонимный суперблок). Ты делегируешь почти всё `libfs`: задача —
правильно собрать корень. Забудешь `d_make_root` (или вернёшь 0 без корня) — `mount`
упадёт или прыгнет по `NULL`. Поэтому стартер намеренно возвращает `-ENOMEM`: пока
корня нет, `mount` обязан **падать с ошибкой**, а не в oops.

## Проверка

QEMU: тест грузит модуль, проверяет `k5fs` в `/proc/filesystems`, монтирует
`mount -t k5fs none /mnt/k5`, убеждается, что корень — каталог, размонтирует и
выгружает модуль. `dmesg` без `BUG:/Oops/WARNING:`.
