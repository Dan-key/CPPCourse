/* ПРИМЕЧАНИЕ: Это упражнение — kernel module, не userspace.
 * Для запуска нужен: Linux kernel headers, QEMU или реальное железо.
 * Используй модуль EL2 раздел "8. Platform driver" как основу.
 *
 * ЗАДАНИЕ: Реализовать platform_driver который:
 * 1. Читает из DT: "clock-frequency" (u32) → выводит в dmesg
 * 2. Читает из DT: "label" (string) → выводит в dmesg
 * 3. Читает из DT: "interrupts" через platform_get_irq → выводит номер
 * 4. В remove() выводит "removed: <label>"
 *
 * DTS для тестирования (добавить к qemu virt DTB через overlay):
 *   mydev@0 {
 *     compatible = "course,dt-parse";
 *     reg = <0 0x100>;
 *     interrupts = <5>;
 *     clock-frequency = <1000000>;
 *     label = "my-test-device";
 *   };
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>

static int dtparse_probe(struct platform_device *pdev)
{
    /* TODO: прочитать свойства из DT и вывести через dev_info */
    return 0;
}

static int dtparse_remove(struct platform_device *pdev)
{
    return 0;
}

static const struct of_device_id dtparse_of_match[] = {
    { .compatible = "course,dt-parse" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dtparse_of_match);

static struct platform_driver dtparse_driver = {
    .probe  = dtparse_probe,
    .remove = dtparse_remove,
    .driver = { .name = "dt-parse", .of_match_table = dtparse_of_match },
};
module_platform_driver(dtparse_driver);
MODULE_LICENSE("GPL");
