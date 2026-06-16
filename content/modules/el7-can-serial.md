# Модуль EL7 — CAN и последовательные интерфейсы

## 0. Карта модуля

**Время:** 10–15 часов

**Зачем:** В промышленных и automotive embedded системах CAN — основная шина для управляющих сообщений. RS-485 — стандарт для ModBus RTU, PROFIBUS, промышленных датчиков. Понимание обоих протоколов критично для embedded Linux инженера на RK3588 — именно через CAN и RS-485 SoC взаимодействует с внешней электроникой: инверторами, ПЛК, датчиками.

**Главные инструменты:** candump, cansend, can-utils, ip link, stty, minicom, libmodbus

**Ресурсы:**
- `Documentation/networking/can.rst` — SocketCAN kernel документация
- `man 3 termios` — POSIX terminal API
- libmodbus.org — документация и примеры
- CiA 301 — CANopen standard (открытый доступ)

---

## 1. CAN Bus — физика и протокол

### 1.1 Электрические характеристики

CAN (Controller Area Network) — дифференциальная двухпроводная шина. Два провода: **CANH** и **CANL**. Разность напряжений CANH−CANL кодирует биты:

| Состояние | CANH | CANL | Разность | Значение бита |
|-----------|------|------|----------|---------------|
| Dominant  | 3.5В | 1.5В | +2В      | 0             |
| Recessive | 2.5В | 2.5В | 0В       | 1             |

**Dominant (0) перекрывает Recessive (1)** — это основа арбитража.

Требования к шине:
- Терминаторы 120 Ом на каждом конце (иначе отражения сигнала)
- Трансивер выдерживает ±12В (устойчивость к синфазным помехам)
- Экранирование кабеля для снижения EMI

### 1.2 Скорости и длины кабеля

| Скорость | Максимальная длина |
|----------|-------------------|
| 10 Kbit/s | 6700 м |
| 50 Kbit/s | 1000 м |
| 125 Kbit/s | 500 м |
| 250 Kbit/s | 250 м |
| 500 Kbit/s | 100 м |
| 1 Mbit/s | 40 м |

Длина × скорость ≈ const. Физическое ограничение — время распространения сигнала должно быть меньше половины битового интервала.

### 1.3 Формат CAN фрейма

**Standard CAN 2.0A (11-bit ID):**
```
SOF(1) | ID(11) | RTR(1) | IDE(1) | r0(1) | DLC(4) | DATA(0-64бит) | CRC(15) | CRC_DEL | ACK | ACK_DEL | EOF(7) | IFS(3)
```

**Extended CAN 2.0B (29-bit ID):**
```
SOF | ID_A(11) | SRR | IDE | ID_B(18) | RTR | r1 | r0 | DLC(4) | DATA | CRC | ACK | EOF | IFS
```

Поля:
- **DLC** (Data Length Code): 0–8 байт данных
- **RTR** (Remote Transmission Request): запрос данных без payload
- **IDE**: 0 = Standard frame, 1 = Extended frame
- **ACK**: все получатели ставят dominant — подтверждение приёма

Типы фреймов:
- **Data frame** — несёт данные (самый частый тип)
- **Remote frame (RTR)** — запрос данных у другого узла
- **Error frame** — передаётся при обнаружении ошибки всеми узлами
- **Overload frame** — запрос паузы между фреймами

### 1.4 Механизм арбитража

Все узлы начинают передачу одновременно. Каждый узел одновременно пишет бит и читает шину:

1. Узел A передаёт **recessive (1)**, видит на шине **dominant (0)** → другой узел передаёт 0 → узел A **проиграл арбитраж**, замолкает
2. Узел с **меньшим ID** = больше нулей = с большей вероятностью побеждает = **высший приоритет**
3. Проигравшие узлы ждут конца фрейма и повторяют попытку

Это **non-destructive arbitration** — данные победителя остаются валидными.

### 1.5 Обнаружение ошибок

CAN имеет пять механизмов обнаружения ошибок:
1. **Bit monitoring** — передатчик читает свой же бит
2. **Bit stuffing** — после 5 одинаковых бит вставляется инверсный (синхронизация)
3. **CRC** — 15-бит CRC над полем ID + data
4. **Frame check** — фиксированные форматные биты (SOF, EOF, delimiters)
5. **ACK check** — передатчик проверяет что кто-то подтвердил приём

**Error passive / Bus-off:**
- Счётчик ошибок TEC/REC: ошибка передачи → +8, успех → −1
- TEC > 127: узел переходит в Error Passive (передаёт passive error frames)
- TEC > 255: Bus-Off — узел отключается от шины

---

## 2. SocketCAN — CAN как сетевой интерфейс Linux

### 2.1 Архитектура SocketCAN

Linux представляет CAN как обычный сетевой интерфейс. Это позволяет использовать стандартный socket API:

```
Приложение
    ↕  socket(PF_CAN, SOCK_RAW, CAN_RAW)
SocketCAN (drivers/net/can/dev/)
    ↕
CAN controller driver (e.g., drivers/net/can/rockchip_canfd.c)
    ↕
Физическая CAN шина
```

Преимущества подхода:
- Стандартный POSIX API: socket, bind, read, write, select
- Множество приложений могут читать шину одновременно
- can-utils работают с любым CAN интерфейсом
- Интеграция с ip link, ip route, ethtool

### 2.2 Настройка интерфейса

```bash
# Загрузить модули (обычно встроены в ядро)
modprobe can
modprobe can_raw
modprobe can_dev

# Настроить скорость и поднять интерфейс
ip link set can0 type can bitrate 500000
ip link set can0 up

# Проверить статус
ip -d link show can0
# Вывод:
# can0: <NOARP,UP,LOWER_UP,ECHO> mtu 16 ...
#     link/can
#     can state ERROR-ACTIVE (berr-counter tx 0 rx 0) restart-ms 0
#     bitrate 500000 sample-point 0.875
#     tq 125 prop-seg 6 phase-seg1 7 phase-seg2 2 sjw 1

# CAN FD (Flexible Data-rate)
ip link set can0 type can bitrate 500000 dbitrate 2000000 fd on
ip link set can0 up

# Отключить интерфейс
ip link set can0 down

# Автоматический перезапуск при bus-off (в мс)
ip link set can0 type can restart-ms 100
```

### 2.3 can-utils — инструменты командной строки

```bash
# Установка
apt install can-utils

# Мониторинг всех фреймов в реальном времени
candump can0
# Пример вывода:
#  (1234567890.123456) can0  123   [8]  DE AD BE EF 00 01 02 03
#  (1234567890.124000) can0  456   [4]  11 22 33 44

# Мониторинг с временными метками
candump -t a can0   # абсолютное время
candump -t d can0   # дельта от предыдущего фрейма

# Отправить фрейм
cansend can0 123#DEADBEEF           # SFF, ID=0x123, 4 байта данных
cansend can0 18FF0102#0102030405060708  # EFF (>11bit → extended)
cansend can0 7FF#                   # пустой фрейм (dlc=0)

# Запись лога в файл
candump -l can0   # создаёт canlog-YYYYMMDD-HHMMSS.log

# Воспроизведение лога
canplayer -I canlog-20240101-120000.log

# Генерация случайных фреймов (нагрузочное тестирование)
cangen can0 -D i -L 8 -I r -g 0

# Статистика шины
canstatistics can0
ip -s link show can0
```

### 2.4 candump формат

```
(timestamp) interface  ID   [dlc]  data...
```

- `123` — SFF (11-bit), hex
- `18FF0102` — EFF (29-bit), hex  
- `[4]` — dlc
- `DE AD BE EF` — hex байты данных через пробел

---

## 3. SocketCAN API в C

### 3.1 Открытие и привязка сокета

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>

int can_socket_open(const char *ifname)
{
    int fd;
    struct sockaddr_can addr;
    struct ifreq ifr;

    /* Создать RAW CAN сокет */
    fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        perror("socket(PF_CAN)");
        return -1;
    }

    /* Получить индекс интерфейса по имени */
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl SIOCGIFINDEX");
        close(fd);
        return -1;
    }

    /* Привязать сокет к интерфейсу */
    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    return fd;
}
```

### 3.2 Отправка фреймов

```c
/* Отправить стандартный фрейм (11-bit ID) */
int can_send(int fd, uint32_t id, const uint8_t *data, uint8_t dlc)
{
    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id  = id & CAN_SFF_MASK;
    frame.can_dlc = dlc;
    if (dlc > 0 && data)
        memcpy(frame.data, data, dlc);

    ssize_t n = write(fd, &frame, sizeof(frame));
    return (n == (ssize_t)sizeof(frame)) ? 0 : -1;
}

/* Отправить расширенный фрейм (29-bit ID) */
int can_send_ext(int fd, uint32_t id, const uint8_t *data, uint8_t dlc)
{
    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id  = (id & CAN_EFF_MASK) | CAN_EFF_FLAG;
    frame.can_dlc = dlc;
    if (dlc > 0 && data)
        memcpy(frame.data, data, dlc);

    ssize_t n = write(fd, &frame, sizeof(frame));
    return (n == (ssize_t)sizeof(frame)) ? 0 : -1;
}
```

### 3.3 Приём фреймов

```c
/* Блокирующий приём */
int can_recv(int fd, struct can_frame *frame)
{
    ssize_t n = read(fd, frame, sizeof(*frame));
    return (n == (ssize_t)sizeof(*frame)) ? 0 : -1;
}

/* Приём с таймаутом */
int can_recv_timeout(int fd, struct can_frame *frame, int timeout_ms)
{
    fd_set rfds;
    struct timeval tv;

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (ret < 0)  return -1;    /* ошибка */
    if (ret == 0) return -2;    /* таймаут */
    return can_recv(fd, frame);
}
```

### 3.4 Фильтрация фреймов

SocketCAN фильтр: принять фрейм если `(frame.can_id & mask) == (filter_id & mask)`

```c
/* Один фильтр */
int can_set_filter(int fd, uint32_t filter_id, uint32_t mask)
{
    struct can_filter flt = {
        .can_id   = filter_id,
        .can_mask = mask,
    };
    return setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FILTER, &flt, sizeof(flt));
}

/* Несколько фильтров */
void example_filters(int fd)
{
    struct can_filter filters[] = {
        /* Принять только ID 0x100 */
        { .can_id = 0x100u, .can_mask = CAN_SFF_MASK },

        /* Принять диапазон 0x200–0x2FF */
        { .can_id = 0x200u, .can_mask = 0x700u },

        /* Принять все EFF фреймы (29-bit) */
        { .can_id = CAN_EFF_FLAG, .can_mask = CAN_EFF_FLAG },
    };
    setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FILTER,
               filters, sizeof(filters));
}

/* Отключить фильтрацию (принимать всё) */
int can_recv_all(int fd)
{
    return setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0);
}
```

### 3.5 Обработка error frames

```c
/* Включить приём error frames */
can_err_mask_t err_mask = CAN_ERR_TX_TIMEOUT | CAN_ERR_BUSOFF |
                           CAN_ERR_CRTL | CAN_ERR_PROT;
setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask));

/* В цикле приёма: */
struct can_frame frame;
can_recv(fd, &frame);
if (frame.can_id & CAN_ERR_FLAG) {
    /* Это error frame, не данные */
    if (frame.can_id & CAN_ERR_BUSOFF)
        printf("Bus-Off!\n");
    if (frame.can_id & CAN_ERR_CRTL)
        printf("Controller error: 0x%02x\n", frame.data[1]);
}
```

---

## 4. CAN FD

CAN FD (Flexible Data-rate) — расширение протокола:
- **Payload:** до 64 байт (vs 8 у классического CAN)
- **Скорость данных:** до 8 Mbit/s в фазе данных (BRS бит включает ускорение)
- **Фаза арбитража:** стандартная скорость (≤1 Mbit/s) — совместимость с CAN 2.0

### 4.1 Настройка

```bash
ip link set can0 type can bitrate 500000 dbitrate 2000000 fd on
ip link set can0 up
```

### 4.2 API в C

```c
#include <linux/can/fd.h>

/* Включить CAN FD на сокете */
int enable_fd = 1;
setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_fd, sizeof(enable_fd));

/* CAN FD фрейм */
struct canfd_frame fdf;
memset(&fdf, 0, sizeof(fdf));
fdf.can_id = 0x123u;
fdf.len    = 64;        /* не can_dlc, а len! */
fdf.flags  = CANFD_BRS; /* bit rate switch */
memset(fdf.data, 0xAA, 64);

write(fd, &fdf, sizeof(fdf));
```

**Важно:** `sizeof(struct canfd_frame) != sizeof(struct can_frame)`. При чтении проверяй возвращённый `n`:
- `n == CAN_MTU (16)` → классический `struct can_frame`
- `n == CANFD_MTU (72)` → `struct canfd_frame`

---

## 5. RS-232 / RS-422 / RS-485

### 5.1 Сравнение стандартов

| Характеристика | RS-232 | RS-422 | RS-485 |
|----------------|--------|--------|--------|
| Тип сигнализации | несимметричная | дифференциальная | дифференциальная |
| Уровни сигнала | ±3…±15В | ±2…±6В | ±1.5…±6В |
| Топология | точка-точка | 1 Tx + до 10 Rx | шина до 32 узлов |
| Макс. скорость | 20 Kbps @ 15м | 10 Mbps @ 12м | 10 Mbps @ 12м |
| Макс. расстояние | 15 м | 1200 м | 1200 м |
| Дуплекс | Full | Full | Half (обычно) |
| Типичное применение | консоль, AT-команды | видеосигналы, промышленность | ModBus RTU, PROFIBUS |

**RS-232 разъёмы:** DB-9 (современный), DB-25 (устарел). Кроссовый кабель для соединения DTE-DTE.

**RS-485 физика:**
- Дифференциальная пара: A (+) и B (−)
- Терминаторы: 120 Ом на каждом конце шины
- Half-duplex: DE (Driver Enable) и RE (Receiver Enable) — пины управления направлением
- При передаче: DE=1 (enable), RE=0 (disable receiver)
- При приёме: DE=0, RE=1

### 5.2 UART в Linux: termios API

```c
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

int uart_open(const char *device, speed_t baudrate)
{
    int fd = open(device, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) return -1;

    struct termios tio;
    if (tcgetattr(fd, &tio) < 0) {
        close(fd);
        return -1;
    }

    /* Сырой режим: отключить line discipline, echo, canonical mode */
    cfmakeraw(&tio);

    /* Скорость */
    cfsetispeed(&tio, baudrate);
    cfsetospeed(&tio, baudrate);

    /* 8N1: 8 бит данных, нет чётности, 1 стоп-бит */
    tio.c_cflag &= (tcflag_t)~(PARENB | PARODD | CSTOPB | CSIZE);
    tio.c_cflag |= CS8 | CREAD | CLOCAL;

    /* Таймаут: VMIN=0, VTIME=10 → ждёт 1 сек или данные */
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 10;

    if (tcsetattr(fd, TCSANOW, &tio) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}
```

**Стандартные константы скоростей:**
`B9600`, `B19200`, `B38400`, `B57600`, `B115200`, `B230400`, `B460800`, `B921600`, `B1000000`, `B1500000`, `B3000000`

### 5.3 VMIN и VTIME — поведение read()

Четыре режима:

| VMIN | VTIME | Поведение read() |
|------|-------|-----------------|
| 0    | 0     | Немедленный возврат (неблокирующий режим, O_NONBLOCK аналог) |
| 0    | T > 0 | Ждать T×0.1с или данные, что наступит раньше |
| N > 0 | 0    | Блокирует пока не придёт N байт |
| N > 0 | T > 0 | Ждать первый байт; после него — T×0.1с между байтами или N байт |

### 5.4 RS-485 через ioctl

```c
#include <linux/serial.h>

struct serial_rs485 rs485 = {
    .flags = SER_RS485_ENABLED |
             SER_RS485_RTS_ON_SEND,   /* RTS high при передаче */
    .delay_rts_before_send = 0,       /* мкс до включения */
    .delay_rts_after_send  = 0,       /* мкс после выключения */
};

if (ioctl(fd, TIOCSRS485, &rs485) < 0)
    perror("TIOCSRS485");
```

**Предпочтительный способ — через DTS:**

```dts
&uart3 {
    status = "okay";
    pinctrl-0 = <&uart3m1_xfer>;
    pinctrl-names = "default";

    /* RS-485 half-duplex */
    linux,rs485-enabled-at-boot-time;
    rs485-rts-delay-rts-before-send = <0>;
    rs485-rts-delay-rts-after-send  = <0>;
    rts-gpio = <&gpio3 RK_PA2 GPIO_ACTIVE_HIGH>;
};
```

UART драйвер автоматически управляет GPIO при каждой передаче — не нужен userspace ioctl.

### 5.5 Нестандартные скорости (> B3000000)

```c
#include <asm/termios.h>

struct termios2 tio2;
ioctl(fd, TCGETS2, &tio2);
tio2.c_cflag &= ~CBAUD;
tio2.c_cflag |= BOTHER;    /* BOTHER = нестандартная скорость */
tio2.c_ispeed = 1500000;   /* 1.5 Mbaud — debug UART RK3588 */
tio2.c_ospeed = 1500000;
ioctl(fd, TCSETS2, &tio2);
```

---

## 6. ModBus RTU через RS-485

ModBus RTU — самый распространённый промышленный протокол связи. Работает поверх RS-485 (реже RS-232). Архитектура: один Master + до 247 Slave устройств.

### 6.1 Формат фрейма ModBus RTU

```
| Addr (1B) | FuncCode (1B) | Data (0-252B) | CRC16 (2B, little-endian) |
```

Межфреймовый разрыв: ≥ 3.5 символьных интервала (время передачи 3.5 × 11 / baudrate). На 9600: ≈4мс. Это единственный способ определить границу фрейма (нет STX/ETX).

### 6.2 Function Codes

| Код | Операция | Объект |
|-----|----------|--------|
| 0x01 | Read Coils | Дискретные выходы (1 бит) |
| 0x02 | Read Discrete Inputs | Дискретные входы |
| 0x03 | **Read Holding Registers** | Аналоговые выходы (16 бит) |
| 0x04 | Read Input Registers | Аналоговые входы |
| 0x05 | Write Single Coil | |
| 0x06 | Write Single Register | |
| 0x0F | Write Multiple Coils | |
| 0x10 | Write Multiple Registers | |

### 6.3 libmodbus

```c
#include <modbus/modbus.h>
#include <errno.h>
#include <string.h>

int main(void)
{
    /* Создать RTU контекст */
    modbus_t *ctx = modbus_new_rtu("/dev/ttyS0", 9600, 'N', 8, 1);
    if (!ctx) {
        fprintf(stderr, "modbus_new_rtu: %s\n", modbus_strerror(errno));
        return 1;
    }

    /* Адрес slave устройства (1-247) */
    modbus_set_slave(ctx, 1);

    /* Debug вывод транзакций (для разработки) */
    modbus_set_debug(ctx, 1);

    /* Подключиться (открыть UART порт) */
    if (modbus_connect(ctx) < 0) {
        fprintf(stderr, "connect: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return 1;
    }

    /* Читать 10 holding registers (FC 0x03) начиная с addr 0 */
    uint16_t regs[10];
    int rc = modbus_read_registers(ctx, 0, 10, regs);
    if (rc < 0) {
        fprintf(stderr, "read: %s\n", modbus_strerror(errno));
    } else {
        for (int i = 0; i < rc; i++)
            printf("reg[%d] = %u (0x%04X)\n", i, regs[i], regs[i]);
    }

    /* Записать одиночный регистр (FC 0x06) */
    if (modbus_write_register(ctx, 5, 1234) < 0)
        fprintf(stderr, "write: %s\n", modbus_strerror(errno));

    /* Записать несколько регистров (FC 0x10) */
    uint16_t wdata[3] = {100, 200, 300};
    modbus_write_registers(ctx, 10, 3, wdata);

    modbus_close(ctx);
    modbus_free(ctx);
    return 0;
}
```

Сборка: `gcc main.c -lmodbus -o modbus_client`

### 6.4 Протокол вручную (без libmodbus)

CRC16 ModBus:
```c
uint16_t modbus_crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001u;
            else
                crc >>= 1;
        }
    }
    return crc;
}
```

---

## 7. RK3588: CAN и UART

### 7.1 CAN на RK3588

RK3588 имеет 3 CAN FD контроллера (`rockchip,rk3588-canfd`):

```dts
/* arch/arm64/boot/dts/rockchip/rk3588s.dtsi */
can0: can@fea50000 {
    compatible = "rockchip,rk3588-canfd", "rockchip,rk3568-canfd";
    reg = <0x0 0xfea50000 0x0 0x1000>;
    interrupts = <GIC_SPI 341 IRQ_TYPE_LEVEL_HIGH>;
    clocks = <&cru CLK_CAN0>, <&cru PCLK_CAN0>;
    clock-names = "baudclk", "apb_pclk";
    resets = <&cru SRST_CAN0>, <&cru SRST_P_CAN0>;
    reset-names = "can", "can-apb";
    status = "disabled";
};
```

Включение на плате:
```dts
/* board DTS */
&can0 {
    status = "okay";
    assigned-clocks = <&cru CLK_CAN0>;
    assigned-clock-rates = <150000000>;
    pinctrl-0 = <&can0m0_pins>;
    pinctrl-names = "default";
};
```

### 7.2 UART на RK3588

10 UART контроллеров. UART2 — debug console (1500000 бод на большинстве плат):

```dts
&uart2 {
    status = "okay";
    pinctrl-0 = <&uart2m0_xfer>;
    pinctrl-names = "default";
    /* 1.5Mbaud debug console */
};

/* RS-485 на UART3 */
&uart3 {
    status = "okay";
    pinctrl-0 = <&uart3m1_xfer>;
    pinctrl-names = "default";
    linux,rs485-enabled-at-boot-time;
    rts-gpio = <&gpio3 RK_PA2 GPIO_ACTIVE_HIGH>;
};
```

---

## 8. Отладка CAN и UART

```bash
# CAN: статистика ошибок
ip -s link show can0
ip -d link show can0   # детальная информация о настройках

# CAN: мониторинг ошибок в реальном времени
candump -e can0        # включить error frames
# Пример: can0  20000004   [8]  00 04 00 00 00 00 60 00  ERRORFRAME
#          bit-error TX timeout controller-problem

# CAN: проверить что физически работает (loopback тест)
ip link set can0 type can loopback on
ip link set can0 up
cansend can0 123#DEADBEEF &
candump -n 1 can0      # принять 1 фрейм

# UART: кто держит порт
fuser /dev/ttyS0
fuser /dev/ttyUSB0

# UART: открыть minicom
minicom -D /dev/ttyS0 -b 115200

# UART: screen (альтернатива minicom)
screen /dev/ttyS0 115200

# UART: strace для отладки termios
strace -e trace=ioctl,read,write -p $(pidof myapp)

# UART: проверить настройки порта
stty -F /dev/ttyS0 -a

# UART: проверить rx/tx счётчики
cat /sys/class/tty/ttyS0/statistics  # если доступно
```

---

## 9. Самопроверка — 10 вопросов

<details>
<summary>1. Почему в CAN шине узел с меньшим ID имеет больший приоритет?</summary>

Потому что **dominant (0) перекрывает recessive (1)**. Узел читает свой же передаваемый бит. Если он передаёт recessive (1) но видит dominant (0) — другой узел передаёт 0. Так как 0 в ID = dominant, узел с меньшим числовым значением ID имеет больше нулей в начале → дольше остаётся на шине → побеждает в арбитраже.

</details>

<details>
<summary>2. Что такое bit stuffing и зачем он нужен?</summary>

После 5 подряд идущих одинаковых бит передатчик вставляет инверсный бит. Приёмник его удаляет. Цель — обеспечить достаточно часто меняющийся сигнал для синхронизации clock recovery приёмника. Без bit stuffing длинная последовательность одинаковых бит не давала бы приёмнику точку синхронизации, и его clock начинал бы «плыть». Максимальный overhead: 20% (чередующиеся 0101...).

</details>

<details>
<summary>3. Как SocketCAN представляет CAN интерфейс в Linux?</summary>

Как обычный сетевой интерфейс (`can0`, `can1`, ...). Настройка через `ip link set can0 type can bitrate 500000`. Работа через стандартный socket API: `socket(PF_CAN, SOCK_RAW, CAN_RAW)`, `bind()`, `read()`, `write()`. Несколько приложений могут одновременно читать шину через разные сокеты.

</details>

<details>
<summary>4. Чем struct canfd_frame отличается от struct can_frame?</summary>

`struct can_frame`: `can_dlc` (0-8), `data[8]`, размер = 16 байт (CAN_MTU). `struct canfd_frame`: `len` (0-64, не dlc!), `flags` (CANFD_BRS, CANFD_ESI), `data[64]`, размер = 72 байт (CANFD_MTU). При чтении через read() нужно проверять возвращённый n чтобы определить тип фрейма.

</details>

<details>
<summary>5. Почему для RS-485 half-duplex нужен сигнал DE (Driver Enable)?</summary>

В RS-485 half-duplex все узлы используют одну и ту же дифференциальную пару для передачи И приёма. Если два узла одновременно включат свои трансиверы — конфликт. DE=0 = передатчик отключён (high-impedance), только приём. DE=1 = передатчик активен. При передаче данных приёмник обычно отключают (RE=0), иначе узел получит собственные данные обратно.

</details>

<details>
<summary>6. Что делает cfmakeraw() при настройке termios?</summary>

Переводит порт в сырой режим, отключая все обработки вводовывода:
- `ICANON=0` — нет построчного режима (не ждём '\\n')
- `ECHO=0` — нет эха
- `ISIG=0` — Ctrl+C, Ctrl+Z не генерируют сигналы
- `IXON=0` — нет программного управления потоком (Ctrl+S/Ctrl+Q)
- `OPOST=0` — нет обработки вывода (преобразования \\n→\\r\\n)
- Необходим для бинарных протоколов.

</details>

<details>
<summary>7. Чем VMIN=1, VTIME=0 отличается от VMIN=0, VTIME=10 в termios?</summary>

`VMIN=1, VTIME=0`: read() блокирует пока не придёт хотя бы 1 байт. Нет таймаута.

`VMIN=0, VTIME=10`: read() ждёт до 1 секунды (10 × 0.1с). Если за это время пришли данные — возвращает их. Если нет — возвращает 0 (таймаут). Удобно для опроса устройства с гарантированным timeout.

</details>

<details>
<summary>8. Какой ModBus function code читает holding registers?</summary>

0x03 (Read Holding Registers). Запрос: `[addr][0x03][start_addr_H][start_addr_L][count_H][count_L][CRC_L][CRC_H]`. Ответ: `[addr][0x03][byte_count][data_H][data_L]...[CRC_L][CRC_H]`. Каждый регистр = 16 бит, big-endian.

</details>

<details>
<summary>9. Как CAN фильтр {.can_id=0x100, .can_mask=0x700} влияет на принимаемые фреймы?</summary>

Условие пропускания: `(frame.can_id & 0x700) == (0x100 & 0x700)` = `(frame.can_id & 0x700) == 0x100`. Это пропускает фреймы с ID в диапазоне 0x100–0x1FF (биты 8-10 = 001, биты 0-7 не проверяются). Маска 0x700 = биты [10:8].

</details>

<details>
<summary>10. Как проверить счётчики ошибок CAN интерфейса?</summary>

```bash
ip -d link show can0   # berr-counter tx N rx M
ip -s link show can0   # RX/TX статистика, errors
```

`berr-counter tx > 127` → Error Passive (узел ошибается при передаче). `berr-counter tx > 255` → Bus-Off (узел отключился от шины). Физически причины: неправильная скорость, нет терминаторов, обрыв кабеля, единственный узел на шине (нет ACK).

</details>

---

## 10. Банк вопросов

### БАЗА

1. Сколько проводов у CAN шины?
   - a) 1 (Single-Wire CAN) b) **2 (CANH и CANL)** c) 3 d) 4

2. Максимальный payload классического CAN фрейма?
   - a) 4 b) **8** c) 16 d) 64

3. Утилита для мониторинга CAN фреймов из пакета can-utils?
   - a) canmon b) **candump** c) canbuf d) canread

4. Какой RS стандарт поддерживает до 32 узлов на шине?
   - a) RS-232 b) RS-422 c) **RS-485** d) RS-530

5. Что делает cfmakeraw()?
   - a) Устанавливает 115200 бод b) **Переводит порт в сырой режим** c) Сбрасывает к умолчаниям d) Включает RTS/CTS

6. ModBus function code для чтения holding registers?
   - a) 0x01 b) 0x02 c) **0x03** d) 0x04

7. Структура CAN фрейма в SocketCAN?
   - a) struct can_packet b) **struct can_frame** c) struct socketcan_frame d) struct canfd_msg

8. Что такое CAN FD?
   - a) Full-Duplex CAN b) **До 64 байт payload и 8 Mbit/s в фазе данных** c) Финский стандарт d) Field Device CAN

### МЕХАНИЗМЫ (self_grade)

1. Механизм арбитража CAN — шаги при одновременной передаче двух узлов
2. Настроить SocketCAN 500Kbit/s + запустить мониторинг — команды
3. C программа принимающая только CAN фреймы 0x100–0x1FF
4. Реализовать RS-485 half-duplex через DTS без userspace ioctl
5. Что такое bit stuffing и какой у него overhead в худшем случае
6. Все 4 комбинации VMIN/VTIME в termios — поведение read()
7. libmodbus: прочитать 10 holding registers — полный код
8. Счётчики ошибок CAN — что такое Error Passive и Bus-Off

### ЭКСПЕРТ (self_grade)

1. Реализовать ModBus RTU без libmodbus: формирование фрейма и CRC16
2. Error frames в SocketCAN: типы и обработка через setsockopt
3. Синхронизация времени нескольких CAN узлов: CANopen SYNC и HW timestamping
4. CAN logger с разбором J1939 29-bit ID: PGN, SA, DA
5. Диагностика RS-485: данные с ошибками или не приходят — план действий
