# Задание: свой slab-кэш под объекты драйвера (+ kmemleak)

Главная идиома K4: когда драйвер аллоцирует **много объектов одного типа**, он заводит
**собственный slab-кэш** (`kmem_cache`) — это быстрее голого `kmalloc` (выделение из
per-CPU freelist), без фрагментации и с **отдельной строкой** в `/proc/slabinfo`/
`slabtop` (видно потребление **именно твоих** объектов).

Здесь ты строишь misc-устройство `/dev/cppslab`, которое ведёт **пул именованных
объектов** в своём кэше: `write "<name>"` создаёт объект (`kmem_cache_alloc`), `read`
перечисляет имена, а `exit` освобождает **все** объекты и уничтожает кэш.

## Что реализовать

- **`init`** — создать кэш: `kmem_cache_create("cpp_entry", sizeof(struct entry), 0,
  SLAB_HWCACHE_ALIGN, NULL)`; затем `misc_register`.
- **`write(name)`** — `kmem_cache_alloc(entry_cache, GFP_KERNEL)`, скопировать имя,
  добавить узел в список под мьютексом.
- **`read`** — перечислить имена объектов через пробел (`"alpha beta\n"`).
- **`exit`** — `misc_deregister` → освободить **все** объекты (`kmem_cache_free`) →
  **`kmem_cache_destroy`** (порядок важен: сначала освободить всё, потом уничтожить кэш).

## Ключевые API

- **`kmem_cache_create(name, size, align, flags, ctor)`** — создать пул объектов размера
  `size`. `name` виден в `/proc/slabinfo`/`slabtop`. `SLAB_HWCACHE_ALIGN` — выровнять по
  строке кэша.
- **`kmem_cache_alloc(cache, gfp)`** / **`kmem_cache_free(cache, obj)`** — взять/вернуть
  объект.
- **`kmem_cache_destroy(cache)`** — уничтожить пул. **Все объекты должны быть
  освобождены** заранее, иначе ядро ругнётся и это утечка.

## Синхронизация с остановкой и утечки

Порядок в `exit`: **снять устройство → освободить все объекты → уничтожить кэш**.
Забудешь `kmem_cache_free` хотя бы одного объекта — **утечка** (и ругань при `destroy`).
Именно её ловит **kmemleak**: на ядре с `CONFIG_DEBUG_KMEMLEAK` сделай
`echo scan > /sys/kernel/debug/kmemleak`, и `cat /sys/kernel/debug/kmemleak` покажет
**стек выделения** утёкшего объекта (где сделан `kmem_cache_alloc`). Это и есть критерий
модуля: «создай slab-кэш под объекты драйвера; поймай намеренную утечку через kmemleak».

## Проверка

QEMU: `echo "alpha" > /dev/cppslab`, `echo "beta" > /dev/cppslab`, `cat /dev/cppslab` →
`"alpha beta"`. `dmesg` чист, `rmmod` проходит (все объекты освобождены до уничтожения
кэша).
