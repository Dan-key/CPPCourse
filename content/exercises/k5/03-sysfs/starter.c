// K5 03-sysfs — атрибут устройства по правилу «один файл — одно значение».
// /sys/kernel/k5_device/mode_level:
//   read  → текущее значение k5_mode (через sysfs_emit)
//   write → разобрать целое из строки (kstrtoint), при мусоре вернуть ошибку
// В exit уменьшить счётчик ссылок kobject (kobject_put) — это и освободит объект.
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/kernel.h>
#include <linux/string.h>

static struct kobject *k5_kobj;
static int k5_mode;

static ssize_t mode_level_show(struct kobject *kobj, struct kobj_attribute *attr,
			       char *buf)
{
	// TODO: вернуть k5_mode в buf через sysfs_emit (а НЕ sprintf):
	//   return sysfs_emit(buf, "%d\n", k5_mode);
	(void)buf; (void)k5_mode;
	return 0;
}
static ssize_t mode_level_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	// TODO: разобрать целое и при ошибке вернуть её:
	//   int ret = kstrtoint(buf, 10, &k5_mode);
	//   if (ret < 0) return ret;
	(void)buf;
	return count;
}
static struct kobj_attribute mode_level_attr =
	__ATTR(mode_level, 0664, mode_level_show, mode_level_store);

static int __init k5_init(void)
{
	int ret;

	k5_kobj = kobject_create_and_add("k5_device", kernel_kobj);
	if (!k5_kobj)
		return -ENOMEM;
	ret = sysfs_create_file(k5_kobj, &mode_level_attr.attr);
	if (ret) {
		kobject_put(k5_kobj);
		return ret;
	}
	return 0;
}
static void __exit k5_exit(void)
{
	kobject_put(k5_kobj);
}
module_init(k5_init);
module_exit(k5_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K5: 03-sysfs");
