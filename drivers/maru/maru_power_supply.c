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

static struct class *mtd_class;
static struct device* mtd_device;

static int capacity = 100;
static int charge_full = 1;
static int charge_now = 0;

//#define DEBUG_MARU_POWER_SUPPLY

static ssize_t show_capacity(struct device *dev, struct device_attribute *attr, char *buf) 
{
#ifdef DEBUG_MARU_POWER_SUPPLY
	printk("[%s] \n", __FUNCTION__);
#endif
	return snprintf(buf, PAGE_SIZE, "%d", capacity);
}

static ssize_t store_capacity(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) 
{
#ifdef DEBUG_MARU_POWER_SUPPLY
	printk("[%s] \n", __FUNCTION__);
#endif
	sscanf(buf, "%d", &capacity);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_charge_full(struct device *dev, struct device_attribute *attr, char *buf) 
{
#ifdef DEBUG_MARU_POWER_SUPPLY
	printk("[%s] \n", __FUNCTION__);
#endif
	return snprintf(buf, PAGE_SIZE, "%d", charge_full);
}

static ssize_t store_charge_full(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) 
{
#ifdef DEBUG_MARU_POWER_SUPPLY
	printk("[%s] \n", __FUNCTION__);
#endif
	sscanf(buf, "%d", &charge_full);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_charge_now(struct device *dev, struct device_attribute *attr, char *buf) 
{
#ifdef DEBUG_MARU_POWER_SUPPLY
	printk("[%s] \n", __FUNCTION__);
#endif
	return snprintf(buf, PAGE_SIZE, "%d", charge_now);
}

static ssize_t store_charge_now(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) 
{
#ifdef DEBUG_MARU_POWER_SUPPLY
	printk("[%s] \n", __FUNCTION__);
#endif
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


