/*
 * MARU dummy driver
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 * Jinhyung Choi <jinh0.choi@samsung.com>
 * Hyunjin Lee <hyunjin816.lee@samsung.com>
 * SangHo Park <sangho.p@samsung.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *					Boston, MA  02110-1301, USA.
 *
 * Contributors:
 * - S-Core Co., Ltd
 *
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/cdev.h>

#include <linux/mm.h>
#include <linux/mm_types.h>

#include "maru_dummy.h"

struct dummy_device {
	const char *name;
	struct class *dev_class;
	struct device* dev_device;
	struct cdev cdev;
	dev_t dev;
	struct file_operations fops;
};

static DECLARE_WAIT_QUEUE_HEAD(wq);

int dummy_driver_debug = 0;
module_param(dummy_driver_debug, int, 0644);
MODULE_PARM_DESC(dummy_driver_debug, "Turn on/off maru dummy debugging (default:off).");

#define GENERATE_ACCESSORS_DEV(_dev)								\
static int maru_##_dev##_open(struct inode *inode, struct file *file)				\
{												\
	maru_device_dbg(1, "open is called.\n");						\
	return 0;										\
}												\
static int maru_##_dev##_close(struct inode *inode, struct file *file)				\
{												\
	maru_device_dbg(1, "close is called.\n");						\
	return 0;										\
}												\
static long maru_##_dev##_ioctl(struct file *file, unsigned int cmd, unsigned long arg)		\
{												\
	maru_device_dbg(1, "ioctl cmd : 0x%08x, arg : 0x%08lx\n", cmd, arg);			\
	return 0;										\
}												\
static ssize_t maru_##_dev##_write(struct file *file, const char __user *buf,			\
								size_t len, loff_t *ppos)	\
{												\
	maru_device_dbg(1, "write is called. size: %d, buf: %s, \n", len, buf);			\
	return len;										\
}												\
static unsigned int maru_##_dev##_poll(struct file *file, struct poll_table_struct *wait)	\
{												\
	return 0;										\
}

#define __ATTRIBUTE_DUMMY_DEVICE(_name) {		\
	.name = __stringify(_name),			\
	.fops = {					\
		.open = maru_##_name##_open,		\
		.unlocked_ioctl = maru_##_name##_ioctl,	\
		.write = maru_##_name##_write,		\
		.poll = maru_##_name##_poll,		\
		.release = maru_##_name##_close		\
	},						\
}

#define __ATTRIBUTE_HYPHEN_DUMMY_DEVICE(_name, _fname) {\
	.name = __stringify(_name),			\
	.fops = {					\
		.open = maru_##_fname##_open,		\
		.unlocked_ioctl = maru_##_fname##_ioctl,\
		.write = maru_##_fname##_write,		\
		.poll = maru_##_fname##_poll,		\
		.release = maru_##_fname##_close	\
	},						\
}

GENERATE_ACCESSORS_DEV(tztv_frc)
GENERATE_ACCESSORS_DEV(tztv_tcon)
GENERATE_ACCESSORS_DEV(micom_cec)
GENERATE_ACCESSORS_DEV(micom_ar)
GENERATE_ACCESSORS_DEV(micom_bsensor)
GENERATE_ACCESSORS_DEV(kfactory)

struct dummy_device dummy_device_group[] = {
	__ATTRIBUTE_DUMMY_DEVICE(tztv_frc),
	__ATTRIBUTE_DUMMY_DEVICE(tztv_tcon),
	__ATTRIBUTE_HYPHEN_DUMMY_DEVICE(micom-cec, micom_cec),
	__ATTRIBUTE_HYPHEN_DUMMY_DEVICE(micom-ar, micom_ar),
	__ATTRIBUTE_HYPHEN_DUMMY_DEVICE(micom-bsensor, micom_bsensor),
	__ATTRIBUTE_DUMMY_DEVICE(kfactory),
};

static void remove_device(struct dummy_device *device)
{
	if (device == NULL) {
		return;
	}

	if (device->dev_device) {
		device_destroy(device->dev_class, device->dev);
		device->dev_device = NULL;
	}

	if (device->dev) {
		unregister_chrdev_region(device->dev, 1);
	}

	if (device->dev_class) {
		class_destroy(device->dev_class);
	}
}

static int create_device(struct dummy_device *device)
{
	int ret = 0;

	if (device == NULL) {
		maru_device_err("failed to create device: device == NULL \n");
		return ret;
	}

	ret = alloc_chrdev_region(&device->dev, 0, 1, device->name);
	if (ret < 0) {
		maru_device_err("%s alloc_chrdev_region failed.\n", device->name);
		return ret;
	}

	device->dev_class = class_create(THIS_MODULE, device->name);
	if (IS_ERR(device->dev_class)) {
		ret = PTR_ERR(device->dev_class);
		maru_device_err("create %s class failed.\n", device->name);
		remove_device(device);
		return ret;
	}

	cdev_init(&device->cdev, &device->fops);

	ret = cdev_add(&device->cdev, device->dev, 1);
	if (ret < 0) {
		maru_device_err("%s cdev_add failed\n", device->name);
		remove_device(device);
		return ret;
	}

	device->dev_device = device_create(device->dev_class, 0, device->dev, NULL, "%s", device->name);
	if (ret < 0) {
		maru_device_err("%s device_create failed\n", device->name);
		remove_device(device);
		return ret;
	}

	return 0;
}

static int __init maru_dummy_init(void)
{
	int ret = 0;
	int i = 0;
	int size = sizeof(dummy_device_group) / sizeof(dummy_device_group[0]);

	for (i = 0; i < size; i++) {
		ret = create_device(&dummy_device_group[i]);
		if (ret < 0)
			continue;
	}

	maru_device_info("dummy driver was initialized.\n");

	return 0;
}

static void __exit maru_dummy_exit(void)
{
	int i = 0;
	int size = sizeof(dummy_device_group) / sizeof(dummy_device_group[0]);

	for (i = 0; i < size; i++) {
		remove_device(&dummy_device_group[i]);
	}

	maru_device_info("dummy driver was exited.\n");
}

module_init(maru_dummy_init);
module_exit(maru_dummy_exit);