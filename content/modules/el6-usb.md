# Модуль EL6 — USB в ядре Linux

> Этап 2C, Embedded Linux. USB — вездесущий интерфейс: внешние хранилища, последовательные адаптеры, сетевые карты, отладочные кабели. На RK3588 есть USB 3.0 host и USB 2.0 OTG. Понимание USB стека необходимо для написания host-драйвера, настройки gadget-режима и отладки USB проблем.

---

## 0. Карта модуля

| | |
|---|---|
| **Время** | 10–15 ч. ~4 ч теория, ~6 ч практика, ~2 ч самопроверка. |
| **Зачем** | USB gadget (RK3588 как USB-устройство для PC), host-драйверы для vendor-specific устройств, отладка через USB serial. |
| **Ресурсы** | `Documentation/usb/` в дереве ядра; LDD3 chapter 13; usb.org/developers/docs/; linux-usb.org |

**Точная карта тем:**

| Тема | Источник |
|------|----------|
| USB архитектура в Linux | `Documentation/usb/usb-help.rst` |
| Writing USB drivers | `Documentation/usb/writing_usb_driver.rst` |
| USB gadget API | `Documentation/usb/gadget_api.rst` |
| ConfigFS gadget | `Documentation/usb/gadget_configfs.rst` |
| URB API | `include/linux/usb.h` |

---

## 1. USB архитектура в Linux

### 1.1 Стек уровней

```
Пользовательское пространство
├── libusb (vendor tools, lsusb, usbmon)
└── /dev/bus/usb/, /dev/hidraw*, /dev/ttyUSB*
         ↕
USB subsystem (drivers/usb/core/)
├── USB hub driver (drivers/usb/core/hub.c)
├── USB core (usb.c, driver.c, message.c, urb.c)
└── USB device drivers (hid, storage, serial, cdc, ...)
         ↕
USB Host Controller Driver (HCD)
├── xHCI (USB 3.x) — drivers/usb/host/xhci*.c
├── eHCI (USB 2.0 High Speed) — drivers/usb/host/ehci*.c
└── oHCI/uHCI (USB 1.1) — для legacy
         ↕
USB Hardware (контроллер на SoC)
```

### 1.2 Скорости USB

| Стандарт | Скорость | HCD |
|----------|----------|-----|
| USB 1.0/1.1 Full Speed | 12 Mbps | OHCI/UHCI |
| USB 1.1 Low Speed | 1.5 Mbps | OHCI/UHCI |
| USB 2.0 High Speed | 480 Mbps | EHCI |
| USB 3.0 SuperSpeed | 5 Gbps | xHCI |
| USB 3.1 SuperSpeed+ | 10 Gbps | xHCI |
| USB 3.2 | 20 Gbps | xHCI |

xHCI поддерживает все скорости — современные SoC используют только xHCI.

### 1.3 Роли USB

**Host** — управляет шиной, инициирует транзакции. PC подключает периферию. RK3588 в режиме host.

**Device (Function/Gadget)** — отвечает на запросы host. USB флешка, клавиатура, мышь. RK3588 в gadget режиме (подключён к PC).

**OTG (On-The-Go)** — может переключаться между host и device. Определяется через ID пин на USB кабеле или через software (dual-role driver).

---

## 2. USB дескрипторы

Каждое USB устройство описывает себя через иерархию дескрипторов. Host запрашивает их через control endpoint (ep0) при подключении.

### 2.1 Иерархия дескрипторов

```
Device Descriptor (1 на устройство)
├── bDeviceClass, bDeviceSubClass, bDeviceProtocol
├── idVendor (VID), idProduct (PID)
├── bcdUSB (версия USB: 0x0200 = USB 2.0)
└── Configuration Descriptor (может быть несколько)
    ├── bNumInterfaces
    ├── bConfigurationValue
    └── Interface Descriptor (один или несколько)
        ├── bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol
        │   Классы: HID(0x03), CDC(0x02), Mass Storage(0x08), Vendor(0xFF)
        └── Endpoint Descriptor (один или несколько)
            ├── bEndpointAddress (номер + направление: IN/OUT)
            ├── bmAttributes (тип: Bulk, Interrupt, Isochronous)
            └── wMaxPacketSize
```

### 2.2 Типы передач (Endpoint types)

**Control** — конфигурирование устройства. Всегда существует ep0. Использует setup packets. Гарантированная доставка.

**Bulk** — большие данные: USB накопители, USB-Ethernet. Гарантированная доставка, нет гарантии задержки. Использует свободную полосу пропускания.

**Interrupt** — периодический опрос: HID (клавиатура, мышь, джойстик). Гарантированная максимальная задержка, но НЕ гарантия доставки (только повтор в рамках интервала опроса).

**Isochronous** — потоковые данные: USB аудио, USB камеры. Гарантированная полоса пропускания, нет повторов при ошибках. Потеря пакета лучше задержки.

### 2.3 Endpoint адреса

```
bEndpointAddress: бит 7 = направление (0=OUT к устройству, 1=IN от устройства)
                  биты [3:0] = номер endpoint

Примеры:
0x81 = EP1 IN  (от устройства к хосту)
0x02 = EP2 OUT (от хоста к устройству)
0x83 = EP3 IN
```

Вспомогательные макросы в ядре:
```c
usb_endpoint_is_bulk_in(ep)    /* Bulk IN */
usb_endpoint_is_bulk_out(ep)   /* Bulk OUT */
usb_endpoint_is_int_in(ep)     /* Interrupt IN */
usb_endpoint_xfer_bulk(ep)     /* тип Bulk */
usb_endpoint_dir_in(ep)        /* направление IN */
```

---

## 3. USB host driver

### 3.1 Базовая структура

USB host driver привязывается к USB интерфейсу (не к устройству целиком — одно устройство может иметь несколько интерфейсов с разными драйверами).

```c
#include <linux/usb.h>
#include <linux/module.h>
#include <linux/slab.h>

#define MY_VID 0x1234
#define MY_PID 0x5678

struct mydev_priv {
    struct usb_device     *udev;
    struct usb_interface  *intf;
    u8                     ep_bulk_in;
    u8                     ep_bulk_out;
    size_t                 bulk_in_size;
};

static const struct usb_device_id mydev_id_table[] = {
    { USB_DEVICE(MY_VID, MY_PID) },           /* точное совпадение VID+PID */
    { USB_DEVICE_AND_INTERFACE_INFO(          /* по классу интерфейса */
        MY_VID, MY_PID,
        USB_CLASS_COMM, 0, 0) },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(usb, mydev_id_table);

static int mydev_probe(struct usb_interface *intf,
                       const struct usb_device_id *id)
{
    struct usb_device *udev = interface_to_usbdev(intf);
    struct usb_host_interface *iface_desc = intf->cur_altsetting;
    struct usb_endpoint_descriptor *ep_in = NULL, *ep_out = NULL;
    struct mydev_priv *priv;
    int i;

    dev_info(&intf->dev,
             "probed: VID=%04x PID=%04x bus=%d addr=%d\n",
             le16_to_cpu(udev->descriptor.idVendor),
             le16_to_cpu(udev->descriptor.idProduct),
             udev->bus->busnum, udev->devnum);

    /* Найти нужные endpoints */
    for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
        struct usb_endpoint_descriptor *ep =
            &iface_desc->endpoint[i].desc;

        if (!ep_in && usb_endpoint_is_bulk_in(ep)) {
            ep_in = ep;
        }
        if (!ep_out && usb_endpoint_is_bulk_out(ep)) {
            ep_out = ep;
        }
    }

    if (!ep_in || !ep_out) {
        dev_err(&intf->dev, "could not find bulk endpoints\n");
        return -ENODEV;
    }

    /* Выделить private data */
    priv = devm_kzalloc(&intf->dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;

    priv->udev         = usb_get_dev(udev);  /* увеличить refcount */
    priv->intf         = intf;
    priv->ep_bulk_in   = ep_in->bEndpointAddress;
    priv->ep_bulk_out  = ep_out->bEndpointAddress;
    priv->bulk_in_size = usb_endpoint_maxp(ep_in);

    usb_set_intfdata(intf, priv);

    dev_info(&intf->dev, "bulk IN: 0x%02x (max %zu bytes), bulk OUT: 0x%02x\n",
             priv->ep_bulk_in, priv->bulk_in_size, priv->ep_bulk_out);
    return 0;
}

static void mydev_disconnect(struct usb_interface *intf)
{
    struct mydev_priv *priv = usb_get_intfdata(intf);

    usb_set_intfdata(intf, NULL);
    if (priv)
        usb_put_dev(priv->udev);

    dev_info(&intf->dev, "disconnected\n");
}

/* Поддержка suspend/resume (опционально) */
static int mydev_suspend(struct usb_interface *intf, pm_message_t message)
{
    return 0;
}

static int mydev_resume(struct usb_interface *intf)
{
    return 0;
}

static struct usb_driver mydev_driver = {
    .name       = "mydevice",
    .probe      = mydev_probe,
    .disconnect = mydev_disconnect,
    .suspend    = mydev_suspend,
    .resume     = mydev_resume,
    .id_table   = mydev_id_table,
    .supports_autosuspend = 1,
};
module_usb_driver(mydev_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Example USB host driver");
MODULE_AUTHOR("BSP Engineer");
```

### 3.2 Открытие для userspace через cdev

Обычно USB host driver регистрирует char device для доступа из userspace:

```c
#include <linux/cdev.h>
#include <linux/fs.h>

static dev_t mydev_devt;
static struct class *mydev_class;

static int mydev_open(struct inode *inode, struct file *file)
{
    struct mydev_priv *priv;
    /* Найти priv по minor номеру */
    priv = /* ... */;
    file->private_data = priv;
    return 0;
}

static ssize_t mydev_read(struct file *file, char __user *buf,
                          size_t count, loff_t *ppos)
{
    struct mydev_priv *priv = file->private_data;
    u8 *kbuf;
    int actual, ret;

    kbuf = kmalloc(count, GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;

    ret = usb_bulk_msg(priv->udev,
                       usb_rcvbulkpipe(priv->udev, priv->ep_bulk_in),
                       kbuf, (int)count, &actual,
                       5000 /* 5 sec timeout */);
    if (ret) {
        kfree(kbuf);
        return ret;
    }

    if (copy_to_user(buf, kbuf, (size_t)actual)) {
        kfree(kbuf);
        return -EFAULT;
    }

    kfree(kbuf);
    return actual;
}

static const struct file_operations mydev_fops = {
    .owner = THIS_MODULE,
    .open  = mydev_open,
    .read  = mydev_read,
};
```

---

## 4. URB — USB Request Block

URB — основной механизм передачи данных в USB стеке Linux.

### 4.1 Синхронные helper-функции

Для простых случаев — не надо работать с URB напрямую:

```c
/* Bulk передача */
int usb_bulk_msg(struct usb_device *usb_dev,
                 unsigned int pipe,
                 void *data,
                 int len,
                 int *actual_length,
                 int timeout);   /* миллисекунды, 0 = бесконечно */

/* pipe макросы: */
usb_rcvbulkpipe(udev, ep_addr)   /* Bulk IN */
usb_sndbulkpipe(udev, ep_addr)   /* Bulk OUT */
usb_rcvintpipe(udev, ep_addr)    /* Interrupt IN */
usb_sndctrlpipe(udev, 0)         /* Control OUT (ep0) */

/* Пример: читать из Bulk IN endpoint */
u8 buf[64];
int actual;
int ret = usb_bulk_msg(udev,
                       usb_rcvbulkpipe(udev, 0x81),  /* EP1 IN */
                       buf, sizeof(buf),
                       &actual,
                       5000);  /* 5 секунд */
if (ret) {
    dev_err(&intf->dev, "bulk read failed: %d\n", ret);
} else {
    dev_info(&intf->dev, "read %d bytes\n", actual);
}

/* Control transfer (например, vendor request) */
int usb_control_msg(struct usb_device *dev,
                    unsigned int pipe,
                    __u8 request,
                    __u8 requesttype,
                    __u16 value,
                    __u16 index,
                    void *data,
                    __u16 size,
                    int timeout);

/* Пример: vendor-specific control IN */
u8 response[4];
ret = usb_control_msg(udev,
                      usb_rcvctrlpipe(udev, 0),
                      0x01,               /* bRequest: vendor command */
                      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                      0x0000,             /* wValue */
                      0x0000,             /* wIndex */
                      response,
                      sizeof(response),
                      5000);
```

### 4.2 Асинхронные URB

Для потоковых данных и периодических передач — асинхронные URB:

```c
/* Структура для URB с буфером */
struct mydev_urb_context {
    struct mydev_priv *priv;
    struct urb        *urb;
    u8                *buf;
    dma_addr_t         dma;
};

static void mydev_bulk_callback(struct urb *urb)
{
    struct mydev_urb_context *ctx = urb->context;

    switch (urb->status) {
    case 0:  /* успех */
        /* обработать данные: ctx->buf[0..urb->actual_length-1] */
        dev_dbg(&ctx->priv->intf->dev,
                "received %d bytes\n", urb->actual_length);
        /* Переотправить URB для непрерывного приёма */
        usb_submit_urb(urb, GFP_ATOMIC);
        break;
    case -ECONNRESET:  /* URB отменён */
    case -ENOENT:
    case -ESHUTDOWN:
        /* Устройство отключается — не переотправлять */
        break;
    default:
        dev_err(&ctx->priv->intf->dev,
                "URB error: %d\n", urb->status);
        usb_submit_urb(urb, GFP_ATOMIC);
        break;
    }
}

static int mydev_start_bulk_read(struct mydev_priv *priv)
{
    struct mydev_urb_context *ctx;
    struct urb *urb;
    size_t buf_size = priv->bulk_in_size;

    ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx)
        return -ENOMEM;

    /* Выделить URB */
    urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!urb) {
        kfree(ctx);
        return -ENOMEM;
    }

    /* Выделить буфер с DMA-совместимым адресом */
    ctx->buf = usb_alloc_coherent(priv->udev, buf_size,
                                  GFP_KERNEL, &ctx->dma);
    if (!ctx->buf) {
        usb_free_urb(urb);
        kfree(ctx);
        return -ENOMEM;
    }

    ctx->priv = priv;
    ctx->urb  = urb;

    /* Заполнить URB */
    usb_fill_bulk_urb(urb,
                      priv->udev,
                      usb_rcvbulkpipe(priv->udev, priv->ep_bulk_in),
                      ctx->buf,
                      (int)buf_size,
                      mydev_bulk_callback,
                      ctx);
    urb->transfer_dma    = ctx->dma;
    urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    /* Отправить */
    return usb_submit_urb(urb, GFP_KERNEL);
}

static void mydev_stop_bulk_read(struct mydev_urb_context *ctx)
{
    usb_kill_urb(ctx->urb);   /* синхронно отменить и дождаться callback */
    usb_free_coherent(ctx->priv->udev, ctx->priv->bulk_in_size,
                      ctx->buf, ctx->dma);
    usb_free_urb(ctx->urb);
    kfree(ctx);
}
```

### 4.3 Interrupt URB

Для HID и подобных устройств:

```c
static void mydev_int_callback(struct urb *urb)
{
    /* аналогично bulk callback */
}

/* usb_fill_int_urb вместо usb_fill_bulk_urb */
usb_fill_int_urb(urb,
                 udev,
                 usb_rcvintpipe(udev, ep_addr),
                 buf,
                 buf_size,
                 mydev_int_callback,
                 ctx,
                 ep->bInterval);  /* интервал опроса из дескриптора */
```

---

## 5. USB Gadget — режим устройства

### 5.1 Концепция

Когда RK3588 подключается по USB к PC — он выступает как USB device (gadget). Ядро Linux поддерживает это через USB Gadget framework:

```
Приложение / gadget driver
         ↕
USB Gadget API (drivers/usb/gadget/)
├── Function drivers (f_acm.c, f_mass_storage.c, f_ecm.c, ...)
└── Composite gadget layer
         ↕
USB Device Controller (UDC) Driver
         ↕
USB Device Controller Hardware
```

**UDC** (USB Device Controller) — аппаратная часть на SoC, работающая в device режиме. На RK3588: `ff000000.usb` (USB2 OTG), `fc000000.usbdrd3` (USB3).

### 5.2 ConfigFS gadget — без написания кода

Современный способ — создать gadget через ConfigFS:

```bash
# Подключить ConfigFS (обычно уже смонтирован в /sys/kernel/config)
mount -t configfs none /sys/kernel/config

# Создать gadget
mkdir /sys/kernel/config/usb_gadget/mygadget
cd /sys/kernel/config/usb_gadget/mygadget

# Vendor/Product ID
echo 0x1d6b > idVendor   # Linux Foundation
echo 0x0104 > idProduct  # Multifunction Composite Gadget

# Строки описания
mkdir strings/0x409       # English
echo "My Company"   > strings/0x409/manufacturer
echo "My Device"    > strings/0x409/product
echo "12345678"     > strings/0x409/serialnumber

# Создать конфигурацию
mkdir configs/c.1
echo 120 > configs/c.1/MaxPower   # mA

# Добавить функцию: ACM (serial port)
mkdir functions/acm.GS0
ln -s functions/acm.GS0 configs/c.1/acm.GS0

# Привязать к UDC (имя из ls /sys/class/udc/)
echo "ff000000.usb" > UDC

# Теперь на PC появится /dev/ttyUSB0 или /dev/ttyACM0
```

Отключение:
```bash
echo "" > /sys/kernel/config/usb_gadget/mygadget/UDC
```

### 5.3 Составной gadget: Mass Storage + ACM

```bash
# Mass Storage функция (нужен файл-образ или блочное устройство)
mkdir functions/mass_storage.0
echo /dev/mmcblk0p1 > functions/mass_storage.0/lun.0/file
echo 1 > functions/mass_storage.0/lun.0/removable

# ACM serial
mkdir functions/acm.GS0

# Добавить обе функции в конфигурацию
ln -s functions/mass_storage.0 configs/c.1/
ln -s functions/acm.GS0 configs/c.1/

echo "ff000000.usb" > UDC
# PC видит: флешку + serial порт
```

### 5.4 g_ether — USB Ethernet gadget

Удобно для отладки по USB:

```bash
modprobe g_ether host_addr=00:11:22:33:44:55 dev_addr=00:11:22:33:44:56

# На PC появится сетевой интерфейс enp0s...
# На RK3588:
ip addr add 192.168.100.1/24 dev usb0
ip link set usb0 up
```

Или через ConfigFS:
```bash
mkdir functions/ecm.usb0  # ECM = Ethernet Control Model
echo "00:11:22:33:44:55" > functions/ecm.usb0/host_addr
ln -s functions/ecm.usb0 configs/c.1/
echo "ff000000.usb" > UDC
```

---

## 6. libusb для userspace

### 6.1 Когда использовать libusb вместо kernel driver

**libusb (userspace):**
- Нет жёстких RT требований
- Vendor-specific протоколы (HID, vendor class)
- Быстрое прототипирование
- Нет нужды в kernel API (блочные устройства, сетевые интерфейсы)
- Права пользователя: достаточно udev правила

**Kernel driver:**
- Нужна интеграция с kernel API (block device, network interface, input subsystem, V4L2, ALSA)
- Максимальная производительность (нет копирования через userspace)
- Gadget (device side) — только kernel
- RT требования

### 6.2 libusb-1.0 API

```c
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <string.h>

#define VID 0x1234
#define PID 0x5678

int main(void)
{
    libusb_context *ctx = NULL;
    libusb_device_handle *handle;
    unsigned char buf[64];
    int actual, ret;

    /* Инициализация */
    ret = libusb_init(&ctx);
    if (ret < 0) {
        fprintf(stderr, "libusb_init: %s\n", libusb_strerror(ret));
        return 1;
    }
    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);

    /* Открыть устройство по VID:PID */
    handle = libusb_open_device_with_vid_pid(ctx, VID, PID);
    if (!handle) {
        fprintf(stderr, "device %04x:%04x not found\n", VID, PID);
        libusb_exit(ctx);
        return 1;
    }

    /* Отключить kernel driver если он захватил интерфейс */
    if (libusb_kernel_driver_active(handle, 0) == 1) {
        ret = libusb_detach_kernel_driver(handle, 0);
        if (ret)
            fprintf(stderr, "detach kernel driver: %s\n", libusb_strerror(ret));
    }

    /* Захватить интерфейс */
    ret = libusb_claim_interface(handle, 0);
    if (ret) {
        fprintf(stderr, "claim interface: %s\n", libusb_strerror(ret));
        goto out_close;
    }

    /* Bulk передача IN (читать) */
    ret = libusb_bulk_transfer(handle,
                               0x81,       /* EP1 IN */
                               buf, sizeof(buf),
                               &actual,
                               5000);      /* 5 sec timeout */
    if (ret) {
        fprintf(stderr, "bulk read: %s\n", libusb_strerror(ret));
    } else {
        printf("read %d bytes\n", actual);
    }

    /* Control transfer */
    unsigned char ctrl_buf[4];
    ret = libusb_control_transfer(handle,
                                  LIBUSB_REQUEST_TYPE_VENDOR |
                                  LIBUSB_RECIPIENT_DEVICE |
                                  LIBUSB_ENDPOINT_IN,
                                  0x01,    /* bRequest */
                                  0x0000,  /* wValue */
                                  0x0000,  /* wIndex */
                                  ctrl_buf, sizeof(ctrl_buf),
                                  5000);

    libusb_release_interface(handle, 0);
out_close:
    libusb_close(handle);
    libusb_exit(ctx);
    return (ret < 0) ? 1 : 0;
}
```

Компиляция:
```bash
gcc -Wall -o myusb myusb.c $(pkg-config --cflags --libs libusb-1.0)
```

### 6.3 udev правило для libusb без root

```
# /etc/udev/rules.d/99-mydevice.rules
SUBSYSTEM=="usb", ATTRS{idVendor}=="1234", ATTRS{idProduct}=="5678", \
    MODE="0666", GROUP="plugdev"
```

---

## 7. Отладка USB проблем

### 7.1 lsusb — идентификация устройств

```bash
# Список всех устройств
lsusb

# Подробные дескрипторы
lsusb -v -d 1234:5678

# Дерево устройств (хаб → устройство)
lsusb -t

# Мониторинг событий подключения/отключения
udevadm monitor --subsystem-match=usb
```

### 7.2 usbmon — трассировка USB трафика

```bash
# Загрузить usbmon
modprobe usbmon

# Список USB шин
ls /sys/kernel/debug/usb/usbmon/

# Захват трафика (шина 1)
cat /sys/kernel/debug/usb/usbmon/1u | head -20

# Или через Wireshark/tshark с usbmon
tshark -i usbmon1 -w usb_capture.pcapng
```

### 7.3 dmesg для USB событий

```bash
# Все USB события
dmesg | grep -i usb

# Типичный успешный probe:
# usb 1-1: new full-speed USB device number 2 using xhci_hcd
# usb 1-1: New USB device found, idVendor=1234, idProduct=5678
# usb 1-1: New USB device strings: Mfr=1, Product=2, SerialNumber=3
# usb 1-1: Product: My Device
# usb 1-1: Manufacturer: My Company
# usb 1-1: SerialNumber: 12345678
# mydevice 1-1:1.0: probed: VID=1234 PID=5678

# Ошибки
dmesg | grep -i "usb.*error\|unable to enumerate"
```

### 7.4 Частые проблемы

| Симптом | Причина | Решение |
|---------|---------|---------|
| "unable to enumerate USB device" | Нет питания, плохой кабель | Проверь USB питание (5V), попробуй другой кабель/порт |
| Устройство в lsusb, driver не загружается | Нет MODULE_DEVICE_TABLE match | Проверь VID/PID в id_table |
| `-EPIPE` (Broken pipe) при bulk | Endpoint stall | `usb_clear_halt()`, перезапусти transfer |
| `-ENODEV` во время transfer | Устройство отключилось | Обработать в disconnect callback, убить pending URB |
| Медленная скорость | Full Speed вместо High Speed | Проверь `lsusb -t` скорость, Hub поддерживает HS? |
| gadget не появляется на PC | Неверный UDC, нет id\_string | `dmesg | grep dwc3`, проверь DTS OTG mode |

---

## 8. Самопроверка

**1. Какие типы endpoint'ов существуют в USB и в чём их различие?**

Control (ep0, конфигурирование, гарантированная доставка), Bulk (большие данные, гарантированная доставка, нет гарантии задержки), Interrupt (периодический опрос, гарантированная максимальная задержка), Isochronous (поток, гарантированная полоса, нет повторов).

**2. Что такое URB и зачем он нужен?**

URB (USB Request Block) — структура, описывающая одну USB передачу: endpoint, буфер, callback функция, статус. Весь USB трафик в ядре проходит через URB. Синхронные helper'ы (usb_bulk_msg) создают URB внутри. Асинхронные URB позволяют продолжать работу пока идёт передача.

**3. В чём разница между USB host driver и USB gadget?**

Host driver — ядро на стороне host (PC или RK3588 как host): управляет шиной, обнаруживает устройства, привязывает драйверы. Gadget — ядро на стороне device (RK3588 подключён к PC): отвечает на запросы host, представляется как определённый класс устройства.

**4. Что такое ConfigFS gadget и как его создать?**

Современный способ создания USB gadget без написания kernel кода: через файловую систему `/sys/kernel/config/usb_gadget/`. Создаёшь директории, пишешь VID/PID/строки, создаёшь функции (acm, mass_storage, ecm), линкуешь в конфигурацию, пишешь имя UDC. Ядро автоматически собирает дескрипторы.

**5. Когда использовать libusb вместо kernel driver?**

libusb: нет нужды в kernel API, vendor-specific устройства, прототипирование, нет RT требований. Kernel driver: нужна интеграция с block/net/input/звук подсистемами, максимальная производительность, gadget.

**6. Что делает `usb_kill_urb` и чем отличается от `usb_unlink_urb`?**

`usb_kill_urb` — синхронно: отменяет URB и блокируется до завершения callback. После возврата URB точно не исполняется. `usb_unlink_urb` — асинхронно: инициирует отмену, callback вызовется с `-ECONNRESET`. Для корректного shutdown в disconnect — всегда `usb_kill_urb`.

**7. Что такое pipe в USB API ядра Linux?**

Pipe — закодированная комбинация направления (IN/OUT), типа (bulk/control/interrupt) и номера endpoint. Создаётся макросами: `usb_rcvbulkpipe(udev, addr)`, `usb_sndbulkpipe(udev, addr)` и т.д. Передаётся в `usb_bulk_msg`, `usb_fill_bulk_urb` вместо отдельных параметров.

**8. Почему USB host driver привязывается к интерфейсу, а не к устройству?**

Одно USB устройство может реализовывать несколько интерфейсов (например, USB гарнитура: один интерфейс — аудио, другой — HID для кнопок). Каждый интерфейс может обслуживаться своим драйвером. Поэтому USB core привязывает драйвер к `usb_interface`, а не к `usb_device`.

**9. Что выдаёт `lsusb -t` и как это помогает при отладке?**

Древовидная структура: хост-контроллер → хаб → устройство, с указанием скорости каждого соединения. Позволяет быстро увидеть: работает ли устройство в ожидаемой скорости (HS/SS или деградировало до FS), через какой хаб подключено, занят ли порт.

**10. Как безопасно обработать отключение устройства при pending URB?**

В `disconnect` callback: (1) выставить флаг о том, что устройство удаляется; (2) `usb_kill_urb()` для каждого pending URB — синхронно ожидает завершения; (3) освободить буферы. В completion callback — проверить статус: `-ECONNRESET`, `-ENOENT`, `-ESHUTDOWN` означают отмену, не нужно переотправлять.
