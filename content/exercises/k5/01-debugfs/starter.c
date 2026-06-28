// K5 01-debugfs — debugfs-интерфейс к драйверу.
// Каталог /sys/kernel/debug/k5_debug с двумя файлами:
//   counter — u32 на чтение/запись (хелпер debugfs_create_u32)
//   status  — read-only, возвращает "Driver is active\n" (свои file_operations)
// В exit ОБЯЗАТЕЛЬНО рекурсивно удалить каталог, иначе cat после rmmod уронит ядро.
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

static struct dentry *k5_dir;
static u32 k5_counter;

static ssize_t status_read(struct file *file, char __user *ubuf,
			   size_t count, loff_t *ppos)
{
	static const char msg[] = "Driver is active\n";
	// TODO: вернуть строку msg пользователю с учётом count/ppos.
	//   простейший способ — simple_read_from_buffer(ubuf, count, ppos,
	//                                                msg, sizeof(msg) - 1);
	(void)msg; (void)ubuf; (void)count; (void)ppos;
	return 0;
}

static const struct file_operations status_fops = {
	.owner = THIS_MODULE,
	.read  = status_read,
	.llseek = default_llseek,
};

static int __init k5_init(void)
{
	// TODO: создать каталог:
	//   k5_dir = debugfs_create_dir("k5_debug", NULL);
	//   if (IS_ERR(k5_dir)) return PTR_ERR(k5_dir);
	// TODO: создать файл counter, привязанный к &k5_counter:
	//   debugfs_create_u32("counter", 0644, k5_dir, &k5_counter);
	// TODO: создать файл status со своими операциями:
	//   debugfs_create_file("status", 0444, k5_dir, NULL, &status_fops);
	(void)k5_counter;
	return 0;
}

static void __exit k5_exit(void)
{
	// TODO: рекурсивно удалить каталог:
	//   debugfs_remove_recursive(k5_dir);
	(void)k5_dir;
}

module_init(k5_init);
module_exit(k5_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K5: 01-debugfs");
