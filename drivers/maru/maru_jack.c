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

int charger_online = 0;
int earjack_online = 0;
int earkey_online = 0;
int hdmi_online = 0;
int usb_online = 0;

struct jack_data {
	int no;
	char buffer[50];
};

#define DEVICE_NAME	"jack"
#define JACK_DEBUG

#ifdef JACK_DEBUG
#define DLOG(level, fmt, ...) \
	printk(level "maru_%s: " fmt, DEVICE_NAME, ##__VA_ARGS__)
#else
// do nothing
#define DLOG(level, fmt, ...)
#endif

static ssize_t show_charger_online(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	DLOG(KERN_INFO, "get charger_online: %d\n", charger_online);
	return snprintf(buf, PAGE_SIZE, "%d", charger_online);
}

static ssize_t store_charger_online(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &charger_online);
	DLOG(KERN_INFO, "set charger_online: %d\n", charger_online);

	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_earjack_online(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	DLOG(KERN_INFO, "get earjack_online: %d\n", earjack_online);
	return snprintf(buf, PAGE_SIZE, "%d", earjack_online);
}

static ssize_t store_earjack_online(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &earjack_online);
	DLOG(KERN_INFO, "set earjack_online: %d\n", earjack_online);

	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_earkey_online(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	DLOG(KERN_INFO, "get earkey_online: %d\n", earkey_online);
	return snprintf(buf, PAGE_SIZE, "%d", earkey_online);
}

static ssize_t store_earkey_online(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &earkey_online);
	DLOG(KERN_INFO, "set earkey_online: %d\n", earkey_online);

	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_hdmi_online(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	DLOG(KERN_INFO, "get hdmi_online: %d\n", hdmi_online);
	return snprintf(buf, PAGE_SIZE, "%d", hdmi_online);
}

static ssize_t store_hdmi_online(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &hdmi_online);
	DLOG(KERN_INFO, "set hdmi_online: %d\n", hdmi_online);

	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_usb_online(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	DLOG(KERN_INFO, "get usb_online: %d\n", usb_online);
	return snprintf(buf, PAGE_SIZE, "%d", usb_online);
}

static ssize_t store_usb_online(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &usb_online);
	DLOG(KERN_INFO, "set usb_online: %d\n", usb_online);

	return strnlen(buf, PAGE_SIZE);
}
static DEVICE_ATTR(charger_online, S_IRUGO | S_IWUSR, show_charger_online, store_charger_online);
static DEVICE_ATTR(earjack_online, S_IRUGO | S_IWUSR, show_earjack_online, store_earjack_online);
static DEVICE_ATTR(earkey_online, S_IRUGO | S_IWUSR, show_earkey_online, store_earkey_online);
static DEVICE_ATTR(hdmi_online, S_IRUGO | S_IWUSR, show_hdmi_online, store_hdmi_online);
static DEVICE_ATTR(usb_online, S_IRUGO | S_IWUSR, show_usb_online, store_usb_online);

static int maru_jack_sysfs_create_file(struct device *dev)
{
	int result = 0;

	DLOG(KERN_INFO, "sysfs_create_file\n");

	result = device_create_file(dev, &dev_attr_charger_online);
	if (result){
		DLOG(KERN_ERR, "failed to create charger_online file\n");
		return result;
	}

	result = device_create_file(dev, &dev_attr_earjack_online);
	if (result){
		DLOG(KERN_ERR, "failed to create earjack_online file\n");
		return result;
	}

	result = device_create_file(dev, &dev_attr_earkey_online);
	if (result){
		DLOG(KERN_ERR, "failed to create earkey_online file\n");
		return result;
	}

	result = device_create_file(dev, &dev_attr_hdmi_online);
	if (result){
		DLOG(KERN_ERR, "failed to create hdmi_online file\n");
		return result;
	}

	result = device_create_file(dev, &dev_attr_usb_online);
	if (result){
		DLOG(KERN_ERR, "failed to create usb_online file\n");
		return result;
	}

	return 0;
}


static void maru_jack_sysfs_remove_file(struct device *dev)
{
	DLOG(KERN_INFO, "sysfs_remove_file\n");

	device_remove_file(dev, &dev_attr_charger_online);
	device_remove_file(dev, &dev_attr_earjack_online);
	device_remove_file(dev, &dev_attr_earkey_online);
	device_remove_file(dev, &dev_attr_hdmi_online);
	device_remove_file(dev, &dev_attr_usb_online);
}

static void maru_jack_sysfs_dev_release(struct device *dev)
{
	DLOG(KERN_INFO, "sysfs_dev_release\n");
}

static struct platform_device the_pdev = {
	.name = DEVICE_NAME,
	.id = -1,
	.dev = {
		.release = maru_jack_sysfs_dev_release,
	}
};

static int __init maru_jack_sysfs_init(void)
{
	int err = 0;
	struct jack_data *data;

	DLOG(KERN_INFO, "sysfs_init\n");

	err = platform_device_register(&the_pdev);
	if (err) {
		DLOG(KERN_ERR, "platform_device_register failure\n");
		return err;
	}

	data = kzalloc(sizeof(struct jack_data), GFP_KERNEL);
	if (!data) {
		DLOG(KERN_ERR, "kzalloc failure\n");
		platform_device_unregister(&the_pdev);
		return -ENOMEM;
	}

	dev_set_drvdata(&the_pdev.dev, (void*)data);

	err = maru_jack_sysfs_create_file(&the_pdev.dev);
	if (err) {
		DLOG(KERN_ERR, "sysfs_create_file failure\n");
		kfree(data);
		platform_device_unregister(&the_pdev);
		return err;
	}

	return 0;
}

static void __exit maru_jack_sysfs_exit(void)
{
	void *data = dev_get_drvdata(&the_pdev.dev);

	DLOG(KERN_INFO, "sysfs_exit\n");

	if (data) {
		kfree(data);
	}
	maru_jack_sysfs_remove_file(&the_pdev.dev);
	platform_device_unregister(&the_pdev);
}

module_init(maru_jack_sysfs_init);
module_exit(maru_jack_sysfs_exit);

MODULE_LICENSE("GPL");
