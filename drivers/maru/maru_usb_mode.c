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

int UsbMenuSel = 0;

struct usb_mode_data {
	int no;
	char buffer[50];
};

#define DEVICE_NAME	"usb_mode"
#define USB_MODE_DEBUG

#ifdef USB_MODE_DEBUG
#define DLOG(level, fmt, ...) \
	printk(level "maru_%s: " fmt, DEVICE_NAME, ##__VA_ARGS__)
#else
// do nothing
#define DLOG(level, fmt, ...)
#endif

static ssize_t show_UsbMenuSel(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	DLOG(KERN_INFO, "get UsbMenuSel: %d\n", UsbMenuSel);
	return snprintf(buf, PAGE_SIZE, "%d", UsbMenuSel);
}

static ssize_t store_UsbMenuSel(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &UsbMenuSel);
	DLOG(KERN_INFO, "set UsbMenuSel: %d\n", UsbMenuSel);

	return strnlen(buf, PAGE_SIZE);
}

static DEVICE_ATTR(UsbMenuSel, S_IRUGO | S_IWUSR, show_UsbMenuSel, store_UsbMenuSel);

static int maru_usb_mode_sysfs_create_file(struct device *dev)
{
	int result = 0;

	DLOG(KERN_INFO, "sysfs_create_file\n");

	result = device_create_file(dev, &dev_attr_UsbMenuSel);
	if (result){
		DLOG(KERN_ERR, "failed to create UsbMenuSel file\n");
		return result;
	}

	return 0;
}


static void maru_usb_mode_sysfs_remove_file(struct device *dev)
{
	DLOG(KERN_INFO, "sysfs_remove_file\n");

	device_remove_file(dev, &dev_attr_UsbMenuSel);
}

static void maru_usb_mode_sysfs_dev_release(struct device *dev)
{
	DLOG(KERN_INFO, "sysfs_dev_release\n");
}

static struct platform_device the_pdev = {
	.name = DEVICE_NAME,
	.id = -1,
	.dev = {
		.release = maru_usb_mode_sysfs_dev_release,
	}
};

static int __init maru_usb_mode_sysfs_init(void)
{
	int err = 0;
	struct usb_mode_data *data;

	DLOG(KERN_INFO, "sysfs_init\n");

	err = platform_device_register(&the_pdev);
	if (err) {
		printk("platform_device_register error\n");
		return err;
	}

	data = kzalloc(sizeof(struct usb_mode_data), GFP_KERNEL);
	if (!data) {
		DLOG(KERN_ERR, "kzalloc failure\n");
		platform_device_unregister(&the_pdev);
		return -ENOMEM;
	}

	dev_set_drvdata(&the_pdev.dev, (void*)data);

	err = maru_usb_mode_sysfs_create_file(&the_pdev.dev);
	if (err) {
		DLOG(KERN_ERR, "sysfs_create_file failure\n");
		kfree(data);
		platform_device_unregister(&the_pdev);
		return err;
	}

	return 0;
}

static void __exit maru_usb_mode_sysfs_exit(void)
{
	void *data = dev_get_drvdata(&the_pdev.dev);

	DLOG(KERN_INFO, "sysfs_exit\n");

	if (data) {
		kfree(data);
	}
	maru_usb_mode_sysfs_remove_file(&the_pdev.dev);
	platform_device_unregister(&the_pdev);
}

module_init(maru_usb_mode_sysfs_init);
module_exit(maru_usb_mode_sysfs_exit);

MODULE_LICENSE("GPL");
