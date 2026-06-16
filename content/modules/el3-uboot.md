# Модуль EL3 — U-Boot: загрузчик

> Этап 2C, Embedded Linux. U-Boot — стандартный загрузчик для embedded Linux: TFTP, NFS, MMC, USB, PCIe boot. Для RK3588 он первое что запускается после ROM кода. Понимание U-Boot критично при porting на новую плату, при отладке ранних стадий загрузки и при интеграции secure boot. Если ты можешь уверенно ответить на все вопросы самопроверки — этот модуль можешь пропустить.

---

## 0. Карта модуля

| | |
|---|---|
| **Время** | 12–18 ч. ~4 ч — чтение, ~6 ч — практика (QEMU или железо), ~2 ч — самопроверка. |
| **Зачем** | U-Boot запускает ядро. Без него плата не загрузится. При porting на новую плату — первый день уйдёт на U-Boot. При отладке kernel panic до монтирования rootfs — без понимания загрузчика не разобраться. |
| **Ресурсы** | u-boot.readthedocs.io, `docs/usage/` в исходниках, `arch/arm/mach-rockchip/`, `board/rockchip/`. |
| **Нужно знать** | Кросс-компиляция (EL1), Device Tree (EL2). |

---

## 1. Процесс загрузки ARM SoC

### 1.1 Общая схема загрузки

ARM SoC загружается поэтапно. Каждый этап инициализирует часть аппаратуры и загружает следующий этап. Причина каскада — ограничения памяти: сразу после включения питания доступно только SRAM внутри чипа (десятки–сотни килобайт), DRAM не работает пока не инициализирован контроллер.

```
Power-on
  ↓
BootROM (встроен в SoC, read-only, производитель)
  ↓  ищет загрузочный образ: eMMC / SD / SPI NOR / USB / JTAG
TPL — Tiny Program Loader (~4 KB, SRAM)
  ↓  инициализирует DRAM контроллер (DDR PHY init)
SPL — Secondary Program Loader (≤200 KB, SRAM → грузит следующий этап в DRAM)
  ↓  инициализирует: clocks, pinmux, PMIC, MMC/SPI для чтения
TF-A BL31 (Trusted Firmware-A, EL3, опционально)
  ↓  устанавливает secure world, ATF runtime services
U-Boot proper (полнофункциональный загрузчик, DRAM, любой размер)
  ↓  загружает ядро + DTB из eMMC / SD / TFTP / USB
Linux Kernel
```

### 1.2 BootROM

BootROM — прошит производителем SoC при изготовлении, изменить нельзя. Его задачи:

- Выбрать источник загрузки (BOOT пины или fuses).
- Прочитать заголовок образа со storage и проверить подпись (если включён secure boot).
- Скопировать TPL в SRAM и передать управление.

Для RK3588 BootROM ищет образ в фиксированном формате (`idbloader`) с заголовком `RKFW` или `RK3X`. Если не нашёл — переходит в режим USB Loader (MaskROM mode): плата появляется как USB устройство, с которым работает `rkdeveloptool`.

BootROM не трогает: DRAM не существует, clocks на минимуме, PMIC не настроен.

### 1.3 TPL (Tiny Program Loader)

Размер: несколько килобайт. Работает в SRAM (обычно 128–256 KB для RK3588).

Единственная задача TPL — инициализировать DRAM контроллер (DDR PHY). Это сложная процедура: тренировка линий, настройка частоты, инициализация PMIC питания DDR. Без неё доступен только SRAM.

Почему TPL отдельно от SPL? Код инициализации DDR — огромный бинарник (сотни KB для современных LPDDR5). Он не помещается в SRAM вместе с SPL. Rockchip решает это: DDR init blob отдельный бинарник, TPL его запускает, результат — работающий DRAM.

Для RK3588: DDR blob приходит из `rkbin` репозитория (проприетарный).

### 1.4 SPL (Secondary Program Loader)

Размер: до 200 KB. Работает в SRAM (но теперь DRAM доступен).

Задачи SPL:
- Настроить clock tree (PLLs, dividers).
- Настроить pinmux для нужных периферийных блоков.
- Настроить PMIC (напряжения питания CPU/GPU/NPU).
- Инициализировать storage (eMMC, SD) для чтения следующего этапа.
- Загрузить U-Boot proper (или TF-A + U-Boot) в DRAM.
- Передать управление.

В SPL доступен урезанный набор API U-Boot: работают базовые драйверы (серийный порт, MMC, SPI), но не работает сеть, нет файловых систем, нет командной строки.

Исходники SPL в U-Boot: большинство драйверов компилируются в двух вариантах — для SPL (`CONFIG_SPL_BUILD=y`) и для U-Boot proper. В SPL-режиме часть функций заменяется заглушками для экономии места.

### 1.5 TF-A (Trusted Firmware-A)

ARM TrustZone разделяет мир на Secure World (EL3/EL1-S) и Normal World (EL2/EL1-N). TF-A — эталонная реализация от ARM для Secure World.

Для RK3588 TF-A (BL31) запускается после SPL, перед U-Boot:
- Устанавливает обработчики SMC (Secure Monitor Call).
- Запускает PSCI (Power State Coordination Interface) — управление питанием CPU, hotplug.
- Инициализирует secure memory.
- Передаёт управление U-Boot в EL2 (или EL1).

После перехода в U-Boot TF-A остаётся резидентным в памяти и обрабатывает вызовы через SMC инструкцию. Ядро Linux вызывает TF-A для управления CPU hotplug, suspend/resume.

Rockchip предоставляет проприетарный BL31 в `rkbin`. Open source вариант доступен через Trusted Firmware-A project (`make PLAT=rk3588 bl31`).

### 1.6 U-Boot proper

После всех подготовительных этапов запускается полнофункциональный U-Boot:
- Полная инициализация периферийных блоков.
- Драйверы: сеть (ETH, USB), storage (MMC, SPI NOR, NAND), дисплей.
- Командная строка (если задержка `BOOTDELAY > 0`).
- Выполнение `BOOTCOMMAND` — загрузка и запуск ядра.

U-Boot proper работает в DRAM и не ограничен по размеру. Современные конфигурации с поддержкой всех интерфейсов занимают несколько мегабайт.

---

## 2. Получение и сборка U-Boot для RK3588

### 2.1 Клонирование и конфигурация

```bash
# Основной репозиторий (mainline)
git clone https://source.denx.de/u-boot/u-boot.git
cd u-boot

# Для некоторых плат нужна ветка с поддержкой (проверь README)
git checkout v2024.10

# Конфигурация для конкретной платы
make CROSS_COMPILE=aarch64-linux-gnu- evb-rk3588_defconfig

# Rock 5A (RK3588S)
make CROSS_COMPILE=aarch64-linux-gnu- rock5a-rk3588s_defconfig

# Список доступных конфигов для Rockchip
ls configs/ | grep rk35
```

### 2.2 Сборка

```bash
export CROSS_COMPILE=aarch64-linux-gnu-
export ARCH=arm64

# Базовая сборка
make -j$(nproc)

# С внешним BL31 от Rockchip
export BL31=/path/to/rkbin/rk35/rk3588_bl31_v1.45.elf
make -j$(nproc)

# Или с open source TF-A (нужно собрать отдельно)
cd /path/to/trusted-firmware-a
make PLAT=rk3588 bl31 CROSS_COMPILE=aarch64-linux-gnu-
export BL31=$(pwd)/build/rk3588/release/bl31/bl31.elf
cd /path/to/u-boot
make -j$(nproc)
```

### 2.3 Артефакты сборки

После успешной сборки в корне u-boot:

| Файл | Описание |
|---|---|
| `u-boot.bin` | U-Boot proper бинарник |
| `u-boot.dtb` | DTB для U-Boot |
| `u-boot.itb` | FIT image: U-Boot + TF-A + DTB |
| `spl/u-boot-spl.bin` | SPL бинарник |
| `idbloader.img` | TPL + SPL, упакованные для Rockchip BootROM |
| `u-boot-rockchip.bin` | Полный образ: idbloader + u-boot.itb, записать на MMC |

Для записи на eMMC/SD:
```bash
# Записать полный образ (самый простой способ)
dd if=u-boot-rockchip.bin of=/dev/sdX bs=512 seek=64

# Или по частям:
dd if=idbloader.img of=/dev/sdX bs=512 seek=64
dd if=u-boot.itb    of=/dev/sdX bs=512 seek=16384
```

Смещения (seek) специфичны для Rockchip: `64` секторов от начала = 32 KB, стандарт для RK3588.

### 2.4 Прошивка через rkdeveloptool

Когда плата в MaskROM mode (зажать MASKROM кнопку при подаче питания):

```bash
# Установка инструмента
git clone https://github.com/rockchip-linux/rkdeveloptool
cd rkdeveloptool && autoreconf -i && ./configure && make

# Проверить что плата видна
rkdeveloptool ld

# Прошить idbloader и u-boot
rkdeveloptool db    rk3588_spl_loader_v1.15.113.bin  # DDR init
rkdeveloptool wl 0x40 idbloader.img
rkdeveloptool wl 0x4000 u-boot.itb
rkdeveloptool rd    # перезагрузка
```

---

## 3. Конфигурационная система U-Boot (Kconfig)

### 3.1 Kconfig — та же система что в ядре

U-Boot использует Kconfig — ту же систему конфигурации, что и Linux kernel. Синтаксис `Kconfig` файлов идентичен, интерфейс `menuconfig` идентичен.

```bash
make menuconfig          # TUI конфигурация (требует libncurses-dev)
make nconfig             # альтернативный TUI
make savedefconfig       # сохранить минимальный defconfig (только отличия от дефолтов)
make defconfig           # применить defconfig из configs/
```

### 3.2 Ключевые параметры конфигурации

**Память и адреса:**
```
CONFIG_SYS_TEXT_BASE        — адрес, куда линкуется U-Boot proper
CONFIG_SYS_MALLOC_LEN       — размер кучи (heap) для U-Boot
CONFIG_SYS_LOAD_ADDR        — адрес загрузки по умолчанию для tftp/load команд
CONFIG_SYS_SDRAM_BASE       — начало DRAM
CONFIG_SYS_SDRAM_SIZE       — размер DRAM
```

**Окружение:**
```
CONFIG_BOOTCOMMAND          — команда загрузки по умолчанию (строка)
CONFIG_BOOTDELAY            — задержка перед autoboot (секунды; -1 = без задержки)
CONFIG_ENV_SIZE             — размер раздела для переменных окружения
CONFIG_ENV_OFFSET           — смещение в storage для хранения env
CONFIG_ENV_IS_IN_MMC        — хранить env на MMC
CONFIG_ENV_IS_IN_SPI_FLASH  — хранить env на SPI NOR
```

**Функциональность:**
```
CONFIG_CMD_TFTP      — команда tftp
CONFIG_CMD_NET       — сетевые команды
CONFIG_CMD_EXT4      — ext4load команда
CONFIG_CMD_BOOTI     — booti (ARM64 Image)
CONFIG_FIT           — поддержка FIT image
CONFIG_FIT_SIGNATURE — проверка подписи FIT
CONFIG_DISTRO_DEFAULTS — набор команд для стандартного дистрибутива
```

### 3.3 Legacy заголовки vs Kconfig

Исторически U-Boot использовал `include/configs/<board>.h` для всей конфигурации (макросы `#define CONFIG_*`). Это legacy-подход. Современный U-Boot переводит все параметры в Kconfig. Встретишь оба подхода в исходниках — `include/configs/` для legacy, `Kconfig` файлы в `configs/`, `arch/`, `board/` для нового.

При porting на новую плату: смотри на ближайшую существующую плату-родственника, копируй её `defconfig` и `include/configs/<board>.h`, адаптируй.

---

## 4. U-Boot Shell: команды и скриптование

### 4.1 Доступ к командной строке

UART: обычно UART2 для RK3588, 1500000 бод, 8N1. Скорость задаётся в defconfig (`CONFIG_BAUDRATE`).

Подключение с хоста:
```bash
picocom -b 1500000 /dev/ttyUSB0
# или
screen /dev/ttyUSB0 1500000
```

При включении платы: нажать любую клавишу в течение `BOOTDELAY` секунд, чтобы прервать autoboot.

### 4.2 Работа с памятью

```bash
# Дамп памяти (md = memory display)
md 0x40200000 0x20          # 32 32-битных слова начиная с адреса
md.b 0x40200000 0x40        # байтами
md.w 0x40200000 0x20        # 16-битными словами
md.q 0x40200000 0x10        # 64-битными словами (arm64)

# Запись в память
mw 0x40200000 0xdeadbeef    # записать 32-бит значение
mw.b 0x40200000 0x00 0x100  # заполнить 256 байт нулями

# Копирование блока памяти
cp 0x40200000 0x44000000 0x100  # скопировать 256 слов
```

### 4.3 Работа с MMC

```bash
mmc list                     # список всех MMC устройств (eMMC, SD)
mmc dev 0                    # выбрать устройство 0 (обычно eMMC)
mmc dev 1                    # устройство 1 (SD карта)
mmc info                     # информация о текущем устройстве
mmc part                     # таблица разделов

# Чтение сырых секторов
mmc read 0x40200000 0x800 0x1000   # адрес назначения, start_sector, count

# Загрузка из файловой системы
ext4load mmc 0:2 0x40200000 /boot/Image           # раздел 2 устройства 0
ext4load mmc 0:2 0x44000000 /boot/rk3588-evb.dtb
ext4ls   mmc 0:2 /boot                             # список файлов
fat load mmc 1:1 0x40200000 Image                  # из FAT раздела
```

### 4.4 Сетевая загрузка

```bash
# Настройка сети
setenv ipaddr    192.168.1.100   # IP платы
setenv serverip  192.168.1.1     # IP хоста с TFTP сервером
setenv netmask   255.255.255.0
setenv gatewayip 192.168.1.1

# Проверить сеть
ping 192.168.1.1

# Загрузка по TFTP
tftp 0x40200000 Image              # скачать файл Image в RAM по адресу
tftp 0x44000000 rk3588-evb.dtb    # загрузить DTB
tftp 0x44800000 initramfs.cpio.gz # загрузить initrd

# DHCP (настроит IP автоматически)
dhcp
dhcp 0x40200000 Image              # DHCP + TFTP в одной команде
```

### 4.5 Запуск ядра

```bash
# ARM64: booti для uncompressed Image
# kernel_addr - [initrd_addr:initrd_size] - dtb_addr
booti 0x40200000 - 0x44000000              # без initrd
booti 0x40200000 0x44800000:0x800000 0x44000000  # с initrd

# bootm для uImage или FIT image
bootm 0x40200000 - 0x44000000

# Передать bootargs ядру
setenv bootargs "console=ttyS2,1500000n8 root=/dev/mmcblk0p2 rw rootwait"
booti 0x40200000 - 0x44000000
```

### 4.6 Переменные окружения

```bash
printenv                     # все переменные
printenv bootargs            # одна переменная
setenv bootargs "console=ttyS2,1500000 root=/dev/mmcblk0p2 rw"
setenv myvar "hello"
unsetenv myvar               # удалить переменную

saveenv                      # сохранить на flash (eMMC/SPI NOR/NAND)
# ВНИМАНИЕ: без saveenv изменения потеряются при перезагрузке

# Конкатенация строк в переменных
setenv bootcmd 'run mmcboot; run netboot; echo BOOT FAILED'
```

### 4.7 U-Boot скриптование

Переменные могут содержать последовательности команд. `run <varname>` выполняет команды из переменной.

```bash
# Скрипт загрузки с MMC
setenv mmcboot 'echo Booting from MMC...; \
    if mmc dev 0; then \
        ext4load mmc 0:2 ${kernel_addr_r} /boot/Image; \
        ext4load mmc 0:2 ${fdt_addr_r} /boot/rk3588-evb.dtb; \
        setenv bootargs "console=ttyS2,1500000 root=/dev/mmcblk0p2 rw rootwait"; \
        booti ${kernel_addr_r} - ${fdt_addr_r}; \
    fi'

# Скрипт сетевой загрузки
setenv netboot 'echo Booting over network...; \
    dhcp; \
    tftp ${kernel_addr_r} Image; \
    tftp ${fdt_addr_r} rk3588-evb.dtb; \
    setenv bootargs "console=ttyS2,1500000 root=/dev/nfs nfsroot=${serverip}:/srv/nfs/rk3588,nfsvers=3 ip=dhcp"; \
    booti ${kernel_addr_r} - ${fdt_addr_r}'

# Цепочка fallback
setenv bootcmd 'run mmcboot; run netboot; echo ALL BOOT METHODS FAILED'

# Сохранить скрипты
saveenv
```

Условия, циклы:
```bash
# if/then/else
if test ${boot_mode} = "net"; then run netboot; else run mmcboot; fi

# Сравнение строк
if itest ${board_rev} == "2"; then echo "Rev 2 board"; fi

# Цикл через setexpr
setexpr count 0
# (циклы в U-Boot shell ограничены; для сложной логики — boot script бинарник)
```

---

## 5. Переменные окружения: адреса загрузки

### 5.1 Стандартные адреса

В современном U-Boot стандартные адреса загрузки хранятся в переменных `*_addr_r`. Они устанавливаются в `board_env_set_def_vars()` или через `CONFIG_MEM_LAYOUT_ENV_SETTINGS`:

| Переменная | Типичное значение | Назначение |
|---|---|---|
| `kernel_addr_r` | 0x40200000 | Ядро Linux (Image) |
| `fdt_addr_r` | 0x44000000 | Device Tree Blob |
| `ramdisk_addr_r` | 0x44800000 | initramfs |
| `pxefile_addr_r` | 0x40100000 | PXE конфиг файл |
| `scriptaddr` | 0x40000000 | Boot скрипт |

### 5.2 Требования к адресам

Адрес загрузки должен:

1. **Находиться в DRAM** — после инициализации DDR. `CONFIG_SYS_SDRAM_BASE` = начало DRAM, обычно 0x40000000 для RK3588.
2. **Не пересекаться с U-Boot** — U-Boot загружается по `CONFIG_SYS_TEXT_BASE`, вокруг него stack и heap.
3. **Не пересекаться с другими загруженными образами** — ядро, DTB и initrd не должны перекрывать друг друга.
4. **Иметь достаточный размер** — ядро ARM64 обычно 20–40 MB, initramfs может быть больше 100 MB.

Типичная раскладка памяти RK3588 (16 GB DRAM, начало 0x40000000):
```
0x40000000  → 0x401FFFFF  : U-Boot (2MB)
0x40200000  → 0x44000000  : Kernel Image (~60MB)
0x44000000  → 0x44100000  : DTB (1MB)
0x44800000  → 0x80000000  : initramfs / свободно
```

### 5.3 Где задаются адреса

В порядке приоритета:
1. Переменная окружения U-Boot (setenv) — переопределяет всё.
2. `CONFIG_MEM_LAYOUT_ENV_SETTINGS` в Kconfig — задаёт дефолтные `*_addr_r`.
3. `include/configs/<board>.h` — legacy способ через `#define`.
4. `arch/arm/mach-rockchip/board.c` — для RK специфики.

---

## 6. Porting U-Boot на новую плату

### 6.1 Стратегия porting

Никогда не начинай с нуля. Найди ближайшую существующую плату на том же SoC и адаптируй.

```bash
# Найти все конфиги для RK3588
ls configs/ | grep rk3588

# Посмотреть файлы для конкретной платы
ls board/rockchip/evb-rk3588/
ls arch/arm/dts/ | grep rk3588
```

### 6.2 Необходимые файлы

Для новой платы нужны следующие файлы:

**`configs/<board>_defconfig`** — главная конфигурация:
```
CONFIG_ARM=y
CONFIG_ARCH_ROCKCHIP=y
CONFIG_SYS_TEXT_BASE=0x00a00000
CONFIG_ROCKCHIP_RK3588=y
CONFIG_TARGET_EVB_RK3588=y
CONFIG_DEFAULT_DEVICE_TREE="rk3588-myboard"
CONFIG_ENV_IS_IN_MMC=y
CONFIG_BOOTDELAY=3
```

**`board/<vendor>/<board>/board.c`** — board-specific код:
```c
#include <common.h>
#include <init.h>

int board_init(void)
{
    /* Инициализация специфичной для платы аппаратуры */
    /* Например: настройка GPIO для USB power */
    return 0;
}

int checkboard(void)
{
    puts("Board: MyCompany MyBoard v1.0\n");
    return 0;
}

/* Дополнительные env переменные специфичные для платы */
int board_late_init(void)
{
    env_set("board", "myboard");
    return 0;
}
```

**`board/<vendor>/<board>/Makefile`**:
```makefile
obj-y += board.o
```

**`include/configs/<board>.h`** — legacy конфигурация:
```c
#ifndef __MYBOARD_RK3588_H
#define __MYBOARD_RK3588_H

#include <configs/rk3588_common.h>

/* Специфика платы */
#define CONFIG_SYS_MMC_ENV_DEV 0        /* eMMC */
#define CONFIG_SYS_MMC_ENV_PART 0       /* user partition */

#endif
```

**`arch/arm/dts/rk3588-myboard.dts`** — Device Tree:
```dts
/dts-v1/;
#include "rk3588.dtsi"

/ {
    model = "MyCompany MyBoard";
    compatible = "mycompany,myboard", "rockchip,rk3588";

    chosen {
        stdout-path = "serial2:1500000n8";
    };
};

/* Настройка eMMC */
&sdhci {
    bus-width = <8>;
    no-sdio;
    no-sd;
    non-removable;
    status = "okay";
};

/* Настройка Ethernet */
&gmac0 {
    phy-mode = "rgmii-id";
    clock_in_out = "output";
    status = "okay";
    /* ... */
};
```

### 6.3 Процесс porting пошагово

1. **Выбрать родительскую плату** — тот же SoC, похожая периферия.
2. **Скопировать defconfig** → `configs/myboard_defconfig`, изменить `TARGET_*` и `DEFAULT_DEVICE_TREE`.
3. **Скопировать board директорию** → адаптировать `board.c` (checkboard, PMIC init если нужно).
4. **Создать DTS** → начать с минимума (UART для вывода), добавлять periferals постепенно.
5. **Скопировать `include/configs/`** → изменить include базового конфига.
6. **Зарегистрировать в Kconfig** → добавить `TARGET_MYBOARD_RK3588` в `arch/arm/mach-rockchip/Kconfig`.
7. **Собрать и проверить минимальный UART вывод.**
8. **Добавить storage (eMMC/SD)** → правильный пинмакс в DTS.
9. **Добавить сеть, USB** → по необходимости.

### 6.4 PMIC интеграция

Для RK3588 типичные PMIC: RK806, RK860. Инициализация PMIC в SPL критична — без правильных напряжений DDR не обучится.

```c
/* board/rockchip/myboard/board.c */
#include <power/rk8xx_pmic.h>

int board_early_init_f(void)
{
    /* Для RK3588: настройка напряжений DDR через PMIC */
    /* Обычно делается автоматически через DTS + PMIC драйвер */
    return 0;
}
```

В DTS для SPL:
```dts
&i2c0 {
    clock-frequency = <400000>;
    status = "okay";

    rk806: pmic@23 {
        compatible = "rockchip,rk806";
        reg = <0x23>;
        /* ... настройки питания ... */
    };
};
```

---

## 7. TFTP/NFS загрузка для разработки

### 7.1 Настройка TFTP сервера на хосте

```bash
# Ubuntu/Debian
apt install tftpd-hpa

# Конфигурация /etc/default/tftpd-hpa
TFTP_USERNAME="tftp"
TFTP_DIRECTORY="/srv/tftp"
TFTP_ADDRESS="0.0.0.0:69"
TFTP_OPTIONS="--secure"

# Создать директорию
mkdir -p /srv/tftp
chmod 777 /srv/tftp

# Скопировать файлы
cp output/images/Image /srv/tftp/
cp output/images/rk3588-myboard.dtb /srv/tftp/

systemctl restart tftpd-hpa
```

### 7.2 Настройка NFS сервера

```bash
# Установка
apt install nfs-kernel-server

# Экспорт /etc/exports
/srv/nfs/rk3588  192.168.1.0/24(rw,sync,no_root_squash,no_subtree_check)

# Применить
exportfs -ra
systemctl restart nfs-kernel-server

# Распаковать rootfs
mkdir -p /srv/nfs/rk3588
tar -xf output/images/rootfs.tar -C /srv/nfs/rk3588
```

### 7.3 U-Boot скрипт для NFS загрузки

```bash
# На плате через UART
setenv serverip 192.168.1.1
setenv ipaddr   192.168.1.100

setenv nfsroot '/srv/nfs/rk3588'

setenv bootargs_nfs 'console=ttyS2,1500000n8 \
    root=/dev/nfs \
    nfsroot=${serverip}:${nfsroot},nfsvers=3,tcp \
    ip=${ipaddr}:${serverip}::${netmask}::eth0:off \
    rootwait rw'

setenv netboot '\
    tftp ${kernel_addr_r} Image; \
    tftp ${fdt_addr_r} rk3588-myboard.dtb; \
    setenv bootargs ${bootargs_nfs}; \
    booti ${kernel_addr_r} - ${fdt_addr_r}'

saveenv
run netboot
```

### 7.4 Workflow при разработке

```
Изменил драйвер на хосте
→ make -j$(nproc)                         # пересобрать ядро
→ cp arch/arm64/boot/Image /srv/tftp/     # обновить на TFTP
→ Перезагрузить плату                    # U-Boot загружает новое ядро
→ Тест
```

Без перепрошивки SD карты. Изменение rootfs:
```
Пересобрал утилиту → скопировал в /srv/nfs/rk3588/usr/bin/ → перезапустил сервис на плате
```

---

## 8. FIT Image (Flattened Image Tree)

### 8.1 Зачем FIT

Проблема раздельной передачи ядро + DTB + initrd: три отдельных файла, три команды tftp, три адреса. FIT объединяет всё в один файл с описанием конфигурации, подписью и хешами.

Преимущества FIT перед раздельной загрузкой:
- Один файл для загрузки.
- Атомарная подпись всего образа (RSA).
- Хеши компонентов (целостность при повреждении flash).
- Поддержка нескольких конфигураций в одном файле (например, для разных ревизий платы).
- U-Boot proper тоже упаковывается как FIT (u-boot.itb = U-Boot + TF-A + DTB).

### 8.2 Формат ITS (Image Tree Source)

```dts
/dts-v1/;

/ {
    description = "RK3588 Linux kernel FIT image";
    #address-cells = <1>;

    images {
        kernel-1 {
            description = "ARM64 Linux kernel";
            data = /incbin/("Image");
            type = "kernel";
            arch = "arm64";
            os = "linux";
            compression = "none";
            load = <0x40200000>;
            entry = <0x40200000>;
            hash-1 {
                algo = "sha256";
            };
        };

        fdt-rk3588-myboard {
            description = "Flattened Device Tree for myboard";
            data = /incbin/("rk3588-myboard.dtb");
            type = "flat_dt";
            arch = "arm64";
            compression = "none";
            hash-1 {
                algo = "sha256";
            };
        };

        ramdisk-1 {
            description = "initramfs";
            data = /incbin/("initramfs.cpio.gz");
            type = "ramdisk";
            arch = "arm64";
            os = "linux";
            compression = "gzip";
            hash-1 {
                algo = "sha256";
            };
        };
    };

    configurations {
        default = "conf-myboard";

        conf-myboard {
            description = "MyBoard configuration";
            kernel = "kernel-1";
            fdt = "fdt-rk3588-myboard";
            ramdisk = "ramdisk-1";
            signature-1 {
                algo = "sha256,rsa2048";
                key-name-hint = "dev";
                sign-images = "kernel", "fdt", "ramdisk";
            };
        };

        /* Вторая конфигурация для другой ревизии платы */
        conf-myboard-v2 {
            description = "MyBoard v2 configuration";
            kernel = "kernel-1";
            fdt = "fdt-rk3588-myboard-v2";
        };
    };
};
```

### 8.3 Сборка FIT image

```bash
# Базовая сборка без подписи
mkimage -f image.its image.itb

# С подписью (требует ключ)
# Сгенерировать ключ:
openssl genrsa -out dev.key 2048
openssl req -batch -new -x509 -key dev.key -out dev.crt

# Подписать FIT:
mkimage -f image.its -k /path/to/keys -K u-boot.dtb -r image.itb

# Встроить публичный ключ в U-Boot DTB для верификации:
# (u-boot.dtb теперь содержит dev.crt → пересобрать U-Boot с этим DTB)
```

### 8.4 Загрузка FIT image

```bash
# Загрузить FIT образ
tftp 0x40000000 image.itb

# Запустить (bootm выбирает конфигурацию по умолчанию)
bootm 0x40000000

# Или указать конфигурацию явно
bootm 0x40000000#conf-myboard-v2

# bootm при необходимости копирует ядро/fdt/ramdisk
# из FIT в правильные адреса памяти автоматически
```

### 8.5 Secure Boot с FIT

При `CONFIG_FIT_SIGNATURE=y` и встроенном публичном ключе U-Boot:
1. При `bootm` верифицирует подпись каждого компонента.
2. При несоответствии подписи — отказывает в загрузке.
3. Таким образом гарантируется что только подписанное ядро запустится.

Для production: публичный ключ в U-Boot, приватный ключ у разработчика → только он может подписывать образы.

---

## 9. Отладка U-Boot

### 9.1 UART лог

UART — единственный надёжный инструмент при проблемах ранней загрузки. Если не видишь вывода:

1. Правильная ли скорость? Для RK3588 обычно 1500000 бод.
2. Правильный ли UART? RK3588: UART2 обычно debug UART.
3. Правильный ли 8N1 формат?

Что смотреть в выводе:
```
DDR Version V1.08 20230329     ← TPL запустился, DDR init
LPDDR5, 2112MHz                ← DDR тип и частота
channel[0] BW=16 Col=10 Bk=8  ← DDR конфигурация

U-Boot SPL 2024.04 (...)       ← SPL запустился
Trying to boot from MMC2       ← SPL пытается загрузить с MMC
```

### 9.2 Уровни логирования

```
# В defconfig для отладки:
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=7     # DEBUG уровень (7 = максимум)
CONFIG_LOG_MAX_LEVEL=7
CONFIG_SPL_LOG=y
CONFIG_SPL_LOG_MAX_LEVEL=6     # INFO в SPL
```

В коде:
```c
#include <log.h>

log_debug("Initializing DDR at %p\n", ddr_base);  /* level 7 */
log_info("Board: %s\n", board_name);               /* level 6 */
log_warning("PMIC version mismatch\n");             /* level 4 */
log_err("DDR init failed: %d\n", ret);             /* level 3 */
```

### 9.3 JTAG отладка

Для раннего boot (TPL/SPL) без UART вывода:

```bash
# OpenOCD с поддержкой RK3588
openocd -f interface/ftdi/olimex-arm-usb-ocd-h.cfg \
        -f target/rk3588.cfg

# В другом терминале — gdb
aarch64-linux-gnu-gdb u-boot-spl
(gdb) target remote :3333
(gdb) break board_init_f
(gdb) continue
```

JTAG прерывает выполнение на уровне процессора — единственный способ отлаживать до инициализации UART.

### 9.4 Анализ crashдампов

При data abort или undefined instruction U-Boot выводит регистры:
```
"data abort"
pc : 0000000040a01234   lr : 0000000040a01210
sp : 0000000045000000   fp : 0000000045000010
x0 : 0000000000000000   x1 : 0000000040b00000
...
resetting ...
```

Адрес `pc` — инструкция, на которой произошёл crash. Декодировать:
```bash
aarch64-linux-gnu-addr2line -e u-boot 0x40a01234
# или
aarch64-linux-gnu-objdump -d u-boot | grep -A5 'a01234:'
```

### 9.5 Типичные проблемы при porting

| Симптом | Причина | Решение |
|---|---|---|
| Нет вывода UART совсем | Неправильный pinmux UART | Проверить DTS pinmux |
| DDR init зависает | PMIC не дал правильное напряжение | Проверить i2c DTS, PMIC адрес |
| `mmc dev 0` зависает | Неправильный pinmux MMC | Проверить DTS sdhci |
| Ядро не запускается | Неверный bootargs | Проверить console=, root= |
| Kernel panic "no init" | root= указывает не тот раздел | Проверить /dev/mmcblkXpY |

---

## 10. Добавление кастомной команды

### 10.1 Структура команды

Все команды U-Boot в директории `cmd/`. Регистрация через макрос `U_BOOT_CMD`:

```c
/* cmd/mycommand.c */
#include <command.h>
#include <common.h>

static int do_mycommand(struct cmd_tbl *cmdtp, int flag,
                        int argc, char *const argv[])
{
    if (argc < 2) {
        return CMD_RET_USAGE;
    }

    printf("MyCommand: argument = %s\n", argv[1]);

    /* Пример: читаем значение из памяти */
    if (strcmp(argv[1], "read") == 0 && argc >= 3) {
        unsigned long addr = simple_strtoul(argv[2], NULL, 16);
        printf("Value at 0x%lx = 0x%08x\n", addr,
               *(unsigned int *)addr);
        return CMD_RET_SUCCESS;
    }

    return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
    mycommand, 3, 1, do_mycommand,
    "My custom command",
    "read <addr>    - read 32-bit value at address\n"
    "mycommand help - show help"
);
```

```makefile
# cmd/Makefile — добавить строку:
obj-$(CONFIG_CMD_MYCOMMAND) += mycommand.o
```

```
# cmd/Kconfig — добавить:
config CMD_MYCOMMAND
    bool "mycommand"
    default n
    help
      Custom command for demonstration.
```

После `make menuconfig` → находишь в "Command line interface" → включаешь `CMD_MYCOMMAND` → `make -j$(nproc)`.

Использование в U-Boot shell:
```bash
mycommand read 0x40200000
# Value at 0x40200000 = 0x56190527
```

---

## 11. Практика модуля

### Задание 1: U-Boot в QEMU ARM64

```bash
# Конфигурация для QEMU
cd u-boot
make CROSS_COMPILE=aarch64-linux-gnu- qemu_arm64_defconfig
make CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc)

# Запуск
qemu-system-aarch64 \
    -machine virt \
    -cpu cortex-a57 \
    -nographic \
    -bios u-boot.bin

# Ожидаемый результат: появится U-Boot shell
# Попробовать: help, printenv, md 0x40000000 0x20
```

### Задание 2: TFTP загрузка в QEMU

```bash
# Запустить TFTP сервер на хосте
# Настроить сеть QEMU (-netdev user,tftp=...)
qemu-system-aarch64 \
    -machine virt \
    -cpu cortex-a57 \
    -nographic \
    -bios u-boot.bin \
    -netdev user,id=eth0,tftp=/srv/tftp \
    -device virtio-net-device,netdev=eth0

# В U-Boot:
setenv serverip 10.0.2.2    # QEMU host
setenv ipaddr   10.0.2.15
tftp 0x40200000 test.bin
md 0x40200000 0x10
```

### Задание 3: Boot script

```bash
# На хосте создать скрипт
cat > /tmp/boot.cmd << 'EOF'
echo "=== Custom Boot Script ==="
printenv bootargs
booti ${kernel_addr_r} - ${fdt_addr_r}
EOF

# Скомпилировать скрипт в формат U-Boot
mkimage -T script -C none -n "Boot Script" \
        -d /tmp/boot.cmd /srv/tftp/boot.scr

# В U-Boot:
tftp 0x40000000 boot.scr
source 0x40000000
```

### Задание 4: Кастомная команда

Реализовать команду `board_info`, которая выводит:
- Объём обнаруженной RAM (`gd->ram_size`).
- Список MMC устройств.
- MAC адрес сетевого интерфейса.

Зарегистрировать в `cmd/`, добавить `CONFIG_CMD_BOARD_INFO`, проверить в QEMU.

---

## 12. Самопроверка

Ответь без подсказок. Разворачивай ответ только после самостоятельной попытки.

**Вопрос 1:** Из каких этапов состоит bootchain RK3588? Что делает каждый этап?

<details>
<summary>Ответ</summary>

BootROM (read-only в SoC) → ищет образ на storage, копирует TPL в SRAM, передаёт управление.
TPL (~4 KB, SRAM) → единственная задача: инициализировать DDR контроллер (DDR PHY training). Без DRAM дальше двигаться нельзя.
SPL (≤200 KB, SRAM) → настройка clock tree, pinmux, PMIC питания; загружает TF-A + U-Boot proper в DRAM.
TF-A BL31 (EL3) → устанавливает Secure World, PSCI, остаётся резидентным для SMC вызовов.
U-Boot proper (DRAM) → полнофункциональный загрузчик: командная строка, драйверы, загрузка ядра.
Linux Kernel.

</details>

**Вопрос 2:** Чем TPL отличается от SPL? Почему TPL нужен отдельно от SPL?

<details>
<summary>Ответ</summary>

TPL — Tiny Program Loader, первый исполняемый код после BootROM. Задача одна: DDR PHY init. Размер ~4 KB.
SPL — Secondary Program Loader, выполняется после TPL, когда DRAM уже работает. Задачи: clock, PMIC, storage, загрузка U-Boot.
Почему отдельно: код DDR init (blob от производителя) сам по себе сотни KB — не помещается в SRAM вместе с SPL. TPL настраивает DDR, только потом SPL может использовать DRAM для себя и для следующего этапа.

</details>

**Вопрос 3:** Что такое `idbloader.img` и зачем он нужен для Rockchip?

<details>
<summary>Ответ</summary>

`idbloader.img` = TPL + SPL, упакованные в формате, который понимает Rockchip BootROM. BootROM ищет загрузочный образ в специфичном формате (заголовок RKFW/RK3X) на фиксированных смещениях на storage (64 сектора от начала = offset 32 KB).
Без правильного idbloader BootROM не найдёт точку входа и перейдёт в MaskROM mode.
Формируется командой: `dd if=idbloader.img of=/dev/sdX bs=512 seek=64`

</details>

**Вопрос 4:** Как загрузить ядро по TFTP с нуля? Перечислить команды по шагам.

<details>
<summary>Ответ</summary>

```bash
# 1. Настроить сеть
setenv ipaddr 192.168.1.100
setenv serverip 192.168.1.1

# 2. Проверить связь
ping 192.168.1.1

# 3. Загрузить ядро и DTB
tftp ${kernel_addr_r} Image
tftp ${fdt_addr_r} rk3588-myboard.dtb

# 4. Настроить bootargs
setenv bootargs "console=ttyS2,1500000 root=/dev/mmcblk0p2 rw rootwait"

# 5. Запустить ядро (ARM64)
booti ${kernel_addr_r} - ${fdt_addr_r}
```

</details>

**Вопрос 5:** Почему важен адрес загрузки ядра и где он задаётся?

<details>
<summary>Ответ</summary>

Адрес должен: (1) находиться в DRAM (после DDR init); (2) не пересекаться с U-Boot и его стеком/heap; (3) не пересекаться с DTB и initrd; (4) иметь достаточно свободного места для распаковки ядра.
Задаётся в порядке приоритета: переменная окружения `kernel_addr_r` → `CONFIG_MEM_LAYOUT_ENV_SETTINGS` в Kconfig → `include/configs/<board>.h` → legacy `CONFIG_SYS_LOAD_ADDR`.

</details>

**Вопрос 6:** Что такое FIT image? Чем лучше передачи ядра + DTB раздельно?

<details>
<summary>Ответ</summary>

FIT (Flattened Image Tree) — единый файл, содержащий ядро + DTB + initrd с метаданными в DTB-подобном формате. Создаётся через `mkimage -f image.its image.itb`.
Преимущества: (1) один файл вместо трёх; (2) RSA подпись всего образа (secure boot); (3) SHA256 хеши компонентов (целостность); (4) несколько конфигураций в одном файле; (5) bootm выбирает конфигурацию автоматически.
U-Boot proper сам упакован как FIT (u-boot.itb = U-Boot + TF-A + DTB).

</details>

**Вопрос 7:** Как изменить BOOTCOMMAND без пересборки U-Boot?

<details>
<summary>Ответ</summary>

`BOOTCOMMAND` — это переменная окружения U-Boot. При старте U-Boot загружает окружение из flash (eMMC/SPI NOR) и выполняет `bootcmd`. Переменную можно переопределить:
```bash
setenv bootcmd 'run netboot'
saveenv    # сохранить на flash
```
При следующей загрузке U-Boot прочитает новую переменную из flash. Пересборка не нужна. `CONFIG_BOOTCOMMAND` в Kconfig задаёт только дефолтное значение, которое используется если в flash нет сохранённого окружения.

</details>

**Вопрос 8:** Что такое NFS root и как настроить в U-Boot?

<details>
<summary>Ответ</summary>

NFS root — rootfs (корневая ФС) на сервере, монтируется ядром при загрузке по сети через NFS протокол. Плата не имеет локального rootfs — это удобно при разработке: изменяй файлы на хосте, перезагружай плату.
Настройка через bootargs:
```bash
setenv bootargs "console=ttyS2,1500000 \
    root=/dev/nfs \
    nfsroot=192.168.1.1:/srv/nfs/rk3588,nfsvers=3,tcp \
    ip=192.168.1.100:192.168.1.1::255.255.255.0::eth0:off \
    rootwait rw"
```
На хосте: `/etc/exports` с правом на запись, `nfs-kernel-server` запущен, rootfs распакован в `/srv/nfs/rk3588`.

</details>

**Вопрос 9:** Зачем нужен TF-A (Trusted Firmware-A) для RK3588?

<details>
<summary>Ответ</summary>

TF-A реализует Secure World для ARM TrustZone:
1. BL31 (Runtime Firmware) остаётся резидентным в Secure EL3 после запуска U-Boot/ядра.
2. Обрабатывает SMC вызовы из Normal World — ядро вызывает TF-A для PSCI (CPU hotplug, suspend/resume).
3. Инициализирует secure память, защищает secure assets.
Без TF-A ядро Linux не сможет управлять CPU hotplug на RK3588 (PSCI не реализован). `suspend/resume` тоже не будет работать.

</details>

**Вопрос 10:** Как добавить новую команду в U-Boot shell?

<details>
<summary>Ответ</summary>

1. Создать файл `cmd/mycommand.c` с функцией `do_mycommand(struct cmd_tbl*, int, int, char**)`.
2. Зарегистрировать макросом `U_BOOT_CMD(name, maxargs, repeatable, func, usage, help)`.
3. Добавить `obj-$(CONFIG_CMD_MYCOMMAND) += mycommand.o` в `cmd/Makefile`.
4. Добавить `config CMD_MYCOMMAND` в `cmd/Kconfig`.
5. Включить в `menuconfig` или defconfig: `CONFIG_CMD_MYCOMMAND=y`.
6. Пересобрать U-Boot.

</details>

---

## 13. Банк вопросов

### БАЗА — термины и факты (MCQ)

**Б1.** Какой этап загрузки RK3588 отвечает за инициализацию DDR контроллера?
- a) BootROM
- **b) TPL**
- c) SPL
- d) TF-A

*Ответ: b. TPL — Tiny Program Loader, его единственная задача — DDR PHY init. BootROM только передаёт управление; SPL работает уже когда DRAM есть.*

---

**Б2.** Команда `booti` в U-Boot предназначена для:
- a) Загрузки любого образа в формате uImage
- b) Загрузки ядра через bootloader интерпретатор
- **c) Запуска ARM64 ядра Linux в формате Image (не сжатый)**
- d) Запуска FIT image

*Ответ: c. `booti` = boot Image (ARM64 Linux). `bootm` — для uImage/FIT. ARM64 ядро компилируется как `Image` (не сжатый) или `Image.gz`; `booti` работает с `Image`.*

---

**Б3.** Что такое `idbloader.img` в контексте Rockchip U-Boot?

- a) Полный образ U-Boot для прошивки через DFU
- **b) TPL и SPL, упакованные в формат, понятный Rockchip BootROM**
- c) DDR initialization blob отдельно
- d) TF-A BL31 + U-Boot proper

*Ответ: b. BootROM ищет образ в Rockchip-специфичном формате на offset 64 сектора. idbloader = упакованные TPL+SPL.*

---

**Б4.** Какая команда U-Boot скачивает файл с TFTP сервера в RAM?

- a) `netload 0x40200000 Image`
- **b) `tftp 0x40200000 Image`**
- c) `tftpboot Image 0x40200000`
- d) `load tftp 0 0x40200000 Image`

*Ответ: b. Синтаксис: `tftp <адрес_в_RAM> <имя_файла_на_сервере>`.*

---

**Б5.** Переменная окружения U-Boot `bootcmd` содержит:

- a) Параметры ядра Linux (console, root и т.п.)
- **b) Команды U-Boot, выполняемые автоматически после задержки BOOTDELAY**
- c) Путь к kernel image на eMMC
- d) IP адрес TFTP сервера

*Ответ: b. `bootcmd` — команда autoboot. `bootargs` — параметры ядра. `serverip` — IP TFTP сервера.*

---

**Б6.** U-Boot использует систему конфигурации:

- a) CMake с toolchain файлами
- b) Autoconf/automake (configure scripts)
- **c) Kconfig — ту же что и ядро Linux**
- d) Собственную систему на базе Python scripts

*Ответ: c. U-Boot и Linux kernel используют идентичную систему Kconfig. `make menuconfig`, `make savedefconfig` — те же команды.*

---

**Б7.** FIT image (Flattened Image Tree) позволяет:

- a) Загружать ядро без DTB
- b) Ускорить загрузку за счёт параллельной передачи файлов
- **c) Упаковать ядро + DTB + initrd в один файл с подписью и хешами**
- d) Монтировать rootfs прямо из RAM без отдельного раздела

*Ответ: c. FIT = один файл с компонентами, метаданными в DTB-формате, поддержкой RSA подписи и SHA256 хешей.*

---

**Б8.** Команда `saveenv` в U-Boot:

- a) Экспортирует переменные окружения в файл на SD карте
- b) Создаёт резервную копию текущего DTB
- **c) Сохраняет переменные окружения U-Boot в постоянную память (eMMC/SPI NOR)**
- d) Сохраняет конфигурацию в defconfig файл

*Ответ: c. Без `saveenv` изменения (`setenv`) живут только до перезагрузки. С `saveenv` — записывается в специальный раздел storage (CONFIG_ENV_OFFSET).*

---

### МЕХАНИЗМЫ — как и почему работает (self_grade)

**М1.** Опиши полный boot sequence RK3588 от включения питания до запуска ядра Linux. Что делает каждый этап, какие ресурсы доступны (SRAM/DRAM), размерные ограничения.

*Эталон: BootROM (SRAM, read-only, ищет idbloader на storage) → TPL (SRAM ~4KB, DDR PHY init) → SPL (SRAM ≤200KB, clock/PMIC/storage init, грузит TF-A+U-Boot в DRAM) → TF-A BL31 (EL3, PSCI, residient) → U-Boot proper (DRAM, без ограничений) → ядро Linux. Ключевые точки: DRAM недоступна до TPL; secure world устанавливается TF-A; U-Boot выполняет bootcmd.*

---

**М2.** Как настроить рабочий workflow TFTP+NFS для разработки? Перечислить: что установить на хосте, какие файлы куда положить, какие команды выполнить в U-Boot. Зачем NFS root вместо перепрошивки SD?

*Эталон: хост: tftpd-hpa (/srv/tftp), nfs-kernel-server (/etc/exports с rw,no_root_squash). Файлы: Image и .dtb в /srv/tftp, rootfs.tar распакован в /srv/nfs/rk3588. U-Boot: setenv ipaddr/serverip, tftp для ядра и dtb, bootargs с root=/dev/nfs nfsroot=... ip=dhcp. Преимущество: изменение ядра = cp Image /srv/tftp → перезагрузка (5 сек), без физического доступа к плате.*

---

**М3.** Опиши процесс создания FIT image с RSA подписью. Что такое ITS файл, как собрать .itb, как встроить публичный ключ в U-Boot, как верификация происходит при boot.

*Эталон: ITS — текстовое описание (DTS синтаксис): images{kernel,fdt,ramdisk} + configurations{default conf с signature секцией}. Сборка: mkimage -f image.its -k keydir -K u-boot.dtb -r image.itb. Публичный ключ встраивается в u-boot.dtb → пересобрать U-Boot с этим DTB → он содержит ключ для верификации. При bootm: U-Boot читает подпись из FIT, проверяет RSA, при несоответствии — отказывает.*

---

**М4.** Как добавить кастомную команду в U-Boot? Опиши: структуру файла, макрос регистрации, добавление в Kconfig и Makefile, как протестировать в QEMU.

*Эталон: cmd/mycmd.c с функцией do_mycmd(cmd_tbl*, flag, argc, argv[]), регистрация U_BOOT_CMD(name, maxargs, rep, func, short_desc, long_desc). cmd/Makefile: obj-$(CONFIG_CMD_MYCMD) += mycmd.o. cmd/Kconfig: config CMD_MYCMD bool. defconfig: CONFIG_CMD_MYCMD=y. Тест: make qemu_arm64_defconfig, make menuconfig включить CMD_MYCMD, make, запустить в qemu-system-aarch64 -bios u-boot.bin, в консоли вызвать команду.*

---

**М5.** Объясни механизм адресации при загрузке ядра: как ядро оказывается по правильному адресу, что происходит если адреса пересекаются с U-Boot, почему booti требует знания load address заранее.

*Эталон: `tftp addr file` копирует файл в RAM по указанному адресу. `booti kernel_addr - dtb_addr` передаёт управление на kernel_addr. ARM64 ядро самодеструктурирует по load address указанному в ELF заголовке Image. Если addr пересекается с U-Boot: U-Boot перепишется ядром в процессе загрузки → crash до передачи управления. Решение: kernel_addr_r >= TEXT_BASE + u-boot_size + stack. При сомнениях — `bdinfo` показывает раскладку памяти.*

---

**М6.** Как работает механизм `saveenv`/`loadenv`? Где физически хранятся переменные окружения для конфигурации eMMC? Что будет если env раздел повреждён?

*Эталон: saveenv сериализует хеш-таблицу переменных в бинарный формат с CRC32 и записывает в CONFIG_ENV_OFFSET на eMMC (или CONFIG_ENV_OFFSET_REDUND для резервной копии). loadenv: U-Boot при старте читает offset, проверяет CRC, если корректен — загружает переменные, иначе использует compile-time defaults (CONFIG_BOOTCOMMAND и т.п.). При повреждении: используются defaults, вывод "*** Warning - bad CRC, using default environment". Дублирование через CONFIG_ENV_REDUND_OFFSET.*

---

**М7.** Опиши минимальный набор шагов для porting U-Boot на новую плату на RK3588: какие файлы создать, в каком порядке, как проверить минимальный вывод UART.

*Эталон: (1) скопировать defconfig ближайшей платы → изменить TARGET_ и DEFAULT_DEVICE_TREE; (2) создать board/vendor/board/board.c с checkboard() и board_init(); (3) создать arch/arm/dts/rk3588-myboard.dts — минимум: chosen{stdout-path=serial2} + sdhci; (4) скопировать include/configs/ → только include rk3588_common.h; (5) добавить TARGET в arch/arm/mach-rockchip/Kconfig; (6) make myboard_defconfig && make → проверить idbloader.img; (7) записать на SD → должен появиться UART вывод "DDR Version... U-Boot SPL...".*

---

**М8.** Плата не показывает вывод в UART совсем. Последовательность диагностики: что проверить, в каком порядке, как изолировать проблему TPL/SPL/U-Boot.

*Эталон: (1) Аппаратура: правильный ли UART0/2, TX/RX не перепутаны, 1500000 бод, 3.3V vs 1.8V уровни. (2) BootROM: если LED мигает/вспыхивает — BootROM запустился, образ не найден. Проверить idbloader записан по правильному offset. (3) TPL: без вывода DDR Version — TPL не запустился или DDR blob не тот. Проверить rkbin версию для данной ревизии DDR. (4) SPL: "U-Boot SPL" не появился после DDR строк — SPL загрузился но упал. JTAG для анализа. (5) U-Boot: появился SPL но не U-Boot — проблема в u-boot.itb (неверный BL31, broken image).*

---

### ЭКСПЕРТ — архитектура и edge cases (self_grade)

**Э1.** Опиши полный flow Secure Boot с FIT signature на RK3588: генерация ключей, подписание образа, встраивание публичного ключа в U-Boot, цепочка доверия от BootROM до ядра. Где находятся слабые места?

*Эталон: Генерация: RSA 2048/4096 ключ пара. Подписание: mkimage с -k keydir создаёт signature ноды в FIT с RSA hash. Встраивание: публичный ключ вкладывается в u-boot.dtb при mkimage -K → пересборка U-Boot с этим DTB → U-Boot содержит ключ hardcoded. При bootm: U-Boot читает signature из FIT, проверяет RSA против встроенного ключа. Цепочка: BootROM (Rockchip fuses или OTP) → подписанный idbloader → подписанный U-Boot → верифицированное FIT → ядро. Слабые места: (1) если Rockchip OTP не залочены — можно заменить U-Boot; (2) приватный ключ скомпрометирован → вся цепочка сломана; (3) BootROM verification — проприетарный, детали не всегда публичны.*

---

**Э2.** DDR init в SPL/TPL для RK3588: что такое DDR PHY training, почему это критично, почему у Rockchip проприетарный blob, что происходит при ошибке init (как диагностировать без UART).

*Эталон: DDR PHY training — калибровка задержек линий данных/адреса для стабильной работы на высокой частоте (LPDDR5 до 4266 MT/s). Без тренировки — битые данные. Процедура специфична для каждого SoC+DRAM чипа: алгоритм Rockchip проприетарный (закрытые IP ядра). Ошибка init: плата зависает на DDR строке без вывода следующей. Диагностика без UART: (1) LED паттерн если настроен в TPL; (2) JTAG — остановить после DDR init, проверить результат в регистрах; (3) rkdeveloptool с другой версией DDR blob (rkbin имеет несколько версий под разные чипы LPDDR).*

---

**Э3.** TF-A интеграция с U-Boot: как SPL загружает BL31, что делает BL31 при инициализации, как U-Boot работает в EL2 после передачи от BL31, что такое PSCI и как ядро Linux его использует.

*Эталон: SPL загружает u-boot.itb (FIT с U-Boot + BL31 + DTB), передаёт управление BL31 entry point. BL31: устанавливает exception vectors для EL3, инициализирует secure interrupts (GIC), настраивает TZASC (TrustZone Address Space Controller), вызывает BL33 (U-Boot) в EL2. U-Boot в EL2 работает без доступа к EL3 ресурсам. PSCI (Power State Coordination Interface): стандартизированный API через SMC для управления питанием CPU. Ядро Linux: CPU hotplug вызывает CPU_ON/CPU_OFF SMC → TF-A обрабатывает, включает/выключает CPU core через GPC. Suspend: SYSTEM_SUSPEND SMC → TF-A сохраняет EL3 контекст.*

---

**Э4.** PMIC init в U-Boot porting: типичные проблемы при интеграции RK806 для RK3588. Как PMIC влияет на DDR init, что нужно в DTS для SPL, что происходит если PMIC даёт неправильное напряжение.

*Эталон: RK806 управляет напряжениями: VDD_CPU (0.75-1.0V), VDD_GPU, VDD_NPU, VDD_DDR (1.1V для LPDDR5). I2C адрес 0x23. DTS для SPL: i2c0 должен быть помечен u-boot,dm-spl чтобы SPL инициализировал его. Проблемы: (1) I2C pinmux не настроен в DTS → PMIC недоступен → DDR init запускается без правильного напряжения → нестабильная тренировка или crash; (2) неправильный PMIC regulator порядок → CPU стартует при слишком низком напряжении → случайные зависания; (3) PMIC OTP не совпадает с ожидаемой прошивкой → driver probe fail. Диагностика: добавить debug output в pmic_init, проверить i2c scan в U-Boot.*

---

**Э5.** ARM TrustZone архитектура: что такое EL0/EL1/EL2/EL3, разница Secure World и Normal World, как работает переключение контекста через SMC, какие ресурсы недоступны Normal World без SMC.

*Эталон: EL (Exception Level): EL0 = userspace, EL1 = ядро OS, EL2 = гипервизор, EL3 = secure monitor (TF-A). Каждый EL имеет свой набор регистров и привилегий. TrustZone: каждый EL может быть Secure (S-EL1, S-EL0) или Non-Secure (N-EL1, N-EL0). EL3 всегда Secure. Переключение: SMC инструкция из EL1 → trap в EL3, TF-A сохраняет NS контекст, выполняет SMC handler, возвращает результат, восстанавливает NS контекст. Защищённые ресурсы: (1) TZASC — диапазоны памяти, недоступные NS; (2) Secure boot keys в OTP; (3) Crypto engine в secure mode; (4) Secure peripherals (например, eFuse controller). Linux в N-EL1 не может получить доступ без разрешения TF-A.*

