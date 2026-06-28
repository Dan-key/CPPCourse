// K5 04-ramfs-mini — простейшая in-memory ФС k5fs (амбициозное задание).
// Регистрируем тип ФС, и при монтировании заполняем super_block: создаём
// корневой inode-каталог и привязываем его к корневому dentry (d_make_root).
// Современный API ядра: init_fs_context → get_tree_nodev → fill_super.
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/pagemap.h>

#define K5FS_MAGIC 0x6b356673  /* "k5fs" */

static const struct super_operations k5fs_sops = {
	.statfs = simple_statfs,
};

static int k5fs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	sb->s_magic          = K5FS_MAGIC;
	sb->s_blocksize      = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_op             = &k5fs_sops;
	sb->s_time_gran      = 1;

	// TODO: создать корневой inode-каталог и корневой dentry:
	//   struct inode *inode = new_inode(sb);
	//   if (!inode) return -ENOMEM;
	//   inode->i_ino  = 1;
	//   inode->i_mode = S_IFDIR | 0755;
	//   simple_inode_init_ts(inode);
	//   inode->i_op  = &simple_dir_inode_operations;
	//   inode->i_fop = &simple_dir_operations;
	//   set_nlink(inode, 2);
	//   sb->s_root = d_make_root(inode);
	//   if (!sb->s_root) return -ENOMEM;
	//   return 0;
	return -ENOMEM; /* заглушка: пока корня нет — mount обязан падать, не падая в oops */
}

static int k5fs_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, k5fs_fill_super);
}
static const struct fs_context_operations k5fs_ctx_ops = {
	.get_tree = k5fs_get_tree,
};
static int k5fs_init_fs_context(struct fs_context *fc)
{
	fc->ops = &k5fs_ctx_ops;
	return 0;
}
static struct file_system_type k5fs_type = {
	.owner           = THIS_MODULE,
	.name            = "k5fs",
	.init_fs_context = k5fs_init_fs_context,
	.kill_sb         = kill_anon_super,
};

static int __init k5_init(void)
{
	return register_filesystem(&k5fs_type);
}
static void __exit k5_exit(void)
{
	unregister_filesystem(&k5fs_type);
}
module_init(k5_init);
module_exit(k5_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K5: 04-ramfs-mini");
