# Модуль EL4 — Buildroot: корневая файловая система

> Этап 2C, Embedded Linux. Buildroot собирает полную корневую ФС (rootfs) для embedded: toolchain, libc, busybox, системные утилиты, твои приложения — всё из исходников под конкретную архитектуру. Это не дистрибутив, а система сборки. Альтернативы: Yocto/OpenEmbedded (мощнее, значительно сложнее), OpenWrt (для роутеров), Alpine Linux (готовый маленький дистрибутив). Если ты уверенно отвечаешь на самопроверку — этот модуль можешь пропустить.

---

## 0. Карта модуля

| | |
|---|---|
| **Время** | 10–15 ч. ~3 ч чтение, ~8 ч сборка и практика, ~2 ч самопроверка. Первая сборка занимает 30–90 минут — запускай параллельно с чтением. |
| **Зачем** | Buildroot — стандартный инструмент для production embedded Linux. Один `make` → полный образ: toolchain + ядро + rootfs + U-Boot + SD card image. Без понимания Buildroot придётся вручную собирать каждый компонент, что нереально для 100+ пакетов. |
| **Ресурсы** | [buildroot.org/downloads/manual/manual.html](https://buildroot.org/downloads/manual/manual.html) — читать от начала до конца (500 страниц, основной справочник). |
| **Нужно знать** | EL1 (кросс-компиляция), EL3 (U-Boot — для понимания интеграции загрузчика). |

---

## 1. Архитектура Buildroot

### 1.1 Структура исходников

```
buildroot/
├── arch/          # архитектурные настройки (CFLAGS, ABI, float)
├── board/         # board-specific файлы: post-build скрипты, overlays, genimage конфиги
├── configs/       # defconfig файлы для известных плат (~1500 конфигов)
├── package/       # описания пакетов (7000+ пакетов)
│   ├── busybox/
│   ├── openssh/
│   ├── python3/
│   └── myapp/    ← место для твоих пакетов
├── fs/            # генераторы rootfs форматов (ext2/3/4, squashfs, cpio, ubi)
├── linux/         # обёртка для сборки ядра Linux
├── boot/          # обёртки для загрузчиков (u-boot, grub, barebox)
├── toolchain/     # сборка или интеграция внешнего toolchain
└── output/        # результат сборки (НЕ трогать вручную!)
    ├── build/     # распакованные исходники и промежуточные артефакты
    ├── host/      # toolchain и host инструменты (работают на x86 хосте)
    ├── staging/   # sysroot: заголовки и библиотеки для target архитектуры
    ├── target/    # содержимое будущего rootfs (копируется в образ)
    └── images/    # финальные образы: rootfs.ext4, sdcard.img, zImage
```

### 1.2 Принцип работы

Buildroot — система сборки на базе GNU Make. Алгоритм:

1. Прочитать конфигурацию (`.config` от menuconfig).
2. Для каждого включённого пакета: скачать исходники (DL_DIR), распаковать в `output/build/<pkg>-<ver>/`, применить патчи.
3. Собрать cross-toolchain (или взять внешний).
4. Собрать каждый пакет: configure/cmake/make с `CROSS_COMPILE` и `--sysroot=output/staging`.
5. Установить в `output/staging/` (библиотеки, заголовки) и `output/target/` (только то что идёт на устройство).
6. Выполнить post-build скрипты.
7. Упаковать `output/target/` в образ(ы).

Важно: `output/target/` не является валидным rootfs на хосте — в нём нет device nodes (создаются при упаковке образа), нет /proc и /sys (монтируются ядром).

### 1.3 output/staging/ vs output/target/

**`output/staging/`** — sysroot для разработчика:
- Содержит заголовочные файлы (`include/`) и библиотеки (`lib/`) для target архитектуры.
- Используется при кросс-компиляции приложений вне Buildroot: `--sysroot=output/staging`.
- Содержит статические и shared библиотеки, debug символы.
- Не копируется на устройство.

**`output/target/`** — будущий rootfs:
- Только то что нужно на устройстве: исполняемые файлы, shared библиотеки без debug символов (stripped), конфиги.
- Не трогать вручную: при следующей сборке Buildroot может перезаписать. Для кастомизации — overlay или post-build script.

---

## 2. Первая сборка

### 2.1 Клонирование

```bash
git clone https://github.com/buildroot/buildroot.git
cd buildroot

# Для production — использовать release тег (стабильнее чем HEAD)
git checkout 2024.02.6  # LTS release
```

### 2.2 Выбор конфигурации

```bash
# Список всех defconfig
make list-defconfigs | less

# Поиск конфигов для Rockchip
make list-defconfigs | grep rockchip

# Для RK3588 EVB
make rockchip_rk3588_evb_defconfig

# Для QEMU AArch64 (без железа — для обучения)
make qemu_aarch64_virt_defconfig

# Для Raspberry Pi 4 (пример)
make raspberrypi4_64_defconfig
```

### 2.3 Тонкая настройка

```bash
# Главная конфигурация системы
make menuconfig

# Конфигурация ядра Linux (если BR2_LINUX_KERNEL=y)
make linux-menuconfig
make linux-savedefconfig   # сохранить в br2-external или board/

# Конфигурация BusyBox
make busybox-menuconfig
make busybox-update-config

# Конфигурация U-Boot (если BR2_TARGET_UBOOT=y)
make uboot-menuconfig
```

### 2.4 Сборка

```bash
# Полная сборка
make -j$(nproc) 2>&1 | tee build.log

# Следить за прогрессом
tail -f build.log

# При ошибке найти причину
grep -i "error:" build.log | head -20
```

Первая сборка: скачивает исходники из интернета (~500 MB–2 GB), собирает gcc (~30 мин), потом пакеты.

### 2.5 Артефакты

```bash
ls output/images/
# rootfs.ext2        — rootfs в ext2 (если включено)
# rootfs.ext4        — rootfs в ext4
# rootfs.tar         — rootfs как тарбол
# rootfs.squashfs    — сжатый read-only rootfs
# sdcard.img         — полный образ SD карты (если genimage настроен)
# Image              — ядро Linux (если BR2_LINUX_KERNEL=y)
# rk3588-evb.dtb     — DTB (если BR2_LINUX_KERNEL=y)
# u-boot-rockchip.bin — U-Boot (если BR2_TARGET_UBOOT=y)
```

---

## 3. Конфигурация системы

### 3.1 Target options

Раздел `Target options` в menuconfig:

```
Target Architecture       → AArch64 (little endian)
Target Architecture Variant → cortex-A76  (для RK3588)
Target ABI                → ELFv1
Floating point strategy   → FP-ARMv8
ARM instruction set       → AArch64
```

Правильный выбор варианта влияет на генерацию оптимизированного кода (`-mcpu=cortex-a76`).

### 3.2 Toolchain

```
Toolchain type:
  - Buildroot toolchain    → Buildroot сам собирает gcc из исходников
                             Плюсы: полный контроль, нет внешних зависимостей
                             Минусы: первая сборка дольше (30–60 мин)
  - External toolchain     → использовать готовый Linaro/ARM/Bootlin toolchain
                             Плюсы: быстрая первая сборка, проверенные toolchain
                             Минусы: зависимость от внешнего источника
```

При использовании external toolchain:
```
Toolchain → External toolchain → Linaro AArch64 2023.05
            или Custom toolchain → указать путь
```

Версия C library:
- `glibc` — полная, большинство приложений, большой размер (~3 MB).
- `musl` — легкий альтернативный libc, хорош для embedded.
- `uclibc-ng` — устаревает, для очень ограниченных систем.

### 3.3 System configuration

```
System hostname          → myboard
System banner            → Welcome to MyBoard Linux
Init system              → BusyBox (проще) | systemd (полнофункциональный)
/dev management          → Dynamic using devtmpfs + mdev (для BusyBox init)
                           или Dynamic using devtmpfs + udev (для systemd)
Root filesystem overlay  → board/mycompany/myboard/rootfs-overlay
Root password            → (оставить пустым для embedded без аутентификации)
                           или задать password hash
```

Init system выбор:
- **BusyBox init** — минималистичный, `/etc/inittab`, скрипты в `/etc/init.d/S??*`. Время загрузки: 1–2 секунды.
- **systemd** — полнофункциональный, parallel boot, socket activation, journald. Требует glibc, увеличивает rootfs на ~30 MB. Время загрузки: 3–8 секунд.
- **OpenRC** — промежуточный вариант, BSD-style init с dependency management.

### 3.4 Выбор пакетов

`Package Selection` → тысячи пакетов в категориях:

```
Libraries → libssl, libjpeg, sqlite, ...
Networking → openssh, nginx, curl, ntp, ...
Development → gdb, strace, ltrace, valgrind, ...
Python      → python3, numpy, ...
System tools → rsync, sudo, ...
Shell and utilities → bash, zsh, tmux, ...
```

Важно: каждый пакет увеличивает rootfs и время сборки. Для production — только то что реально нужно.

---

## 4. Написание пакета Buildroot

### 4.1 Структура пакета

Каждый пакет в директории `package/<name>/`:

```
package/myapp/
├── Config.in      — Kconfig описание (включается в menuconfig)
├── myapp.mk       — правила сборки (Makefile-синтаксис)
└── myapp.hash     — хеши исходников (для верификации)
```

### 4.2 Generic package (пользовательская система сборки)

```makefile
# package/myapp/myapp.mk

MYAPP_VERSION = 1.2.3
MYAPP_SITE = https://example.com/releases
MYAPP_SOURCE = myapp-$(MYAPP_VERSION).tar.gz

# Или локальный путь (для разработки)
MYAPP_VERSION = local
MYAPP_SITE = $(TOPDIR)/../myapp
MYAPP_SITE_METHOD = local

# Зависимости (другие пакеты должны быть собраны раньше)
MYAPP_DEPENDENCIES = libusb libevent

# Лицензионная информация
MYAPP_LICENSE = MIT
MYAPP_LICENSE_FILES = LICENSE

# Переменные для кросс-компиляции предоставляются Buildroot автоматически:
# $(TARGET_CC), $(TARGET_CFLAGS), $(TARGET_LDFLAGS), $(TARGET_CROSS) и т.д.

define MYAPP_BUILD_CMDS
    $(MAKE) CC="$(TARGET_CC)" \
            CFLAGS="$(TARGET_CFLAGS)" \
            LDFLAGS="$(TARGET_LDFLAGS)" \
            -C $(@D) all
endef

define MYAPP_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/myapp \
               $(TARGET_DIR)/usr/bin/myapp
    $(INSTALL) -D -m 0644 $(@D)/myapp.conf.example \
               $(TARGET_DIR)/etc/myapp.conf
endef

# При необходимости — установка в staging (для библиотек)
define MYAPP_INSTALL_STAGING_CMDS
    $(INSTALL) -D -m 0644 $(@D)/include/myapp.h \
               $(STAGING_DIR)/usr/include/myapp.h
    $(INSTALL) -D -m 0755 $(@D)/libmyapp.so \
               $(STAGING_DIR)/usr/lib/libmyapp.so
endef

$(eval $(generic-package))
```

### 4.3 Autoconf package

Для пакетов использующих `./configure && make && make install`:

```makefile
# package/mylib/mylib.mk

MYLIB_VERSION = 2.0
MYLIB_SITE = https://example.com/mylib-$(MYLIB_VERSION).tar.gz

MYLIB_INSTALL_STAGING = YES   # установить в staging (sysroot)
MYLIB_INSTALL_TARGET = YES    # установить в target (rootfs)

MYLIB_DEPENDENCIES = host-pkgconf libssl

# configure flags (дополнительно к стандартным)
MYLIB_CONF_OPTS = \
    --disable-static \
    --enable-shared \
    --without-python \
    --with-ssl=$(STAGING_DIR)/usr

MYLIB_LICENSE = LGPL-2.1+
MYLIB_LICENSE_FILES = COPYING

$(eval $(autotools-package))
```

Buildroot автоматически вызывает: `./configure --host=aarch64-linux-gnu --prefix=/usr ...`, `make`, `make DESTDIR=$(TARGET_DIR) install`.

### 4.4 CMake package

```makefile
# package/myapp-cmake/myapp-cmake.mk

MYAPP_CMAKE_VERSION = 3.1
MYAPP_CMAKE_SITE = https://example.com/myapp-$(MYAPP_CMAKE_VERSION).tar.gz

MYAPP_CMAKE_INSTALL_STAGING = NO
MYAPP_CMAKE_INSTALL_TARGET = YES

MYAPP_CMAKE_DEPENDENCIES = libpthread

# CMake переменные
MYAPP_CMAKE_CONF_OPTS = \
    -DENABLE_TESTS=OFF \
    -DENABLE_DOCS=OFF \
    -DCMAKE_BUILD_TYPE=Release

$(eval $(cmake-package))
```

Buildroot передаёт в cmake: toolchain файл с правильным `CMAKE_SYSTEM_PROCESSOR`, `CMAKE_C_COMPILER`, `CMAKE_SYSROOT`.

### 4.5 Config.in

```
# package/myapp/Config.in
config BR2_PACKAGE_MYAPP
    bool "myapp"
    depends on BR2_PACKAGE_LIBUSB  # зависит от libusb
    select BR2_PACKAGE_LIBEVENT    # автоматически включает libevent
    help
      My embedded application.

      Project: https://example.com/myapp
```

Добавить в `package/Config.in` (в нужный раздел):
```
source "package/myapp/Config.in"
```

### 4.6 Хеши для верификации

```
# package/myapp/myapp.hash
# Формат: алгоритм  хеш  имя_файла
sha256  abc123def456...  myapp-1.2.3.tar.gz
```

Buildroot проверяет хеш при каждой загрузке. Для локальных пакетов (`SITE_METHOD = local`) — не нужен.

---

## 5. Board overlay и post-build скрипты

### 5.1 Rootfs overlay

Overlay — директория с файлами, которые копируются поверх `output/target/` после сборки всех пакетов. Это правильный способ добавить board-specific файлы.

```
board/mycompany/myboard/rootfs-overlay/
├── etc/
│   ├── hostname                  → /etc/hostname на устройстве
│   ├── network/
│   │   └── interfaces            → /etc/network/interfaces
│   ├── init.d/
│   │   ├── S10myapp             → BusyBox init скрипт (S = start, 10 = порядок)
│   │   └── S99production-check
│   └── ssh/
│       └── authorized_keys       → SSH ключи для root
├── usr/
│   └── share/
│       └── myboard/
│           └── production.db
└── lib/
    └── firmware/
        └── mydevice.bin          → firmware для устройства
```

В `menuconfig` → `System configuration` → `Root filesystem overlay directories`:
```
BR2_ROOTFS_OVERLAY = "board/mycompany/myboard/rootfs-overlay"
```

Можно указать несколько директорий через пробел:
```
BR2_ROOTFS_OVERLAY = "board/mycompany/common-overlay board/mycompany/myboard/rootfs-overlay"
```

### 5.2 BusyBox init скрипты

```bash
#!/bin/sh
# board/mycompany/myboard/rootfs-overlay/etc/init.d/S10myapp
# BusyBox init запускает скрипты S??* при старте, K??* при остановке

DAEMON=/usr/bin/myapp
PIDFILE=/var/run/myapp.pid

case "$1" in
    start)
        echo -n "Starting myapp: "
        start-stop-daemon -S -q -m -p $PIDFILE \
            --exec $DAEMON -- --config /etc/myapp.conf
        echo "OK"
        ;;
    stop)
        echo -n "Stopping myapp: "
        start-stop-daemon -K -q -p $PIDFILE
        echo "OK"
        ;;
    restart)
        "$0" stop
        sleep 1
        "$0" start
        ;;
    *)
        echo "Usage: $0 {start|stop|restart}"
        exit 1
esac
```

### 5.3 Post-build скрипт

Выполняется после сборки всех пакетов и наложения overlay, до упаковки в образ. Получает путь к `TARGET_DIR` как `$1`.

```bash
#!/bin/sh
# board/mycompany/myboard/post-build.sh

set -e  # завершиться при ошибке

TARGET_DIR="$1"
BOARD_DIR="$(dirname "$0")"

echo "Running post-build script for MyBoard..."

# Удалить лишние файлы (документация, man pages)
rm -rf "${TARGET_DIR}/usr/share/doc"
rm -rf "${TARGET_DIR}/usr/share/man"
rm -rf "${TARGET_DIR}/usr/share/info"

# Создать нужные директории
mkdir -p "${TARGET_DIR}/data/config"
mkdir -p "${TARGET_DIR}/var/log/myapp"

# Создать символическую ссылку
ln -sf /data/config/app.json "${TARGET_DIR}/etc/myapp.json"

# Записать информацию о сборке
cat > "${TARGET_DIR}/etc/build-info" << EOF
BUILD_DATE=$(date -u +%Y-%m-%dT%H:%M:%SZ)
BUILD_HOST=$(hostname)
BUILDROOT_VERSION=$(cat ${BOARD_DIR}/../../.git/HEAD 2>/dev/null || echo unknown)
EOF

echo "Post-build done."
```

Зарегистрировать в menuconfig:
```
BR2_ROOTFS_POST_BUILD_SCRIPT = "board/mycompany/myboard/post-build.sh"
```

### 5.4 Post-image скрипт

Выполняется после создания образов (`output/images/`). Используется для финальной упаковки.

```bash
#!/bin/sh
# board/mycompany/myboard/post-image.sh
BINARIES_DIR="${BINARIES_DIR}"  # = output/images/

# Вычислить хеш образа для проверки при обновлении
sha256sum "${BINARIES_DIR}/sdcard.img" > "${BINARIES_DIR}/sdcard.img.sha256"

echo "OTA package ready: ${BINARIES_DIR}/sdcard.img"
```

---

## 6. Форматы образов rootfs

### 6.1 Обзор форматов

| Формат | Конфиг | Запись | Сжатие | Применение |
|---|---|---|---|---|
| ext2/3/4 | `BR2_TARGET_ROOTFS_EXT2=y` | да | нет | eMMC/SD, основной раздел |
| squashfs | `BR2_TARGET_ROOTFS_SQUASHFS=y` | нет | да (lz4/xz/zstd) | read-only rootfs, ROM |
| cpio | `BR2_TARGET_ROOTFS_CPIO=y` | нет | опц. | initramfs (встраивается в ядро) |
| tar | `BR2_TARGET_ROOTFS_TAR=y` | — | опц. | развернуть на диск вручную |
| ubifs | `BR2_TARGET_ROOTFS_UBIFS=y` | да | да | raw NAND flash |
| f2fs | `BR2_TARGET_ROOTFS_F2FS=y` | да | нет | SSD/eMMC оптимизированный |

### 6.2 Выбор формата для production

**squashfs** предпочтителен для production read-only rootfs:
- Сжатие: xz или zstd → образ 2–4× меньше ext4.
- Read-only: невозможно испортить при неожиданном отключении питания (power-cut safe).
- Целостность: нет журналирования — нечего восстанавливать.

**ext4** для раздела с данными (/data, /var):
- Запись, журналирование.
- При power-cut: fsck при следующем монтировании.

Типичная схема разделов eMMC для RK3588:
```
/dev/mmcblk0boot0  → U-Boot (idbloader + u-boot.itb)
/dev/mmcblk0p1     → boot (ext4 или FAT): kernel + dtb
/dev/mmcblk0p2     → rootfs (squashfs, read-only)
/dev/mmcblk0p3     → data (ext4, read-write)
/dev/mmcblk0p4     → update (второй rootfs для A/B обновления)
```

Монтирование squashfs rootfs с overlayfs для /tmp и /etc изменений:
```
SquashFS (read-only) + tmpfs (RAM) → overlayfs → видимый /
```

### 6.3 SD card image с genimage

Buildroot может автоматически создать полный образ SD карты:

```
# board/mycompany/myboard/genimage.cfg
image sdcard.img {
    hdimage {
        gpt = "false"
    }

    partition u-boot {
        in-partition-table = "no"
        image = "u-boot-rockchip.bin"
        offset = 32K       # Rockchip: 64 сектора * 512 = 32768 = 32K
    }

    partition boot {
        partition-type = 0x0C  # FAT32
        bootable = "yes"
        image = "boot.vfat"
        size = 64M
    }

    partition rootfs {
        partition-type = 0x83  # Linux
        image = "rootfs.ext4"
        size = 512M
    }

    partition data {
        partition-type = 0x83
        image = "data.ext4"
        size = 256M
        # или size = 0 для "всё оставшееся место"
    }
}
```

Включить в menuconfig:
```
BR2_PACKAGE_HOST_GENIMAGE=y
BR2_ROOTFS_POST_IMAGE_SCRIPT="board/mycompany/myboard/post-image.sh"
```

В `post-image.sh` вызвать genimage:
```bash
genimage --config "${BOARD_DIR}/genimage.cfg" \
         --rootpath "${TARGET_DIR}" \
         --tmppath "${BINARIES_DIR}/genimage.tmp" \
         --inputpath "${BINARIES_DIR}" \
         --outputpath "${BINARIES_DIR}"
```

---

## 7. SDK — разработка приложений вне Buildroot

### 7.1 Генерация SDK

```bash
make sdk
```

Создаёт в `output/images/` архив вида `aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz`.

SDK содержит:
- Cross-compiler: `aarch64-buildroot-linux-gnu-gcc`.
- Sysroot: все библиотеки и заголовки target архитектуры.
- pkg-config с правильными путями для target.
- `environment-setup` скрипт для настройки окружения.

### 7.2 Установка и использование SDK

```bash
# Установить
mkdir -p /opt/myboard-sdk
tar -xf output/images/aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz \
    -C /opt/myboard-sdk --strip-components=1

# Выполнить скрипт для relocation путей (обязательно)
/opt/myboard-sdk/relocate-sdk.sh

# Настроить окружение
source /opt/myboard-sdk/environment-setup

# Проверить
echo $CC          # → aarch64-buildroot-linux-gnu-gcc
echo $CFLAGS      # → -mcpu=cortex-a76 ...
pkg-config --libs libssl  # → пути к target SSL

# Компилировать приложение
gcc -o myapp myapp.c    # gcc = cross-compiler через $CC alias
# или явно
${CC} ${CFLAGS} -o myapp myapp.c ${LDFLAGS}

# CMake проект
cmake -DCMAKE_TOOLCHAIN_FILE=$CMAKE_TOOLCHAIN_FILE ..
make
```

### 7.3 Проверка что бинарник для правильной архитектуры

```bash
file myapp
# myapp: ELF 64-bit LSB pie executable, ARM aarch64, ...

aarch64-buildroot-linux-gnu-readelf -d myapp | grep NEEDED
# shows shared library dependencies
```

---

## 8. Ускорение сборки

### 8.1 ccache

```
# menuconfig → Build options → Enable compiler cache
BR2_CCACHE=y
BR2_CCACHE_DIR="/home/user/.buildroot-ccache"
```

ccache кэширует результаты компиляции. При повторной сборке с теми же исходниками — на 10× быстрее. Общий кэш между проектами — разные конфиги для одного SoC переиспользуют кэш.

```bash
# Статистика кэша
ccache -s
```

### 8.2 Shared директория загрузок

```bash
# Установить глобально (в .bashrc или перед make)
export BR2_DL_DIR="/home/user/buildroot-dl"
# или в menuconfig → Build options → Download dir

# При следующей сборке другого проекта на том же хосте
# исходники не скачиваются повторно
```

### 8.3 External toolchain

Вместо сборки gcc из исходников — использовать готовый:
```
menuconfig → Toolchain → External toolchain
```

Экономит 30–60 минут первой сборки.

### 8.4 Команды для работы с отдельными пакетами

```bash
# Пересобрать один пакет (удалить .stamp_built и пересобрать)
make myapp-rebuild

# Полная очистка одного пакета и пересборка с нуля
make myapp-dirclean myapp

# Только конфигурация пакета (для autotools/cmake)
make myapp-configure

# Только установка (предполагая что собран)
make myapp-install

# Показать все цели для пакета
make myapp-<TAB>

# Список всех пакетов с зависимостями (граф)
make graph-depends
# Результат: output/graphs/graph-depends.pdf

# Показать что будет пересобрано при изменении файла
make myapp-show-depends

# Посмотреть конфигурацию конкретного пакета
make myapp-show-info
```

### 8.5 Параллельная сборка

```bash
make -j$(nproc)              # использовать все CPU
make -j8                     # явно задать количество джобов
```

### 8.6 Типичное время сборки

| Конфигурация | Железо | Время (без ccache) | Время (с ccache) |
|---|---|---|---|
| Buildroot toolchain + minimal rootfs | 8-core i7 | 45–90 мин | 5–15 мин |
| External toolchain + minimal rootfs | 8-core i7 | 10–20 мин | 2–5 мин |
| + ядро Linux | +5–15 мин | +2–5 мин |
| + U-Boot | +2–5 мин | +1–2 мин |

---

## 9. Интеграция с ядром и U-Boot

### 9.1 Сборка ядра через Buildroot

```
menuconfig → Kernel
  BR2_LINUX_KERNEL=y
  BR2_LINUX_KERNEL_DEFCONFIG="rockchip_defconfig"   # mainline конфиг
  # или
  BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE="board/mycompany/myboard/linux.config"
  BR2_LINUX_KERNEL_DTS_SUPPORT=y
  BR2_LINUX_KERNEL_INTREE_DTS_NAME="rockchip/rk3588-evb1-v10"
```

Buildroot скачает ядро, применит патчи (если есть), вызовет `make ARCH=arm64 CROSS_COMPILE=... rockchip_defconfig && make -j$(nproc)`.

Для кастомного ядра:
```
BR2_LINUX_KERNEL_CUSTOM_TARBALL=y
BR2_LINUX_KERNEL_CUSTOM_TARBALL_LOCATION="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.6.tar.xz"
# или
BR2_LINUX_KERNEL_CUSTOM_GIT=y
BR2_LINUX_KERNEL_CUSTOM_REPO_URL="https://github.com/mycompany/linux.git"
BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION="myboard-v6.6"
```

### 9.2 U-Boot через Buildroot

```
menuconfig → Bootloaders → U-Boot
  BR2_TARGET_UBOOT=y
  BR2_TARGET_UBOOT_BUILD_SYSTEM_KCONFIG=y
  BR2_TARGET_UBOOT_BOARD_DEFCONFIG="evb-rk3588"
  BR2_TARGET_UBOOT_NEEDS_DTC=y
```

Для Rockchip нужны дополнительные бинарники (TF-A, DDR blob):
```
BR2_TARGET_UBOOT_CUSTOM_EXTRA_ENV_VAR="\
    BL31=/path/to/bl31.elf \
    ROCKCHIP_TPL=/path/to/ddr.bin"
```

### 9.3 br2-external

Для production проекта: держать board-specific файлы в отдельном репозитории, не в Buildroot дереве.

```bash
# Структура внешнего репозитория
myproject-br2ext/
├── Config.in           # корневой Kconfig
├── external.mk         # корневой Makefile include
├── external.desc       # описание (name, desc, url)
├── configs/
│   └── myboard_defconfig
├── package/
│   └── myapp/
│       ├── Config.in
│       └── myapp.mk
└── board/
    └── mycompany/
        └── myboard/
            ├── post-build.sh
            ├── rootfs-overlay/
            └── genimage.cfg

# Использование
BR2_EXTERNAL=/path/to/myproject-br2ext make myboard_defconfig
make -j$(nproc)
```

Преимущество: обновление Buildroot (`git pull`) не затрагивает проектные файлы.

---

## 10. UBIFS для raw NAND flash

### 10.1 Когда нужен UBIFS

NAND flash (в отличие от eMMC) требует специальной работы: страницы нельзя записывать повторно без предварительного стирания блока, блоки имеют ограниченный ресурс (~100k циклов). UBIFS + UBI управляет этим автоматически.

```
BR2_TARGET_ROOTFS_UBIFS=y
BR2_TARGET_ROOTFS_UBIFS_LEBSIZE=0x1f000     # logical erase block size
BR2_TARGET_ROOTFS_UBIFS_MINIOSIZE=0x800     # minimum I/O size (page size)
BR2_TARGET_ROOTFS_UBIFS_MAXLEBCNT=2048      # max erase blocks
```

Параметры зависят от конкретного NAND чипа (читать datasheet).

```bash
# На устройстве: монтирование UBI
ubiattach /dev/ubi_ctrl -m 0          # присоединить MTD устройство
mount -t ubifs ubi0:rootfs /          # монтировать volume
```

### 10.2 Прошивка через U-Boot

```bash
# U-Boot: прошить NAND
nand erase.part rootfs
ubi part rootfs
ubifsmount ubi0
ubifsls /
```

---

## 11. Лицензионный анализ

### 11.1 Buildroot умеет собирать лицензионные отчёты

```bash
make legal-info
```

Создаёт `output/legal-info/`:
```
legal-info/
├── buildroot.config      # конфигурация сборки
├── host-licenses/        # лицензии host инструментов
├── host-sources/         # исходники host инструментов
├── licenses/             # лицензионные файлы target пакетов
├── manifest.csv          # таблица: пакет, версия, лицензия
└── sources/              # исходники target пакетов (для GPL compliance)
```

`manifest.csv` — главный документ для юридической проверки. Формат:
```
package,version,license,license-file,cpe-id
busybox,1.36.1,GPL-2.0+,LICENSE,...
openssl,3.1.4,OpenSSL,LICENSE.txt,...
```

### 11.2 GPL compliance

GPL требует: при распространении бинарника — предоставить исходники или обязательство предоставить. Buildroot собирает все исходники в `output/legal-info/sources/`.

Внимание: при добавлении пакетов с GPL лицензией в продукт — обязательно проверить правила distribution.

---

## 12. Практика модуля

### Задание 1: Минимальная сборка для QEMU

```bash
cd buildroot
make qemu_aarch64_virt_defconfig

# Минимизировать: убрать лишнее через menuconfig
# Оставить: busybox, dropbear SSH (опционально)
make menuconfig

make -j$(nproc)

# Запустить в QEMU
qemu-system-aarch64 \
    -machine virt \
    -cpu cortex-a57 \
    -m 512M \
    -nographic \
    -kernel output/images/Image \
    -append "console=ttyAMA0 root=/dev/vda rw" \
    -drive file=output/images/rootfs.ext4,format=raw,id=hd0 \
    -device virtio-blk-device,drive=hd0

# Войти в систему (root без пароля)
# Проверить что работает: ls /proc, busybox --list, uname -a
```

### Задание 2: Пакет для C приложения

```c
/* myapp/main.c */
#include <stdio.h>
int main(void) {
    printf("Hello from Buildroot package!\n");
    printf("Arch: " __ARCH__ "\n");
    return 0;
}
```

```makefile
# myapp/Makefile
CC ?= gcc
all: myapp
myapp: main.c
	$(CC) $(CFLAGS) -o myapp main.c
clean:
	rm -f myapp
```

Написать `package/myapp/myapp.mk` с `SITE_METHOD = local` и `SITE` = абсолютный путь, добавить в `package/Config.in`, включить в menuconfig, собрать, проверить в QEMU что `/usr/bin/myapp` запускается и выводит правильную архитектуру.

### Задание 3: Overlay и init скрипт

Создать overlay `board/test/myboard/rootfs-overlay/` с:
- `/etc/hostname` = `myboard-test`
- `/etc/init.d/S50hello` — init скрипт выводящий `Hello from MyBoard` при старте
- `/etc/myboard-version` с датой сборки (через `$(date)` в post-build скрипте)

Настроить `BR2_ROOTFS_OVERLAY` и `BR2_ROOTFS_POST_BUILD_SCRIPT`, запустить в QEMU, убедиться что hostname и вывод init скрипта правильные.

### Задание 4: SDK и внешняя компиляция

```bash
# Сгенерировать SDK
make sdk

# Установить
mkdir -p /tmp/test-sdk
tar -xf output/images/*sdk*.tar.gz -C /tmp/test-sdk --strip-components=1
/tmp/test-sdk/relocate-sdk.sh
source /tmp/test-sdk/environment-setup

# Скомпилировать приложение вне Buildroot
cd /tmp
cat > hello.c << 'EOF'
#include <stdio.h>
int main(void) { printf("SDK test\n"); return 0; }
EOF

${CC} ${CFLAGS} -o hello hello.c
file hello   # должен быть aarch64
```

---

## 13. Самопроверка

**Вопрос 1:** Что находится в `output/staging/` и зачем он нужен при разработке?

<details>
<summary>Ответ</summary>

`output/staging/` — это sysroot для target архитектуры: заголовочные файлы (`include/`), статические и shared библиотеки (`lib/`), pkg-config файлы — всё для AArch64, не для хоста. Используется при кросс-компиляции приложений вне Buildroot: `gcc --sysroot=output/staging`. На устройство не копируется: stripped библиотеки без debug символов идут в `output/target/`, полные версии — только в staging.

</details>

**Вопрос 2:** Чем `BR2_PACKAGE_HOST_*` отличается от `BR2_PACKAGE_*`?

<details>
<summary>Ответ</summary>

`BR2_PACKAGE_*` — пакет собирается для target архитектуры (AArch64), устанавливается в `output/target/` (rootfs устройства). Пример: `BR2_PACKAGE_OPENSSH=y` — openssh будет на устройстве.
`BR2_PACKAGE_HOST_*` — пакет собирается для host архитектуры (x86_64), устанавливается в `output/host/`, используется только в процессе сборки на хосте. Пример: `BR2_PACKAGE_HOST_GENIMAGE=y` — genimage запускается на хосте для создания SD card image. На устройство не попадает.

</details>

**Вопрос 3:** Как добавить пакет с autoconf сборкой?

<details>
<summary>Ответ</summary>

1. Создать `package/mypkg/mypkg.mk` с переменными `MYPKG_VERSION`, `MYPKG_SITE`, `MYPKG_CONF_OPTS` (дополнительные configure флаги) и строкой `$(eval $(autotools-package))`.
2. Создать `package/mypkg/Config.in` с `config BR2_PACKAGE_MYPKG`.
3. Добавить `source "package/mypkg/Config.in"` в `package/Config.in`.
4. Создать `package/mypkg/mypkg.hash` с SHA256 хешем архива.
5. Включить в menuconfig: `make menuconfig` → найти пакет → включить.
Buildroot автоматически вызовет configure с `--host=aarch64-linux-gnu --prefix=/usr`, make и make install.

</details>

**Вопрос 4:** Почему squashfs предпочтителен для production rootfs на embedded?

<details>
<summary>Ответ</summary>

squashfs — read-only сжатая ФС. Преимущества: (1) Power-cut safe: нет состояния записи → при обрыве питания FS не повреждается, fsck не нужен; (2) Сжатие xz/zstd: образ 2–4× меньше ext4, важно для ограниченного flash; (3) Целостность: read-only → приложение не может случайно изменить системные файлы; (4) Нет износа flash: NAND/NOR flash имеет ресурс записи; read-only rootfs не изнашивает. Изменяемые данные (/var, /tmp) — в отдельном ext4 разделе или overlayfs поверх tmpfs.

</details>

**Вопрос 5:** Что происходит при `make myapp-rebuild`?

<details>
<summary>Ответ</summary>

Buildroot удаляет stamp файл `.stamp_built` в `output/build/myapp-<ver>/`, что заставляет пересобрать только фазы build и install. Исходники не скачиваются заново (если не изменился `MYAPP_VERSION`), patch не применяется, configure не перезапускается. После rebuild → повторная установка в `output/target/`. Для полного сброса с нуля: `make myapp-dirclean` (удаляет весь `output/build/myapp-*/`) и затем `make myapp`.

</details>

**Вопрос 6:** Как настроить NFS root для разработки с Buildroot?

<details>
<summary>Ответ</summary>

1. В Buildroot: включить `BR2_TARGET_ROOTFS_TAR=y`, собрать.
2. На хосте: `tar -xf output/images/rootfs.tar -C /srv/nfs/myboard`, настроить `/etc/exports`: `/srv/nfs/myboard 192.168.1.0/24(rw,sync,no_root_squash)`.
3. В U-Boot: `setenv bootargs "root=/dev/nfs nfsroot=192.168.1.1:/srv/nfs/myboard,nfsvers=3 ip=dhcp console=ttyS2,1500000"`.
Быстрый workflow: изменить файл в `/srv/nfs/myboard/` → он сразу доступен на устройстве без перезагрузки (для запущенных процессов), или перезагрузить для init изменений.

</details>

**Вопрос 7:** Что такое `BR2_ROOTFS_OVERLAY`?

<details>
<summary>Ответ</summary>

`BR2_ROOTFS_OVERLAY` — путь к директории (или несколько путей через пробел), файлы из которой копируются поверх `output/target/` после сборки всех пакетов. Позволяет добавить board-specific файлы: конфиги, init скрипты, SSH ключи, firmware — без изменения пакетов Buildroot. При пересборке overlay применяется заново. В отличие от прямого изменения `output/target/` — сохраняется при `make clean`.

</details>

**Вопрос 8:** Почему нельзя вручную менять файлы в `output/target/`?

<details>
<summary>Ответ</summary>

`output/target/` управляется Buildroot: при пересборке пакета Buildroot перезапишет его файлы в `output/target/`. Ручные изменения будут потеряны. Правильные способы кастомизации: (1) overlay (`BR2_ROOTFS_OVERLAY`) — для фиксированных файлов; (2) post-build скрипт — для динамической генерации; (3) написать пакет Buildroot — для приложений; (4) пропатчить существующий пакет — для изменения upstream кода.

</details>

**Вопрос 9:** Как проверить лицензионную совместимость всех пакетов?

<details>
<summary>Ответ</summary>

```bash
make legal-info
```
Создаёт `output/legal-info/manifest.csv` со всеми пакетами, их версиями и лицензиями. В `output/legal-info/licenses/` — тексты лицензий. В `output/legal-info/sources/` — исходники (для GPL compliance: нужно предоставить при распространении бинарников). Анализировать `manifest.csv`: проверить что нет несовместимых лицензий, что GPL пакеты не используют проприетарные компоненты без разрешения.

</details>

**Вопрос 10:** Чем Buildroot отличается от Yocto/OpenEmbedded подходом?

<details>
<summary>Ответ</summary>

Buildroot: простота и скорость. Фиксированная версия всего: один `make` → конкретный набор версий пакетов. Конфигурация одним menuconfig. Ограниченная гибкость: сложно иметь несколько версий одного пакета, нет shared state cache между проектами (кроме DL_DIR). Хорош для: production продукта с фиксированными зависимостями, embedded устройства с небольшим набором пакетов.
Yocto/OpenEmbedded: гибкость и масштабируемость. Layers (слои) — независимые репозитории с рецептами. Shared state cache — артефакты сборки переиспользуются между проектами. Поддерживает несколько версий пакетов. Крутая кривая обучения. Хорош для: дистрибутива, когда нужны варианты сборки, корпоративная разработка несколькими командами.

</details>

---

## 14. Банк вопросов

### БАЗА — термины и факты (MCQ)

**Б1.** Что содержит `output/staging/` в Buildroot?

- a) Финальные образы: rootfs.ext4, sdcard.img
- **b) Sysroot: заголовки и библиотеки для target архитектуры (не копируются на устройство)**
- c) Исходники всех пакетов после распаковки
- d) Host инструменты: cross-compiler, dtc, mkimage

*Ответ: b. staging = sysroot для кросс-компиляции. На устройство идёт output/target/, скомпилированные образы — output/images/.*

---

**Б2.** Команда `make menuconfig` в Buildroot позволяет:

- a) Только настроить конфигурацию ядра Linux
- **b) Настроить конфигурацию Buildroot в целом: toolchain, пакеты, init system, форматы образов**
- c) Создать новый defconfig для платы
- d) Настроить U-Boot конфигурацию

*Ответ: b. make menuconfig = конфигурация системы Buildroot. make linux-menuconfig = конфигурация ядра. make uboot-menuconfig = конфигурация U-Boot.*

---

**Б3.** Что делает `BR2_ROOTFS_OVERLAY`?

- a) Монтирует overlayfs поверх squashfs на устройстве
- b) Создаёт дополнительный ext4 раздел поверх rootfs
- **c) Копирует файлы из указанной директории поверх output/target/ после сборки**
- d) Встраивает дополнительный initramfs в образ ядра

*Ответ: c. Overlay = директория с board-specific файлами (конфиги, init скрипты), копируется поверх target/ до упаковки в образ.*

---

**Б4.** Чем `BR2_PACKAGE_HOST_GENIMAGE` отличается от `BR2_PACKAGE_GENIMAGE`?

- a) HOST версия поддерживает больше форматов образов
- **b) HOST версия собирается для x86 хоста и запускается при сборке; обычная версия — для target устройства**
- c) Они идентичны, HOST — устаревший префикс
- d) HOST версия использует аппаратное ускорение на хосте

*Ответ: b. HOST пакеты = инструменты для хоста (genimage, dtc, mkimage). Они нужны в процессе сборки на x86, а не на ARM устройстве.*

---

**Б5.** Зачем нужен SDK (`make sdk`)?

- a) Для обновления прошивки устройства по OTA
- b) Для ускорения последующих сборок Buildroot
- **c) Для разработки приложений вне Buildroot: cross-compiler + sysroot в одном архиве**
- d) Для интеграции с IDE (Eclipse, CLion)

*Ответ: c. SDK = self-contained архив с toolchain и sysroot, устанавливается разработчикам которые не работают с Buildroot напрямую.*

---

**Б6.** Почему squashfs предпочтительнее ext4 для rootfs на production embedded устройстве?

- a) squashfs поддерживает journaling, что защищает от потери данных
- b) squashfs быстрее читается с NAND flash
- **c) squashfs read-only и сжатый: нет риска повреждения при обрыве питания, меньше размер**
- d) squashfs работает на всех типах flash без wear leveling

*Ответ: c. Read-only = power-cut safe (нечего «достирывать»). Сжатие xz/zstd = меньше flash занимает. ext4 для данных, squashfs для rootfs.*

---

**Б7.** Что означает префикс `BR2_PACKAGE_HOST_` перед именем пакета?

- a) Пакет собирается в контейнере Docker на хосте
- **b) Пакет собирается для архитектуры хоста (x86_64) и используется в процессе сборки**
- c) Пакет загружается с хост-сервера, а не из интернета
- d) Пакет является зеркалом upstream пакета для embedded

*Ответ: b. HOST пакеты = build-time инструменты: dtc, genimage, mkimage, pkgconf. Работают на x86 хосте, на ARM устройство не попадают.*

---

**Б8.** Что делает `BR2_CCACHE=y` в Buildroot?

- a) Включает кэширование загруженных исходников в DL_DIR
- b) Кэширует результаты компиляции ядра между перезапусками
- **c) Включает ccache для кэширования результатов компиляции объектных файлов**
- d) Кэширует конфигурацию menuconfig между сборками

*Ответ: c. ccache перехватывает вызовы компилятора, сохраняет результаты в кэш. При повторной сборке с теми же исходниками — берёт из кэша. Ускорение: 5–20× после прогрева кэша.*

---

### МЕХАНИЗМЫ — как и почему работает (self_grade)

**М1.** Объясни что происходит при `make myapp-rebuild`. Чем отличается от `make myapp-dirclean && make myapp`? Когда каждый из вариантов нужен?

*Эталон: `myapp-rebuild` удаляет `.stamp_built` → пересобирает фазы build+install (configure не повторяется). Полезно если изменился код пакета без смены зависимостей. `myapp-dirclean` удаляет весь `output/build/myapp-*/` → следующий `make myapp` начинает с нуля: extract, patch, configure, build, install. Нужно при смене версии, смене configure флагов или подозрении на битое состояние. Полный сброс всего: `make clean` удаляет весь output/ кроме DL_DIR.*

---

**М2.** Как написать Buildroot пакет для C++ приложения, использующего CMake и зависящего от libssl и libusb? Перечислить все необходимые файлы и их содержимое.

*Эталон: `package/myapp/Config.in` с `config BR2_PACKAGE_MYAPP depends on BR2_PACKAGE_OPENSSL && BR2_PACKAGE_LIBUSB`. `package/myapp/myapp.mk`: MYAPP_VERSION, MYAPP_SITE, MYAPP_DEPENDENCIES = openssl libusb, MYAPP_CONF_OPTS = -DENABLE_TESTS=OFF, `$(eval $(cmake-package))`. `package/myapp/myapp.hash` с SHA256 архива. Добавить `source "package/myapp/Config.in"` в package/Config.in. Buildroot автоматически передаст cmake toolchain файл с правильными AArch64 настройками.*

---

**М3.** Объясни механизм `post-build.sh`: когда вызывается, какие переменные доступны, как правильно использовать для генерации build-info файла с датой и версией?

*Эталон: post-build.sh вызывается после установки всех пакетов в TARGET_DIR и наложения overlay, до упаковки в образ. Аргумент $1 = path к TARGET_DIR. Переменные: TARGET_DIR, HOST_DIR, STAGING_DIR, BINARIES_DIR, BR2_CONFIG (путь к .config). Пример build-info: `echo "DATE=$(date -u +%Y%m%d)" > ${TARGET_DIR}/etc/build-info`. Важно: скрипт выполняется на хосте с x86 окружением, не на ARM. Не вызывать ARM бинарники напрямую.*

---

**М4.** Как настроить reproducible builds в Buildroot? Что мешает воспроизводимости и что делает Buildroot для её обеспечения?

*Эталон: Проблемы воспроизводимости: таймстемпы в файлах, hostname в бинарниках, порядок сборки. Buildroot меры: SOURCE_DATE_EPOCH для фиксации времени в архивах; фиксированные версии пакетов через defconfig; DL_DIR с хешами (HASH файлы) для верификации загрузок. Не воспроизводимо: если пакеты берутся с git HEAD без тега (BR2_LINUX_KERNEL_CUSTOM_GIT без фиксированного commit). Рекомендация production: фиксировать все версии, использовать release тег Buildroot, хранить DL_DIR в артефактах CI.*

---

**М5.** Объясни pipeline от исходников до SD card image: какие стадии, какие инструменты, где что создаётся?

*Эталон: (1) Download → output/dl/ (tarball/git clone); (2) Extract → output/build/pkg-ver/; (3) Patch → применить patch/*.patch; (4) Configure → ./configure или cmake; (5) Build → make; (6) Install staging → output/staging/ (библиотеки+заголовки); (7) Install target → output/target/ (stripped бинарники); (8) Post-build скрипт → модификации target/; (9) mkfs → output/images/rootfs.ext4 (через mkfs.ext4); (10) genimage → output/images/sdcard.img (partition table + boot + rootfs).*

---

**М6.** Как правильно хранить board-specific файлы: когда использовать overlay, когда post-build скрипт, когда br2-external? Опиши production-рекомендованную структуру проекта.

*Эталон: Overlay — для статических файлов: конфиги, SSH ключи, init скрипты, firmware blob. Post-build скрипт — для динамически генерируемых: build-info с датой, условная логика, удаление лишних файлов. br2-external — отдельный git репозиторий для всего board-specific: конфиги, пакеты, overlay, скрипты. Production структура: `company-bsp/` (br2-external) рядом с `buildroot/`, подключается через `BR2_EXTERNAL=../company-bsp`. Buildroot обновляется независимо от BSP репозитория.*

---

**М7.** Как настроить сборку ядра с кастомным defconfig и кастомными патчами через Buildroot?

*Эталон: В menuconfig: BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE="board/vendor/board/linux.config" (абсолютный путь или относительно TOPDIR). Патчи: создать директорию BR2_GLOBAL_PATCH_DIR или использовать `board/vendor/board/linux-patches/` — Buildroot применяет все .patch файлы в алфавитном порядке. Поддержка нескольких версий ядра: BR2_LINUX_KERNEL_CUSTOM_TARBALL_LOCATION с конкретной версией или git repo + branch. После изменения linux.config: `make linux-update-defconfig` обновит файл.*

---

**М8.** Сравни `glibc`, `musl` и `uclibc-ng` для embedded: размер, совместимость, когда выбирать что.

*Эталон: glibc: полная POSIX совместимость, все pthreads, locale, NSS. Размер: ~3–5 MB stripped. Практически все upstream приложения компилируются без проблем. Выбор для: RK3588 с 2+ GB RAM где размер не критичен, Python/Node.js/Qt требуют glibc. musl: маленький (~200 KB), чистая реализация POSIX. Ограничения: нет NSS, locale ограниченны, некоторые пакеты (nscd, libnsl-зависимые) не работают. Выбор для: минимальный rootfs, контейнеры, статическая линковка. uclibc-ng: устаревает, меньше поддержки, избегать для новых проектов.*

---

### ЭКСПЕРТ — архитектура и edge cases (self_grade)

**Э1.** Полная процедура license compliance audit для production продукта: какие команды, какие файлы проверять, что делать с GPL пакетами, как автоматизировать в CI.

*Эталон: `make legal-info` → output/legal-info/. manifest.csv — автоматически анализировать: (1) список всех GPL/LGPL пакетов → нужно предоставить исходники при распространении; (2) copyleft contamination: GPL приложение может требовать GPL для linkеd библиотек; (3) проверить что нет несовместимых лицензий (GPLv2-only vs GPLv3). CI: архивировать output/legal-info/sources/ как артефакт сборки; сравнивать manifest.csv между сборками; алертить при добавлении новых лицензий. Инструменты: scancode-toolkit для дополнительного сканирования, SPDX для машиночитаемого формата.*

---

**Э2.** Как написать Buildroot пакет для библиотеки с cmake, которая должна быть доступна как в staging (для линковки других пакетов) так и в target (shared lib). Объясни INSTALL_STAGING vs INSTALL_TARGET.

*Эталон: `MYLIB_INSTALL_STAGING = YES` → Buildroot вызывает `make DESTDIR=$(STAGING_DIR) install` → заголовки в staging/usr/include/, .so в staging/usr/lib/. Другие пакеты могут linковаться через pkg-config (если mylib.pc устанавливается в staging/usr/lib/pkgconfig/). `MYLIB_INSTALL_TARGET = YES` → `make DESTDIR=$(TARGET_DIR) install` → только .so (без заголовков, без static libs) → попадает на устройство для runtime. Важно: в cmake пакете static libs не устанавливаются в target автоматически (Buildroot имеет BR2_PREFER_STATIC_LIB для исключений).*

---

**Э3.** Multi-config Buildroot: как поддерживать несколько вариантов сборки (debug/release, разные платы) в одном репозитории без конфликтов?

*Эталон: Buildroot не поддерживает несколько output/ из коробки, но: (1) `O=/path/to/output` переменная задаёт output директорию: `make O=/build/myboard-release rockchip_rk3588_defconfig && make O=/build/myboard-release`; (2) отдельные defconfig для каждого варианта (myboard_release_defconfig, myboard_debug_defconfig); (3) br2-external с board/ директориями для каждой платы; (4) CI матрица: parallel builds для каждого O= и defconfig. Debug вариант: включить `BR2_ENABLE_DEBUG=y`, `BR2_STRIP_NONE=y`, добавить gdb, strace.*

---

**Э4.** UBIFS для raw NAND flash: объясни разницу между MTD, UBI и UBIFS. Какие параметры нужно знать из NAND datasheet для конфигурации Buildroot?

*Эталон: MTD (Memory Technology Device) — ядерный уровень для raw flash: прямой доступ к erase blocks и pages через /dev/mtdX. UBI (Unsorted Block Images) — уровень поверх MTD: wear leveling, bad block management, логические volumes. UBIFS — файловая система поверх UBI volumes: journaling, compression, работает как обычная FS. Параметры из datasheet: page size (min I/O unit, обычно 2KB или 4KB), erase block size (обычно 128KB-2MB), OOB size (spare bytes per page). Buildroot: UBIFS_LEBSIZE = erase_block_size - 2*page_size (overhead); MINIOSIZE = page_size; MAXLEBCNT = flash_size / erase_block_size - reserved.*

---

**Э5.** Reproducible builds в Buildroot: что именно не воспроизводимо, как зафиксировать, как проверить что два разных билда дают идентичные образы.

*Эталон: Не воспроизводимо: (1) таймстемпы в файлах (ctime, mtime) → использовать SOURCE_DATE_EPOCH; (2) hostname в бинарниках (gcc встраивает) → BR2_REPRODUCIBLE=y устанавливает SOURCE_DATE_EPOCH; (3) git пакеты без фиксированного commit → использовать теги; (4) зависимость от системных библиотек хоста → Docker контейнер с фиксированными версиями. Проверка: sha256sum двух образов, или diffoscope для побайтного сравнения и выявления различий. Production pipeline: Docker образ с фиксированными зависимостями + фиксированный тег Buildroot + зафиксированные версии всех кастомных пакетов.*

