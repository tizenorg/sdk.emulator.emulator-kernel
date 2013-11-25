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

static struct class *mtd_class;
static struct device* mtd_device;

#define DEVICE_NAME				"power_supply"
#define FILE_PERMISSION			(S_IRUGO | S_IWUSR)
#define HALF_CAPACITY			50

//#define DEBUG_MARU_POWER_SUPPLY

#ifdef DEBUG_MARU_POWER_SUPPLY
#define DLOG(level, fmt, ...) \
	printk(level "maru_%s: " fmt, DEVICE_NAME, ##__VA_ARGS__)
#else
// do nothing
#define DLOG(level, fmt, ...)
#endif

static int capacity = HALF_CAPACITY;
static int charge_full = 0;
static int charge_now = 0;

static ssize_t show_capacity(struct device *dev, struct device_attribute *attr, char *buf)
{
	DLOG(KERN_INFO, "get capacity: %d\n", capacity);
	return snprintf(buf, PAGE_SIZE, "%d", capacity);
}

static ssize_t store_capacity(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &capacity);
	DLOG(KERN_INFO, "set capacity: %d\n", capacity);

	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_charge_full(struct device *dev, struct device_attribute *attr, char *buf)
{
	DLOG(KERN_INFO, "get capacity_full: %d\n", capacity);
	return snprintf(buf, PAGE_SIZE, "%d", charge_full);
}

static ssize_t store_charge_full(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &charge_full);
	DLOG(KERN_INFO, "set capacity_full: %d\n", capacity);

	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_charge_now(struct device *dev, struct device_attribute *attr, char *buf)
{
	DLOG(KERN_INFO, "get capacity_now: %d\n", capacity);
	return snprintf(buf, PAGE_SIZE, "%d", charge_now);
}

static ssize_t store_charge_now(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &charge_now);
	DLOG(KERN_INFO, "set capacity_now: %d\n", capacity);

	return strnlen(buf, PAGE_SIZE);
}

static struct device_attribute ps_device_attributes[] = {
	__ATTR(capacity, FILE_PERMISSION, show_capacity, store_capacity),
	__ATTR(charge_full, FILE_PERMISSION, show_charge_full, store_charge_full),
	__ATTR(charge_now, FILE_PERMISSION, show_charge_now, store_charge_now),
};

struct device new_device_dev;

static int __init maru_power_supply_sysfs_init(void)
{
	int err = 0, i = 0;

	printk(KERN_INFO "maru_%s: sysfs_init\n", DEVICE_NAME);

	mtd_class = class_create(THIS_MODULE, DEVICE_NAME);
	mtd_device = device_create(mtd_class, NULL, (dev_t)NULL, NULL, "battery");

	for (i = 0; i < 3; i++) {
		err = device_create_file(mtd_device, &ps_device_attributes[i]);
		if (err) {
			printk(KERN_ERR
				"maru_%s: failed to create power_supply files\n", DEVICE_NAME);
			break;
		}
	}

	if (i != 3) {
		while (--i >= 0) {
			device_remove_file(mtd_device, &ps_device_attributes[i]);
		}

		device_unregister(mtd_device);
	}

	return err;
}

static void __exit maru_power_supply_sysfs_exit(void)
{
	printk(KERN_INFO "maru_%s: sysfs_exit\n", DEVICE_NAME);
	class_destroy(mtd_class);
}

module_init(maru_power_supply_sysfs_init);
module_exit(maru_power_supply_sysfs_exit);

MODULE_LICENSE("GPL");
