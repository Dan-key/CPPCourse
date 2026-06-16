# Модуль EL2 — Device Tree

## 0. Карта модуля

- **Время:** 12–18 часов
- **Зачем:** каждый ARM драйвер читает конфигурацию из DTS; без понимания DTS невозможно адаптировать существующий драйвер или написать новый под конкретное железо
- **Главные инструменты:** `dtc`, `fdtdump`, `fdtget`/`fdtput`, `kernel Documentation/devicetree/bindings/`
- **Ресурсы:** devicetree.org/specifications, Bootlin DT course materials, `arch/arm64/boot/dts/rockchip/`

---

## 1. Зачем Device Tree существует

### История: «Board soup»

До 2011 года ядро Linux для архитектуры ARM содержало сотни board-specific C-файлов в `arch/arm/mach-*/`. Каждая плата регистрировала свои устройства статически — вручную прописанные адреса регистров, номера прерываний, тактовые частоты. Любое новое железо требовало нового C-файла в ядре, нового `mach-*` каталога, правок в `Kconfig` и `Makefile`.

В 2011 году Линус Торвальдс высказался об этой ситуации прямо:

> *«this whole thing is a fucking pain in the ass»*

Проблема была не только эстетической. К 2011 году в `arch/arm/` насчитывалось более 1000 board files. Каждый дистрибутив ARM ядра тащил за собой сотни специфичных C-файлов с хардкоженными константами. Слияние нового железа в mainline превращалось в кошмар ревью.

### Решение: декларативное описание аппаратуры

Device Tree — это иерархическое описание аппаратного обеспечения в декларативном текстовом формате (DTS — Device Tree Source), которое компилируется в бинарный файл (DTB — Device Tree Blob). Загрузчик (U-Boot, UEFI) передаёт DTB ядру при старте.

**Ключевая идея разделения обязанностей:**

| Ядро (код) | Device Tree (данные) |
|------------|---------------------|
| Описывает **что умеет делать** драйвер | Описывает **что есть на плате** |
| Алгоритм работы с UART IP-блоком | Адрес конкретного UART, его прерывание, тактовая |
| Универсальный драйвер для семейства SoC | Конкретная конфигурация конкретной платы |

### До и после

**До DT (arch/arm/ в 2010):**
```
arch/arm/mach-omap1/    ← 47 файлов
arch/arm/mach-omap2/    ← 89 файлов
arch/arm/mach-imx/      ← 38 файлов
arch/arm/mach-s3c24xx/  ← 52 файла
... (ещё ~80 mach-* каталогов)
```

**После DT (arch/arm/mach-* сегодня):** большинство `mach-*` каталогов содержат только минимальный `board-generic.c`, остальное — в DTS файлах `arch/arm/boot/dts/`.

---

## 2. Синтаксис DTS — полный разбор

### Корневой узел

Каждый DTS файл начинается с директивы версии и корневого узла:

```dts
/dts-v1/;

/ {
    /* содержимое дерева */
};
```

`/dts-v1/;` — обязательная директива, указывает версию формата. Точка с запятой обязательна.

Корневой узел `/` — единственный узел без имени и адреса. Все остальные узлы — его потомки.

### Узлы: имя и unit-address

```
node-name@unit-address {
    properties...
    child-nodes...
};
```

- **node-name** — имя узла, описывает тип устройства. Допустимые символы: `[a-zA-Z0-9,._+-]`. По соглашению: строчные, через дефис. Примеры: `serial`, `i2c`, `gpio-controller`.
- **unit-address** — адрес устройства в пространстве адресов родителя. Обычно совпадает с первым значением свойства `reg`. Для CPU — порядковый номер (0, 1, 2...).
- **Узел без адреса** (нет `@unit-address`) — допустим для узлов без физического адреса: `cpus`, `chosen`, `aliases`, `memory` (хотя `memory` обычно имеет адрес).

Пример:
```dts
serial@ff180000 {   /* UART по адресу 0xff180000 */
    ...
};

i2c@ff3c0000 {      /* I2C контроллер */
    eeprom@50 {     /* I2C устройство с адресом 0x50 */
        ...
    };
};

cpus {              /* Нет адреса — это контейнер, не устройство */
    cpu@0 { ... };
    cpu@1 { ... };
};
```

### Типы значений свойств

**Числа (u32, списки u32):**
```dts
reg = <0xff180000 0x100>;          /* два u32: адрес и размер */
interrupts = <GIC_SPI 33 4>;       /* три u32 */
#address-cells = <1>;              /* одно u32 */
```
Угловые скобки `< >` — массив u32 в big-endian формате.

**Числа u64 (два u32):**
```dts
reg = <0x0 0xff180000 0x0 0x100>;  /* адрес = 0x00ff180000, размер = 0x100 */
```
64-битное значение записывается как пара u32: старшие 32 бита, затем младшие.

**Строки:**
```dts
compatible = "rockchip,rk3588-uart";
status = "okay";
```
Строки в двойных кавычках, null-terminated.

**Список строк (stringlist):**
```dts
compatible = "rockchip,rk3588-uart", "snps,dw-apb-uart";
clock-names = "baudclk", "apb_pclk";
```
Несколько строк через запятую, в итоговом бинаре хранятся конкатенированно с нулями.

**Байт-массив:**
```dts
local-mac-address = [AA BB CC DD EE FF];  /* 6 байт */
```
Квадратные скобки, байты в hex без префикса `0x`.

**Булевое свойство (флаг):**
```dts
big-endian;           /* присутствие = true */
gpio-controller;      /* присутствие = true */
/* отсутствие = false — свойство просто не указывается */
```
Нет значения — само наличие свойства несёт смысл.

**Пустое значение:**
```dts
ranges;    /* пустое значение — особый случай для ranges */
```

**Смешанное (prop-encoded-array):**
```dts
clocks = <&cru SCLK_UART0>, <&cru PCLK_UART0>;
```
Список phandle+specifier пар.

### Комментарии

```dts
// Однострочный комментарий (C++ стиль)

/* Многострочный комментарий
   (C стиль) */
```

### Директивы

```dts
/dts-v1/;                          /* версия формата — обязательна */
/memreserve/ 0x40000000 0x100000;  /* зарезервировать память: не для ядра */
/delete-node/ &uart5;              /* удалить узел (по ссылке или имени) */
/delete-property/ clock-frequency; /* удалить свойство внутри узла */
/plugin/;                          /* это overlay (не base DTB) */
```

`/memreserve/` полезен для зарезервированных областей: framebuffer, carve-out для DSP/MCU, shared memory.

### Полный минимальный DTS

```dts
/dts-v1/;

/ {
    #address-cells = <1>;
    #size-cells = <1>;

    cpus {
        #address-cells = <1>;
        #size-cells = <0>;          /* у CPU нет "размера" */

        cpu@0 {
            device_type = "cpu";
            compatible = "arm,cortex-a55";
            reg = <0>;
        };
    };

    memory@40000000 {
        device_type = "memory";
        reg = <0x40000000 0x40000000>; /* 1GB начиная с 1GB */
    };

    chosen {
        bootargs = "console=ttyS0,115200 root=/dev/mmcblk0p2 rw";
        stdout-path = "serial0:115200n8";
    };

    aliases {
        serial0 = &uart0;
    };
};
```

`chosen` — специальный узел, не описывает устройство. Используется загрузчиком и ядром для передачи параметров командной строки, stdout-path, initrd.

`aliases` — сопоставляет символические имена (`serial0`) с узлами через phandle-ссылки. Используется для нумерации устройств.

---

## 3. Стандартные свойства — детальная таблица

### compatible

**Тип:** stringlist  
**Назначение:** определяет с каким драйвером совместимо устройство. Ядро перебирает строки от первой к последней, ищет совпадение в `of_match_table` зарегистрированных драйверов.

**Порядок строк: наиболее специфичный → наименее специфичный:**
```dts
compatible = "rockchip,rk3588-uart",   /* самый специфичный: этот SoC */
             "rockchip,rk3399-uart",   /* семейство */
             "snps,dw-apb-uart";       /* IP-блок (наиболее общий) */
```

Это позволяет иметь один универсальный драйвер `snps,dw-apb-uart` и опциональные специализированные драйверы для конкретных SoC.

**Формат строк:** `"vendor,model"`. Vendor prefix — официальный идентификатор производителя из `Documentation/devicetree/bindings/vendor-prefixes.yaml`.

### reg

**Тип:** prop-encoded-array  
**Назначение:** адрес(а) и размер(ы) регистровых блоков устройства.

Формат зависит от `#address-cells` и `#size-cells` **родительского** узла:
```dts
/* Родитель: #address-cells = <1>, #size-cells = <1> */
reg = <0xff180000 0x100>;    /* один блок: адрес 0xff180000, размер 0x100 */

/* Родитель: #address-cells = <2>, #size-cells = <2> (64-bit) */
reg = <0x0 0xff180000 0x0 0x100>;  /* то же, но с 64-bit полями */

/* Несколько блоков */
reg = <0xff180000 0x100>,   /* UART registers */
      <0xff190000 0x1000>;  /* DMA buffer */
```

### interrupts

**Тип:** prop-encoded-array  
**Назначение:** описание прерываний устройства. Формат зависит от типа interrupt controller.

Для GIC (ARM Generic Interrupt Controller):
```dts
interrupts = <GIC_SPI 33 IRQ_TYPE_LEVEL_HIGH>;
/* GIC_SPI = 0 (shared peripheral interrupt)
   33 = номер прерывания
   IRQ_TYPE_LEVEL_HIGH = 4 (тип сигнала) */

/* Несколько прерываний */
interrupts = <GIC_SPI 33 IRQ_TYPE_LEVEL_HIGH>,
             <GIC_SPI 34 IRQ_TYPE_EDGE_RISING>;
```

### interrupt-parent

**Тип:** phandle  
**Назначение:** ссылка на interrupt controller, обрабатывающий прерывания этого устройства. Наследуется от родительского узла если не указан явно.

```dts
gic: interrupt-controller@fd400000 {
    compatible = "arm,gic-v3";
    interrupt-controller;    /* этот узел — interrupt controller */
    #interrupt-cells = <3>;  /* три u32 на прерывание */
    ...
};

/ {
    interrupt-parent = <&gic>;  /* все устройства по умолчанию используют gic */

    uart0: serial@ff180000 {
        interrupts = <GIC_SPI 33 IRQ_TYPE_LEVEL_HIGH>;
        /* interrupt-parent унаследован от / */
    };
};
```

### clocks и clock-names

**Тип:** clocks — phandle+specifier list; clock-names — stringlist  
**Назначение:** тактовые сигналы, потребляемые устройством.

```dts
clocks = <&cru SCLK_UART0>, <&cru PCLK_UART0>;
clock-names = "baudclk", "apb_pclk";
```

Параллельные массивы: `clocks[0]` имеет имя `clock-names[0]`. Драйвер запрашивает тактовую по имени:
```c
clk = devm_clk_get(dev, "baudclk");
```

### resets

**Тип:** phandle+specifier list  
**Назначение:** линии сброса устройства.

```dts
resets = <&cru SRST_P_UART0>, <&cru SRST_UART0>;
reset-names = "apb", "uart";
```

### pinctrl-0 / pinctrl-names

**Тип:** pinctrl-0 — phandle list; pinctrl-names — stringlist  
**Назначение:** конфигурация пинов (мультиплексирование, pull-up/down, drive strength) для каждого состояния устройства.

```dts
pinctrl-names = "default", "sleep";
pinctrl-0 = <&uart0m0_xfer>;           /* пины для состояния "default" */
pinctrl-1 = <&uart0m0_sleep>;          /* пины для состояния "sleep" */
```

Ядро автоматически переключает pinctrl состояние при suspend/resume.

### status

**Тип:** string  
**Значения:**

| Значение | Смысл |
|----------|-------|
| `"okay"` | Устройство включено, драйвер должен инициализироваться |
| `"disabled"` | Устройство существует но не активно; драйвер не загружается |
| `"fail"` | Устройство неисправно (задаётся firmware) |
| `"fail-sss"` | Устройство неисправно, причина в строке `sss` |

**Паттерн использования:** SoC DTS содержит все узлы со `status = "disabled"`. Board DTS включает нужные через `status = "okay"`.

### #address-cells и #size-cells

**Тип:** u32  
**Назначение:** определяют формат свойства `reg` у **дочерних** узлов.

```dts
soc {
    #address-cells = <2>;   /* адрес = два u32 (64-bit) */
    #size-cells = <2>;      /* размер = два u32 (64-bit) */

    uart0: serial@ff180000 {
        reg = <0x0 0xff180000  0x0 0x100>;
        /*     ^^^^ адрес ^^^^  ^^^ размер ^^^ */
        /*     (2 ячейки)       (2 ячейки)    */
    };
};
```

**Почему `#address-cells = <2>` нужен для 64-bit адресов:**

ARM64 SoC имеют регистры за пределами 4GB (адреса > 0xFFFFFFFF). Один u32 не вмещает такой адрес. Два u32 дают 64-bit диапазон:

```dts
/* НЕПРАВИЛЬНО для 64-bit: адрес обрезается до 32 бит */
#address-cells = <1>;
uart0: serial@ff180000 {
    reg = <0xff180000 0x100>;     /* OK только если адрес < 4GB */
};

/* ПРАВИЛЬНО для 64-bit */
#address-cells = <2>;
uart0: serial@ff180000 {
    reg = <0x0 0xff180000 0x0 0x100>;  /* 0x00000000_ff180000, размер 0x100 */
};
```

### ranges

**Тип:** prop-encoded-array или пустое  
**Назначение:** трансляция адресов из пространства дочерних узлов в пространство родителя.

Формат одной записи: `<child-address parent-address size>`

```dts
pcie: pcie@f8000000 {
    #address-cells = <3>;    /* PCI address: space, addr-hi, addr-lo */
    #size-cells = <2>;

    ranges =
        /* child-addr(3)          parent-addr(2)    size(2) */
        <0x01000000 0 0x00000000  0xf9000000 0 0x00010000>  /* I/O */
        <0x02000000 0 0xf8000000  0xf8000000 0 0x01000000>; /* MEM */
};
```

Пустой `ranges;` означает прозрачное отображение — дочерние адреса равны родительским.

### dma-ranges

Аналогично `ranges`, но для DMA трансляций. Используется когда IOMMU или особенности аппаратуры требуют отдельного отображения DMA адресов.

---

## 4. Phandle и ссылки между узлами

### Метки (labels) и ссылки

```dts
/* SoC DTS: определяем узел с меткой */
uart0: serial@ff180000 {
    compatible = "rockchip,rk3588-uart", "snps,dw-apb-uart";
    reg = <0x0 0xff180000 0x0 0x100>;
    interrupts = <GIC_SPI 33 IRQ_TYPE_LEVEL_HIGH>;
    clocks = <&cru SCLK_UART0>, <&cru PCLK_UART0>;
    clock-names = "baudclk", "apb_pclk";
    status = "disabled";  /* по умолчанию выключен */
};

/* Board DTS: переопределяем узел через ссылку */
&uart0 {
    status = "okay";
    pinctrl-0 = <&uart0m0_xfer>;
    pinctrl-names = "default";
};
```

**Разбор синтаксиса:**

- `uart0:` — **метка** (label). Должна быть уникальной в пределах DTS после всех включений. Может содержать `[a-zA-Z0-9_]`.
- `&uart0` — **phandle-ссылка** на узел с меткой `uart0`. Используется в двух контекстах:
  1. **Значение свойства:** `clocks = <&cru SCLK_UART0>` — ссылка на clock provider
  2. **Переопределение узла:** `&uart0 { ... }` — добавить/изменить свойства узла

### Механизм override (переопределения)

При компиляции DTS с `#include` (через cpp), узлы, объявленные через `&label`, **объединяются** с оригинальным узлом:

```dts
/* После компиляции оба блока объединяются в один узел serial@ff180000 */
uart0: serial@ff180000 {
    compatible = "rockchip,rk3588-uart", "snps,dw-apb-uart";
    reg = <0x0 0xff180000 0x0 0x100>;
    interrupts = <GIC_SPI 33 IRQ_TYPE_LEVEL_HIGH>;
    clocks = <&cru SCLK_UART0>, <&cru PCLK_UART0>;
    clock-names = "baudclk", "apb_pclk";
    status = "okay";               /* переопределено */
    pinctrl-0 = <&uart0m0_xfer>;  /* добавлено */
    pinctrl-names = "default";    /* добавлено */
};
```

Переопределение работает по правилу: **последнее присвоение свойства побеждает**.

### Числовой phandle

При компиляции в DTB, `dtc` заменяет все метки числами — **phandle**. Каждый узел с меткой получает уникальный u32. Свойства-ссылки (`<&uart0>`) заменяются соответствующими числами.

```c
/* В бинарном DTB внутри clocks = <&cru SCLK_UART0>: */
/* clocks = <0x00000003 0x0000001a>  где 0x3 = phandle узла cru */
```

Явно задать phandle можно так:
```dts
uart0: serial@ff180000 {
    phandle = <5>;   /* редко нужно — dtc делает это автоматически */
};
```

### Разница между &label и числовым phandle

| `&label` | `phandle = <N>` |
|----------|-----------------|
| Используется в исходниках DTS | Используется в скомпилированном DTB |
| Читаем для человека | Читаем для машины |
| dtc заменяет при компиляции | Задаётся явно (редко) |
| Проверяется dtc на существование | Не проверяется автоматически |

---

## 5. Bindings — контракт между DT и драйвером

### Что такое binding

**Binding** — это документация и схема валидации, описывающая:
- какие свойства обязательны/опциональны для данного `compatible`
- типы и допустимые значения свойств
- семантику каждого свойства

Bindings живут в `Documentation/devicetree/bindings/` ядра Linux.

### Старый формат: .txt

Исторически bindings были текстовыми файлами без машинной проверки:
```
Documentation/devicetree/bindings/serial/snps-dw-apb-uart.txt
```

Такие файлы описывают требования человекочитаемым текстом. Они устарели, но ещё встречаются.

### Новый формат: YAML schema (dt-schema)

Начиная с ядра ~5.x, bindings переводятся в YAML формат с валидацией:

```yaml
# Documentation/devicetree/bindings/serial/snps,dw-apb-uart.yaml
%YAML 1.2
---
$id: http://devicetree.org/schemas/serial/snps,dw-apb-uart.yaml#
$schema: http://devicetree.org/meta-schemas/core.yaml#

title: Synopsys DesignWare UART

maintainers:
  - Jamie Iles <jamie@jamieiles.com>

allOf:
  - $ref: serial.yaml#

properties:
  compatible:
    oneOf:
      - items:
          - enum:
              - rockchip,rk3588-uart
              - rockchip,rk3399-uart
          - const: snps,dw-apb-uart
      - const: snps,dw-apb-uart

  reg:
    maxItems: 1

  interrupts:
    maxItems: 1

  clocks:
    minItems: 1
    maxItems: 2

  clock-names:
    oneOf:
      - items:
          - const: baudclk
          - const: apb_pclk
      - items:
          - const: apb_pclk

  reg-shift:
    description: quantity to shift the register offset by
    enum: [0, 1, 2]
    default: 0

  reg-io-width:
    description: the size of the register accesses
    enum: [1, 4]
    default: 1

required:
  - compatible
  - reg
  - interrupts

additionalProperties: false
```

### Валидация DTS против bindings

```bash
# Установить инструменты валидации
pip install dtschema

# Проверить весь DTB
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- dtbs_check

# Проверить конкретный DTB
make ARCH=arm64 CHECK_DTBS=y rk3588-evb1-v10.dtb

# Вывод: предупреждения и ошибки о несоответствии binding
```

**Почему важна валидация:** без схемы опечатка в имени свойства (`clok-frequency` вместо `clock-frequency`) молча игнорируется — ядро просто не найдёт свойство и использует дефолт. С валидацией — это ошибка сборки.

### Написание нового binding

При разработке нового IP-блока или нового compatible — нужно добавить YAML binding:

```bash
# Проверить конкретный файл binding
python3 scripts/dtschema/validate-schema.py \
    Documentation/devicetree/bindings/serial/snps,dw-apb-uart.yaml

# Проверить DTS против конкретного binding
dt-validate -s Documentation/devicetree/bindings/ my_board.dtb
```

---

## 6. dtc — Device Tree Compiler

### Основные операции

```bash
# Компиляция DTS → DTB
dtc -I dts -O dtb -o output.dtb input.dts

# Дизассемблирование DTB → DTS (реверс из бинарного)
dtc -I dtb -O dts -o output.dts input.dtb

# С предобработкой cpp (для #include, #define, макросов)
cpp -nostdinc \
    -I include \
    -I arch/arm64/boot/dts \
    -undef -D__DTS__ \
    input.dts | dtc -I dts -O dtb -o output.dtb -

# Сборка DTB через систему сборки ядра
make ARCH=arm64 dtbs
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- rk3588-evb1-v10.dtb

# Показать предупреждения
dtc -W all -I dts -O dtb -o output.dtb input.dts
```

**Флаги dtc:**

| Флаг | Смысл |
|------|-------|
| `-I dts` | Входной формат: DTS (текст) |
| `-I dtb` | Входной формат: DTB (бинарь) |
| `-O dtb` | Выходной формат: DTB |
| `-O dts` | Выходной формат: DTS |
| `-o file` | Выходной файл |
| `-@` | Сохранить символы для overlay (пометить метки) |
| `-W all` | Включить все предупреждения |
| `-E all` | Все предупреждения как ошибки |

### fdtdump, fdtget, fdtput

```bash
# Дамп всего содержимого DTB в читаемом виде
fdtdump output.dtb

# Прочитать конкретное свойство
fdtget output.dtb /serial@ff180000 compatible

# Прочитать несколько свойств
fdtget output.dtb /serial@ff180000 status reg

# Изменить свойство в DTB (без перекомпиляции!)
fdtput -t s output.dtb /serial@ff180000 status "okay"
fdtput -t u output.dtb /serial@ff180000 clock-frequency 9600

# Флаги типов для fdtput
# -t s  = строка
# -t u  = u32
# -t x  = u32 в hex
# -t b  = байт-массив
```

`fdtput` полезен для быстрого изменения DTB без редактирования исходников — например, для тестирования разных конфигураций.

### Структура DTB (внутренний формат)

DTB — бинарный файл с фиксированной структурой:

```
Offset 0:  Magic number = 0xd00dfeed (big-endian u32)
Offset 4:  Total size
Offset 8:  Offset to structure block
Offset 12: Offset to strings block
Offset 16: Offset to memory reservation block
Offset 20: Version (текущая: 17)
Offset 24: Last compatible version (16)
Offset 28: Boot CPU id
Offset 32: Strings block size
Offset 36: Structure block size
...
Memory reservation block  ← /memreserve/ записи
Structure block           ← узлы и свойства (токены FDT_BEGIN_NODE и т.д.)
Strings block             ← строки имён свойств (разделяемые между всеми узлами)
```

Строки имён свойств хранятся отдельно и используются по offset — это экономит место, т.к. `compatible`, `reg`, `status` встречаются сотни раз.

### Работа с DTB в Buildroot/Yocto

```bash
# Buildroot: DTB в output/images/
ls output/images/*.dtb

# Yocto: DTB в deploy директории
ls tmp/deploy/images/*/

# U-Boot: загрузить DTB и передать ядру
load mmc 0:1 ${fdt_addr} rk3588-evb1-v10.dtb
booti ${kernel_addr} - ${fdt_addr}
```

---

## 7. of_* API — чтение DT из ядра

### Основные заголовки

```c
#include <linux/of.h>              /* базовые of_* функции */
#include <linux/of_device.h>       /* of_match_device, of_device_get_match_data */
#include <linux/of_address.h>      /* of_iomap, of_address_to_resource */
#include <linux/of_irq.h>          /* of_irq_get */
#include <linux/platform_device.h> /* platform_get_resource, platform_get_irq */
```

### Чтение числовых свойств

```c
/* Чтение одиночных значений */
int of_property_read_u8 (const struct device_node *np, const char *propname, u8  *out);
int of_property_read_u16(const struct device_node *np, const char *propname, u16 *out);
int of_property_read_u32(const struct device_node *np, const char *propname, u32 *out);
int of_property_read_u64(const struct device_node *np, const char *propname, u64 *out);

/* Возврат: 0 = успех, -EINVAL = узел/свойство не найдено, -EOVERFLOW = тип не совпал */

/* Чтение массивов u32 */
int of_property_read_u32_array(const struct device_node *np,
                                const char *propname,
                                u32 *out_values,
                                size_t sz);
/* sz = количество элементов для чтения */

/* Чтение переменного числа элементов */
int of_property_count_u32_elems(const struct device_node *np, const char *propname);

/* Пример: */
u32 freq = 115200;  /* дефолт */
of_property_read_u32(np, "clock-frequency", &freq);
/* если свойство отсутствует — freq остаётся 115200 */
```

### Чтение строк

```c
/* Одна строка */
int of_property_read_string(const struct device_node *np,
                             const char *propname,
                             const char **out_string);
/* *out_string указывает прямо в DTB (read-only, не копировать) */

/* Строка по индексу из stringlist */
int of_property_read_string_index(const struct device_node *np,
                                   const char *propname,
                                   int index,
                                   const char **out_string);

/* Количество строк в stringlist */
int of_property_count_strings(const struct device_node *np, const char *propname);

/* Пример: */
const char *mode;
if (of_property_read_string(np, "uart-mode", &mode))
    mode = "normal";  /* дефолт */
```

### Булевые свойства

```c
bool of_property_read_bool(const struct device_node *np, const char *propname);
/* Возвращает true если свойство присутствует (даже без значения) */

/* Пример: */
bool big_endian = of_property_read_bool(np, "big-endian");
```

### Навигация по дереву

```c
/* Найти дочерний узел по имени */
struct device_node *of_get_child_by_name(const struct device_node *np,
                                          const char *name);
/* Возвращает узел с увеличенным ref count — нужно of_node_put() */

/* Итерация по всем дочерним */
struct device_node *child;
for_each_child_of_node(np, child) {
    /* обработать child */
    /* of_node_put(child) вызывается макросом при break */
}

/* Следующий дочерний */
struct device_node *of_get_next_child(const struct device_node *np,
                                       struct device_node *prev);
/* prev = NULL для первого; не забыть of_node_put(prev) */

/* Найти узел по phandle свойству */
struct device_node *of_parse_phandle(const struct device_node *np,
                                      const char *phandle_name,
                                      int index);
/* index = номер элемента если свойство — список phandle */
```

### MMIO ресурсы

```c
/* Получить ресурс из reg[index] */
struct resource *platform_get_resource(struct platform_device *pdev,
                                        unsigned int type,   /* IORESOURCE_MEM */
                                        unsigned int index); /* 0, 1, 2... */

/* Автоматический ioremap с devm (освобождается при remove) */
void __iomem *devm_platform_ioremap_resource(struct platform_device *pdev,
                                              unsigned int index);
/* Возвращает ERR_PTR при ошибке — проверять IS_ERR() */

/* Альтернатива с явным именем */
void __iomem *devm_platform_ioremap_resource_byname(struct platform_device *pdev,
                                                      const char *name);
/* Нужны reg-names в DTS:
   reg-names = "core", "aux";
   reg = <addr1 size1>, <addr2 size2>; */
```

### Прерывания

```c
/* Получить IRQ номер по индексу */
int platform_get_irq(struct platform_device *pdev, unsigned int num);
/* num = 0 для первого прерывания */
/* Возвращает < 0 при ошибке — это errno */

/* По имени (если используется interrupt-names) */
int platform_get_irq_byname(struct platform_device *pdev, const char *name);
/* DTS: interrupt-names = "tx", "rx";
        interrupts = <...>, <...>; */
```

### Паттерн использования в probe

```c
static int mydrv_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct device_node *np = dev->of_node;
    u32 freq = 115200;     /* дефолты */
    const char *mode;
    void __iomem *base;
    int irq, ret;

    /* 1. Обязательный ресурс: MMIO — при ошибке probe завершается */
    base = devm_platform_ioremap_resource(pdev, 0);
    if (IS_ERR(base))
        return PTR_ERR(base);

    /* 2. Обязательное прерывание */
    irq = platform_get_irq(pdev, 0);
    if (irq < 0)
        return irq;

    /* 3. Опциональные свойства: если нет — используем дефолт */
    of_property_read_u32(np, "clock-frequency", &freq);

    if (of_property_read_string(np, "uart-mode", &mode))
        mode = "normal";

    dev_info(dev, "base=%p irq=%d freq=%u mode=%s\n",
             base, irq, freq, mode);
    return 0;
}
```

### Работа с phandle-ссылками

```c
/* DTS:
 *   my_device {
 *       power-supply = <&regulator_3v3>;
 *   };
 */
struct device_node *supply_np;
supply_np = of_parse_phandle(np, "power-supply", 0);
if (supply_np) {
    /* работаем с supply_np */
    of_node_put(supply_np);  /* обязательно! */
}

/* Для стандартных подсистем (regulator, clock, gpio) используют
   специализированные API, а не of_parse_phandle напрямую: */
struct regulator *reg = devm_regulator_get(dev, "power");
struct clk *clk       = devm_clk_get(dev, "baudclk");
```

---

## 8. Platform driver — полный скелет с of_match

```c
// SPDX-License-Identifier: GPL-2.0
/*
 * myuart.c — пример platform driver с of_match_table
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/io.h>

/* Приватные данные драйвера — по одному экземпляру на устройство */
struct myuart_priv {
    struct device  *dev;
    void __iomem   *base;
    int             irq;
    u32             baud;
};

/* Вариант платформы — разные регистры или возможности */
enum myuart_variant {
    MYUART_V1 = 0,
    MYUART_V2 = 1,
};

static int myuart_probe(struct platform_device *pdev)
{
    const struct of_device_id *match;
    enum myuart_variant variant;
    struct myuart_priv *priv;

    /* 1. Выделить приватные данные через devm — освобождаются автоматически */
    priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;

    priv->dev = &pdev->dev;
    platform_set_drvdata(pdev, priv);

    /* 2. Маппинг MMIO (reg[0]) */
    priv->base = devm_platform_ioremap_resource(pdev, 0);
    if (IS_ERR(priv->base))
        return PTR_ERR(priv->base);

    /* 3. IRQ */
    priv->irq = platform_get_irq(pdev, 0);
    if (priv->irq < 0)
        return priv->irq;

    /* 4. Определить вариант через .data в of_match_table */
    match = of_match_device(pdev->dev.driver->of_match_table, &pdev->dev);
    variant = match ? (enum myuart_variant)(uintptr_t)match->data : MYUART_V1;

    /* 5. Опциональные DT свойства с дефолтами */
    priv->baud = 115200;
    of_property_read_u32(pdev->dev.of_node, "clock-frequency", &priv->baud);

    dev_info(&pdev->dev, "myuart v%d probed: base=%p irq=%d baud=%u\n",
             (int)variant + 1, priv->base, priv->irq, priv->baud);
    return 0;
}

static int myuart_remove(struct platform_device *pdev)
{
    /* devm ресурсы освобождаются автоматически после return */
    dev_info(&pdev->dev, "myuart removed\n");
    return 0;
}

/* Таблица совместимости — ядро ищет здесь при match с DT */
static const struct of_device_id myuart_of_match[] = {
    { .compatible = "myco,myuart-v1", .data = (void *)MYUART_V1 },
    { .compatible = "myco,myuart-v2", .data = (void *)MYUART_V2 },
    { /* sentinel — обязателен, завершает список */ }
};
MODULE_DEVICE_TABLE(of, myuart_of_match);
/*
 * MODULE_DEVICE_TABLE генерирует метаданные для модуля,
 * чтобы udev/modprobe могли автоматически загружать модуль
 * при появлении совместимого устройства.
 */

static struct platform_driver myuart_driver = {
    .probe  = myuart_probe,
    .remove = myuart_remove,
    .driver = {
        .name           = "myuart",          /* fallback name (не для DT matching) */
        .of_match_table = myuart_of_match,   /* DT matching таблица */
        .pm             = NULL,              /* power management ops */
    },
};

/*
 * module_platform_driver() — макрос, разворачивающийся в:
 * static int __init myuart_init(void) { return platform_driver_register(&myuart_driver); }
 * static void __exit myuart_exit(void) { platform_driver_unregister(&myuart_driver); }
 * module_init(myuart_init);
 * module_exit(myuart_exit);
 */
module_platform_driver(myuart_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Example platform UART driver with DT support");
MODULE_AUTHOR("BSP Engineer");
```

### Как происходит matching

1. Загрузчик передаёт DTB ядру через регистр x0 (ARM64)
2. Ядро разбирает DTB, создаёт дерево `device_node`
3. Для каждого узла со `status = "okay"` ядро создаёт `platform_device`
4. При регистрации `platform_driver` ядро проходит по всем `platform_device`
5. Для каждого — сравнивает `compatible` из DT с записями в `of_match_table`
6. При совпадении вызывает `probe()`

### DTS для тестирования в QEMU

```dts
/dts-v1/;
/ {
    #address-cells = <1>;
    #size-cells = <1>;

    /* Минимальный interrupt controller для QEMU */
    intc: interrupt-controller {
        compatible = "linux,dummy-intc";
        interrupt-controller;
        #interrupt-cells = <1>;
    };

    myuart@0 {
        compatible = "myco,myuart-v2";
        reg = <0 0x100>;
        interrupt-parent = <&intc>;
        interrupts = <5>;
        clock-frequency = <9600>;
        status = "okay";
    };
};
```

### Makefile для out-of-tree модуля

```makefile
# Makefile
obj-m += myuart.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

# Для cross-compile:
# make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR=/path/to/kernel
```

---

## 9. DT Overlay

### Концепция

DT Overlay (DTBO) — это фрагмент DTB, который применяется поверх base DTB **без перекомпиляции и перезагрузки**. Overlay может добавлять узлы, модифицировать свойства существующих, удалять узлы.

**Типичные применения:**
- Raspberry Pi HAT (Hardware Attached on Top) — автоопределение через EEPROM
- BeagleBone capes — расширительные платы
- Производственные варианты одной платы с разными опциями
- Динамическое добавление устройств на I2C/SPI без перезагрузки

### Структура overlay DTS

```dts
/dts-v1/;
/plugin/;    /* ← ключевая директива: это overlay */

/ {
    /* Метаданные overlay */
    compatible = "rockchip,rk3588";   /* для какого базового DTB */

    fragment@0 {
        target = <&i2c1>;    /* ссылка на узел в base DTB */
        __overlay__ {
            status = "okay";
            #address-cells = <1>;
            #size-cells = <0>;

            my_sensor: tmp102@48 {
                compatible = "ti,tmp102";
                reg = <0x48>;
            };
        };
    };

    fragment@1 {
        target-path = "/";    /* ссылка по пути (для узлов без метки) */
        __overlay__ {
            my_leds {
                compatible = "gpio-leds";
                led0 {
                    gpios = <&gpio1 0 GPIO_ACTIVE_HIGH>;
                };
            };
        };
    };
};
```

**Разница `target` vs `target-path`:**
- `target = <&i2c1>` — по phandle (требует метку в base DTB)
- `target-path = "/soc/i2c@ff3c0000"` — по абсолютному пути

### Компиляция overlay

```bash
# -@ флаг обязателен: сохраняет символы для разрешения ссылок
dtc -@ -I dts -O dtb -o my_overlay.dtbo my_overlay.dts

# Overlay из ядра (если есть DTS в дереве)
make ARCH=arm64 dtbs  # компилирует и *.dtbo если они в Makefile
```

### Применение overlay

**Raspberry Pi (через dtoverlay):**
```bash
# Применить overlay
dtoverlay my_overlay.dtbo

# Список активных overlay
dtoverlay -l

# Удалить overlay
dtoverlay -r my_overlay
```

**Через configfs (универсальный метод, Linux ≥ 4.4):**
```bash
# Смонтировать configfs если не смонтирован
mount -t configfs configfs /sys/kernel/config

# Создать директорию для overlay
mkdir /sys/kernel/config/device-tree/overlays/my-sensor

# Загрузить DTBO файл
cat my_overlay.dtbo > /sys/kernel/config/device-tree/overlays/my-sensor/dtbo

# Применить
echo 1 > /sys/kernel/config/device-tree/overlays/my-sensor/status

# Удалить
rmdir /sys/kernel/config/device-tree/overlays/my-sensor
```

**U-Boot (применение перед передачей ядру):**
```
load mmc 0:1 ${fdt_addr} base.dtb
load mmc 0:1 ${overlay_addr} my_overlay.dtbo
fdt addr ${fdt_addr}
fdt resize 65536
fdt apply ${overlay_addr}
booti ${kernel_addr} - ${fdt_addr}
```

---

## 10. RK3588 DTS архитектура — реальный пример

### Иерархия включений

```
arch/arm64/boot/dts/rockchip/
├── rk3588s.dtsi          ← SoC уровень: все встроенные IP-блоки (disabled)
│   └── (включает rk3588.dtsi для расширенных конфигураций)
├── rk3588.dtsi           ← надстройка: дополнительные блоки RK3588 vs RK3588S
├── rk3588s-rock-5a.dts   ← Radxa Rock 5A (использует rk3588s.dtsi)
├── rk3588-rock-5b.dts    ← Radxa Rock 5B (использует rk3588.dtsi)
└── rk3588-evb1-v10.dts   ← Rockchip EVB плата для разработки
```

**Принцип слоёв:**
- `rk3588s.dtsi` — всё что есть в кремнии: cpus, gic, все UART/I2C/SPI/USB/PCIe/GMAC/ISP. Всё со `status = "disabled"`.
- `rk3588.dtsi` — дополнительные блоки отсутствующие в rk3588s: второй GMAC, дополнительные PCIe и т.д.
- Board DTS — включает нужные dtsi, включает нужные устройства, описывает внешние чипы.

### Пример board DTS

```dts
/* rk3588s-rock-5a.dts */
/dts-v1/;
#include "rk3588s.dtsi"          /* SoC уровень */
#include "rk3588s-rock5a.dtsi"  /* общие для всех Rock 5A */

/ {
    model = "Radxa ROCK 5 Model A";
    compatible = "radxa,rock-5a", "rockchip,rk3588s";
    /* Несколько строк: наиболее специфичная сначала */
};
```

```dts
/* rk3588s-rock5a.dtsi — общие настройки для Rock 5A */

/* Включить debug UART (UART2 на большинстве RK3588 плат) */
&uart2 {
    status = "okay";
};

/* Включить I2C0 для PMIC */
&i2c0 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&i2c0m2_xfer>;

    /* RK806 PMIC от Rockchip */
    rk806: pmic@23 {
        compatible = "rockchip,rk806";
        reg = <0x23>;
        interrupt-parent = <&gpio0>;
        interrupts = <7 IRQ_TYPE_LEVEL_LOW>;
        pinctrl-names = "default";
        pinctrl-0 = <&pmic_pins>, <&rk806_dvs1_null>,
                    <&rk806_dvs2_null>, <&rk806_dvs3_null>;

        regulators {
            /* DCDC регуляторы для CPU, GPU, DDR */
            vdd_cpu_big0_s0: dcdc-reg1 {
                regulator-always-on;
                regulator-boot-on;
                regulator-min-microvolt = <550000>;
                regulator-max-microvolt = <1050000>;
                regulator-name = "vdd_cpu_big0_s0";
                regulator-enable-ramp-delay = <400>;
            };
            /* ... остальные регуляторы ... */
        };
    };
};

/* Ethernet */
&gmac1 {
    status = "okay";
    phy-mode = "rgmii-rxid";
    clock_in_out = "output";
    snps,reset-gpio = <&gpio3 RK_PB7 GPIO_ACTIVE_LOW>;
    snps,reset-active-low;
    snps,reset-delays-us = <0 20000 100000>;
    pinctrl-names = "default";
    pinctrl-0 = <&gmac1_miim
                 &gmac1_tx_bus2
                 &gmac1_rx_bus2
                 &gmac1_rgmii_clk
                 &gmac1_rgmii_bus>;
    tx_delay = <0x42>;
    rx_delay = <0x00>;
    fixed-link {
        speed = <1000>;
        full-duplex;
    };
};
```

### Как найти нужный узел в rk3588s.dtsi

```bash
# Найти uart2 в SoC DTS
grep -n "uart2:" arch/arm64/boot/dts/rockchip/rk3588s.dtsi
# → uart2: serial@feb50000 {

# Посмотреть определение
grep -A 15 "uart2:" arch/arm64/boot/dts/rockchip/rk3588s.dtsi

# Найти все совместимые с определённым IP
grep -r "rockchip,rk3588-uart" arch/arm64/boot/dts/rockchip/
```

### pinctrl в RK3588

Pinctrl в RK3588 DTS — отдельная тема, краткий обзор:

```dts
/* В rk3588s.dtsi — объявление pinctrl */
pinctrl: pinctrl {
    compatible = "rockchip,rk3588-pinctrl";
    rockchip,grf = <&ioc>;
    #address-cells = <2>;
    #size-cells = <2>;
    ranges;

    uart2 {
        /omit-if-no-ref/
        uart2m0_xfer: uart2m0-xfer {
            rockchip,pins =
                <1 RK_PB5 10 &pcfg_pull_up>,  /* TX */
                <1 RK_PB6 10 &pcfg_pull_none>; /* RX */
        };
    };
};

/* В board DTS — использование */
&uart2 {
    pinctrl-names = "default";
    pinctrl-0 = <&uart2m0_xfer>;
    status = "okay";
};
```

---

## 11. Чтение /proc и /sys для отладки DT

### Дерево устройств через sysfs

```bash
# Смонтированное дерево устройств (всегда доступно)
ls /sys/firmware/devicetree/base/

# Структура совпадает с DTS иерархией:
ls /sys/firmware/devicetree/base/serial@ff180000/

# Прочитать строковое свойство
cat /sys/firmware/devicetree/base/serial@ff180000/compatible
# → rockchip,rk3588-uartsnps,dw-apb-uart   (нулевые байты между строками)

# Прочитать бинарное свойство (reg — binary)
xxd /sys/firmware/devicetree/base/serial@ff180000/reg

# Найти все устройства с определённым compatible
grep -r "rockchip,rk3588-uart" /sys/firmware/devicetree/base/
```

### Проверка привязки драйверов

```bash
# Все зарегистрированные platform drivers
ls /sys/bus/platform/drivers/

# Все platform devices
ls /sys/bus/platform/devices/

# Проверить: какой драйвер привязан к устройству
ls -la /sys/bus/platform/devices/ff180000.serial/driver
# → .../ff180000.serial/driver -> ../../../bus/platform/drivers/dw-apb-uart

# Устройства без драйвера (не забиндились)
for dev in /sys/bus/platform/devices/*/; do
    [ ! -e "${dev}driver" ] && echo "$dev"
done

# Принудительная привязка
echo "ff180000.serial" > /sys/bus/platform/drivers/dw-apb-uart/bind

# Принудительное отвязывание
echo "ff180000.serial" > /sys/bus/platform/drivers/dw-apb-uart/unbind
```

### Получение DTB работающего ядра

```bash
# DTB загруженного ядра (Linux ≥ 4.11 с CONFIG_PROC_DEVICETREE не нужен)
cat /sys/firmware/fdt > /tmp/running.dtb

# Дизассемблировать в DTS
dtc -I dtb -O dts -o /tmp/running.dts /tmp/running.dtb
less /tmp/running.dts

# Поиск конкретного устройства
grep -A 20 "serial@ff180000" /tmp/running.dts
```

### /proc/device-tree

```bash
# Если ядро собрано с CONFIG_PROC_DEVICETREE (устарело, но встречается)
ls /proc/device-tree/
cat /proc/device-tree/model
```

### Отладка matching

```bash
# Посмотреть kernel log при загрузке — matching сообщения
dmesg | grep "of_platform_populate\|platform.*probe\|compatible"

# Посмотреть что probe вызвал для конкретного устройства
dmesg | grep "ff180000"

# Включить динамический debug для platform bus
echo 'file drivers/base/platform.c +p' > /sys/kernel/debug/dynamic_debug/control
echo 'file drivers/of/platform.c +p' >> /sys/kernel/debug/dynamic_debug/control

# Список устройств из DT
cat /sys/kernel/debug/devicetree/populated  # если CONFIG_OF_OVERLAY
```

---

## 12. Самопроверка — 10 вопросов с развёрнутыми ответами

**1. Зачем нужен Device Tree — что было до него и почему DT лучше?**

<details>
<summary>Ответ</summary>

До Device Tree ядро Linux для ARM содержало сотни board-specific C-файлов в `arch/arm/mach-*/`. Каждая плата вручную регистрировала платформенные устройства с хардкоженными адресами, прерываниями, тактовыми частотами. Добавление новой платы = новый C-файл в ядре, правки Kconfig и Makefile, отдельная ветка ядра.

DT решает проблему разделением кода и данных: драйвер описывает **что умеет делать** (алгоритм работы с IP-блоком), DT описывает **что есть на плате** (конкретные адреса, прерывания). Один драйвер работает на сотнях плат с одним IP-блоком. Адаптация нового железа — только DTS файл, без изменений ядра.

</details>

**2. Что такое compatible и как ядро использует его для matching с драйвером?**

<details>
<summary>Ответ</summary>

`compatible` — список строк в формате `"vendor,model"`, упорядоченный от наиболее специфичного к наименее. При появлении устройства (создании `platform_device` из DT узла) ядро перебирает записи в `of_match_table` зарегистрированных драйверов и ищет совпадение с каждой строкой из `compatible`. Первое совпадение выигрывает, и ядро вызывает `probe()` соответствующего драйвера.

`MODULE_DEVICE_TABLE(of, ...)` генерирует метаданные в `.ko` файле, позволяя udev/modprobe автоматически загружать модуль при обнаружении устройства.

</details>

**3. Что означает `#address-cells = <2>` у родительского узла?**

<details>
<summary>Ответ</summary>

`#address-cells = <2>` означает, что поле адреса в свойстве `reg` дочерних узлов занимает **два u32** (64 бита). Аналогично `#size-cells = <2>` означает два u32 для поля размера.

Пример: при `#address-cells = <2>` и `#size-cells = <2>` запись `reg = <0x0 0xff180000 0x0 0x100>` означает: адрес `0x00000000_ff180000` (64-bit), размер `0x00000000_00000100` (64-bit). Это необходимо для ARM64 SoC с регистрами выше 4GB.

</details>

**4. Что такое phandle и как работают ссылки `&label`?**

<details>
<summary>Ответ</summary>

Phandle — уникальное числовое (u32) значение, идентифицирующее узел внутри DTB. При компиляции DTS в DTB, `dtc` автоматически присваивает числовые phandle всем узлам, имеющим метки (labels).

Метка `uart0:` в DTS — это синтаксический сахар. `dtc` записывает узлу свойство `phandle = <N>`. Когда другой узел ссылается на `<&uart0>`, `dtc` заменяет это на `<N>`. При применении overlay — механизм тот же, но разрешение ссылок происходит динамически в ядре.

</details>

**5. Как платформенный драйвер получает базовый адрес MMIO из DT?**

<details>
<summary>Ответ</summary>

Через `devm_platform_ioremap_resource(pdev, 0)`. Эта функция:
1. Читает `reg[0]` из DT узла (через `platform_get_resource(pdev, IORESOURCE_MEM, 0)`)
2. Запрашивает регион памяти (`request_mem_region`)
3. Отображает физический адрес в виртуальное пространство ядра (`ioremap`)
4. Регистрирует освобождение ресурса через devm — автоматически вызывается при `remove()`

Возвращает `void __iomem *` или `ERR_PTR` при ошибке. Всегда проверять `IS_ERR()`.

</details>

**6. Зачем нужен `status = "disabled"` в SoC DTS и `status = "okay"` в board DTS?**

<details>
<summary>Ответ</summary>

SoC DTS (`.dtsi`) описывает **все** IP-блоки кремния, но большинство из них на конкретной плате не используются или не подключены. Если все узлы были бы `status = "okay"`, ядро попыталось бы инициализировать каждый UART, каждый I2C, каждый SPI — даже если пины не разведены, нет внешних устройств, или есть конфликты ресурсов.

`disabled` по умолчанию + `okay` в board DTS даёт явный контроль: BSP-инженер сознательно включает только то, что реально есть на плате. Это предотвращает crash при обращении к незадействованным периферийным блокам.

</details>

**7. Что такое DT overlay и когда он используется?**

<details>
<summary>Ответ</summary>

DT Overlay — фрагмент DTB (DTBO), применяемый поверх base DTB без перезагрузки. Overlay может добавлять узлы, изменять свойства, включать устройства.

Используется когда конфигурация не известна на этапе сборки: Raspberry Pi HAT определяет себя через EEPROM и просит загрузчик/ядро применить нужный overlay; BeagleBone capes аналогично. В производстве: одна базовая прошивка для платформы + overlays для производственных вариантов (с Wi-Fi или без, с 4G или без). Через configfs — динамическое добавление I2C/SPI устройств на работающей системе.

</details>

**8. Как проверить что DTS валиден относительно binding?**

<details>
<summary>Ответ</summary>

Через систему сборки ядра:
```bash
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- dtbs_check
```
Или для конкретного DTB:
```bash
make ARCH=arm64 CHECK_DTBS=y rk3588-evb1-v10.dtb
```
Требует установленного `dtschema` (`pip install dtschema`). Проверяет каждый узел с `compatible` против соответствующего YAML binding в `Documentation/devicetree/bindings/`. Выдаёт предупреждения о неизвестных свойствах, неверных типах, отсутствующих обязательных свойствах.

</details>

**9. Почему для 64-бит адресов нужен `#address-cells = <2>`?**

<details>
<summary>Ответ</summary>

Поле `reg` хранит значения как массив u32 (32-битных беззнаковых целых). Один u32 вмещает максимум `0xFFFFFFFF` = 4GB - 1. ARM64 SoC имеют периферию с адресами выше 4GB (например, `0x900000000` для некоторых PCIe или памяти). Чтобы представить такой адрес, нужны два u32: старшие 32 бита и младшие 32 бита. `#address-cells = <2>` сообщает парсеру DT, что нужно читать два u32 для одного адреса и интерпретировать их как 64-битное число.

</details>

**10. Как прочитать Device Tree загруженного ядра на работающем устройстве?**

<details>
<summary>Ответ</summary>

```bash
# Скопировать DTB загруженного ядра
cat /sys/firmware/fdt > /tmp/running.dtb

# Дизассемблировать
dtc -I dtb -O dts -o /tmp/running.dts /tmp/running.dtb

# Читать дерево через sysfs напрямую
ls /sys/firmware/devicetree/base/
cat /sys/firmware/devicetree/base/model

# Для бинарных свойств
xxd /sys/firmware/devicetree/base/serial@ff180000/reg
```

Это полезно для диагностики: проверить что overlay применился, что загрузчик правильно передал параметры, что `bootargs` установлен верно.

</details>

---

## 13. Банк вопросов

### БАЗА — Multiple Choice Questions

**Вопрос 1.** Что происходило в ARM Linux до появления Device Tree?

- A) Каждая плата имела отдельный Makefile с флагами сборки компилятора
- B) Сотни board-specific C-файлов в `arch/arm/mach-*/` статически описывали аппаратуру **(ПРАВИЛЬНО)**
- C) Device Tree уже существовал, но был необязательным
- D) Ядро автоматически определяло аппаратуру через ACPI таблицы

**Вопрос 2.** Что определяет свойство `compatible`?

- A) Версию ядра Linux, с которой совместимо данное устройство
- B) Имя C-структуры драйвера в исходниках ядра
- C) Строку(и) для поиска драйвера через `of_match_table` зарегистрированных драйверов **(ПРАВИЛЬНО)**
- D) Физический адрес устройства в пространстве памяти

**Вопрос 3.** Для чего нужно свойство `status = "disabled"`?

- A) Сообщает ядру что устройство физически отсутствует на плате
- B) Предотвращает инициализацию драйвера для данного узла, позволяя board DTS выборочно включать нужные устройства **(ПРАВИЛЬНО)**
- C) Отключает питание устройства через power management
- D) Запрещает изменение свойств узла через overlay

**Вопрос 4.** Что возвращает `platform_get_irq()` при ошибке?

- A) NULL
- B) 0 (нулевой номер прерывания)
- C) Отрицательный код ошибки errno (например, -ENXIO, -EINVAL) **(ПРАВИЛЬНО)**
- D) `IRQ_NONE`

**Вопрос 5.** Зачем нужны свойства `#address-cells` и `#size-cells`?

- A) Определяют максимальное количество дочерних устройств
- B) Задают количество u32 ячеек для полей адреса и размера в `reg` дочерних узлов **(ПРАВИЛЬНО)**
- C) Указывают размер адресного пространства шины
- D) Определяют разрядность процессора (32 или 64 бит)

**Вопрос 6.** Что такое DTB?

- A) Device Tree Binary — скомпилированный бинарный формат Device Tree, передаваемый загрузчиком ядру **(ПРАВИЛЬНО)**
- B) Debug Trace Buffer — буфер для отладки ядра
- C) Driver Table Base — таблица зарегистрированных драйверов
- D) Dynamic Tree Builder — инструмент динамического построения дерева устройств

**Вопрос 7.** Как связаны labels и phandles в DTS?

- A) Labels и phandles — разные несвязанные механизмы идентификации узлов
- B) Label — текстовая метка в исходнике DTS; dtc при компиляции заменяет её числовым phandle в DTB **(ПРАВИЛЬНО)**
- C) Label — глобальное имя, phandle — локальное имя в пределах узла
- D) Labels используются только в overlay, phandles — только в base DTB

**Вопрос 8.** Что делает `of_property_read_u32()` если свойство отсутствует в узле?

- A) Выводит предупреждение в dmesg и возвращает 0
- B) Вызывает kernel panic
- C) Возвращает отрицательный код ошибки (-EINVAL), не изменяя out_value **(ПРАВИЛЬНО)**
- D) Возвращает 0 и записывает 0 в out_value

---

### МЕХАНИЗМЫ — Самопроверка с развёрнутыми ответами

**М1.** Объясни как ядро сопоставляет DT узел с драйвером — все шаги от boot до вызова `probe()`.

<details>
<summary>Ответ</summary>

1. **Загрузчик** (U-Boot) загружает DTB в RAM, передаёт адрес ядру через регистр `x0` (ARM64).
2. **Ядро** в `setup_arch()` → `unflatten_device_tree()` разбирает flat DTB в дерево структур `device_node`.
3. `of_platform_populate()` проходит по дереву, для каждого узла с `status = "okay"` создаёт `struct platform_device`, заполняет его ресурсами из `reg` и `interrupts`.
4. При регистрации `platform_driver` через `platform_driver_register()` (или `module_platform_driver()`) — bus driver вызывает `platform_match()` для каждой пары device/driver.
5. `platform_match()` проверяет `of_match_table`: для каждой записи сравнивает `entry->compatible` с каждой строкой в `compatible` свойстве DT узла.
6. При совпадении вызывается `driver->probe(pdev)` с указателем на `platform_device`.
7. В `probe()` — `pdev->dev.of_node` указывает на `device_node`, из которого можно читать свойства.

</details>

**М2.** Как платформенный драйвер работает с несколькими `reg`-блоками?

<details>
<summary>Ответ</summary>

DTS может содержать несколько блоков в `reg`:
```dts
reg = <0xff180000 0x100>,   /* блок 0: основные регистры */
      <0xff190000 0x1000>;  /* блок 1: DMA буфер */
```

Драйвер получает каждый блок по индексу:
```c
base = devm_platform_ioremap_resource(pdev, 0);  /* reg[0] */
dma  = devm_platform_ioremap_resource(pdev, 1);  /* reg[1] */
```

Если блоки именованы через `reg-names`:
```dts
reg-names = "core", "dma";
```
То можно использовать:
```c
base = devm_platform_ioremap_resource_byname(pdev, "core");
dma  = devm_platform_ioremap_resource_byname(pdev, "dma");
```

</details>

**М3.** Объясни механизм трансляции адресов через `ranges`.

<details>
<summary>Ответ</summary>

`ranges` описывает маппинг адресного пространства дочерних узлов в пространство родителя. Каждая запись: `<child-addr parent-addr size>`.

Пример: PCI bridge с `ranges = <0 0xf8000000 0 0xf8000000 0 0x01000000>` означает: дочерний адрес 0 → родительский адрес 0xf8000000, размер 0x1000000. Устройство за PCI bridge с адресом 0x00100000 в PCI пространстве доступно с CPU по адресу 0xf8100000.

Пустой `ranges;` — прозрачное отображение: child-addr = parent-addr. Отсутствие `ranges` — дочерние адреса не транслируются (нет прямого доступа с CPU). Ядро использует `ranges` для вычисления физических адресов при построении ресурсов устройств.

</details>

**М4.** Как DT overlay применяется без перезагрузки?

<details>
<summary>Ответ</summary>

Через configfs (ядро с `CONFIG_OF_OVERLAY`):
1. Создать директорию: `mkdir /sys/kernel/config/device-tree/overlays/my-overlay`
2. Записать DTBO бинарь: `cat my.dtbo > .../my-overlay/dtbo`
3. Ядро разрешает ссылки в overlay против live device tree (находит phandle по меткам)
4. Применяет изменения: создаёт/модифицирует `device_node` структуры
5. Для новых узлов — вызывает `of_platform_populate()`, что создаёт `platform_device` и запускает matching/probe

При удалении директории — ядро откатывает изменения, вызывает `remove()` для добавленных устройств.

</details>

**М5.** Как написать platform driver, поддерживающий несколько вариантов compatible с разным поведением?

<details>
<summary>Ответ</summary>

Использовать поле `.data` в `of_device_id`:
```c
struct mydrv_data { int version; bool has_dma; };

static const struct mydrv_data v1_data = { .version = 1, .has_dma = false };
static const struct mydrv_data v2_data = { .version = 2, .has_dma = true };

static const struct of_device_id mydrv_of_match[] = {
    { .compatible = "myco,mydrv-v1", .data = &v1_data },
    { .compatible = "myco,mydrv-v2", .data = &v2_data },
    { }
};

static int mydrv_probe(struct platform_device *pdev)
{
    const struct mydrv_data *data;
    data = of_device_get_match_data(&pdev->dev);
    /* data указывает на v1_data или v2_data в зависимости от совпавшего compatible */
    if (data->has_dma) { /* инициализировать DMA */ }
}
```

</details>

**М6.** Как с помощью `of_parse_phandle` получить `device_node` связанного устройства?

<details>
<summary>Ответ</summary>

```dts
/* DTS */
my_device {
    companion = <&other_chip>;
};
```

```c
/* Драйвер */
struct device_node *companion_np;

companion_np = of_parse_phandle(dev->of_node, "companion", 0);
if (!companion_np) {
    dev_err(dev, "no companion specified\n");
    return -ENODEV;
}

/* Использовать companion_np для получения данных */
u32 freq;
of_property_read_u32(companion_np, "clock-frequency", &freq);

/* ОБЯЗАТЕЛЬНО уменьшить ref count */
of_node_put(companion_np);
```

Для стандартных подсистем лучше использовать специализированные API (`devm_regulator_get`, `devm_clk_get_by_index`) — они сами разрешают phandle.

</details>

**М7.** Почему нельзя сохранять указатель на строку, возвращённую `of_property_read_string()`, дольше жизни device_node?

<details>
<summary>Ответ</summary>

`of_property_read_string()` возвращает указатель **прямо внутрь DTB**, который хранится в памяти ядра. При применении overlay или динамическом изменении дерева — эта память может быть перемещена или освобождена. Также при unflattening — если DTB передан bootloader без копирования, он может быть в нестабильной памяти.

Безопасная практика: скопировать строку через `devm_kstrdup()` в probe(), не хранить сырой указатель из DT после инициализации. Или убедиться что DTB имеет время жизни >= драйвера (в большинстве случаев так и есть, но для overlay осторожность не помешает).

</details>

**М8.** Как протестировать DT-based драйвер в QEMU без реального железа?

<details>
<summary>Ответ</summary>

1. Собрать QEMU с поддержкой `virt` машины для ARM64.
2. Создать минимальный DTS с нужным устройством, скомпилировать в DTB.
3. Запустить QEMU с кастомным DTB:
```bash
qemu-system-aarch64 \
    -M virt -cpu cortex-a53 \
    -dtb my_board.dtb \
    -kernel Image \
    -append "console=ttyAMA0 root=/dev/ram" \
    -initrd rootfs.cpio.gz \
    -nographic
```
4. Загрузить модуль: `insmod my_driver.ko`
5. Проверить dmesg: `dmesg | grep my_driver`

Для устройств с реальными регистрами — QEMU не эмулирует их, поэтому probe пройдёт (адрес замаппируется), но ioread/iowrite будут возвращать 0xFF или вызовут abort. Для логики probe/remove это достаточно.

</details>

---

### ЭКСПЕРТ — Глубокие вопросы

**Э1.** Как работает трансляция прерываний через `interrupt-map`?

<details>
<summary>Ответ</summary>

`interrupt-map` используется когда прерывания устройств проходят через промежуточный контроллер (например, PCI) с нелинейным маппингом.

```dts
interrupt-map-mask = <0xf800 0 0 7>;
interrupt-map =
    /* child-unit-addr  child-irq  parent          parent-irq */
    <0x0000 0 0 1  &gic  0 0 GIC_SPI 128 IRQ_TYPE_LEVEL_HIGH>
    <0x0000 0 0 2  &gic  0 0 GIC_SPI 129 IRQ_TYPE_LEVEL_HIGH>
    <0x0000 0 0 3  &gic  0 0 GIC_SPI 130 IRQ_TYPE_LEVEL_HIGH>
    <0x0000 0 0 4  &gic  0 0 GIC_SPI 131 IRQ_TYPE_LEVEL_HIGH>;
```

Алгоритм разрешения: 1) взять child unit address + child interrupt specifier; 2) применить `interrupt-map-mask`; 3) найти совпадение в `interrupt-map`; 4) получить parent и parent-irq. Если parent сам не является корневым interrupt controller — рекурсивно продолжить. Ядро реализует это в `of_irq_parse_one()`.

</details>

**Э2.** В чём разница между Linux DT и ACPI с точки зрения BSP-разработчика?

<details>
<summary>Ответ</summary>

| Аспект | Device Tree | ACPI |
|--------|-------------|------|
| Формат | Текст (DTS) → бинарь (DTB), open spec | Бинарные таблицы (AML байткод), UEFI/ACPI spec |
| Кто предоставляет | Загрузчик / ядро / BSP инженер | Firmware (BIOS/UEFI) |
| Изменяемость | Можно пересобрать DTS, применить overlay | Требует перепрошивки firmware |
| Валидация | dt-schema (YAML), открытый | iasl, proprietary инструменты |
| ARM-сервер | ACPI обязателен для SBSA-совместимых серверов | — |
| Embedded Linux | DT де-факто стандарт | Редко |
| Power management | Описывается в DTS (регуляторы, clocks) | ACPI методы (AML код) |
| Сложность | Проще для BSP инженера | Сложнее: нужен AML |

Для embedded ARM: DT почти всегда. Для ARM-серверов (Ampere, ThunderX): ACPI. Некоторые платформы поддерживают оба (Raspberry Pi 4, некоторые Qualcomm).

</details>

**Э3.** Как ядро обрабатывает конфликт свойств при применении overlay?

<details>
<summary>Ответ</summary>

При применении overlay через `of_overlay_apply()`:
1. Overlay создаёт changeset — набор операций (ADD_NODE, ADD_PROPERTY, UPDATE_PROPERTY, REMOVE_PROPERTY, REMOVE_NODE).
2. Для `UPDATE_PROPERTY`: новое значение заменяет старое. Нет слияния массивов — полное замещение.
3. Для `ADD_NODE`: создаётся новый дочерний узел.
4. Changeset применяется атомарно под блокировкой `of_mutex`.
5. После применения вызываются notifier'ы (`of_reconfig_notifier_register`) — platform bus получает уведомление и создаёт/удаляет `platform_device`.

При откате (rmdir overlay в configfs): changeset обращается — `UNDO_ADD` и `UNDO_UPDATE` восстанавливают оригинальные значения. Если `remove()` драйвера завершился с ошибкой — overlay всё равно откатывается, может привести к zombie-устройству.

</details>

**Э4.** Что такое live device tree patch и как применить через configfs?

<details>
<summary>Ответ</summary>

Live patch — применение изменений к работающему DT без перезагрузки. Механизм — configfs overlay:

```bash
# Монтирование (если не в fstab)
mount -t configfs configfs /sys/kernel/config

# Создать overlay слот
mkdir /sys/kernel/config/device-tree/overlays/my-patch

# Загрузить DTBO
cat my_patch.dtbo > /sys/kernel/config/device-tree/overlays/my-patch/dtbo
# После записи — overlay применяется автоматически

# Проверить статус
cat /sys/kernel/config/device-tree/overlays/my-patch/status
# → applied

# Проверить что новый узел появился
ls /sys/firmware/devicetree/base/new-device@0/

# Откатить
rmdir /sys/kernel/config/device-tree/overlays/my-patch
```

Требует `CONFIG_OF_OVERLAY=y`, `CONFIG_OF_CONFIGFS=y`. На Raspberry Pi автоматически через `dtoverlay` утилиту.

</details>

**Э5.** Как написать DT binding validation schema (YAML) для нового устройства?

<details>
<summary>Ответ</summary>

```yaml
# Documentation/devicetree/bindings/misc/myco,mydevice.yaml
%YAML 1.2
---
$id: http://devicetree.org/schemas/misc/myco,mydevice.yaml#
$schema: http://devicetree.org/meta-schemas/core.yaml#

title: MyCo MyDevice

maintainers:
  - Your Name <you@example.com>

description: |
  MyDevice — гипотетическое устройство с UART-подобным интерфейсом.

properties:
  compatible:
    enum:
      - myco,mydevice-v1
      - myco,mydevice-v2

  reg:
    maxItems: 1
    description: Базовый адрес регистрового блока, размер 0x100.

  interrupts:
    maxItems: 1

  clocks:
    maxItems: 1

  clock-names:
    const: core

  myco,baud-rate:
    $ref: /schemas/types.yaml#/definitions/uint32
    description: Скорость передачи данных в бодах.
    enum: [9600, 19200, 38400, 57600, 115200]
    default: 115200

  myco,big-endian:
    type: boolean
    description: Если присутствует — регистры интерпретируются в big-endian.

required:
  - compatible
  - reg
  - interrupts

additionalProperties: false

examples:
  - |
    mydevice@ff200000 {
        compatible = "myco,mydevice-v1";
        reg = <0xff200000 0x100>;
        interrupts = <GIC_SPI 42 IRQ_TYPE_LEVEL_HIGH>;
        clocks = <&cru CLK_MYDEV>;
        clock-names = "core";
        myco,baud-rate = <115200>;
    };
```

Ключевые правила: vendor prefix обязателен для vendor-specific свойств (`myco,`); `additionalProperties: false` для строгой валидации; `required` содержит минимально необходимое; `examples` обязателен для принятия в ядро.

</details>
