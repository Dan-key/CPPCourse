# Модуль EL5 — Platform drivers: I2C и SPI

> Этап 2C, Embedded Linux. I2C и SPI — хлеб насущный BSP-инженера. На RK3588 через I2C подключены PMIC (управление питанием), датчики температуры, акселерометры, дисплейные контроллеры. Через SPI — NOR flash, дисплеи в SPI-режиме, радио-модули. Без понимания device model и этих двух шин ни один реальный BSP-проект не строится.

---

## 0. Карта модуля

| | |
|---|---|
| **Время** | 15–20 ч. ~5 ч теория, ~8 ч практика с кодом, ~3 ч самопроверка. |
| **Зачем** | I2C и SPI — самые распространённые шины в embedded. Все сенсоры, PMIC, flash, радио-модули приходят через них. Умение написать probe/remove с devm_* — базовый навык BSP-инженера. |
| **Ресурсы** | `Documentation/i2c/` и `Documentation/spi/` в дереве ядра; LWN articles о device model; LDD3 (устарел, но структура актуальна); Jonathan Corbet, «Linux Device Drivers, 4th edition» (черновик на lwn.net). |
| **Инструменты** | `i2cdetect`, `i2cget`, `i2cdump`, `i2cset` (пакет i2c-tools); `spidev_test`; `dmesg`, `dynamic_debug`, `ftrace`. |

**Точная карта тем:**

| Тема | Источник |
|------|----------|
| Linux device model (bus/device/driver) | `Documentation/driver-api/driver-model/` |
| Platform driver | `Documentation/driver-api/platform.rst` |
| I2C subsystem | `Documentation/i2c/writing-clients.rst` |
| SMBus API | `Documentation/i2c/smbus-protocol.rst` |
| SPI subsystem | `Documentation/spi/spi-summary.rst` |
| devm_* managed resources | `Documentation/driver-api/driver-model/devres.rst` |
| regmap | `Documentation/driver-api/regmap.rst` |
| Clock framework | `Documentation/driver-api/clk.rst` |
| GPIO consumer API | `Documentation/driver-api/gpio/consumer.rst` |

---

## 1. Linux device model: bus → device → driver

### 1.1 Трёхуровневая модель

Linux device model — трёхуровневая иерархия, общая для всей периферии:

**bus_type** — шина (I2C bus, SPI bus, platform bus, USB bus, PCI bus). Описывает правила матчинга device с driver для данного типа шины. В коде: `struct bus_type`.

**device** — конкретное устройство на шине. Несёт адрес (I2C: 7-бит; SPI: CS номер; platform: диапазон MMIO). В коде: `struct device` — встраивается во все специфичные структуры: `struct i2c_client`, `struct spi_device`, `struct platform_device`.

**device_driver** — код, умеющий работать с устройствами определённого типа. В коде: `struct device_driver` — встраивается в `struct i2c_driver`, `struct spi_driver`, `struct platform_driver`.

Иерархия в sysfs:
```
/sys/bus/i2c/devices/3-0028/    ← device (i2c bus 3, addr 0x28)
/sys/bus/i2c/drivers/mysensor/  ← driver
```

### 1.2 Matching: как ядро сопоставляет device с driver

При регистрации driver или обнаружении device ядро выполняет матчинг. Для platform/I2C/SPI — три механизма в порядке приоритета:

**1. Device Tree (of_match_table)** — по строке `compatible`:
```c
static const struct of_device_id mysensor_of_match[] = {
    { .compatible = "myco,mysensor-v1" },
    { .compatible = "myco,mysensor-v2", .data = (void *)2 },
    { /* sentinel — обязателен! */ }
};
MODULE_DEVICE_TABLE(of, mysensor_of_match);
```
В DTS:
```dts
mysensor@28 {
    compatible = "myco,mysensor-v1";
    reg = <0x28>;
};
```

**2. id_table** — для legacy (без DT) или board-file-based платформ:
```c
static const struct i2c_device_id mysensor_id[] = {
    { "mysensor", 0 },
    { "mysensor-v2", 2 },
    { }
};
MODULE_DEVICE_TABLE(i2c, mysensor_id);
```

**3. ACPI matching** — для x86-embedded платформ (не рассматривается в этом модуле).

`MODULE_DEVICE_TABLE` генерирует таблицу alias'ов в `.ko` файле — это позволяет `udev`/`modprobe` автоматически загружать нужный модуль при появлении устройства.

### 1.3 probe() и remove()

`probe()` вызывается ядром когда device и driver сматчились. Задачи probe:
1. Выделить private data (`devm_kzalloc`)
2. Получить/замаппировать ресурсы (регистры, IRQ, clock, GPIO)
3. Инициализировать железо
4. Зарегистрировать в подсистемах (IIO, hwmon, input, net, ...)

Возвращает 0 при успехе, отрицательный errno при ошибке.

`remove()` (для platform/SPI) или `remove()` с `void` возвратом (для I2C с kernel >= 6.1) — при отключении устройства или выгрузке модуля. `devm_*` ресурсы освобождаются автоматически после `remove()`.

### 1.4 devm_* — managed resources

Ключевое правило: **всегда используй devm_* вместо обычных аллокаций**. Managed ресурсы автоматически освобождаются когда device unbind'ится (при вызове `remove()` или при ошибке в `probe()`).

| Обычная функция | devm_* аналог |
|----------------|---------------|
| `kzalloc()` / `kfree()` | `devm_kzalloc()` |
| `ioremap()` / `iounmap()` | `devm_ioremap()` / `devm_platform_ioremap_resource()` |
| `request_irq()` / `free_irq()` | `devm_request_irq()` |
| `clk_get()` / `clk_put()` | `devm_clk_get()` |
| `gpiod_get()` / `gpiod_put()` | `devm_gpiod_get()` |
| `regmap_init_*()` | `devm_regmap_init_*()` |
| `iio_device_alloc()` | `devm_iio_device_alloc()` |

Порядок освобождения — обратный порядку аллокации (как стек). Это правильно: последний захваченный ресурс освобождается первым.

Если `probe()` вернул ошибку — все `devm_*` аллокации, сделанные до этой точки, автоматически откатываются. Это делает обработку ошибок в probe тривиальной: просто `return ret`.

```c
static int mydev_probe(struct platform_device *pdev)
{
    struct mydev_priv *priv;
    
    priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;
    
    priv->base = devm_platform_ioremap_resource(pdev, 0);
    if (IS_ERR(priv->base))
        return PTR_ERR(priv->base);   /* kzalloc освободится автоматически */
    
    priv->irq = platform_get_irq(pdev, 0);
    if (priv->irq < 0)
        return priv->irq;             /* kzalloc и ioremap освободятся автоматически */
    
    return 0;
}
```

---

## 2. Platform driver — базовая структура

Platform device — устройство без настоящей «интеллектуальной» шины. MMIO-регистры на фиксированных адресах, прерывания, описанные в DTS. Большинство SoC-периферии (UART, I2C controller, SPI controller, таймеры, GPIO) — это platform devices.

```c
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>

/* Private data структура — храним всё что нужно драйверу */
struct mydev_priv {
    struct device *dev;
    void __iomem *base;    /* замаппированные регистры */
    int irq;
    u32 clock_freq;
};

static int mydev_probe(struct platform_device *pdev)
{
    struct mydev_priv *priv;
    struct resource *res;
    int ret;
    
    /* 1. Выделить private data, привязать к device */
    priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;
    priv->dev = &pdev->dev;
    platform_set_drvdata(pdev, priv);  /* сохранить для получения в remove/ioctl */
    
    /* 2. Получить и замаппировать регистры (ресурс 0 из DTS reg = <addr size>) */
    priv->base = devm_platform_ioremap_resource(pdev, 0);
    if (IS_ERR(priv->base))
        return PTR_ERR(priv->base);
    
    /* 3. Получить прерывание */
    priv->irq = platform_get_irq(pdev, 0);
    if (priv->irq < 0)
        return priv->irq;
    
    /* 4. Прочитать свойства из DT */
    ret = of_property_read_u32(pdev->dev.of_node, "clock-frequency",
                               &priv->clock_freq);
    if (ret)
        priv->clock_freq = 100000;  /* default */
    
    /* 5. Инициализация железа */
    /* writel(CTRL_ENABLE, priv->base + REG_CTRL); */
    
    dev_info(&pdev->dev, "probed at %p, irq=%d, freq=%u Hz\n",
             priv->base, priv->irq, priv->clock_freq);
    return 0;
}

static int mydev_remove(struct platform_device *pdev)
{
    /* struct mydev_priv *priv = platform_get_drvdata(pdev); */
    /* devm_* ресурсы освободятся автоматически после return */
    dev_info(&pdev->dev, "removed\n");
    return 0;
}

static const struct of_device_id mydev_of_match[] = {
    { .compatible = "myco,mydev-v1" },
    { .compatible = "myco,mydev-v2", .data = (void *)2UL },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mydev_of_match);

static struct platform_driver mydev_driver = {
    .probe  = mydev_probe,
    .remove = mydev_remove,
    .driver = {
        .name           = "mydev",
        .of_match_table = mydev_of_match,
    },
};
module_platform_driver(mydev_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("My platform driver example");
MODULE_AUTHOR("BSP Engineer");
```

DTS для platform device:
```dts
mydevice@ff100000 {
    compatible = "myco,mydev-v1";
    reg = <0x0 0xff100000 0x0 0x1000>;  /* base addr, size */
    interrupts = <GIC_SPI 100 IRQ_TYPE_LEVEL_HIGH>;
    clock-frequency = <200000000>;
    status = "okay";
};
```

### 2.1 Чтение/запись MMIO регистров

Для MMIO всегда используй специальные accessor'ы — они обеспечивают барьеры памяти и volatile семантику:

```c
/* Чтение */
u32 val = readl(priv->base + REG_STATUS);      /* 32-бит */
u16 val = readw(priv->base + REG_DATA);        /* 16-бит */
u8  val = readb(priv->base + REG_CTRL);        /* 8-бит */

/* Запись */
writel(0x01, priv->base + REG_CTRL);
writew(0xABCD, priv->base + REG_DATA);
writeb(0xFF, priv->base + REG_STATUS);

/* Определения смещений регистров */
#define REG_CTRL    0x00
#define REG_STATUS  0x04
#define REG_DATA    0x08
#define REG_IRQ_EN  0x0C

/* Биты регистра CTRL */
#define CTRL_ENABLE  BIT(0)
#define CTRL_RESET   BIT(1)
#define CTRL_IE      BIT(2)
```

Никогда не разыменовывай `void __iomem *` напрямую — это UB и нарушение sparse annotations.

### 2.2 dev_err_probe — правильная обработка ошибок probe

```c
/* Плохо: */
if (IS_ERR(clk)) {
    dev_err(&pdev->dev, "failed to get clock: %d\n", PTR_ERR(clk));
    return PTR_ERR(clk);
}

/* Хорошо: */
if (IS_ERR(clk))
    return dev_err_probe(&pdev->dev, PTR_ERR(clk), "failed to get clock\n");
```

`dev_err_probe` дополнительно подавляет лишний вывод для `-EPROBE_DEFER` (ресурс ещё не готов, probe будет повторён). Это важно: `-EPROBE_DEFER` — нормальная ситуация (например, clock driver ещё не загрузился), и не надо засорять dmesg сообщениями об ошибке.

---

## 3. I2C subsystem

### 3.1 Физика и протокол I2C

I2C (Inter-Integrated Circuit, произносится «eye-squared-C») — двухпроводная шина: **SCL** (clock, тактовый) и **SDA** (data, данные). Оба провода — open-drain с pull-up резисторами (типично 4.7 кОм для 100 кГц, 2.2 кОм для 400 кГц).

**Стандартные скорости:**
| Режим | Скорость |
|-------|----------|
| Standard Mode | 100 кГц |
| Fast Mode | 400 кГц |
| Fast Mode Plus | 1 МГц |
| High Speed Mode | 3.4 МГц |
| Ultra Fast Mode | 5 МГц (только запись) |

**Адресация:** 7-бит (стандарт, адреса 0x08–0x77 пригодны для использования). 10-бит режим тоже существует, но редок. Адрес задаётся аппаратно или через пины A0/A1/A2 на микросхеме.

**Формат транзакции:**
```
START → [7-бит addr][R/W] → ACK → [byte][ACK] × N → STOP
```

Для чтения регистра (write then read):
```
START → [addr][W] → ACK → [reg] → ACK →
REPEATED START → [addr][R] → ACK → [data byte] → NAK → STOP
```

**Multimaster:** несколько master'ов на одной шине возможны (аппаратный арбитраж). Ядро Linux управляет этим автоматически через мьютекс adapter'а.

### 3.2 I2C client (slave device) драйвер

```c
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

struct mysensor_priv {
    struct i2c_client *client;
    s32 last_temp;
};

/* Чтение одного регистра через SMBus */
static int mysensor_read_reg(struct i2c_client *client, u8 reg)
{
    int ret = i2c_smbus_read_byte_data(client, reg);
    if (ret < 0)
        dev_err(&client->dev, "read reg 0x%02x failed: %d\n", reg, ret);
    return ret;
}

/* Запись одного регистра через SMBus */
static int mysensor_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
    int ret = i2c_smbus_write_byte_data(client, reg, val);
    if (ret < 0)
        dev_err(&client->dev, "write reg 0x%02x failed: %d\n", reg, ret);
    return ret;
}

/* Чтение блока регистров — raw i2c_transfer */
static int mysensor_read_block(struct i2c_client *client, u8 reg,
                               u8 *buf, size_t len)
{
    struct i2c_msg msgs[2] = {
        {
            .addr  = client->addr,
            .flags = 0,             /* write: нет I2C_M_RD */
            .len   = 1,
            .buf   = &reg,          /* адрес первого регистра */
        },
        {
            .addr  = client->addr,
            .flags = I2C_M_RD,      /* read */
            .len   = (u16)len,
            .buf   = buf,
        },
    };
    int ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
    if (ret != ARRAY_SIZE(msgs))
        return ret < 0 ? ret : -EIO;
    return 0;
}

#define MYSENSOR_REG_WHO_AM_I   0x00
#define MYSENSOR_REG_CTRL1      0x01
#define MYSENSOR_REG_SCALE      0x02
#define MYSENSOR_REG_DATA_H     0x10
#define MYSENSOR_REG_DATA_L     0x11

#define MYSENSOR_CHIP_ID        0x42
#define MYSENSOR_CTRL_ENABLE    BIT(0)

static int mysensor_probe(struct i2c_client *client)
{
    struct mysensor_priv *priv;
    int ret;

    /* Выделить private data */
    priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;
    priv->client = client;
    i2c_set_clientdata(client, priv);

    /* Проверить chip ID (WHO_AM_I регистр) */
    ret = mysensor_read_reg(client, MYSENSOR_REG_WHO_AM_I);
    if (ret < 0)
        return dev_err_probe(&client->dev, ret, "can't read chip id\n");
    if (ret != MYSENSOR_CHIP_ID)
        return dev_err_probe(&client->dev, -ENODEV,
                             "bad chip id: 0x%02x (expected 0x%02x)\n",
                             ret, MYSENSOR_CHIP_ID);

    /* Инициализация: включить устройство */
    ret = mysensor_write_reg(client, MYSENSOR_REG_CTRL1, MYSENSOR_CTRL_ENABLE);
    if (ret)
        return ret;

    dev_info(&client->dev, "mysensor probed, addr=0x%02x\n", client->addr);
    return 0;
}

static void mysensor_remove(struct i2c_client *client)
{
    /* devm_* очищается автоматически */
    dev_info(&client->dev, "removed\n");
}

static const struct of_device_id mysensor_of_match[] = {
    { .compatible = "myco,mysensor" },
    { }
};
MODULE_DEVICE_TABLE(of, mysensor_of_match);

static const struct i2c_device_id mysensor_id[] = {
    { "mysensor", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, mysensor_id);

static struct i2c_driver mysensor_driver = {
    .probe    = mysensor_probe,
    .remove   = mysensor_remove,
    .id_table = mysensor_id,
    .driver   = {
        .name           = "mysensor",
        .of_match_table = mysensor_of_match,
    },
};
module_i2c_driver(mysensor_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Example I2C sensor driver");
MODULE_AUTHOR("BSP Engineer");
```

### 3.3 DTS для I2C устройства

```dts
/* В board DTS или overlay: */
&i2c3 {
    status = "okay";
    clock-frequency = <400000>;  /* Fast mode: 400 кГц */

    mysensor@28 {
        compatible = "myco,mysensor";
        reg = <0x28>;            /* 7-бит I2C адрес */

        /* GPIO прерывание */
        interrupt-parent = <&gpio3>;
        interrupts = <5 IRQ_TYPE_LEVEL_LOW>;

        /* Дополнительные свойства */
        vdd-supply = <&ldo5_reg>;  /* питание от регулятора */
    };
};
```

Несколько устройств на одной шине:
```dts
&i2c2 {
    status = "okay";

    pmic@20 {
        compatible = "ti,tps65090";
        reg = <0x20>;
    };

    touchscreen@40 {
        compatible = "elan,ektf2127";
        reg = <0x40>;
        interrupt-parent = <&gpio1>;
        interrupts = <7 IRQ_TYPE_EDGE_FALLING>;
        reset-gpios = <&gpio1 8 GPIO_ACTIVE_LOW>;
    };

    rtc@51 {
        compatible = "nxp,pcf8563";
        reg = <0x51>;
    };
};
```

### 3.4 SMBus vs raw I2C

SMBus — подмножество I2C с чётко определёнными транзакциями. Используй SMBus API когда устройство его поддерживает — проще и надёжнее:

```c
/* Читать один байт из регистра */
s32 i2c_smbus_read_byte_data(struct i2c_client *client, u8 command);

/* Писать один байт в регистр */
s32 i2c_smbus_write_byte_data(struct i2c_client *client, u8 command, u8 value);

/* Читать 16 бит (возвращает little-endian! RK3588 — little-endian, совпадает) */
s32 i2c_smbus_read_word_data(struct i2c_client *client, u8 command);

/* Писать 16 бит */
s32 i2c_smbus_write_word_data(struct i2c_client *client, u8 command, u16 value);

/* Читать блок (до 32 байт, первый байт = длина) */
s32 i2c_smbus_read_i2c_block_data(struct i2c_client *client, u8 command,
                                   u8 length, u8 *values);

/* Писать блок */
s32 i2c_smbus_write_i2c_block_data(struct i2c_client *client, u8 command,
                                    u8 length, const u8 *values);
```

Все SMBus функции возвращают `s32`: при ошибке — отрицательный errno; при успехе чтения — прочитанное значение (не 0!).

Raw `i2c_transfer` нужен когда:
- Нужен Repeated START без промежуточного STOP
- Длина блока > 32 байт (выходит за SMBus лимит)
- Нестандартная последовательность транзакций
- Нужен 10-бит адрес

### 3.5 Получение IRQ из DTS в I2C драйвере

```c
static int mysensor_probe(struct i2c_client *client)
{
    struct mysensor_priv *priv;
    int irq, ret;

    /* ... alloc priv ... */

    /* Получить IRQ из DTS (client->irq заполняется ядром автоматически
       если в DTS есть interrupts = <...>) */
    irq = client->irq;
    if (irq <= 0) {
        dev_info(&client->dev, "no interrupt configured, polling mode\n");
    } else {
        ret = devm_request_threaded_irq(&client->dev, irq,
                                        NULL,                    /* hard IRQ handler */
                                        mysensor_irq_thread,     /* threaded handler */
                                        IRQF_TRIGGER_LOW | IRQF_ONESHOT,
                                        dev_name(&client->dev),
                                        priv);
        if (ret)
            return dev_err_probe(&client->dev, ret, "failed to request IRQ\n");
    }

    return 0;
}

static irqreturn_t mysensor_irq_thread(int irq, void *data)
{
    struct mysensor_priv *priv = data;
    /* Обработка в контексте потока (можно спать, делать I2C транзакции) */
    mysensor_read_and_process(priv);
    return IRQ_HANDLED;
}
```

`devm_request_threaded_irq` — предпочтительно для I2C/SPI драйверов: обработчик выполняется в контексте ядерного потока, что позволяет делать I2C транзакции (они могут спать).

### 3.6 i2ctools — отладка без драйвера

Крайне полезно перед написанием драйвера — убедиться что устройство физически присутствует:

```bash
# Список I2C шин в системе
ls /dev/i2c-*
i2cdetect -l

# Сканировать шину: показывает какие адреса отвечают на ACK
# -y: не спрашивать подтверждение (осторожно с некоторыми устройствами!)
i2cdetect -y 3

# Пример вывода:
#      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
# 00:          -- -- -- -- -- -- -- -- -- -- -- -- --
# 10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
# 20: -- -- -- -- -- -- -- -- 28 -- -- -- -- -- -- --  ← наше устройство!
# 30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

# Читать регистр 0x00 у устройства на адресе 0x28, шина 3
i2cget -y 3 0x28 0x00

# Читать слово (16 бит)
i2cget -y 3 0x28 0x10 w

# Писать регистр 0x01 значение 0x01
i2cset -y 3 0x28 0x01 0x01

# Дамп всех регистров (0x00-0xFF) в hex
i2cdump -y 3 0x28

# Если устройство занято драйвером — сначала unbind:
echo "3-0028" > /sys/bus/i2c/drivers/mysensor/unbind
# После отладки:
echo "3-0028" > /sys/bus/i2c/drivers/mysensor/bind
```

---

## 4. SPI subsystem

### 4.1 Физика SPI

SPI (Serial Peripheral Interface) — четыре линии:
- **MOSI** (Master Out Slave In) — данные от master к slave
- **MISO** (Master In Slave Out) — данные от slave к master
- **SCK** (Serial Clock) — тактовый сигнал от master
- **CS/SS** (Chip Select / Slave Select) — выбор устройства, active low

Full-duplex: MOSI и MISO работают одновременно. Это означает, что каждая транзакция — обмен: master отправляет N байт и одновременно принимает N байт.

Нет ограничения адресации — каждое устройство имеет свою линию CS. Нет стандарта на протокол уровня данных — каждый чип определяет свой.

**Скорости:** от нескольких МГц до 100+ МГц (QSPI). Типично для NOR flash: 25–104 МГц.

### 4.2 Режимы SPI (CPOL/CPHA)

CPOL (Clock Polarity) — уровень SCK в idle. CPHA (Clock Phase) — момент сэмплирования данных.

| Mode | CPOL | CPHA | SCK idle | Сэмплирование |
|------|------|------|----------|---------------|
| 0    | 0    | 0    | Low      | Rising edge   |
| 1    | 0    | 1    | Low      | Falling edge  |
| 2    | 1    | 0    | High     | Falling edge  |
| 3    | 1    | 1    | High     | Rising edge   |

Большинство SPI flash (W25Qxx, MX25Lxx) работают в Mode 0 или Mode 3. Дисплейные контроллеры часто Mode 0. Всегда смотри даташит — неправильный режим даёт мусор в данных без явной ошибки.

В DTS:
- `spi-cpol` — установить CPOL=1
- `spi-cpha` — установить CPHA=1
- Без обоих флагов — Mode 0

### 4.3 SPI device драйвер

```c
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/of.h>

struct myflash_priv {
    struct spi_device *spi;
    u32 max_speed_hz;
};

/* Простая запись (только MOSI, MISO игнорируется) */
static int myflash_write_cmd(struct spi_device *spi, u8 cmd)
{
    return spi_write(spi, &cmd, 1);
}

/* Full-duplex одиночная транзакция */
static int myflash_xfer(struct spi_device *spi,
                        const void *tx, void *rx, size_t len)
{
    struct spi_transfer xfer = {
        .tx_buf = tx,
        .rx_buf = rx,
        .len    = len,
    };
    struct spi_message msg;

    spi_message_init(&msg);
    spi_message_add_tail(&xfer, &msg);
    return spi_sync(spi, &msg);
}

/* Write-then-read в одной CS-транзакции (без промежуточного CS deassert) */
static int myflash_write_then_read(struct spi_device *spi,
                                   const void *txbuf, size_t txlen,
                                   void *rxbuf, size_t rxlen)
{
    return spi_write_then_read(spi, txbuf, txlen, rxbuf, rxlen);
}

/* Несколько SPI transfers в одном сообщении (CS держится всё время) */
static int myflash_read_data(struct spi_device *spi,
                             u32 addr, u8 *buf, size_t len)
{
    /* READ команда: 0x03, 3 байта адреса, затем читаем данные */
    u8 cmd[4] = {
        0x03,
        (addr >> 16) & 0xFF,
        (addr >>  8) & 0xFF,
        (addr >>  0) & 0xFF,
    };
    struct spi_transfer xfers[2] = {
        {
            .tx_buf = cmd,
            .len    = sizeof(cmd),
        },
        {
            .rx_buf = buf,
            .len    = len,
        },
    };
    struct spi_message msg;

    spi_message_init(&msg);
    spi_message_add_tail(&xfers[0], &msg);
    spi_message_add_tail(&xfers[1], &msg);
    return spi_sync(spi, &msg);
}

/* Читать JEDEC ID (Manufacturer + Device ID) */
static int myflash_read_jedec_id(struct spi_device *spi, u8 id[3])
{
    u8 cmd = 0x9F;
    return spi_write_then_read(spi, &cmd, 1, id, 3);
}

#define MYFLASH_JEDEC_MANUFACTURER  0xEF  /* Winbond */
#define MYFLASH_JEDEC_DEVICE_TYPE   0x40
#define MYFLASH_JEDEC_CAPACITY      0x14  /* W25Q80: 1 MB */

static int myflash_probe(struct spi_device *spi)
{
    struct myflash_priv *priv;
    u8 jedec_id[3];
    int ret;

    /* SPI параметры (mode, max_speed_hz, bits_per_word) применяются из DTS
       до вызова probe — можно читать spi->mode, spi->max_speed_hz */
    dev_info(&spi->dev, "SPI mode %u, speed %u Hz, %u bits/word\n",
             spi->mode, spi->max_speed_hz, spi->bits_per_word);

    priv = devm_kzalloc(&spi->dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;
    priv->spi = spi;
    priv->max_speed_hz = spi->max_speed_hz;
    spi_set_drvdata(spi, priv);

    /* Идентификация чипа */
    ret = myflash_read_jedec_id(spi, jedec_id);
    if (ret)
        return dev_err_probe(&spi->dev, ret, "JEDEC read failed\n");

    dev_info(&spi->dev, "JEDEC ID: %02x %02x %02x\n",
             jedec_id[0], jedec_id[1], jedec_id[2]);

    if (jedec_id[0] != MYFLASH_JEDEC_MANUFACTURER)
        return dev_err_probe(&spi->dev, -ENODEV,
                             "unexpected manufacturer: 0x%02x\n", jedec_id[0]);

    return 0;
}

static void myflash_remove(struct spi_device *spi)
{
    dev_info(&spi->dev, "removed\n");
}

static const struct of_device_id myflash_of_match[] = {
    { .compatible = "myco,myflash" },
    { }
};
MODULE_DEVICE_TABLE(of, myflash_of_match);

static const struct spi_device_id myflash_id[] = {
    { "myflash", 0 },
    { }
};
MODULE_DEVICE_TABLE(spi, myflash_id);

static struct spi_driver myflash_driver = {
    .probe    = myflash_probe,
    .remove   = myflash_remove,
    .id_table = myflash_id,
    .driver   = {
        .name           = "myflash",
        .of_match_table = myflash_of_match,
    },
};
module_spi_driver(myflash_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Example SPI flash driver");
MODULE_AUTHOR("BSP Engineer");
```

### 4.4 DTS для SPI устройства

```dts
&spi2 {
    status = "okay";
    /* Параметры контроллера: скорость, number of CS */
    num-cs = <2>;

    /* Устройство на CS0 */
    myflash@0 {
        compatible = "myco,myflash";
        reg = <0>;                       /* CS номер */
        spi-max-frequency = <25000000>;  /* 25 МГц */
        /* Mode 0 по умолчанию (нет spi-cpol, нет spi-cpha) */
    };

    /* Устройство в Mode 3 на CS1 */
    myradio@1 {
        compatible = "myco,myradio";
        reg = <1>;
        spi-max-frequency = <8000000>;
        spi-cpol;     /* CPOL=1 */
        spi-cpha;     /* CPHA=1 → Mode 3 */
        spi-cs-high;  /* CS active high (нестандартно) */
    };
};
```

### 4.5 Асинхронные SPI транзакции

Для высокой пропускной способности — `spi_async` с completion callback:

```c
static void myflash_complete(void *ctx)
{
    struct completion *done = ctx;
    complete(done);
}

static int myflash_async_read(struct spi_device *spi, u8 *buf, size_t len)
{
    DECLARE_COMPLETION_ONSTACK(done);
    struct spi_transfer xfer = { .rx_buf = buf, .len = len };
    struct spi_message msg;

    spi_message_init(&msg);
    msg.complete = myflash_complete;
    msg.context  = &done;
    spi_message_add_tail(&xfer, &msg);

    spi_async(spi, &msg);           /* не блокирует */
    wait_for_completion(&done);     /* ждём callback */
    return msg.status;
}
```

Но в большинстве случаев `spi_sync` достаточно — SPI контроллер обычно имеет DMA и прерывания, `spi_sync` не занимает CPU во время передачи.

---

## 5. Clock и reset management

### 5.1 Common Clock Framework (CCF)

Большинство SoC-периферии требует тактирование перед использованием. В Linux это Common Clock Framework:

```c
#include <linux/clk.h>
#include <linux/reset.h>

static int mydrv_probe(struct platform_device *pdev)
{
    struct clk *clk_bus, *clk_periph;
    struct reset_control *rst;
    unsigned long rate;
    int ret;

    /* Получить clock по имени из clock-names */
    clk_bus = devm_clk_get(&pdev->dev, "apb_pclk");
    if (IS_ERR(clk_bus))
        return dev_err_probe(&pdev->dev, PTR_ERR(clk_bus),
                             "can't get apb_pclk\n");

    clk_periph = devm_clk_get(&pdev->dev, "baudclk");
    if (IS_ERR(clk_periph))
        return dev_err_probe(&pdev->dev, PTR_ERR(clk_periph),
                             "can't get baudclk\n");

    /* Включить clock: prepare + enable */
    ret = clk_prepare_enable(clk_bus);
    if (ret)
        return ret;

    /* devm_clk_get_enabled = devm_clk_get + clk_prepare_enable */
    clk_periph = devm_clk_get_enabled(&pdev->dev, "baudclk");
    if (IS_ERR(clk_periph))
        return PTR_ERR(clk_periph);

    /* Задать частоту */
    ret = clk_set_rate(clk_periph, 115200 * 16);
    if (ret)
        dev_warn(&pdev->dev, "can't set clock rate: %d\n", ret);

    rate = clk_get_rate(clk_periph);
    dev_info(&pdev->dev, "clock rate: %lu Hz\n", rate);

    /* Reset control */
    rst = devm_reset_control_get_exclusive(&pdev->dev, NULL);
    if (!IS_ERR(rst)) {
        reset_control_assert(rst);     /* сбросить устройство */
        udelay(1);
        reset_control_deassert(rst);   /* снять сброс */
    }

    return 0;
}
```

DTS:
```dts
mydevice@ff100000 {
    reg = <0x0 0xff100000 0x0 0x1000>;

    /* Два clock'а: shorted names для devm_clk_get */
    clocks = <&cru SCLK_UART2>, <&cru PCLK_UART2>;
    clock-names = "baudclk", "apb_pclk";

    /* Reset */
    resets = <&cru SRST_UART2>;
    reset-names = "uart2";
};
```

### 5.2 PM Runtime (Power Management Runtime)

Для устройств с поддержкой power management — включать clock/питание только при использовании:

```c
#include <linux/pm_runtime.h>

static int mydev_probe(struct platform_device *pdev)
{
    /* ... */

    pm_runtime_enable(&pdev->dev);
    pm_runtime_set_active(&pdev->dev);

    return 0;
}

static int mydev_remove(struct platform_device *pdev)
{
    pm_runtime_disable(&pdev->dev);
    return 0;
}

/* Эти callbacks вызываются PM runtime: */
static int mydev_runtime_suspend(struct device *dev)
{
    struct mydev_priv *priv = dev_get_drvdata(dev);
    clk_disable_unprepare(priv->clk);
    return 0;
}

static int mydev_runtime_resume(struct device *dev)
{
    struct mydev_priv *priv = dev_get_drvdata(dev);
    return clk_prepare_enable(priv->clk);
}

static const struct dev_pm_ops mydev_pm_ops = {
    SET_RUNTIME_PM_OPS(mydev_runtime_suspend, mydev_runtime_resume, NULL)
};

static struct platform_driver mydev_driver = {
    .driver = {
        .name   = "mydev",
        .pm     = &mydev_pm_ops,
    },
};
```

---

## 6. GPIO в ядре

### 6.1 GPIO consumer API (современный)

С kernel 3.13+ — gpiod API (descriptor-based). Никогда не используй старый gpio_request/gpio_set_value.

```c
#include <linux/gpio/consumer.h>

struct gpio_desc *gpio_reset;
struct gpio_desc *gpio_irq;

/* Получить GPIO из DTS по имени (суффикс -gpios в DTS) */
/* GPIOD_OUT_LOW: выход, изначально низкий уровень */
gpio_reset = devm_gpiod_get(&pdev->dev, "reset", GPIOD_OUT_LOW);
if (IS_ERR(gpio_reset))
    return dev_err_probe(&pdev->dev, PTR_ERR(gpio_reset),
                         "can't get reset gpio\n");

/* GPIOD_IN: вход */
gpio_irq = devm_gpiod_get_optional(&pdev->dev, "irq", GPIOD_IN);
/* _optional: не ошибка если GPIO не задан в DTS */

/* Управление */
gpiod_set_value(gpio_reset, 1);    /* assert reset */
udelay(10);
gpiod_set_value(gpio_reset, 0);    /* deassert reset */

/* Чтение */
int val = gpiod_get_value(gpio_irq);

/* Если GPIO может спать (GPIO expander по I2C) */
gpiod_set_value_cansleep(gpio_reset, 1);
int val = gpiod_get_value_cansleep(gpio_irq);
```

DTS:
```dts
mysensor@28 {
    compatible = "myco,mysensor";
    reg = <0x28>;

    /* <&gpio_bank gpio_num GPIO_ACTIVE_LOW/HIGH> */
    reset-gpios = <&gpio3 5 GPIO_ACTIVE_LOW>;  /* GPIO3_B5, active low */
    irq-gpios   = <&gpio2 8 GPIO_ACTIVE_HIGH>; /* GPIO2_C0, active high */
};
```

Ключевой момент: gpiod API автоматически инвертирует логику при `GPIO_ACTIVE_LOW`. Вызов `gpiod_set_value(gpio, 1)` означает «assert» — то есть реально выставит 0 на пине если `GPIO_ACTIVE_LOW`. Это делает код независимым от полярности.

### 6.2 GPIO IRQ

```c
static int mysensor_probe(struct i2c_client *client)
{
    struct gpio_desc *irq_gpio;
    int irq;

    irq_gpio = devm_gpiod_get(&client->dev, "irq", GPIOD_IN);
    if (IS_ERR(irq_gpio))
        return PTR_ERR(irq_gpio);

    /* Получить IRQ номер от GPIO дескриптора */
    irq = gpiod_to_irq(irq_gpio);
    if (irq < 0)
        return irq;

    return devm_request_threaded_irq(&client->dev, irq,
                                     NULL, mysensor_irq_thread,
                                     IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                                     "mysensor", priv);
}
```

---

## 7. regmap — абстракция над I2C/SPI/MMIO регистрами

### 7.1 Зачем regmap

regmap — уровень абстракции для чтения/записи регистров чипа. Преимущества:
- **Кэширование**: уменьшает количество I2C/SPI транзакций для регистров только для чтения или редко меняющихся
- **Отладка**: автоматически создаёт debugfs интерфейс для чтения/записи регистров
- **Переносимость**: один и тот же код работы с регистрами — для I2C, SPI и MMIO версий одного чипа
- **Валидация**: `max_register`, `writeable_reg`, `volatile_reg` — защита от ошибок

### 7.2 regmap для I2C

```c
#include <linux/regmap.h>

/* Функции для описания поведения регистров */
static bool mysensor_writeable_reg(struct device *dev, unsigned int reg)
{
    /* Регистры только для чтения */
    return reg != 0x00 && reg != 0x0F;  /* WHO_AM_I и STATUS — read-only */
}

static bool mysensor_volatile_reg(struct device *dev, unsigned int reg)
{
    /* Регистры, которые аппаратура может менять сама — не кэшируем */
    return reg >= 0x10 && reg <= 0x1F;  /* данные измерений */
}

static const struct regmap_config mysensor_regmap_config = {
    .reg_bits      = 8,       /* ширина адреса регистра */
    .val_bits      = 8,       /* ширина значения */
    .max_register  = 0xFF,    /* максимальный адрес */
    .writeable_reg = mysensor_writeable_reg,
    .volatile_reg  = mysensor_volatile_reg,
    .cache_type    = REGCACHE_RBTREE,  /* кэшировать non-volatile регистры */
};

static int mysensor_probe(struct i2c_client *client)
{
    struct regmap *regmap;
    unsigned int val;
    int ret;

    regmap = devm_regmap_init_i2c(client, &mysensor_regmap_config);
    if (IS_ERR(regmap))
        return PTR_ERR(regmap);

    /* Чтение */
    ret = regmap_read(regmap, 0x00, &val);
    if (ret)
        return ret;
    dev_info(&client->dev, "WHO_AM_I = 0x%02x\n", val);

    /* Запись */
    ret = regmap_write(regmap, 0x01, 0x01);
    if (ret)
        return ret;

    /* Изменить только отдельные биты: маска + значение */
    /* регистр 0x02, маска 0x30 (биты [5:4]), значение 0x10 (scale=1) */
    ret = regmap_update_bits(regmap, 0x02, 0x30, 0x10);
    if (ret)
        return ret;

    /* Чтение блока (несколько регистров подряд) */
    u8 data[4];
    ret = regmap_bulk_read(regmap, 0x10, data, sizeof(data));
    if (ret)
        return ret;

    return 0;
}
```

### 7.3 regmap для SPI

```c
static const struct regmap_config myflash_regmap_config = {
    .reg_bits  = 8,
    .val_bits  = 8,
    .max_register = 0xFF,
};

static int myflash_probe(struct spi_device *spi)
{
    struct regmap *regmap;

    /* Единственное отличие от I2C — инициализатор */
    regmap = devm_regmap_init_spi(spi, &myflash_regmap_config);
    if (IS_ERR(regmap))
        return PTR_ERR(regmap);

    /* Далее тот же regmap API */
    /* ... */
    return 0;
}
```

### 7.4 regmap debugfs

Автоматически создаётся при наличии debugfs:
```bash
# Посмотреть все регистры
cat /sys/kernel/debug/regmap/3-0028/registers

# Записать регистр
echo "01 01" > /sys/kernel/debug/regmap/3-0028/registers
```

---

## 8. Отладка I2C/SPI проблем

### 8.1 Диагностика через dmesg и sysfs

```bash
# Kernel log — первое что смотришь
dmesg | grep -i i2c
dmesg | grep -i spi
dmesg | grep -i mysensor

# Все зарегистрированные I2C устройства
ls /sys/bus/i2c/devices/
# 3-0028 = I2C шина 3, адрес 0x28

# Имя привязанного драйвера
cat /sys/bus/i2c/devices/3-0028/name
# или
cat /sys/bus/i2c/devices/3-0028/modalias

# Привязан ли драйвер?
ls /sys/bus/i2c/devices/3-0028/driver

# Принудительный bind/unbind для тестирования
echo "3-0028" > /sys/bus/i2c/drivers/mysensor/unbind
echo "3-0028" > /sys/bus/i2c/drivers/mysensor/bind
```

### 8.2 Dynamic Debug

```bash
# Включить dev_dbg() для конкретного модуля
echo "module mysensor +p" > /sys/kernel/debug/dynamic_debug/control

# Включить для конкретного файла
echo "file drivers/sensors/mysensor.c +p" > /sys/kernel/debug/dynamic_debug/control

# Выключить
echo "module mysensor -p" > /sys/kernel/debug/dynamic_debug/control

# Просмотреть активные правила
cat /sys/kernel/debug/dynamic_debug/control | grep mysensor
```

Для `dev_dbg` в коде:
```c
dev_dbg(&client->dev, "read reg 0x%02x = 0x%02x\n", reg, val);
```

### 8.3 ftrace для I2C транзакций

```bash
# Трассировать все I2C события
echo 1 > /sys/kernel/debug/tracing/events/i2c/enable
echo 1 > /sys/kernel/debug/tracing/tracing_on

# Выполнить операцию

echo 0 > /sys/kernel/debug/tracing/tracing_on
cat /sys/kernel/debug/tracing/trace | head -30

# Пример вывода:
# i2cdetect-1234  [001] .... 123.456: i2c_write: i2c-3 #0 a=028 f=0000 l=1 [01]
# i2cdetect-1234  [001] .... 123.457: i2c_read:  i2c-3 #0 a=028 f=0001 l=1
# i2cdetect-1234  [001] .... 123.458: i2c_reply: i2c-3 #0 a=028 f=0001 l=1 [42]
# i2cdetect-1234  [001] .... 123.459: i2c_result: i2c-3 #0 n=2 ret=2

# Фильтровать только нашу шину
echo "bus_num==3" > /sys/kernel/debug/tracing/events/i2c/i2c_write/filter
```

### 8.4 Частые проблемы и их причины

| Симптом | Вероятная причина | Диагностика |
|---------|-------------------|-------------|
| `probe` не вызывается | `compatible` не совпал, DTS не применился | `dmesg | grep of_platform`, проверь DTB |
| `ENODEV (-19)` в `probe` | Неверный chip ID или адрес | i2cget/i2cdetect перед написанием драйвера |
| `ENXIO (-6)` при I2C | Устройство не отвечает на ACK | i2cdetect, проверь pull-up резисторы, питание |
| `EBUSY` при request_irq | IRQ уже занято | `cat /proc/interrupts | grep <irq>` |
| `EPROBE_DEFER (-517)` | Зависимость (clock, регулятор) ещё не готова | Нормально, probe повторится — не ошибка |
| Мусор в данных SPI | Неверный Mode (CPOL/CPHA) | Осциллограф, проверь даташит |
| Kernel panic в probe | NULL разыменование, не проверен PTR_ERR | KASAN, добавь проверки IS_ERR |
| Утечка после rmmod | Не все ресурсы с devm_* | kmemleak, или добавь devm_* |

### 8.5 KASAN — обнаружение ошибок памяти в ядре

```bash
# Если ядро собрано с CONFIG_KASAN:
# При разыменовании NULL или out-of-bounds — KASAN BUG в dmesg
dmesg | grep -A 30 "KASAN"

# Пример:
# BUG: KASAN: null-ptr-deref in mysensor_probe+0x48/0x120 [mysensor]
# Read of size 4 at addr 0000000000000010 by task kworker/0:1/42
```

---

## 9. Практика: полный пример — термометр LM75 поверх I2C

LM75 — простой I2C термометр. Хорошая учебная цель: один 16-бит регистр температуры.

```c
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/of.h>

/* LM75 регистры */
#define LM75_REG_TEMP   0x00
#define LM75_REG_CONF   0x01
#define LM75_REG_THYST  0x02
#define LM75_REG_TOS    0x03

struct lm75_priv {
    struct i2c_client *client;
    struct device     *hwmon_dev;
};

/* Читать температуру. LM75: 16-бит, big-endian, 9-бит разрешение.
   Биты [15:7]: целая + дробная часть в 0.5°C единицах.
   Дробная: бит 7 = 0.5°C.
   Возвращает миллиградусы Цельсия. */
static int lm75_read_temp(struct i2c_client *client, long *temp)
{
    s16 raw;
    s32 ret;

    ret = i2c_smbus_read_word_data(client, LM75_REG_TEMP);
    if (ret < 0)
        return ret;

    /* i2c_smbus_read_word_data возвращает little-endian на хосте,
       но LM75 отдаёт big-endian — нужен swap */
    raw = (s16)be16_to_cpu((u16)ret);

    /* Только старшие 9 бит значимы (сдвиг вправо на 7) */
    *temp = (raw >> 7) * 500;  /* 500 мградусов = 0.5°C */
    return 0;
}

/* hwmon read callback */
static int lm75_read(struct device *dev, enum hwmon_sensor_types type,
                     u32 attr, int channel, long *val)
{
    struct lm75_priv *priv = dev_get_drvdata(dev);

    if (type != hwmon_temp)
        return -EOPNOTSUPP;
    if (attr != hwmon_temp_input)
        return -EOPNOTSUPP;

    return lm75_read_temp(priv->client, val);
}

static umode_t lm75_is_visible(const void *data,
                               enum hwmon_sensor_types type,
                               u32 attr, int channel)
{
    if (type == hwmon_temp && attr == hwmon_temp_input)
        return 0444;
    return 0;
}

static const struct hwmon_channel_info *lm75_info[] = {
    HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
    NULL
};

static const struct hwmon_ops lm75_hwmon_ops = {
    .is_visible = lm75_is_visible,
    .read       = lm75_read,
};

static const struct hwmon_chip_info lm75_chip_info = {
    .ops  = &lm75_hwmon_ops,
    .info = lm75_info,
};

static int lm75_probe(struct i2c_client *client)
{
    struct lm75_priv *priv;
    long temp;
    int ret;

    priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;
    priv->client = client;
    i2c_set_clientdata(client, priv);

    /* Проверить связь */
    ret = lm75_read_temp(client, &temp);
    if (ret)
        return dev_err_probe(&client->dev, ret, "failed to read temp\n");
    dev_info(&client->dev, "initial temp: %ld mC\n", temp);

    /* Зарегистрировать в hwmon подсистеме */
    priv->hwmon_dev = devm_hwmon_device_register_with_info(
                            &client->dev, "lm75", priv,
                            &lm75_chip_info, NULL);
    if (IS_ERR(priv->hwmon_dev))
        return PTR_ERR(priv->hwmon_dev);

    return 0;
}

static const struct of_device_id lm75_of_match[] = {
    { .compatible = "national,lm75" },
    { .compatible = "ti,tmp75" },
    { }
};
MODULE_DEVICE_TABLE(of, lm75_of_match);

static const struct i2c_device_id lm75_id[] = {
    { "lm75", 0 },
    { "tmp75", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, lm75_id);

static struct i2c_driver lm75_driver = {
    .probe    = lm75_probe,
    .id_table = lm75_id,
    .driver   = {
        .name           = "lm75",
        .of_match_table = lm75_of_match,
    },
};
module_i2c_driver(lm75_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LM75 I2C thermometer driver");
```

После загрузки модуля:
```bash
# Температура через hwmon
cat /sys/class/hwmon/hwmon0/temp1_input   # значение в мградусах
# 25000 = 25.000°C

# Через sensors (если установлен lm-sensors)
sensors
```

---

## 10. Самопроверка

Ответь без подсматривания. Если затрудняешься — вернись к соответствующему разделу.

**1. Что такое devm_* и почему они предпочтительны?**

devm_* («device-managed») функции автоматически освобождают захваченный ресурс когда device unbind'ится (при remove или при ошибке в probe). Устраняют необходимость вручную откатывать ресурсы при ошибке в probe и в remove. Ключевое преимущество: при ошибке в середине probe все ранее захваченные devm_* ресурсы освобождаются автоматически — просто верни отрицательный errno.

**2. Как происходит matching device-driver в Linux device model?**

Ядро проходит три механизма в порядке приоритета: (1) DT matching по `compatible` строке через `of_match_table`; (2) `id_table` matching по имени устройства; (3) ACPI matching. При совпадении вызывается `probe()`.

**3. В чём разница i2c_smbus_read_byte_data и i2c_transfer?**

`i2c_smbus_read_byte_data` — стандартизированная SMBus транзакция: write регистра, repeated start, read 1 байт. Проще, надёжнее, поддерживается всеми I2C контроллерами. `i2c_transfer` — raw I2C: массив сообщений с произвольными флагами, для нестандартных транзакций, блоков > 32 байт, 10-бит адресации.

**4. Что такое CPOL и CPHA и как выбрать правильный SPI mode?**

CPOL: уровень SCK в idle (0=Low, 1=High). CPHA: момент сэмплирования (0=first edge, 1=second edge). Mode = CPOL*2 + CPHA. Правильный mode — из даташита устройства. Неправильный mode: данные принимаются в неправильный момент → мусор без явной ошибки.

**5. Как regmap упрощает разработку драйверов?**

Даёт единый API (regmap_read/write/update_bits) поверх I2C, SPI или MMIO. Автоматическое кэширование non-volatile регистров уменьшает число шинных транзакций. debugfs интерфейс для отладки без изменения кода. Переносимость: один и тот же код для I2C и SPI версий чипа.

**6. Зачем нужен MODULE_DEVICE_TABLE и что он делает?**

Генерирует таблицу alias'ов в `.ko` файле (секция `__mod_*__device_table`). udev читает эту таблицу при появлении устройства и вызывает `modprobe` для загрузки нужного модуля автоматически. Без него модуль нужно загружать вручную.

**7. Как получить GPIO из DT в новом gpiod API?**

`devm_gpiod_get(&dev, "name", GPIOD_OUT_LOW)` — где "name" это префикс имени в DTS (для "reset-gpios" → "reset"). Возвращает `struct gpio_desc *`. Управление через `gpiod_set_value()` / `gpiod_get_value()`. API автоматически обрабатывает полярность (`GPIO_ACTIVE_LOW`).

**8. Что такое platform device и чем он отличается от I2C/SPI device?**

Platform device — устройство без интеллектуальной шины: MMIO-регистры на фиксированных адресах, прерывания через GIC. Описывается в DTS через `reg = <addr size>`. I2C/SPI device — устройство на настоящей шине с протоколом (адрес, транзакции). Большинство SoC-периферии (UART, I2C контроллер, SPI контроллер) — platform devices.

**9. Почему dev_err_probe предпочтительна вместо dev_err + return?**

`dev_err_probe` подавляет вывод ошибки при `-EPROBE_DEFER` (ресурс ещё не готов, probe будет повторён — это нормальная ситуация, не ошибка). Обычный `dev_err` засорял бы dmesg ложными сообщениями об ошибке при каждой отсроченной попытке.

**10. Как с помощью i2cdetect убедиться что устройство физически есть на шине?**

`i2cdetect -y <bus_num>` — сканирует все адреса 0x03–0x77, отправляя probe транзакцию и ожидая ACK. Если устройство отвечает ACK — его адрес появится в таблице. Предупреждение: некоторые устройства реагируют на probe нежелательно — для таких используй `-r` (read mode) или `-q` (quick mode).
