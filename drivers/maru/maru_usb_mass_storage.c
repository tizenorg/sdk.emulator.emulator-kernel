/*
 * Virtual device node for event injector of emulator
 *
 * Copyright (c) 2011 - 2012 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 * Sungmin Ha <sungmin82.ha@samsung.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Contributors:
 * - S-Core Co., Ltd
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

static char mode[1024];
static char file0[1024];
static char file1[1024];

u64 file0_mask = 0x0000000000000000;
u64 file1_mask = 0x0000000000000001;

struct my_data {
	int no;
	char test[50];
};

static ssize_t show_mode(struct device *dev, 
		struct device_attribute *attr, char *buf) 
{
	printk("[%s] \n", __FUNCTION__);
	return snprintf(buf, PAGE_SIZE, "%s", mode);
}

static ssize_t store_mode(struct device *dev, 
		struct device_attribute *attr, const char *buf, size_t count) 
{
	printk("[%s] \n", __FUNCTION__);
	sscanf(buf, "%s", mode);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_file(struct device *dev, 
		struct device_attribute *attr, char *buf) 
{
	ssize_t ret = 0;
	printk("[%s] \n", __FUNCTION__);
	if(*(dev->dma_mask) == file0_mask) {
		ret = snprintf(buf, PAGE_SIZE, "%s", file0);
	} else {
		ret = snprintf(buf, PAGE_SIZE, "%s", file1);
	}

	return ret;
}

static ssize_t store_file(struct device *dev, 
		struct device_attribute *attr, const char *buf, size_t count) 
{
	size_t ret;
	printk("[%s]\n", __FUNCTION__);
	if(*(dev->dma_mask) == file0_mask) {
		sscanf(buf, "%s", file0);
	} else {
		sscanf(buf, "%s", file1);
	}
	
	ret = strnlen(buf, PAGE_SIZE);
	if(ret == 0) {
		return 1;
	} else {
		return strnlen(buf, PAGE_SIZE);
	}
}

static DEVICE_ATTR(mode, S_IRUGO | S_IWUSR, show_mode, store_mode);
static DEVICE_ATTR(file, S_IRUGO | S_IWUSR, show_file, store_file);

static int sysfs_lun0_create_file(struct device *dev) 
{
	int result = 0;

	printk("[%d] [%s] \n", __LINE__, __FUNCTION__);

	result = device_create_file(dev, &dev_attr_mode);
	if (result){
		printk("[%d] [%s] error \n", __LINE__, __FUNCTION__);
		return result;
	}

	result = device_create_file(dev, &dev_attr_file);
	if (result){
		printk("[%d] [%s] error \n", __LINE__, __FUNCTION__);
		return result;
	}

	return 0;
}

static int sysfs_lun1_create_file(struct device *dev) 
{
	int result = 0;

	printk("[%d] [%s] \n", __LINE__, __FUNCTION__);

	result = device_create_file(dev, &dev_attr_file);
	if (result){
		printk("[%d] [%s] error \n", __LINE__, __FUNCTION__);
		return result;
	}

	return 0;
}

static void sysfs_test_dev_release(struct device *dev) {}
static void sysfs_test_dev_release_lun0(struct device *dev) {}

static struct platform_device the_pdev = {
	.name = "usb_mass_storage",
	.id = -1,
	.dev = {
		.release = sysfs_test_dev_release,
	}
};

static struct platform_device the_pdev_sub1 = {
	.name = "lun0",
	.id = -1,	
	.dev = {
		.release = sysfs_test_dev_release_lun0,
		.parent = &the_pdev.dev,
		.dma_mask = &file0_mask,
	}
};

static struct platform_device the_pdev_sub2 = {
	.name = "lun1",
	.id = -1,	
	.dev = {
		.release = sysfs_test_dev_release_lun0,
		.parent = &the_pdev.dev,
		.dma_mask = &file1_mask,
	}
};

static int __init sysfs_test_init(void) 
{
	int err = 0;
	struct my_data *data;

	printk("[%s] \n", __FUNCTION__);

	memset(mode, 0, sizeof(mode));
	memset(file0, 0, sizeof(file0));
	memset(file1, 0, sizeof(file1));

	err = platform_device_register(&the_pdev);
	if (err) {
		printk("platform_device_register error\n");
		return err;
	}

	err = platform_device_register(&the_pdev_sub1);
	if (err) {
		printk("platform_device_register error\n");
		return err;
	}

	err = platform_device_register(&the_pdev_sub2);
	if (err) {
		printk("platform_device_register error\n");
		return err;
	}

	data = kzalloc(sizeof(struct my_data), GFP_KERNEL);
	if (!data) {
		printk("[%s] kzalloc error\n", __FUNCTION__);
		err = -ENOMEM;
		platform_device_unregister(&the_pdev);
        	return err;
	}

	dev_set_drvdata(&the_pdev.dev, (void*)data);
	dev_set_drvdata(&the_pdev_sub1.dev, (void*)data);
	dev_set_drvdata(&the_pdev_sub2.dev, (void*)data);

	err = sysfs_lun0_create_file(&the_pdev_sub1.dev);
	if (err) {
		printk("sysfs_create_file error\n");
		kfree(data);
	}
	
	err = sysfs_lun1_create_file(&the_pdev_sub2.dev);
	if (err) {
		printk("sysfs_create_file error\n");
		kfree(data);
	}
	
	return 0;
}

static void __exit sysfs_test_exit(void) 
{
	void *data = dev_get_drvdata(&the_pdev.dev);

	printk("[%s] \n", __FUNCTION__);

	kfree(data);
	platform_device_unregister(&the_pdev_sub2);
	platform_device_unregister(&the_pdev_sub1);
	platform_device_unregister(&the_pdev);
}

module_init(sysfs_test_init);
module_exit(sysfs_test_exit);


MODULE_LICENSE("GPL");


