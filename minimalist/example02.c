#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

static int __init my_module_init(void)
{
	pr_warn("Module loaded\n");
	return 0;
}
module_init(my_module_init);

static void __exit my_module_exit(void)
{
	pr_warn("Module unloaded\n");
}
module_exit(my_module_exit);

MODULE_AUTHOR("Your Name <name@domain.com>");
MODULE_LICENSE("GPL");
