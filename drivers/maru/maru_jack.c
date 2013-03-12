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

int charger_online = 0;
int earjack_online = 0;
int earkey_online = 0;
int hdmi_online = 0;
int usb_online = 0;

struct my_data {
	int no;
	char test[50];
};

static ssize_t show_charger_online(struct device *dev, 
		struct device_attribute *attr, char *buf) 
{
	printk("[%s] \n", __FUNCTION__);
	return snprintf(buf, PAGE_SIZE, "%d", charger_online);
}

static ssize_t store_charger_online(struct device *dev, 
		struct device_attribute *attr, const char *buf, size_t count) 
{
	printk("[%s] \n", __FUNCTION__);
	sscanf(buf, "%d", &charger_online);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_earjack_online(struct device *dev, 
		struct device_attribute *attr, char *buf) 
{
	printk("[%s] \n", __FUNCTION__);
	return snprintf(buf, PAGE_SIZE, "%d", earjack_online);
}

static ssize_t store_earjack_online(struct device *dev, 
		struct device_attribute *attr, const char *buf, size_t count) 
{
	printk("[%s] \n", __FUNCTION__);
	sscanf(buf, "%d", &earjack_online);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_earkey_online(struct device *dev, 
		struct device_attribute *attr, char *buf) 
{
	printk("[%s] \n", __FUNCTION__);
	return snprintf(buf, PAGE_SIZE, "%d", earkey_online);
}

static ssize_t store_earkey_online(struct device *dev, 
		struct device_attribute *attr, const char *buf, size_t count) 
{
	printk("[%s] \n", __FUNCTION__);
	sscanf(buf, "%d", &earkey_online);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_hdmi_online(struct device *dev, 
		struct device_attribute *attr, char *buf) 
{
	printk("[%s] \n", __FUNCTION__);
	return snprintf(buf, PAGE_SIZE, "%d", hdmi_online);
}

static ssize_t store_hdmi_online(struct device *dev, 
		struct device_attribute *attr, const char *buf, size_t count) 
{
	printk("[%s] \n", __FUNCTION__);
	sscanf(buf, "%d", &hdmi_online);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_usb_online(struct device *dev, 
		struct device_attribute *attr, char *buf) 
{
	printk("[%s] \n", __FUNCTION__);
	return snprintf(buf, PAGE_SIZE, "%d", usb_online);
}

static ssize_t store_usb_online(struct device *dev, 
		struct device_attribute *attr, const char *buf, size_t count) 
{
	printk("[%s] \n", __FUNCTION__);
	sscanf(buf, "%d", &usb_online);
	return strnlen(buf, PAGE_SIZE);
}
static DEVICE_ATTR(charger_online, S_IRUGO | S_IWUSR, show_charger_online, store_charger_online);
static DEVICE_ATTR(earjack_online, S_IRUGO | S_IWUSR, show_earjack_online, store_earjack_online);
static DEVICE_ATTR(earkey_online, S_IRUGO | S_IWUSR, show_earkey_online, store_earkey_online);
static DEVICE_ATTR(hdmi_online, S_IRUGO | S_IWUSR, show_hdmi_online, store_hdmi_online);
static DEVICE_ATTR(usb_online, S_IRUGO | S_IWUSR, show_usb_online, store_usb_online);

static int sysfs_test_create_file(struct device *dev) 
{
	int result = 0;

	printk("[%d] [%s] \n", __LINE__, __FUNCTION__);

	result = device_create_file(dev, &dev_attr_charger_online);
	if (result){
		printk("[%d] [%s] error \n", __LINE__, __FUNCTION__);
		return result;
	}

	result = device_create_file(dev, &dev_attr_earjack_online);
	if (result){
		printk("[%d] [%s] error \n", __LINE__, __FUNCTION__);
		return result;
	}

	result = device_create_file(dev, &dev_attr_earkey_online);
	if (result){
		printk("[%d] [%s] error \n", __LINE__, __FUNCTION__);
		return result;
	}

	result = device_create_file(dev, &dev_attr_hdmi_online);
	if (result){
		printk("[%d] [%s] error \n", __LINE__, __FUNCTION__);
		return result;
	}

	result = device_create_file(dev, &dev_attr_usb_online);
	if (result){
		printk("[%d] [%s] error \n", __LINE__, __FUNCTION__);
		return result;
	}

	return 0;
}


static void sysfs_test_remove_file(struct device *dev) 
{
	printk("[%s] \n", __FUNCTION__);
	device_remove_file(dev, &dev_attr_charger_online);
	device_remove_file(dev, &dev_attr_earjack_online);
	device_remove_file(dev, &dev_attr_earkey_online);
	device_remove_file(dev, &dev_attr_hdmi_online);
	device_remove_file(dev, &dev_attr_usb_online);
}

static void sysfs_test_dev_release(struct device *dev) {}

static struct platform_device the_pdev = {
	.name = "jack",
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
		platform_device_unregister(&the_pdev);
        	return err;
	}

	dev_set_drvdata(&the_pdev.dev, (void*)data);

	err = sysfs_test_create_file(&the_pdev.dev);
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
	sysfs_test_remove_file(&the_pdev.dev);
	platform_device_unregister(&the_pdev);
}

module_init(sysfs_test_init);
module_exit(sysfs_test_exit);


MODULE_LICENSE("GPL");


