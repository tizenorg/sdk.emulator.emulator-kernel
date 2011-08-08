#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>

static struct class *mtd_class;
static struct device* mtd_device;

static int capacity = 100;
static int charge_full = 1;
static int charge_now = 1;

static ssize_t show_capacity(struct device *dev, struct device_attribute *attr, char *buf) 
{
	printk("[%s] \n", __FUNCTION__);
	return snprintf(buf, PAGE_SIZE, "%d", capacity);
}

static ssize_t store_capacity(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) 
{
	printk("[%s] \n", __FUNCTION__);
	sscanf(buf, "%d", &capacity);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_charge_full(struct device *dev, struct device_attribute *attr, char *buf) 
{
	printk("[%s] \n", __FUNCTION__);
	return snprintf(buf, PAGE_SIZE, "%d", charge_full);
}

static ssize_t store_charge_full(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) 
{
	printk("[%s] \n", __FUNCTION__);
	sscanf(buf, "%d", &charge_full);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_charge_now(struct device *dev, struct device_attribute *attr, char *buf) 
{
	printk("[%s] \n", __FUNCTION__);
	return snprintf(buf, PAGE_SIZE, "%d", charge_now);
}

static ssize_t store_charge_now(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) 
{
	printk("[%s] \n", __FUNCTION__);
	sscanf(buf, "%d", &charge_now);
	return strnlen(buf, PAGE_SIZE);
}

static struct device_attribute ps_device_attributes[] = {
	__ATTR(capacity, 0644, show_capacity, store_capacity),
	__ATTR(charge_full, 0644, show_charge_full, store_charge_full),
	__ATTR(charge_now, 0644, show_charge_now, store_charge_now),
};

struct device new_device_dev;

static int __init sysfs_test_init(void) 
{
	int err;
	printk("[%s] \n", __FUNCTION__);

	mtd_class = class_create(THIS_MODULE, "power_supply");
	mtd_device = device_create(mtd_class, NULL, (dev_t)NULL, NULL, "battery");
	
	err = device_create_file(mtd_device, &ps_device_attributes[0]);
	err = device_create_file(mtd_device, &ps_device_attributes[1]);
	err = device_create_file(mtd_device, &ps_device_attributes[2]);

	return 0;
}

static void __exit sysfs_test_exit(void) 
{
	printk("[%s] \n", __FUNCTION__);
	class_destroy(mtd_class);
}

module_init(sysfs_test_init);
module_exit(sysfs_test_exit);


MODULE_LICENSE("GPL");


