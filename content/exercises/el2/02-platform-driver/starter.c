/* ПРИМЕЧАНИЕ: Kernel module. Нужны: kernel headers, QEMU/hardware.
 *
 * ЗАДАНИЕ: Реализовать platform_driver для гипотетического "таймера":
 * 1. В probe(): прочитать base address через devm_platform_ioremap_resource()
 * 2. В probe(): прочитать IRQ через platform_get_irq()
 * 3. В probe(): прочитать "period-ms" (u32) из DT, default = 1000
 * 4. Зарегистрировать IRQ handler через devm_request_irq()
 * 5. В IRQ handler: инкрементировать счётчик, вывести в dmesg каждые 10 прерываний
 * 6. Экспортировать счётчик через /sys/devices/.../counter (через sysfs атрибут)
 *
 * DTS:
 *   mytimer@ff100000 {
 *     compatible = "course,platform-timer";
 *     reg = <0 0xff100000 0 0x100>;
 *     interrupts = <GIC_SPI 10 IRQ_TYPE_LEVEL_HIGH>;
 *     period-ms = <500>;
 *   };
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/sysfs.h>

struct mytimer_priv {
    void __iomem *base;
    int           irq;
    u32           period_ms;
    unsigned long counter;
};

static irqreturn_t mytimer_irq(int irq, void *dev_id)
{
    /* TODO: инкрементировать счётчик */
    return IRQ_HANDLED;
}

static int mytimer_probe(struct platform_device *pdev)
{
    struct mytimer_priv *priv;
    /* TODO */
    (void)priv;
    return 0;
}

static int mytimer_remove(struct platform_device *pdev) { return 0; }

static const struct of_device_id mytimer_of_match[] = {
    { .compatible = "course,platform-timer" },
    { }
};
MODULE_DEVICE_TABLE(of, mytimer_of_match);

static struct platform_driver mytimer_driver = {
    .probe  = mytimer_probe,
    .remove = mytimer_remove,
    .driver = { .name = "course-timer", .of_match_table = mytimer_of_match },
};
module_platform_driver(mytimer_driver);
MODULE_LICENSE("GPL");
