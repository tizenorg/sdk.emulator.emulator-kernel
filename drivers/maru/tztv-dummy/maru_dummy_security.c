/*
 * MARU security driver
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 * Hyunjin Lee <hyunjin816.lee@samsung.com>
 * SangHo Park <sangho1206.park@samsung.com>
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
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <linux/interrupt.h>

#define SECURITY_DEV_NAME "security"

static unsigned security_debug = 0;
static struct class *security_class;
static struct cdev security_cdev;
dev_t security_dev;

module_param(security_debug, int, 0644);
MODULE_PARM_DESC(security_debug, "Turn on/off maru security debugging (default:off).");

#define maru_security_err(fmt, arg...) \
	printk(KERN_ERR "[ERR]maru_security[%s]: " fmt, __func__, ##arg)

#define maru_security_warn(fmt, arg...) \
	printk(KERN_WARNING "[WARN]maru_security[%s]: " fmt, __func__, ##arg)

#define maru_security_info(fmt, arg...) \
	printk(KERN_INFO "[INFO]maru_security[%s]: " fmt, __func__, ##arg)

#define maru_security_dbg(level, fmt, arg...) \
	do { \
		if (security_debug >= (level)) { \
			printk(KERN_ERR "[DBG]maru_security[%s]: " fmt, \
							__func__, ##arg); \
		} \
	} while (0)
#define SECURITY_G_SEQNUM _IOR('S', 0, unsigned int)

struct security_information {
	unsigned int seq_num;
};

/* -----------------------------------------------------------------
	file operations
   ------------------------------------------------------------------*/
static int maru_security_open(struct inode *inode, struct file *file)
{
	int ret = 0;
	struct security_information *sec_info;

	maru_security_dbg(5, "[security]enter.\n");

	sec_info = kzalloc(sizeof(struct security_information), GFP_KERNEL);
	if (!sec_info) {
		maru_security_err("kzalloc() failed\n");
		ret = -ENOMEM;
		goto enomem;
	}

	sec_info->seq_num = 0;
	file->private_data = sec_info;

enomem:
	return ret;
}

static long maru_security_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct security_information *sec_info = file->private_data;
	int __user *data = (int __user *)arg;

	switch (cmd) {
	case SECURITY_G_SEQNUM:
		ret = copy_to_user(data, &sec_info->seq_num, sizeof(unsigned int));
		if (ret) {
			ret = -EFAULT;
			break;
		}
		sec_info->seq_num++;
		break;
	default:
		maru_security_info("[security] unsupported command : %08x.\n", cmd);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int maru_security_close(struct inode *inode, struct file *file)
{
	struct security_information *sec_info = file->private_data;

	maru_security_dbg(5, "[security]enter.\n");

	if (sec_info) {
		kfree(sec_info);
		sec_info = NULL;
	}

	return 0;
}

static const struct file_operations maru_security_fops = {
	.open = maru_security_open,
	.unlocked_ioctl = maru_security_ioctl,
	.release = maru_security_close,
};

/* -----------------------------------------------------------------
	Initialization
   ------------------------------------------------------------------*/
static int __init maru_security_init(void)
{
	int ret = 0;

	/* allocate character device */
	ret = alloc_chrdev_region(&security_dev, 0, 1, SECURITY_DEV_NAME);
	if (ret < 0) {
		maru_security_err("security alloc_chrdev_region failed.\n");
		goto alloc_chrdev_region_err;
	}

	/* create class */
	security_class = class_create(THIS_MODULE, SECURITY_DEV_NAME);
	if (IS_ERR(security_class)) {
		ret = PTR_ERR(security_class);
		maru_security_err("create security class failed.\n");
		goto create_class_err;
	}

	/* character device initialize */
	cdev_init(&security_cdev, &maru_security_fops);

	ret = cdev_add(&security_cdev, security_dev, 1);
	if (ret < 0) {
		maru_security_err("security cdev_add failed\n");
		goto cdev_add_err;
	}

	/* create device */
	device_create(security_class, 0, security_dev, NULL, "%s", SECURITY_DEV_NAME);

	maru_security_info("security driver was registerd.\n");

	return ret;

cdev_add_err:
	class_destroy(security_class);
create_class_err:
	unregister_chrdev_region(security_dev, 1);
alloc_chrdev_region_err:
	return ret;
}

static void __exit maru_security_exit(void)
{
	cdev_del(&security_cdev);
	class_destroy(security_class);
	unregister_chrdev_region(security_dev, 1);

	maru_security_info("security driver was exited.\n");
}

module_init(maru_security_init);
module_exit(maru_security_exit);