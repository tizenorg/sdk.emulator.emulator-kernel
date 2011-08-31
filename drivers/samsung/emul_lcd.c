/*
 * LCD node
 *
 * Copyright (C) 2003,2004 Hewlett-Packard Company
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/ctype.h>
#include <linux/err.h>

#include <asm/uaccess.h>

static struct class *emul_lcd_class;
static struct device *emul_lcd_dev;
static int lcd_power = 0;

static ssize_t lcd_power_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	printk(KERN_INFO "lcd_power = %d\n", lcd_power);
	return sprintf(buf, "%d\n", lcd_power);
}

static DEVICE_ATTR(lcd_power, 0664, lcd_power_show, NULL);
static struct device_attribute *emul_lcd_device_attrib[] = {
	&dev_attr_lcd_power,
};

static int __init emul_lcd_class_init(void)
{
	int i, ret;

	emul_lcd_class = class_create(THIS_MODULE, "lcd");
	if (IS_ERR(emul_lcd_class)) {
		printk(KERN_WARNING "Unable to create backlight class; errno = %ld\n",
				PTR_ERR(emul_lcd_class));
		return PTR_ERR(emul_lcd_class);
	}

	emul_lcd_dev = device_create(emul_lcd_class, NULL, NULL, NULL, "emulator");

	for (i=0; i < ARRAY_SIZE(emul_lcd_device_attrib); i++) {
		ret = device_create_file(emul_lcd_dev, emul_lcd_device_attrib[i]);
		if (ret != 0) {
			printk(KERN_ERR "emul_lcd: Failed to create attr %d: %d\n", i, ret);
			return ret;
		}
	}

	return 0;
}

static void __exit emul_lcd_class_exit(void)
{
	class_destroy(emul_lcd_class);
}

/*
 * if this is compiled into the kernel, we need to ensure that the
 * class is registered before users of the class try to register lcd's
 */
module_init(emul_lcd_class_init);
module_exit(emul_lcd_class_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("s-core");
MODULE_DESCRIPTION("Emulator LCD driver for x86");
