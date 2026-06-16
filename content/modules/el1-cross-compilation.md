# Модуль EL1 — Кросс-компиляция и ARM toolchain

## 0. Карта модуля

| Параметр | Значение |
|----------|---------|
| Время | 10–15 ч |
| Уровень | Embedded Linux, начальный |
| Зависимости | F1 (C для системного кода), F2 (Linux API) |

**Зачем:** весь embedded Linux собирается на x86 хосте для ARM цели (host != target). Без понимания toolchain-цепочки невозможно отлаживать ошибки сборки, настраивать sysroot, собирать ядро под конкретный SoC.

**Главные инструменты:**
- `gcc` кросс-компилятор, `clang --target`
- `binutils`: `objdump`, `readelf`, `nm`, `strip`, `size`
- `gdb` multiarch, `gdbserver`
- `QEMU` user-mode и system-mode
- `file(1)`, `ldd` (динамический линкёр)

**Референсы:**
- GCC manual, раздел «Cross-Compilation»
- Bootlin embedded Linux training materials (free PDF)
- `man gcc`, `man ld`, `man readelf`
- devicetree.org/specifications (для следующего модуля)

---

## 1. Почему кросс-компиляция — не просто «другой gcc»

### 1.1 Тройка build/host/target

В мире компиляторов есть три независимые машины:

| Понятие | Что это | Пример |
|---------|---------|--------|
| **Build** | Машина, где **компилируется сам компилятор** | `x86_64-linux-gnu` |
| **Host** | Машина, где **запускается компилятор** | `x86_64-linux-gnu` (обычно = Build) |
| **Target** | Машина, где **будет запущен результат компиляции** | `aarch64-linux-gnu` (RK3588) |

В типичном embedded-проекте Build == Host == рабочая станция разработчика (x86_64), Target == ARM плата. Это и есть кросс-компиляция: один компилятор запускается на одной архитектуре и генерирует код для другой.

Термин «native compilation» — когда все три одинаковы. «Canadian cross» — когда все три разные (компилятор для ARM собирается на x86, но сам собирается MIPS-машиной).

### 1.2 Три части toolchain

Toolchain — не один бинарь, а набор инструментов:

**1. Compiler (gcc/clang)** — принимает исходник на C/C++, генерирует машинный код для target-архитектуры. Без понимания target-ISA компилятор не может генерировать правильные инструкции.

**2. Binutils** — набор утилит для работы с object-файлами и ELF:
- `as` — ассемблер (`.s` → `.o`)
- `ld` — линкёр (`.o` + `.a`/`.so` → исполняемый ELF)
- `objdump` — дизассемблер и инспектор секций
- `readelf` — парсинг ELF-заголовков, секций, символов
- `nm` — список символов из object-файла
- `strip` — удаление отладочных символов (уменьшение размера)
- `objcopy` — конвертация форматов (ELF → raw binary, srec, ihex)
- `size` — размеры секций .text/.data/.bss

**3. C library (libc)** — runtime для target:
- **glibc** (GNU C Library) — полнофункциональная, для Linux-систем с glibc ≥ 2.x. Большой размер (~2 МБ).
- **musl** — компактная, строгое следование стандарту. Используется в Alpine Linux, Buildroot musl-вариантах.
- **uClibc-ng** — для очень ограниченных систем (< 512 КБ флеш).
- **newlib** — для baremetal (arm-none-eabi), нет поддержки Linux syscalls.
- **Bionic** — Android's C library.

### 1.3 Разбор имён toolchain (GNU triplet)

Формат: `arch-vendor-os-abi`

| Toolchain | arch | vendor | os | abi |
|-----------|------|--------|----|----|
| `aarch64-linux-gnu-gcc` | aarch64 | (нет) | linux | gnu (glibc) |
| `arm-linux-gnueabihf-gcc` | arm | (нет) | linux | gnueabihf |
| `arm-none-eabi-gcc` | arm | none | none (bare) | eabi |
| `x86_64-linux-gnu-gcc` | x86_64 | (нет) | linux | gnu |

**Расшифровка ABI-суффиксов для ARM:**
- `eabi` — Embedded ABI (современный 32-бит ARM ABI)
- `eabihf` — EABI + Hard Float: float-аргументы передаются через FPU-регистры (VFP/NEON), не через целочисленные. Быстрее, но несовместимо с soft-float библиотеками.
- отсутствие `hf` → soft-float или soft-fp ABI

### 1.4 Три главных toolchain для ARM

**`arm-linux-gnueabihf`** (ARM 32-бит, hard-float, Linux, glibc):
- Для Cortex-A с Linux ОС (Raspberry Pi 32-бит, i.MX6, AM335x)
- Работает с устройствами где ARMv7-A + VFPv3/NEON
- Пример: Orange Pi Zero с Allwinner H2+

**`aarch64-linux-gnu`** (ARM 64-бит, Linux, glibc):
- Для ARMv8-A и выше: RK3588, RK3568, i.MX8, Raspberry Pi 4+
- Всегда hard-float (в AArch64 нет soft-float ABI)
- Нативный размер слова — 64 бит

**`arm-none-eabi`** (ARM baremetal, нет ОС, нет glibc):
- Для Cortex-M (STM32, nRF52, LPC), Cortex-R без ОС
- Нет `fork()`, `mmap()`, `pthread` — нет syscalls
- Линкуется с newlib или без libc
- Генерирует «голый» машинный код — загрузчик сам кладёт в нужный адрес

Критически важно не путать: `arm-none-eabi-gcc` компилирует для STM32 — не для Linux на ARM. Бинарник не запустится на RK3588 под Linux.

---

## 2. Установка и проверка toolchain

### 2.1 Дистрибутивный toolchain (Ubuntu/Debian)

```bash
# Установка для 64-бит ARM (AArch64)
sudo apt install gcc-aarch64-linux-gnu \
                 binutils-aarch64-linux-gnu \
                 g++-aarch64-linux-gnu

# Установка для 32-бит ARM с hard-float
sudo apt install gcc-arm-linux-gnueabihf \
                 binutils-arm-linux-gnueabihf

# Проверка версии
aarch64-linux-gnu-gcc --version
# → aarch64-linux-gnu-gcc (Ubuntu 11.4.0-1ubuntu1~22.04) 11.4.0

# Проверка target (machine)
aarch64-linux-gnu-gcc -dumpmachine
# → aarch64-linux-gnu
```

### 2.2 Linaro / Bootlin prebuilt toolchains

Дистрибутивный toolchain — удобен, но версия GCC зафиксирована. Когда нужна конкретная версия (например, GCC 12.x для kernel 6.6, или musl вместо glibc):

**Linaro:** https://releases.linaro.org/components/toolchain/binaries/
- Обновлённые GCC для Arm Ltd., бинарники для Ubuntu
- Пример: `gcc-linaro-12.3.1-2023.06-x86_64_aarch64-linux-gnu.tar.xz`

**Bootlin:** https://toolchains.bootlin.com/
- Toolchain builder на основе Buildroot
- Выбор: архитектура + C library (glibc/musl/uClibc) + версия GCC
- Особенно полезен для musl-toolchain

Установка (без package manager):
```bash
tar -xf gcc-linaro-12.3.1-2023.06-x86_64_aarch64-linux-gnu.tar.xz -C /opt/
export PATH=/opt/gcc-linaro-12.3.1-2023.06-x86_64_aarch64-linux-gnu/bin:$PATH
aarch64-linux-gnu-gcc --version
```

### 2.3 Проверка что собрали для правильной архитектуры

```bash
# Тестовая программа
cat > hello.c << 'EOF'
#include <stdio.h>
int main(void) { puts("hello ARM"); return 0; }
EOF

# Кросс-компиляция
aarch64-linux-gnu-gcc hello.c -o hello_arm

# Проверка 1: file — тип ELF
file hello_arm
# → hello_arm: ELF 64-bit LSB pie executable, ARM aarch64,
#   version 1 (SYSV), dynamically linked,
#   interpreter /lib/ld-linux-aarch64.so.1, ...

# Проверка 2: readelf — машинная архитектура
readelf -h hello_arm | grep Machine
# → Machine:                           AArch64

# Проверка 3: попытка запустить на x86 завершится ошибкой
./hello_arm
# → bash: ./hello_arm: cannot execute binary file: Exec format error
```

### 2.4 Ключевые флаги компилятора для embedded

```bash
# Оптимизация под размер (важно для flash-ограниченных устройств)
aarch64-linux-gnu-gcc -Os hello.c -o hello_arm

# Указание ISA и CPU
aarch64-linux-gnu-gcc -march=armv8-a -mtune=cortex-a55 hello.c -o hello_arm

# Для 32-бит Cortex-A7
arm-linux-gnueabihf-gcc -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard hello.c -o hello_arm32

# Статическая линковка (всё в один бинарь)
aarch64-linux-gnu-gcc -static hello.c -o hello_arm_static
file hello_arm_static
# → ELF 64-bit LSB executable, ..., statically linked

# Просмотр размеров секций
aarch64-linux-gnu-size hello_arm
#   text    data     bss     dec     hex filename
#   1234      56       0    1290     50a hello_arm
```

---

## 3. Sysroot — корень чужой системы

### 3.1 Что такое sysroot

**Sysroot** — директория, которая является «виртуальным корнем /» для компилятора при поиске заголовков и библиотек target-системы.

Компилятор без sysroot ищет:
- Заголовки: `/usr/include/`
- Библиотеки: `/usr/lib/`, `/lib/`

Это заголовки **host** системы. Если target использует другую версию glibc или имеет специфические ядерные заголовки — компиляция против host-заголовков приведёт к:
- `GLIBC_2.33 not found` при запуске на устройстве (если host glibc новее)
- Ошибкам API если ядерные интерфейсы отличаются
- `undefined reference` для библиотек, которых нет на target

С sysroot компилятор ищет:
- `<sysroot>/usr/include/`
- `<sysroot>/usr/lib/`
- `<sysroot>/lib/`

### 3.2 Флаги sysroot

```bash
# Указание sysroot при компиляции
aarch64-linux-gnu-gcc \
    --sysroot=/path/to/sysroot \
    -o prog prog.c

# Или через переменную (Buildroot-style)
SYSROOT=$(aarch64-linux-gnu-gcc -print-sysroot)
echo $SYSROOT
# → /usr/aarch64-linux-gnu  (для дистрибутивного toolchain)
```

Дистрибутивный toolchain уже содержит минимальный sysroot: `/usr/aarch64-linux-gnu/`. Это набор заголовков и библиотек скомпилированных для aarch64. Но версии glibc могут не совпадать с конкретным устройством.

### 3.3 Как получить правильный sysroot

**Вариант 1: Buildroot**
```bash
# После make в Buildroot
ls output/staging/
# → bin/ etc/ lib/ sbin/ usr/

# Это и есть sysroot: заголовки и библиотеки как на target
aarch64-linux-gnu-gcc --sysroot=output/staging/ -o prog prog.c
```

**Вариант 2: Yocto SDK**
```bash
# Yocto генерирует скрипт-обёртку
source environment-setup-aarch64-poky-linux

# После source: переменные CC, CXX, LD, CFLAGS уже выставлены
# с --sysroot= внутри
$CC -o prog prog.c   # использует правильный sysroot автоматически

# Посмотреть что выставлено
echo $CC
# → aarch64-poky-linux-gcc -march=armv8-a --sysroot=/opt/poky/4.0/...
```

**Вариант 3: debootstrap / multistrap (Debian target)**
```bash
# Создать sysroot с пакетами Debian bookworm для arm64
sudo apt install multistrap
multistrap -a arm64 -d /opt/sysroot-arm64 \
    -f multistrap.conf

# Исправить symlinks (debootstrap создаёт абсолютные)
sudo apt install symlinks
sudo symlinks -cr /opt/sysroot-arm64

aarch64-linux-gnu-gcc \
    --sysroot=/opt/sysroot-arm64 \
    -o prog prog.c
```

### 3.4 pkg-config при кросс-компиляции

pkg-config по умолчанию читает `/usr/lib/pkgconfig` — файлы HOST-системы. При кросс-компиляции нужны .pc файлы из sysroot:

```bash
# Неправильно (ищет host библиотеки):
pkg-config --libs libgpiod
# → -lgpiod  (или «not found» если нет на host)

# Правильно:
export PKG_CONFIG_SYSROOT_DIR=/opt/sysroot-arm64
export PKG_CONFIG_PATH=/opt/sysroot-arm64/usr/lib/aarch64-linux-gnu/pkgconfig
export PKG_CONFIG_LIBDIR=/opt/sysroot-arm64/usr/lib/pkgconfig
pkg-config --libs libgpiod
# → -lgpiod  (ищет в sysroot)

# В Buildroot: BR2_PACKAGE_HOST_PKGCONF обрабатывает это автоматически
# В Yocto: source environment-setup-* выставляет всё автоматически
```

---

## 4. Сборка ядра Linux для ARM

### 4.1 Три ключевые переменные

```bash
export ARCH=arm64          # целевая архитектура (arm, arm64, mips, riscv, ...)
export CROSS_COMPILE=aarch64-linux-gnu-   # префикс toolchain (ВАЖНО: с тире в конце)
export KDIR=/path/to/linux-kernel-source  # путь к дереву ядра
```

`CROSS_COMPILE` — не полный путь, а **префикс**: система сборки подставит суффиксы `gcc`, `ld`, `objdump` и т.д. к этому префиксу.

`ARCH` определяет:
- Какой `arch/arm64/` исходников используется
- Какой defconfig доступен
- Какой формат образа ядра генерируется

### 4.2 Конфигурация ядра

```bash
cd /path/to/linux

# Дефолтная минимальная конфигурация для arm64 (generic QEMU)
make ARCH=arm64 defconfig

# Специфичная конфигурация для RK3588
# Файл: arch/arm64/configs/rockchip_defconfig
make ARCH=arm64 rockchip_defconfig

# Список доступных defconfig
ls arch/arm64/configs/
# → defconfig  rockchip_defconfig  ...

# Интерактивная настройка через TUI (требует libncurses-dev)
make ARCH=arm64 menuconfig

# Сохранить текущую конфигурацию как новый defconfig
make ARCH=arm64 savedefconfig
# → создаёт defconfig в корне дерева

# Просмотр отличий от базового defconfig
diff arch/arm64/configs/rockchip_defconfig .config
```

Конфигурация сохраняется в `.config`. При следующей сборке используется автоматически.

### 4.3 Сборка

```bash
# Полная сборка: ядро + device trees + модули
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
     -j$(nproc) \
     Image dtbs modules

# Альтернатива: zImage (сжатое, для некоторых загрузчиков)
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image.gz

# Только device tree blobs
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- dtbs

# Конкретный DTB
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
     rockchip/rk3588-evb1-v10.dtb
```

### 4.4 Артефакты сборки

```
arch/arm64/boot/Image          — несжатое ядро (предпочтительно для U-Boot)
arch/arm64/boot/Image.gz       — gzip-сжатое
arch/arm64/boot/dts/rockchip/  — DTB-файлы для Rockchip SoC
  rk3588-evb1-v10.dtb
  rk3588s-rock-5a.dtb
  ...
*.ko                           — модули ядра (разбросаны по дереву)
```

### 4.5 Установка модулей

```bash
# Установка модулей в директорию для последующей упаковки в rootfs
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
     INSTALL_MOD_PATH=/path/to/rootfs \
     modules_install

# Структура после установки:
# /path/to/rootfs/lib/modules/6.6.0-rc7/
#   kernel/drivers/.../*.ko
#   modules.dep
#   modules.alias
```

### 4.6 Кросс-компиляция out-of-tree kernel modules

Внимание: нельзя использовать `/lib/modules/$(uname -r)/build` при кросс-компиляции. Это путь к **host** ядру, а нам нужны заголовки **target** ядра.

```makefile
# Makefile для out-of-tree модуля (кросс-компиляция)
ARCH         ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-
# Путь к заголовкам/источникам ядра для TARGET архитектуры
KERNELDIR    ?= /path/to/linux-arm64-source

obj-m := mymodule.o

all:
	$(MAKE) -C $(KERNELDIR) \
		M=$(PWD) \
		ARCH=$(ARCH) \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
```

```bash
# Сборка
make ARCH=arm64 \
     CROSS_COMPILE=aarch64-linux-gnu- \
     KERNELDIR=/path/to/linux-arm64-source

# Результат
file mymodule.ko
# → ELF 64-bit LSB relocatable, ARM aarch64
```

Отличие от host-компиляции LKM (текущий курсовой Makefile):
- Host LKM: `KERNELDIR=/lib/modules/$(uname -r)/build` — там x86 ядро
- Cross LKM: `KERNELDIR=/path/to/arm64-kernel` — собранное дерево для ARM

---

## 5. QEMU для embedded разработки

### 5.1 User-mode emulation (qemu-user)

Запускает ARM бинарники **прямо на x86** без полной эмуляции системы. QEMU перехватывает syscalls и транслирует их в host-syscalls.

```bash
# Установка
sudo apt install qemu-user-static

# Запуск ARM64 бинарника на x86
qemu-aarch64-static -L /usr/aarch64-linux-gnu ./hello_arm
# → hello ARM

# -L указывает где искать динамический линкёр и библиотеки target
# Если без -L: ld-linux-aarch64.so.1 будет искаться по host-пути и не найдётся

# Запуск с sysroot из Buildroot
qemu-aarch64-static -L /path/to/buildroot/output/staging ./prog
```

**Применение:**
- Запуск unit-тестов для ARM-библиотек без реального железа
- Отладка userspace (gdb + qemu-user)
- Генерация кода в CI/CD

**Ограничения qemu-user:**
- Нет эмуляции MMU с разными привилегиями
- Нет ядерных features (epoll, io_uring работают но через host-ядро)
- Нет аппаратных периферий (GPIO, I2C, SPI)
- Некоторые syscall-константы отличаются (SIGRTMIN и др.)
- Невозможно тестировать поведение при нехватке памяти или прерываниях

### 5.2 System-mode emulation (qemu-system-aarch64)

Полная эмуляция: свой CPU, своя RAM, виртуальные устройства. Запускает ядро Linux + rootfs.

```bash
sudo apt install qemu-system-arm

# Запуск ядра с virtio-блок rootfs
qemu-system-aarch64 \
    -machine virt \
    -cpu cortex-a57 \
    -m 1G \
    -nographic \
    -kernel arch/arm64/boot/Image \
    -append "console=ttyAMA0 root=/dev/vda rw rootwait" \
    -drive file=rootfs.ext4,format=raw,if=none,id=hd0 \
    -device virtio-blk-device,drive=hd0

# Выход из QEMU: Ctrl-A, затем X
```

**Параметры `-machine`:**
- `virt` — generic QEMU arm board (не реальное железо, но поддерживает virtio)
- `raspi3b` — Raspberry Pi 3B (с некоторыми ограничениями)
- `sifive_u` — RISC-V SiFive

**Параметры `-cpu`:**
```bash
# Список доступных CPU
qemu-system-aarch64 -machine virt -cpu help
# cortex-a57, cortex-a72, max, ...
```

**С DTB:**
```bash
qemu-system-aarch64 \
    -machine virt \
    -cpu cortex-a72 \
    -m 2G \
    -nographic \
    -kernel Image \
    -dtb my-board.dtb \
    -append "console=ttyAMA0 root=/dev/vda rw" \
    -drive file=rootfs.ext4,format=raw,if=none,id=hd \
    -device virtio-blk-device,drive=hd
```

**Virtio устройства для тестирования в QEMU:**
- `virtio-blk-device` — блочное устройство (диск)
- `virtio-net-device` — сеть
- `virtio-serial-device` — последовательный порт
- `virtio-rng-device` — генератор случайных чисел

```bash
# Сетевое взаимодействие host-QEMU (user networking)
qemu-system-aarch64 ... \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
    -device virtio-net-device,netdev=net0

# SSH к QEMU-гостю:
ssh -p 2222 root@localhost
```

### 5.3 Создание минимального rootfs для QEMU

```bash
# Вариант 1: busybox статический rootfs
cd /tmp
mkdir rootfs && cd rootfs
mkdir -p bin sbin etc proc sys dev tmp

# Скачать busybox для arm64 (статический)
wget https://busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox-aarch64 \
     -O bin/busybox
chmod +x bin/busybox
# Создать симлинки
(cd bin; for applet in sh ls mount; do ln -s busybox $applet; done)

cat > etc/inittab << 'EOF'
::sysinit:/bin/mount -t proc proc /proc
::sysinit:/bin/mount -t sysfs sysfs /sys
::respawn:/bin/sh
EOF

# Упаковать в ext4
dd if=/dev/zero of=rootfs.ext4 bs=1M count=64
mkfs.ext4 rootfs.ext4
sudo mount -o loop rootfs.ext4 /mnt
sudo cp -a . /mnt/
sudo umount /mnt
```

### 5.4 QEMU vs реальное железо

| Критерий | QEMU system | Реальное железо |
|----------|-------------|-----------------|
| Запуск ядра | Да | Да |
| virtio-драйверы | Да | Нет (на реальных платах) |
| Реальный I2C/SPI/GPIO | Нет | Да |
| Прерывания по таймеру | Да (эмулировано) | Да (аппаратно) |
| Реальное тактирование | Нет | Да |
| JTAG/UART-отладка | Через монитор | Физические пины |
| Воспроизводимость | 100% | Зависит от железа |

**Вывод:** QEMU отлично подходит для разработки ядра, тестирования платформенных драйверов без аппаратной зависимости, CI/CD. Для тестирования конкретных периферийных драйверов (SPI-дисплей, I2C-акселерометр) нужен реальный devboard.

---

## 6. Отладка: gdb + gdbserver

### 6.1 Remote debugging через gdbserver

```bash
# Шаг 1: скопировать gdbserver на target
# (gdbserver должен быть скомпилирован для target ABI)
apt install gdbserver   # если Debian/Ubuntu на target
# или взять из Buildroot/Yocto output

# Шаг 2: на target (ARM), запустить под gdbserver
gdbserver :1234 ./my_program arg1 arg2

# Или подключиться к уже запущенному процессу по PID
gdbserver :1234 --attach $(pidof my_program)
```

```bash
# Шаг 3: на host (x86), запустить кросс-gdb
# Важно: тот же бинарь что и на target (с отладочной информацией)
aarch64-linux-gnu-gdb ./my_program

# Подключиться к target
(gdb) target remote 192.168.1.10:1234

# Сообщить gdb где искать shared libraries
(gdb) set sysroot /opt/sysroot-arm64

# Работа как с локальным процессом
(gdb) break main
(gdb) continue
(gdb) backtrace
(gdb) info registers
(gdb) x/10i $pc    # дизассемблировать 10 инструкций с PC
```

### 6.2 gdb с QEMU user-mode

```bash
# Запустить QEMU в режиме ожидания gdb
qemu-aarch64-static -g 1234 -L /usr/aarch64-linux-gnu ./my_program &

# Подключиться
aarch64-linux-gnu-gdb ./my_program
(gdb) target remote localhost:1234
(gdb) continue
```

### 6.3 Отладка ядра: kgdb

```bash
# Ядро должно быть собрано с CONFIG_KGDB=y CONFIG_KGDB_SERIAL_CONSOLE=y

# На target: активировать kgdb через magic sysrq
echo g > /proc/sysrq-trigger

# На host: подключиться через serial
aarch64-linux-gnu-gdb vmlinux
(gdb) target remote /dev/ttyUSB0
(gdb) continue
```

### 6.4 JTAG + OpenOCD

Для отладки загрузчика (U-Boot), ранней стадии ядра, или когда консоль недоступна:

```bash
# OpenOCD с JTAG-пробником (J-Link, FT232H, CMSIS-DAP)
openocd -f interface/jlink.cfg -f target/rk3588.cfg

# В другом терминале:
aarch64-linux-gnu-gdb u-boot
(gdb) target remote localhost:3333
(gdb) load   # загрузить бинарь через JTAG
(gdb) continue
```

---

## 7. Makefile для embedded проектов

### 7.1 Паттерн кросс-Makefile

```makefile
# Cross-compilation Makefile
# Использование:
#   make                          — host сборка (для тестирования)
#   make ARCH=arm64               — кросс для AArch64
#   make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-  — кросс для ARMv7

ARCH         ?= $(shell uname -m | sed 's/x86_64/x86/')
CROSS_COMPILE ?=

CC       = $(CROSS_COMPILE)gcc
CXX      = $(CROSS_COMPILE)g++
LD       = $(CROSS_COMPILE)ld
AR       = $(CROSS_COMPILE)ar
OBJDUMP  = $(CROSS_COMPILE)objdump
OBJCOPY  = $(CROSS_COMPILE)objcopy
STRIP    = $(CROSS_COMPILE)strip
SIZE     = $(CROSS_COMPILE)size

# Базовые флаги
CFLAGS   := -Wall -Wextra -Wshadow -Wconversion
CFLAGS   += -std=c17
CFLAGS   += -Os              # оптимизация под размер для embedded
CFLAGS   += -ffunction-sections -fdata-sections  # для --gc-sections

LDFLAGS  := -Wl,--gc-sections   # убрать неиспользуемые секции

# ARM-специфичные флаги
ifeq ($(ARCH),arm64)
    CFLAGS += -march=armv8-a -mtune=cortex-a55
endif
ifeq ($(ARCH),arm)
    CFLAGS += -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard
endif

SRCS := main.c utils.c
OBJS := $(SRCS:.c=.o)
TARGET := myapp

.PHONY: all clean size disasm

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	$(SIZE) $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

size: $(TARGET)
	$(SIZE) $(TARGET)

disasm: $(TARGET)
	$(OBJDUMP) -d $(TARGET) | less

clean:
	rm -f $(OBJS) $(TARGET)
```

### 7.2 Использование переменных окружения в CI

```bash
# В GitHub Actions или GitLab CI:
env:
  ARCH: arm64
  CROSS_COMPILE: aarch64-linux-gnu-

# Или передать напрямую:
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j4
```

`ARCH ?=` — оператор условного присваивания: переменная устанавливается только если не задана извне. Это позволяет переопределять из командной строки или окружения.

---

## 8. ABI — почему бинарники несовместимы

### 8.1 ARM ABI история

**OABI (Old ABI)** — устаревший, до 2005. Не встречается в современных системах.

**EABI (Embedded ABI)** — современный для 32-бит ARM. Определяет:
- Соглашение о передаче аргументов (регистры r0–r3, остальное через стек)
- Выравнивание стека (8-байт для вызовов)
- Формат ELF и имена секций

**Hard-float (hf) vs Soft-float:**

```
Soft-float: float-аргументы → целочисленные регистры r0-r3
Hard-float: float-аргументы → VFP-регистры s0-s7 / d0-d7
```

Смешивать нельзя: если вызываешь soft-float функцию из hard-float кода — аргументы окажутся не в тех регистрах. Линкёр при правильной настройке это поймает:

```
/usr/bin/ld: error: mylib.a(module.o) uses VFP register arguments,
output does not
```

**AArch64 (ARM 64-бит):** один ABI, всегда hard-float. Первые 8 аргументов — в регистрах x0–x7 (целые) или v0–v7 (float/SIMD). Нет вариантов.

### 8.2 Диагностика ABI бинарников

```bash
# Проверить ABI бинарника
readelf -h mylib.so | grep -E "Class|ABI|Flags"
# EF_ARM_ABI_VER5 + EF_ARM_ABI_FLOAT_HARD → hard-float
# EF_ARM_ABI_VER5 + EF_ARM_ABI_FLOAT_SOFT → soft-float

# Команда file тоже показывает
file mylib.so
# → ELF 32-bit LSB shared object, ARM, EABI5 version 1 (SYSV),
#   dynamically linked, ... hard-float
```

### 8.3 Проблема «GLIBC not found»

```
./myapp: /lib/aarch64-linux-gnu/libc.so.6: version 'GLIBC_2.33' not found
```

Причина: бинарник собран против **новой** glibc (host), устройство имеет **старую** glibc.

```bash
# Посмотреть какие версии glibc требует бинарник
objdump -p ./myapp | grep GLIBC
# → GLIBC_2.17
# → GLIBC_2.33    ← эта версия слишком новая

# Посмотреть версию glibc на устройстве
ssh root@device /lib/aarch64-linux-gnu/libc.so.6
# → GNU C Library (Ubuntu GLIBC 2.31-13) stable release version 2.31.
```

**Решения:**

1. **Правильный sysroot** — компилировать против glibc той же версии что на устройстве:
   ```bash
   aarch64-linux-gnu-gcc --sysroot=/opt/sysroot-with-glibc-2.31 -o myapp myapp.c
   ```

2. **Статическая линковка** (`-static`) — glibc встраивается в бинарник:
   ```bash
   aarch64-linux-gnu-gcc -static -o myapp myapp.c
   # Бинарник не зависит от системной glibc
   file myapp
   # → ELF 64-bit LSB executable, ..., statically linked
   ```

3. **musl-toolchain** — musl статически линкуется по умолчанию, нет версионирования:
   ```bash
   # Toolchain от Bootlin с musl
   aarch64-linux-musl-gcc -o myapp myapp.c
   ```

### 8.4 Когда статическая линковка оправдана

**Плюсы:**
- Нет зависимости от системных библиотек
- Работает на любой версии glibc / любом дистрибутиве
- Упрощает деплой (один файл)

**Минусы:**
- Размер: простой hello_world — ~800 КБ со статической glibc
- Нет обновлений безопасности libc без пересборки бинарника
- Некоторые функции glibc не работают статически (NSS — name resolution)

**Когда оправдано:**
- Утилиты для recovery/initramfs (busybox-style)
- Диагностические инструменты без гарантии окружения
- Контейнеры FROM scratch

```bash
# Размер сравнения
aarch64-linux-gnu-gcc -O2 hello.c -o hello_dynamic
aarch64-linux-gnu-gcc -O2 -static hello.c -o hello_static
ls -lh hello_dynamic hello_static
# → hello_dynamic: 8.0K
# → hello_static:  812K
```

---

## 9. Типичный рабочий процесс BSP-инженера

### 9.1 Общая схема

```
                    ┌─────────────────────────────────────────┐
                    │              Хост (x86_64)              │
                    │                                         │
  ┌──────────────┐  │  ┌──────────────┐  ┌────────────────┐  │
  │ Исходники    │  │  │   Buildroot  │  │  Артефакты:    │  │
  │ ядра /       │──┼─▶│   / make     │─▶│  Image         │  │
  │ u-boot /     │  │  │   ARCH=arm64 │  │  rk3588.dtb    │  │
  │ приложений   │  │  │   CROSS=...  │  │  rootfs.ext4   │  │
  └──────────────┘  │  └──────────────┘  └───────┬────────┘  │
                    │                             │           │
                    └─────────────────────────────│───────────┘
                                                  │
                              ┌───────────────────┼────────────────┐
                              │                   │                │
                           TFTP/NFS           SCP/rsync      rkdeveloptool
                              │                   │                │
                              ▼                   ▼                ▼
                    ┌─────────────────────────────────────────────────┐
                    │                   Target (ARM)                  │
                    │              RK3588 / Raspberry Pi              │
                    └─────────────────────────────────────────────────┘
```

### 9.2 Методы доставки образа

**TFTP (быстро при разработке ядра):**
```bash
# На host: установить TFTP сервер
sudo apt install tftpd-hpa
sudo cp Image rk3588.dtb /var/lib/tftpboot/

# В U-Boot на target:
setenv serverip 192.168.1.100
setenv ipaddr 192.168.1.10
tftp 0x08000000 Image
tftp 0x0a000000 rk3588.dtb
booti 0x08000000 - 0x0a000000
```

**NFS root (итерационная разработка rootfs):**
```bash
# На host: экспортировать rootfs через NFS
sudo apt install nfs-kernel-server
echo "/opt/nfs-root *(rw,sync,no_subtree_check,no_root_squash)" >> /etc/exports
sudo exportfs -av

# U-Boot командная строка ядра:
setenv bootargs "console=ttyS2,1500000 root=/dev/nfs \
  nfsroot=192.168.1.100:/opt/nfs-root,tcp,nfsvers=4 \
  ip=dhcp rw"

# Преимущество: изменить файл на host → он сразу виден на target
# Не нужно перезаписывать SD-карту или eMMC
```

**SD/eMMC (финальная прошивка):**
```bash
# Записать raw image на SD
dd if=sdcard.img of=/dev/sdX bs=4M status=progress

# Или по компонентам
dd if=Image of=/dev/sdX2 bs=4M
dd if=rootfs.ext4 of=/dev/sdX3 bs=4M
```

**rkdeveloptool (Rockchip RK3588 по USB):**
```bash
# Установка
sudo apt install rkdeveloptool
# или собрать из исходников:
# git clone https://github.com/rockchip-linux/rkdeveloptool

# Перевести плату в режим Maskrom:
# Зажать кнопку RECOVERY/MASKROM при подаче питания

# Проверить что видно устройство
sudo rkdeveloptool ld
# → DevNo=1    Vid=0x2207,Pid=0x350b,LocationID=xxx    Maskrom

# Записать loader (DDR-инициализация + miniloader)
sudo rkdeveloptool db rk3588_spl_loader.bin

# Записать полный образ
sudo rkdeveloptool wl 0 update.img

# Перезагрузка
sudo rkdeveloptool rd
```

### 9.3 Структура проекта BSP-инженера

```
my-bsp/
├── Makefile                # Верхнеуровневый make: ядро + u-boot + rootfs
├── toolchain/              # Toolchain или symlinks
│   └── aarch64-linux-gnu/
├── kernel/                 # Дерево ядра Linux (git submodule или tar)
│   ├── arch/arm64/configs/myboard_defconfig
│   └── arch/arm64/boot/dts/vendor/myboard.dts
├── u-boot/                 # U-Boot (git submodule)
│   └── configs/myboard_defconfig
├── buildroot/              # Buildroot (для rootfs)
│   └── configs/myboard_defconfig
├── overlays/               # Файлы поверх rootfs: конфиги, init-скрипты
│   └── etc/
├── out/                    # Артефакты сборки
│   ├── Image
│   ├── myboard.dtb
│   └── rootfs.ext4
└── scripts/
    ├── flash-sd.sh         # dd на SD-карту
    └── flash-usb.sh        # rkdeveloptool
```

---

## 10. Практика — задания

### Задание 01-endian-detect

**Цель:** реализовать обнаружение endianness и байтовые операции без UB.

**Файл:** `content/exercises/el1/01-endian-detect/starter.c`

Реализовать четыре функции:

```c
/* Возвращает 1 если система little-endian, 0 если big-endian.
   Использовать memcpy или union — НЕ указатель-каст (strict aliasing UB). */
int is_little_endian(void);

/* Поменять порядок байт: 0x01020304 → 0x04030201 */
uint32_t bswap32(uint32_t x);

/* Конвертировать из CPU byte order в big-endian network order */
uint32_t cpu_to_be32(uint32_t x);

/* Конвертировать из big-endian в CPU byte order */
uint32_t be32_to_cpu(uint32_t x);
```

**Почему важно:** сетевые протоколы, регистры SoC, flash-заголовки — всё big-endian. x86 и ARM — little-endian. При чтении 32-бит значения из памяти устройства без конвертации получишь перевёрнутые данные.

**Подходы к bswap32 без UB:**
```c
/* Вариант 1: битовые операции (всегда portable) */
uint32_t bswap32(uint32_t x) {
    return ((x & 0xFFu) << 24)
         | ((x & 0xFF00u) << 8)
         | ((x & 0xFF0000u) >> 8)
         | ((x & 0xFF000000u) >> 24);
}

/* Вариант 2: через uint8_t массив (явный) */
uint32_t bswap32(uint32_t x) {
    uint8_t b[4]; uint32_t r;
    memcpy(b, &x, 4);
    uint8_t s[4] = {b[3], b[2], b[1], b[0]};
    memcpy(&r, s, 4);
    return r;
}

/* Вариант 3: GCC built-in (быстро, но не стандарт C) */
return __builtin_bswap32(x);
```

### Задание 02-cross-hello (чекпоинт, без автотестирования)

**Шаги:**
1. Установить `gcc-aarch64-linux-gnu`
2. Написать минимальный `hello.c`
3. Скомпилировать: `aarch64-linux-gnu-gcc hello.c -o hello_arm`
4. Проверить: `file hello_arm` должен показать `ARM aarch64`
5. Проверить заголовок ELF: `readelf -h hello_arm | grep Machine`
6. Запустить на x86 — должна быть ошибка «Exec format error»
7. Установить `qemu-aarch64-static` и запустить через QEMU

Это не автотестируемое задание. Это практический checkpoint.

### Задание 03-abi-struct

**Цель:** понять padding и wire format serialization.

**Файл:** `content/exercises/el1/03-abi-struct/starter.c`

Реализовать сериализацию структуры протокольного заголовка в wire format (без padding, big-endian) и измерить разницу sizeof между упакованной и неупакованной версиями.

**Ключевые концепции:**
- Компилятор вставляет padding для выравнивания членов структуры
- `__attribute__((packed))` убирает padding, но может вызвать unaligned access на строгих платформах
- Для wire format правильно: явная сериализация через memcpy + byte order conversion
- `offsetof()` позволяет проверить реальное расположение членов

---

## 11. Самопроверка

<details>
<summary>1. Что такое ARCH, CROSS_COMPILE, KDIR в контексте сборки ядра?</summary>

**ARCH** — целевая архитектура ядра. Определяет какой подкаталог `arch/` используется и набор доступных defconfig. Примеры: `arm`, `arm64`, `mips`, `riscv`, `x86`.

**CROSS_COMPILE** — **префикс** кросс-toolchain. К нему добавляются суффиксы `gcc`, `ld`, `objdump` и т.д. Обязательно с тире в конце: `aarch64-linux-gnu-`. Система сборки ядра вызывает `$(CROSS_COMPILE)gcc` для компиляции.

**KDIR** (или KERNELDIR) — путь к дереву ядра с заголовками и скриптами сборки. Используется при сборке out-of-tree модулей: `make -C $(KDIR) M=$(PWD) modules`.
</details>

<details>
<summary>2. Чем отличается arm-none-eabi от aarch64-linux-gnu?</summary>

`arm-none-eabi` — для baremetal ARM (Cortex-M, Cortex-R без ОС):
- Нет ОС (`none` в имени)
- Нет Linux syscalls
- Линкуется с newlib (минимальная libc для embedded)
- Генерирует код для ARMv6-M, ARMv7-M (Thumb-2)
- Используется для STM32, nRF52, LPC серий

`aarch64-linux-gnu` — для 64-бит ARM с Linux:
- Полная glibc
- Linux syscalls (fork, mmap, pthread, ...)
- ARMv8-A ISA (AArch64)
- Генерирует ELF с dynamic linker `/lib/ld-linux-aarch64.so.1`
- Используется для RK3588, i.MX8, Raspberry Pi 4
</details>

<details>
<summary>3. Что такое sysroot и зачем он нужен?</summary>

Sysroot — директория, которая для компилятора является «виртуальным корнем `/`» target-системы. Содержит заголовочные файлы (`usr/include/`) и библиотеки (`usr/lib/`, `lib/`) той версии которая установлена на устройстве.

Без sysroot: компилятор использует `/usr/include` и `/usr/lib` host-системы → несовместимость версий glibc → `GLIBC_X.Y not found` на устройстве.

С правильным sysroot: компилятор линкует против тех же версий библиотек что и на устройстве → бинарник работает.

Источники sysroot: Buildroot `output/staging/`, Yocto SDK, debootstrap для Debian-based target.
</details>

<details>
<summary>4. Почему нельзя использовать /lib/modules/$(uname -r)/build для кросс-компиляции LKM?</summary>

`/lib/modules/$(uname -r)/build` — симлинк на заголовки **host** ядра, то есть ядра которое сейчас запущено на x86 машине разработчика. Это заголовки для x86_64, версии ядра работающего на host.

При кросс-компиляции LKM нужны заголовки **target** ядра — ARM64 версии которая будет запущена на устройстве. Использование host-заголовков приведёт к: несоответствию ABI модуля (vermagic mismatch), неправильным типам данных (если arch-specific), ошибкам линковки.

Правильно: `KERNELDIR=/path/to/cross-built-arm64-kernel` — дерево ядра для которого собираем модуль.
</details>

<details>
<summary>5. Что такое hard-float ABI и почему нельзя смешивать hf и non-hf библиотеки?</summary>

Hard-float (hf) ABI: аргументы типа `float` и `double` передаются через FPU-регистры (VFP: s0–s15, d0–d7). Это быстрее — нет копирования из FPU-регистров в целочисленные.

Soft-float ABI: аргументы float/double передаются через целочисленные регистры (r0–r3) как bit patterns. Совместимо с CPU без FPU, но медленнее.

Если библиотека скомпилирована с soft-float, а вызывающий код — с hard-float: при вызове функции аргументы окажутся в **разных регистрах**, чем ожидает callee. Результат — неправильные данные, не паника при компиляции.

Линкёр с корректными флагами (`--warn-mismatch`) диагностирует это:
```
error: uses VFP register arguments, output does not
```

В AArch64 (64-бит) этой проблемы нет — единственный ABI, всегда hard-float.
</details>

<details>
<summary>6. Как проверить что бинарник собран для правильной архитектуры?</summary>

Три инструмента:

```bash
# file — быстрая проверка
file myapp
# → ELF 64-bit LSB pie executable, ARM aarch64, ...

# readelf — детальный заголовок ELF
readelf -h myapp | grep -E "Class|Machine|Flags"
# → Class:  ELF64
# → Machine: AArch64
# → Flags:  0x0

# objdump — машинный код
objdump -d myapp | head
# → myapp: file format elf64-littleaarch64
# Дизассемблер покажет aarch64 инструкции

# Для ARM 32-бит:
readelf -h myapp | grep Flags
# → 0x5000400 EF_ARM_ABI_VER5, EF_ARM_ABI_FLOAT_HARD
```
</details>

<details>
<summary>7. Что делает qemu-user-static и в чём его ограничения?</summary>

`qemu-user-static` — статически скомпилированный QEMU в режиме user-mode emulation. Перехватывает syscalls ARM-бинарника и транслирует их в x86 syscalls. Работает «прозрачно»: ARM ELF запускается как обычный процесс на x86.

Флаг `-L /path/to/sysroot` указывает где искать динамический линкёр и библиотеки ARM.

**Ограничения:**
- Нет изоляции привилегий (нет kernel/user разделения)
- Работает через host ядро: некоторые syscall-номера отличаются
- Нет эмуляции аппаратных периферий (GPIO, I2C, SPI)
- Производительность: ~2–10x медленнее native
- Некоторые signal-related операции ведут себя иначе
- Нельзя тестировать поведение при page faults, OOM, прерываниях
</details>

<details>
<summary>8. Как получить sysroot для конкретного дистрибутива на target?</summary>

**Buildroot:** после успешной сборки `make` — `output/staging/` является sysroot. Содержит заголовки и библиотеки в точности той версии которая будет на target.

**Yocto:** SDK генерируется командой `bitbake core-image-minimal -c populate_sdk`. Устанавливается скриптом `.sh`, который создаёт sysroot и `environment-setup-*` скрипт для настройки переменных.

**Debian/Ubuntu ARM target:** использовать `debootstrap` или `multistrap`:
```bash
sudo debootstrap --arch=arm64 bookworm /opt/sysroot-arm64 http://ports.ubuntu.com/
```

**Ручной способ** (если есть доступ к устройству):
```bash
# Скопировать /usr/include и /usr/lib с устройства
rsync -av root@device:/usr/include /opt/sysroot/usr/
rsync -av root@device:/usr/lib /opt/sysroot/usr/
rsync -av root@device:/lib /opt/sysroot/
```
</details>

<details>
<summary>9. Почему «GLIBC not found» на устройстве и как исправить без статической линковки?</summary>

Причина: бинарник при линковке записывает в ELF секцию `.gnu.version_r` требуемые версии символов glibc. Если линковался против новой glibc (host), а на устройстве старая — динамический линкёр видит несоответствие версий и отказывается запускать.

**Исправление без static:**
1. Правильный sysroot — собирать против той же версии glibc что на устройстве:
   ```bash
   aarch64-linux-gnu-gcc --sysroot=/opt/sysroot-with-old-glibc -o myapp myapp.c
   ```

2. Ограничение минимальной версии glibc через linker script (сложно).

3. Использовать musl-toolchain — musl не имеет версионирования символов, нет этой проблемы.

4. Обновить glibc на устройстве (не всегда возможно).
</details>

<details>
<summary>10. Что такое rkdeveloptool и для чего он используется?</summary>

`rkdeveloptool` — официальная утилита Rockchip для прошивки устройств на Rockchip SoC (RK3588, RK3568, RK3399 и др.) по USB в режиме Maskrom или Loader.

Режимы:
- **Maskrom** — ROM-режим: перехватывается кнопкой при старте, позволяет прошить loader с нуля
- **Loader** — загружен miniloader, доступны операции с flash

Основные команды:
```bash
rkdeveloptool ld          # список подключённых устройств
rkdeveloptool db loader.bin  # Download Boot (загрузить loader в RAM и запустить)
rkdeveloptool wl 0 full.img  # Write LBA: записать образ на flash
rkdeveloptool rd          # Reboot Device
rkdeveloptool ul          # Upgrade Loader
```

Используется для: первоначальной прошивки, восстановления после брика, автоматизации производства.
</details>

---

## 12. Банк вопросов

### БАЗА (MCQ)

**Вопрос 1.** Что обозначает `aarch64` в имени `aarch64-linux-gnu-gcc`?

A) Это кодовое имя чипа Rockchip RK3588  
B) ARMv8-A, 64-битный режим выполнения (AArch64 state)  
C) Версия GNU toolchain 64  
D) Архитектура ARM Cortex-A64

<details><summary>Ответ</summary>B. AArch64 — официальное название 64-битного режима выполнения ARMv8-A, определённого Arm Ltd. Cortex-A64 не существует как процессор. RK3588 содержит Cortex-A76/A55 ядра, но это не значение слова aarch64.</details>

---

**Вопрос 2.** Какой флаг gcc указывает директорию sysroot?

A) `-root=`  
B) `--target-dir=`  
C) `--sysroot=`  
D) `-I/path/to/sysroot/usr/include` (только для заголовков)

<details><summary>Ответ</summary>C. `--sysroot=/path` устанавливает корень для поиска и заголовков, и библиотек. Вариант D частично решает проблему заголовков, но не библиотек. Для полного sysroot нужен именно `--sysroot`.</details>

---

**Вопрос 3.** Какая переменная задаёт целевую архитектуру при сборке ядра Linux?

A) `TARGET`  
B) `ARCH`  
C) `PLATFORM`  
D) `CPU`

<details><summary>Ответ</summary>B. `ARCH=arm64` (или `arm`, `mips`, `riscv` и т.д.). Эта переменная используется Makefile ядра для выбора поддиректории `arch/` и правильных инструментов.</details>

---

**Вопрос 4.** Чем `Image` (arch/arm64/boot/Image) отличается от `vmlinuz`?

A) Image — только для ARM, vmlinuz — только для x86  
B) Image — несжатое ядро без self-decompressor; vmlinuz — сжатый архив с self-decompressor для x86  
C) Image — файл для U-Boot, vmlinuz — для GRUB  
D) Это одно и то же, разные имена

<details><summary>Ответ</summary>B. `Image` — несжатое ядро для AArch64 (U-Boot/UEFI сами управляют загрузкой). `vmlinuz` на x86 — сжатый kernel image с встроенным распаковщиком. `Image.gz` — сжатая версия без self-decompressor. На AArch64 загрузчики обычно умеют работать с `Image` напрямую.</details>

---

**Вопрос 5.** Что делает QEMU в user-mode (`qemu-aarch64-static`)?

A) Эмулирует полную ARM систему с ядром и виртуальными устройствами  
B) Запускает ARM бинарники на x86, транслируя ARM инструкции и syscalls в host-эквиваленты  
C) Эмулирует только MMU и защиту памяти  
D) Компилирует ARM код на лету (JIT компиляция для x86)

<details><summary>Ответ</summary>B. QEMU user-mode транслирует инструкции (через DBT — Dynamic Binary Translation) и перехватывает syscalls, передавая их host-ядру с конверсией номеров. Нет полной эмуляции оборудования.</details>

---

**Вопрос 6.** Для чего используется `objdump` из binutils?

A) Только для дизассемблирования машинного кода  
B) Для конвертации ELF в raw binary  
C) Для дизассемблирования, отображения секций, заголовков, relocations и отладочной информации  
D) Для линковки объектных файлов

<details><summary>Ответ</summary>C. objdump многофункционален: `-d` дизассемблирует, `-h` показывает секции, `-r` показывает relocations, `-S` совмещает с исходниками (при -g). Конвертацию делает objcopy, линковку — ld.</details>

---

**Вопрос 7.** Что такое DTB (Device Tree Blob)?

A) Debug Trace Buffer — буфер трассировки ARM  
B) Бинарная скомпилированная форма Device Tree Source (.dts) файла  
C) Формат прошивки Rockchip  
D) Таблица прерываний для ARM GIC

<details><summary>Ответ</summary>B. DTB — скомпилированный DTS. Компилятор dtc преобразует текстовый .dts в компактный бинарный .dtb. Ядро Linux читает DTB при загрузке для определения топологии оборудования.</details>

---

**Вопрос 8.** Чем статическая линковка отличается от динамической в embedded контексте?

A) Статическая работает только на ARM, динамическая — на x86  
B) При статической — все библиотеки встроены в бинарник; при динамической — загружаются из ФС в runtime  
C) Статическая быстрее при загрузке системы, динамическая — при выполнении  
D) Разницы нет, выбор только из соображений стиля

<details><summary>Ответ</summary>B. Статическая: один самодостаточный файл, нет зависимостей, больший размер. Динамическая: меньший размер бинарника, библиотеки обновляются независимо, но нужны на файловой системе target в правильных версиях.</details>

---

### МЕХАНИЗМЫ (self_grade)

**Вопрос 1.** Опиши полный процесс сборки ядра Linux для RK3588 с нуля: какие шаги, какие команды, какие артефакты получишь.

<details><summary>Эталонный ответ</summary>

1. Установить toolchain: `apt install gcc-aarch64-linux-gnu`
2. Получить исходники: `git clone https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git`
3. Задать переменные: `export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-`
4. Сконфигурировать: `make rockchip_defconfig`
5. (Опционально) настроить: `make menuconfig`
6. Собрать: `make -j$(nproc) Image dtbs modules`
7. Артефакты:
   - `arch/arm64/boot/Image` — ядро
   - `arch/arm64/boot/dts/rockchip/rk3588-evb1-v10.dtb` — Device Tree
   - `*.ko` по всему дереву — модули
8. Установить модули: `make INSTALL_MOD_PATH=/rootfs modules_install`
</details>

---

**Вопрос 2.** Как настроить gdbserver на устройстве и подключиться с хоста? Какие требования к бинарнику?

<details><summary>Эталонный ответ</summary>

Требования к бинарнику: скомпилировать с `-g` (отладочные символы), не stripать. На target положить тот же бинарь что и на хост (или только symbols для match).

На target:
```bash
gdbserver :1234 ./myapp arg1
# или attach к запущенному:
gdbserver :1234 --attach $(pidof myapp)
```

На host:
```bash
aarch64-linux-gnu-gdb ./myapp   # тот же бинарь
(gdb) set sysroot /opt/sysroot  # shared libs
(gdb) target remote 192.168.1.10:1234
(gdb) break main
(gdb) continue
```

Через QEMU user-mode:
```bash
qemu-aarch64-static -g 1234 -L /sysroot ./myapp &
aarch64-linux-gnu-gdb ./myapp
(gdb) target remote localhost:1234
```
</details>

---

**Вопрос 3.** Объясни разницу между qemu-system и qemu-user для embedded разработки. Когда что использовать?

<details><summary>Эталонный ответ</summary>

**qemu-user:**
- Транслирует отдельные бинарники, syscalls проходят через host ядро
- Нет эмуляции железа, нет своего ядра
- Быстро запускается, минимальная настройка
- Использовать: unit-тесты библиотек, проверка что бинарник компилируется и базово работает, CI/CD для userspace-кода

**qemu-system:**
- Полная эмуляция: CPU, RAM, виртуальные устройства (virtio)
- Запускает своё ядро Linux
- Медленнее запускается, требует образ ядра и rootfs
- Использовать: тестирование ядра, разработка драйверов (virtio), тестирование boot sequence, CI для kernel модулей

Ни то, ни другое не заменяет реальное железо для тестирования аппаратных интерфейсов (I2C, SPI, GPIO, реальные прерывания).
</details>

---

**Вопрос 4.** Как собрать out-of-tree kernel module для другой архитектуры и другой версии ядра? Что нужно подготовить?

<details><summary>Эталонный ответ</summary>

Нужно:
1. Дерево ядра для target архитектуры с выполненным `make ARCH=arm64 ... modules_prepare` (или полной сборкой)
2. toolchain для target

Makefile модуля:
```makefile
ARCH ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-
KERNELDIR ?= /path/to/arm64-kernel-tree
obj-m := mymod.o

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) \
	    ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules
```

Нельзя: использовать `/lib/modules/$(uname -r)/build` — это host ядро.

Проверка результата:
```bash
file mymod.ko          # ELF 64-bit LSB relocatable, ARM aarch64
modinfo mymod.ko       # vermagic должен совпадать с target ядром
```
</details>

---

**Вопрос 5.** Почему pkg-config при кросс-компиляции требует специальной настройки? Что нужно выставить?

<details><summary>Эталонный ответ</summary>

pkg-config по умолчанию ищет `.pc` файлы в `/usr/lib/pkgconfig` и `/usr/share/pkgconfig` — это **host** пути. Флаги которые он выдаёт (`-I/usr/include/glib-2.0 -lglib-2.0`) относятся к **host** библиотекам.

Нужные переменные:
```bash
export PKG_CONFIG_SYSROOT_DIR=/opt/sysroot-arm64
export PKG_CONFIG_PATH=/opt/sysroot-arm64/usr/lib/pkgconfig
export PKG_CONFIG_LIBDIR=/opt/sysroot-arm64/usr/lib/pkgconfig:/opt/sysroot-arm64/usr/share/pkgconfig
```

После этого pkg-config найдёт `.pc` файлы в sysroot и подставит правильные пути (с учётом `PKG_CONFIG_SYSROOT_DIR` как префикса).

Buildroot и Yocto SDK делают это автоматически через wrapper-скрипты.
</details>

---

**Вопрос 6.** Что такое Linaro toolchain и чем он отличается от дистрибутивного (apt install gcc-aarch64-linux-gnu)?

<details><summary>Эталонный ответ</summary>

**Дистрибутивный toolchain** (ubuntu packages):
- Версия GCC фиксирована и отстаёт от upstream (Ubuntu 22.04 → GCC 11)
- Обновляется только при смене дистрибутива
- Просто установить: `apt install gcc-aarch64-linux-gnu`

**Linaro toolchain:**
- Готовые бинарники актуальных версий GCC (12.x, 13.x) для ARM
- Поставляется Arm Ltd., оптимизирован для ARM SoC
- Можно выбрать конкретную версию GCC под требования проекта
- Установка: скачать tarball, распаковать в `/opt`, добавить в `PATH`

**Bootlin toolchain:**
- Собирается Buildroot-инфраструктурой
- Позволяет выбрать C library (glibc/musl/uClibc), точную версию GCC
- Особенно полезен для musl-based систем

Используют Linaro/Bootlin когда: нужна конкретная версия GCC, несовместимая с дистрибутивом; нужен musl вместо glibc; нужно точное воспроизведение окружения сборки.
</details>

---

**Вопрос 7.** Объясни BUILD/HOST/TARGET тройку toolchain на конкретном примере «Canadian cross».

<details><summary>Эталонный ответ</summary>

Обычный случай (Build=Host=x86, Target=ARM):
- Build: x86 машина где GCC был скомпилирован
- Host: x86 машина где мы запускаем GCC
- Target: ARM устройство где будет работать скомпилированный код

Canadian cross — все три разные. Пример: разрабатываем toolchain для MIPS на macOS, который будет использоваться на Windows:
- Build: macOS (где собираем сам компилятор)
- Host: Windows (где toolchain будет запускаться)
- Target: MIPS Linux (для которого toolchain генерирует код)

Практическое применение: генерация Windows-toolchain для ARM в Linux CI-системе.

В autotools: `./configure --build=x86_64-linux-gnu --host=arm-linux-gnueabihf --target=arm-linux-gnueabihf`
</details>

---

**Вопрос 8.** Как прошить полный образ на RK3588 через USB с нуля (устройство не загружается)?

<details><summary>Эталонный ответ</summary>

1. Перевести в Maskrom-режим: зажать кнопку MASKROM (или Recovery) при подаче питания. У некоторых плат — замкнуть контакты на плате.

2. Проверить что устройство видно:
```bash
sudo rkdeveloptool ld
# DevNo=1  Vid=0x2207,Pid=0x350b  Maskrom
```

3. Загрузить DDR-инициализацию и miniloader:
```bash
sudo rkdeveloptool db rk3588_spl_loader_v1.xx.bin
```

4. Прошить образ (метод 1 — полный gpt образ):
```bash
sudo rkdeveloptool wl 0 update.img
```

Или по частям (метод 2):
```bash
sudo rkdeveloptool wl 0x40 uboot.img        # U-Boot
sudo rkdeveloptool wl 0x4000 boot.img       # ядро + DTB
sudo rkdeveloptool wl 0x14000 rootfs.img    # rootfs
```

5. Перезагрузить:
```bash
sudo rkdeveloptool rd
```
</details>

---

### ЭКСПЕРТ (self_grade)

**Вопрос 1.** Как организовать NFS-root разработку для быстрого цикла правка-тест без перепрошивки eMMC?

<details><summary>Эталонный ответ</summary>

**Схема:**
1. Rootfs лежит на хост-машине, экспортируется по NFS
2. Ядро и DTB загружаются по TFTP или со статического раздела eMMC/SD
3. Параметры ядра: `root=/dev/nfs nfsroot=HOST_IP:/opt/nfs-root,tcp,vers=4 ip=dhcp rw`

**Настройка host:**
```bash
sudo apt install nfs-kernel-server tftpd-hpa
echo "/opt/nfs-root *(rw,sync,no_subtree_check,no_root_squash)" >> /etc/exports
sudo exportfs -av
sudo cp Image rk3588.dtb /var/lib/tftpboot/
```

**U-Boot конфигурация:**
```
setenv bootargs console=ttyS2,1500000 root=/dev/nfs \
    nfsroot=192.168.1.100:/opt/nfs-root,tcp,nfsvers=4 ip=dhcp rw
run tftp_boot
```

**Цикл разработки:**
1. Изменить файл на host в `/opt/nfs-root`
2. На target: перезапустить сервис или обновить файл — изменение видно мгновенно
3. Ядро перезагружать только при изменении ядра/DTB

**Ограничения NFS-root:**
- Требует сеть при загрузке (initramfs может помочь с инициализацией сети)
- NFS latency влияет на I/O интенсивные операции
- Не работает без сети (production сценарии)
</details>

---

**Вопрос 2.** Почему статическая линковка с glibc не всегда решает проблему совместимости? Что именно может сломаться?

<details><summary>Эталонный ответ</summary>

Статическая линковка glibc встраивает userspace-часть libc в бинарник, но многие функции glibc при runtime делают системные вызовы (syscalls) напрямую к ядру.

**Проблема 1: syscall ABI ядра**
Ядро Linux гарантирует стабильность syscall ABI, но добавляет новые syscalls. Если glibc использует новый syscall (e.g., `openat2`, `clone3`, `io_uring_setup`), а на устройстве старое ядро — при вызове получим `ENOSYS`.

**Проблема 2: NSS (Name Service Switch)**
`getaddrinfo()`, `gethostbyname()`, `getpwuid()` — используют NSS плагины (`libnss_dns.so`, `libnss_files.so`). При статической линковке NSS плагины **не включаются**. Результат: DNS resolution не работает даже в статическом бинарнике.

Решение для DNS в статическом бинарнике: использовать musl (нет NSS проблемы) или `getaddrinfo()` обходить через raw DNS запросы.

**Проблема 3: locale**
Некоторые locale-зависимые функции читают файлы из `/usr/lib/locale/` в runtime.

**Вывод:** musl + статическая линковка — более предсказуемое решение для portable static binaries.
</details>

---

**Вопрос 3.** Как проверить что модуль ядра (.ko) совместим с запущенным ядром на устройстве?

<details><summary>Эталонный ответ</summary>

При загрузке модуля ядро проверяет **vermagic** строку в модуле. Она включает:
- Версию ядра (e.g., `6.6.0-rc7`)
- Конфигурацию (SMP, preemption model)
- GCC версию
- Флаги (e.g., `mod_unload`)

```bash
# Посмотреть vermagic модуля:
modinfo mymodule.ko | grep vermagic
# → vermagic: 6.6.0-rc7 SMP preempt mod_unload aarch64

# Посмотреть vermagic запущенного ядра:
cat /proc/version
# или
modinfo $(ls /lib/modules/$(uname -r)/**/*.ko | head -1) | grep vermagic
```

Если vermagic не совпадает:
```
ERROR: could not insert module mymodule.ko: Invalid module format
dmesg | tail
# → mymodule: disagrees about version of symbol struct_module
```

**Решение:** пересобрать модуль против того же дерева ядра (с той же `.config`) что было использовано для сборки ядра на устройстве.

Checksum символов (`Module.symvers`) также проверяется — нужно собирать против точного дерева.
</details>

---

**Вопрос 4.** Что такое multilib и зачем нужны 32-бит библиотеки в 64-бит системе?

<details><summary>Эталонный ответ</summary>

Multilib — способность системы запускать бинарники разной разрядности, имея несколько наборов библиотек.

На AArch64 Linux системе возможно запускать 32-бит ARMv7 бинарники (через compatibility layer AArch32). Для этого нужны 32-бит библиотеки:
- `/lib/arm-linux-gnueabihf/libc.so.6` — 32-бит glibc
- `/usr/lib32/` или `/lib/arm-linux-gnueabihf/` — 32-бит библиотеки

Зачем нужно в embedded:
1. Старые проприетарные бинарники (GPU driver userspace, vendor tools) компилировались только для 32-бит ARM
2. Совместимость с legacy software
3. Rockchip/Mali GPU userspace libraries долго существовали только в 32-бит версиях

Для host: при кросс-компиляции для 32-бит target на 64-бит host toolchain тоже использует multilib (разные sysroot для разных ABI).

```bash
# На 64-бит Ubuntu: поддержка 32-бит ARM
sudo dpkg --add-architecture armhf
sudo apt update
sudo apt install libc6:armhf
```
</details>

---

**Вопрос 5.** Как настроить CI/CD pipeline для embedded проекта с QEMU-эмуляцией?

<details><summary>Эталонный ответ</summary>

**Уровни тестирования:**

1. **Unit-тесты userspace (qemu-user):**
```yaml
# .gitlab-ci.yml
build-arm64:
  image: ubuntu:22.04
  script:
    - apt install -y gcc-aarch64-linux-gnu qemu-user-static
    - aarch64-linux-gnu-gcc -o tests tests.c lib.c
    - qemu-aarch64-static -L /usr/aarch64-linux-gnu ./tests
```

2. **Тестирование ядра (qemu-system):**
```yaml
test-kernel:
  script:
    - make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j4 Image
    - qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 512M
        -kernel arch/arm64/boot/Image -nographic
        -append "console=ttyAMA0 init=/test_runner" 
        -drive file=test-rootfs.ext4,format=raw,if=none,id=hd
        -device virtio-blk-device,drive=hd
        -serial file:output.log
    - grep "TEST PASSED" output.log
```

3. **Артефакты:**
```yaml
artifacts:
  paths:
    - arch/arm64/boot/Image
    - arch/arm64/boot/dts/**/*.dtb
    - out/*.ko
```

**Ограничения QEMU CI:**
- Не тестирует аппаратные интерфейсы
- Производительность QEMU ≠ производительность реального железа
- Драйверы специфичного SoC не тестируются

**Для аппаратного тестирования:** LAVA (Linaro Automated Validation Architecture) — позволяет запускать тесты на реальных платах через сеть.
</details>

---

**Вопрос 6.** Объясни процесс загрузки RK3588: от включения питания до запуска ядра Linux.

<details><summary>Эталонный ответ</summary>

RK3588 использует многостадийный boot:

**1. ROM (BootROM):**
- Выполняется из встроенного ROM чипа
- Инициализирует минимум: тактирование, UART для отладки
- Ищет загружаемый образ в порядке: SPI NOR → eMMC → SD-карта → USB (Maskrom)
- Загружает и проверяет подпись TPL

**2. TPL (Tertiary Program Loader):**
- Первичная инициализация DRAM контроллера
- Необходимо перед тем как можно использовать DRAM
- Очень маленький, умещается в SRAM SoC (~32–64 КБ)

**3. SPL (Secondary Program Loader):**
- Запускается из DRAM (DDR уже инициализирован)
- Инициализирует больше периферии
- Загружает полный U-Boot в DRAM

**4. U-Boot (Das U-Boot):**
- Полнофункциональный загрузчик
- Инициализирует: eMMC/SD, сеть (TFTP), USB
- Читает переменные окружения (uEnv.txt или NVRAM)
- Загружает: ядро Image + DTB + initramfs
- Передаёт управление ядру через `booti` команду

**5. Linux Kernel:**
- Получает управление, парсит DTB
- Инициализирует подсистемы в порядке: arch init → memory → interrupts → drivers → rootfs
- Запускает init (PID 1)

Файлы на прошивке: `idbloader.img` (TPL+SPL) → `uboot.img` → `boot.img` (ядро+DTB) → `rootfs.img`
</details>
