/*
 * Virtual device node for event injector of emulator
 *
 * Copyright (c) 2011 - 2013 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 * SooYoung Ha <yoosah.ha@samsung.com>
 * JinHyung Choi <jinhyung2.choi@samsung.com>
 * Sungmin Ha <sungmin82.ha@samsung.com>
 * YeongKyoon Lee <yeongkyoon.lee@samsung.com
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

struct usb_storage_data {
	int no;
	char buffer[50];
};

#define DEVICE_NAME				"usb_mass_storage"
#define SUB_DEVICE0_NAME		"lun0"
#define SUB_DEVICE1_NAME		"lun1"

#define USB_STORAGE_DEBUG

#ifdef USB_STORAGE_DEBUG
#define DLOG(level, fmt, ...) \
	printk(level "maru_%s: " fmt, DEVICE_NAME, ##__VA_ARGS__)
#else
// do nothing
#define DLOG(level, fmt, ...)
#endif

static void __exit maru_usb_mass_storage_sysfs_exit(void);

static ssize_t show_mode(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	DLOG(KERN_INFO, "get mode: %s\n", mode);
	return snprintf(buf, PAGE_SIZE, "%s", mode);
}

static ssize_t store_mode(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%s", mode);
	DLOG(KERN_INFO, "set mode: %s\n", mode);

	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_file(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	if (*(dev->dma_mask) == file0_mask) {
		DLOG(KERN_INFO, "get file0: %s\n", file0);
		ret = snprintf(buf, PAGE_SIZE, "%s", file0);
	} else {
		DLOG(KERN_INFO, "get file1: %s\n", file1);
		ret = snprintf(buf, PAGE_SIZE, "%s", file1);
	}

	return ret;
}

static ssize_t store_file(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	size_t ret;

	if (*(dev->dma_mask) == file0_mask) {
		sscanf(buf, "%s", file0);
		DLOG(KERN_INFO, "set file0: %s\n", file0);
	} else {
		sscanf(buf, "%s", file1);
		DLOG(KERN_INFO, "set file1: %s\n", file1);
	}

	ret = strnlen(buf, PAGE_SIZE);
	if (ret == 0) {
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

	DLOG(KERN_INFO, "sysfs_create_lun0_file\n");

	result = device_create_file(dev, &dev_attr_mode);
	if (result){
		DLOG(KERN_ERR, "failed to create lun0 mode\n");
		return result;
	}

	result = device_create_file(dev, &dev_attr_file);
	if (result){
		DLOG(KERN_ERR, "failed to create lun0 file\n");
		return result;
	}

	return 0;
}

static int sysfs_lun1_create_file(struct device *dev)
{
	int result = 0;

	DLOG(KERN_INFO, "sysfs_create_lun1_file\n");

	result = device_create_file(dev, &dev_attr_file);
	if (result){
		DLOG(KERN_ERR, "failed to create lun1 file\n");
		return result;
	}

	return 0;
}

static void maru_usb_mass_storage_sysfs_dev_release(struct device *dev)
{
	DLOG(KERN_INFO, "sysfs_dev_release\n");
}

static void maru_usb_mass_storage_sysfs_dev_release_lun0(struct device *dev)
{
	DLOG(KERN_INFO, "sysfs_dev_release_lun0\n");
}

static struct platform_device the_pdev = {
	.name = DEVICE_NAME,
	.id = -1,
	.dev = {
		.release = maru_usb_mass_storage_sysfs_dev_release,
	}
};

static struct platform_device the_pdev_sub1 = {
	.name = SUB_DEVICE0_NAME,
	.id = -1,
	.dev = {
		.release = maru_usb_mass_storage_sysfs_dev_release_lun0,
		.parent = &the_pdev.dev,
		.dma_mask = &file0_mask,
	}
};

static struct platform_device the_pdev_sub2 = {
	.name = SUB_DEVICE1_NAME,
	.id = -1,
	.dev = {
		.release = maru_usb_mass_storage_sysfs_dev_release_lun0,
		.parent = &the_pdev.dev,
		.dma_mask = &file1_mask,
	}
};

static int __init maru_usb_mass_storage_sysfs_init(void)
{
	int err = 0;
	struct usb_storage_data *data;

	DLOG(KERN_INFO, "sysfs_init\n");

	memset(mode, 0, sizeof(mode));
	memset(file0, 0, sizeof(file0));
	memset(file1, 0, sizeof(file1));

	err = platform_device_register(&the_pdev);
	if (err) {
		DLOG(KERN_ERR, "platform_device_register failure for device\n");
		return err;
	}

	err = platform_device_register(&the_pdev_sub1);
	if (err) {
		DLOG(KERN_ERR, "platform_device_register failure for sub_device0\n");
		platform_device_unregister(&the_pdev);
		return err;
	}

	err = platform_device_register(&the_pdev_sub2);
	if (err) {
		DLOG(KERN_ERR, "platform_device_register failure for sub_device1\n");
		platform_device_unregister(&the_pdev_sub1);
		platform_device_unregister(&the_pdev);
		return err;
	}

	data = kzalloc(sizeof(struct usb_storage_data), GFP_KERNEL);
	if (!data) {
		DLOG(KERN_ERR, "kzalloc failure\n");
		platform_device_unregister(&the_pdev);
		return ENOMEM;
	}

	dev_set_drvdata(&the_pdev.dev, (void*)data);
	dev_set_drvdata(&the_pdev_sub1.dev, (void*)data);
	dev_set_drvdata(&the_pdev_sub2.dev, (void*)data);

	err = sysfs_lun0_create_file(&the_pdev_sub1.dev);
	if (err) {
		DLOG(KERN_ERR, "sysfs_create_lun0_file failure\n");
		platform_device_unregister(&the_pdev_sub1);
		platform_device_unregister(&the_pdev);
		kfree(data);
		return err;
	}

	err = sysfs_lun1_create_file(&the_pdev_sub2.dev);
	if (err) {
		DLOG(KERN_ERR, "sysfs_create_lun1_file failure\n");
		maru_usb_mass_storage_sysfs_exit();
		return err;
	}

	return 0;
}

static void __exit maru_usb_mass_storage_sysfs_exit(void)
{
	void *data = dev_get_drvdata(&the_pdev.dev);

	DLOG(KERN_INFO, "sysfs_exit\n");

	if (data) {
		kfree(data);
	}
	platform_device_unregister(&the_pdev_sub2);
	platform_device_unregister(&the_pdev_sub1);
	platform_device_unregister(&the_pdev);
}

module_init(maru_usb_mass_storage_sysfs_init);
module_exit(maru_usb_mass_storage_sysfs_exit);

MODULE_LICENSE("GPL");
