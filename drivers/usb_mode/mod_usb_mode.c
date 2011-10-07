#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>

int UsbMenuSel = 0;

struct my_data {
	int no;
	char test[50];
};

static ssize_t show_UsbMenuSel(struct device *dev, 
		struct device_attribute *attr, char *buf) 
{
	printk("[%s] \n", __FUNCTION__);
	return snprintf(buf, PAGE_SIZE, "%d", UsbMenuSel);
}

static ssize_t store_UsbMenuSel(struct device *dev, 
		struct device_attribute *attr, const char *buf, size_t count) 
{
	printk("[%s] \n", __FUNCTION__);
	sscanf(buf, "%d", &UsbMenuSel);
	return strnlen(buf, PAGE_SIZE);
}

static DEVICE_ATTR(UsbMenuSel, S_IRUGO | S_IWUSR, show_UsbMenuSel, store_UsbMenuSel);

static int sysfs_test_create_file(struct device *dev) 
{
	int result = 0;

	printk("[%d] [%s] \n", __LINE__, __FUNCTION__);

	result = device_create_file(dev, &dev_attr_UsbMenuSel);
	if (result){
		printk("[%d] [%s] error \n", __LINE__, __FUNCTION__);
		return result;
	}

	return 0;
}


static void sysfs_test_remove_file(struct device *dev) 
{
	printk("[%s] \n", __FUNCTION__);
	device_remove_file(dev, &dev_attr_UsbMenuSel);
}

static void sysfs_test_dev_release(struct device *dev) {}

static struct platform_device the_pdev = {
	.name = "usb_mode",
	.id = -1,
	.dev = {
		.release = sysfs_test_dev_release,
	}
};

static int __init sysfs_test_init(void) 
{
	int err = 0;
	struct my_data *data;

	printk("[%s] \n", __FUNCTION__);

	err = platform_device_register(&the_pdev);
	if (err) {
		printk("platform_device_register error\n");
		return err;
	}

	data = kzalloc(sizeof(struct my_data), GFP_KERNEL);
	if (!data) {
		printk("[%s] kzalloc error\n", __FUNCTION__);
		err = -ENOMEM;
		goto alloc_err;
	}

	dev_set_drvdata(&the_pdev.dev, (void*)data);

	err = sysfs_test_create_file(&the_pdev.dev);
	if (err) {
		printk("sysfs_create_file error\n");
		goto sysfs_err;
	}

	return 0;

sysfs_err:
	kfree(data);

alloc_err:
	platform_device_unregister(&the_pdev);
	return err;
}

static void __exit sysfs_test_exit(void) 
{
	void *data = dev_get_drvdata(&the_pdev.dev);

	printk("[%s] \n", __FUNCTION__);

	kfree(data);
	sysfs_test_remove_file(&the_pdev.dev);
	platform_device_unregister(&the_pdev);
}

module_init(sysfs_test_init);
module_exit(sysfs_test_exit);


MODULE_LICENSE("GPL");


