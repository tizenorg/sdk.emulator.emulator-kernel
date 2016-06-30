/*
 * MARU MICOM ISP
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 * Dongkyun Yun <dk77.yun@samsung.com>
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
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/cdev.h>

#define DRIVER_NAME "isp-wt61p807"
#define DEV_NAME    "micom-isp"

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off maru micom isp debugging (default:off).");

#define print_err(fmt, arg...) \
	printk(KERN_ERR "[%s](%s)[%s:%d]: " fmt, DEV_NAME, __func__, \
			current->comm, current->pid, ##arg);

#define print_warn(fmt, arg...) \
	printk(KERN_WARNING "[%s](%s)[%s:%d]: " fmt, DEV_NAME, __func__, \
			current->comm, current->pid, ##arg);

#define print_info(fmt, arg...) \
	printk(KERN_INFO "[%s](%s)[%s:%d]: " fmt, DEV_NAME, __func__, \
			current->comm, current->pid, ##arg);

#define print_dbg(level, fmt, arg...) \
	do { \
		if (debug >= (level)) { \
			printk(KERN_INFO "[%s](%s:%d)[%s:%d]: " fmt, DEV_NAME, \
					__func__, __LINE__, current->comm, current->pid, ##arg); \
		} \
	} while (0)

/* contains device information */
struct wt61p807_isp_data {
	struct cdev *isp_dev;
	struct class *isp_class;
	struct device *isp_device;
	int micom_isp_major;
	int ref_count;
};

struct wt61p807_isp_data *wt61p807_isp;

/* static mutexes for micom isp driver */
DEFINE_MUTEX(isp_dev_lock);

/* global structures for wt61p807_isp_data and wt61p807_isp_cdev as those must
 * be accessible from other functions ie.open, release etc.
 */
struct wt61p807_isp_data m_isp_dev;
static struct cdev wt61p807_isp_cdev;

/* micom isp file operations */
static int micom_isp_open(struct inode *inode, struct file *filp);
static int micom_isp_release(struct inode *inode, struct file *filp);
static long micom_isp_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg);

/* file operations for micom isp device */
const struct file_operations wt61p807_isp_fops = {
	.owner = THIS_MODULE,
	.open = micom_isp_open,
	.unlocked_ioctl = micom_isp_ioctl,
	.release = micom_isp_release,
};

/*
 *
 *   @fn        static int micom_isp_open(struct inode *inode, \
 *                              struct file *filp);
 *   @brief     opens micom isp device and returns file descriptor
 *   @details   opens micom isp device and increments m_isp_dev_p->ref_count by
 *              one.
 *
 *   @param     inode   pointer to device node's inode structure
 *              filp    pointer to device node file
 *
 *   @return    returns file descriptor if device is opened successfully
 */
static int micom_isp_open(struct inode *inode, struct file *filp)
{
	struct wt61p807_isp_data *wt61p807_isp = &m_isp_dev;

	/* acquire lock before setting is_open.*/
	mutex_lock(&isp_dev_lock);

	wt61p807_isp->ref_count++;
	print_dbg(1, "reference count : %d\n", wt61p807_isp->ref_count);

	/* Release lock*/
	mutex_unlock(&isp_dev_lock);

	return 0;
}

/*   @fn        static long micom_isp_ioctl(struct file *filp, \
 *                              unsigned int cmd, unsigned long arg);
 *   @brief     handles IOCTLs addressed to micom isp device and returns status
 *   @details   valid IOCTLs:
 *                      MICOM_MSG_IOCTL_SEND_MSG: Used to send messages
 *                      containing normal data to micom device. It expects
 *                      acknowledgement from the device.
 *                      MICOM_MSG_IOCTL_SEND_MSG_NO_ACK: Used to send messages
 *                      containing normal buffer data without expecting any
 *                      acknowledgement from micom isp device.
 *
 *   @param     filp    pointer to device node file
 *              cmd     IOCTL command.
 *              arg     argument to ioctl command (struct sdp_micom_usr_isp).
 *
 *   @return    returns status of IOCTL
 *                      -EINVAL: if null arg is passed from user.
 *                      -EFAULT: if copy_from_user() fails to copy
 *                      -ERANGE: if micom command sent from user exceeds the
 *                      defined max value (0xFF)
 *                      zero:   if suceess
 */
static long micom_isp_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	long status = 0;

	print_dbg(1, "device ioctl \n");

	return status;
}

/*
 *
 *   @fn        static int micom_isp_release(struct inode *inode, \
 *                              struct file *filp);
 *   @brief     closes micom isp device and returns status
 *   @details
 *
 *   @param     inode   pointer to device node's inode structure
 *              filp    pointer to device node file
 *
 *   @return    returns zero if device is closed
 */
static int micom_isp_release(struct inode *inode, struct file *filp)
{

	struct wt61p807_isp_data *wt61p807_isp = &m_isp_dev;

	/* acquire lock before setting is_open.*/
	mutex_lock(&isp_dev_lock);

	wt61p807_isp->ref_count--;
	print_dbg(1 , "reference count : %d\n", wt61p807_isp->ref_count);

	/* Release lock*/
	mutex_unlock(&isp_dev_lock);

	return 0;
}

/* Device initialization routine */
static int __init micom_isp_init(void)
{
	dev_t devid = 0;
	int ret = -1;

	print_info("called \n");

	/* allocate char device region */
	ret = alloc_chrdev_region(&devid, 0, 1, DRIVER_NAME);
	if (ret) {
		print_err("alloc_chrdev_region failed with %d\n", ret);
		goto chrdev_alloc_fail;
	}

	/* initialize associated cdev and attach the file_operations */
	cdev_init(&wt61p807_isp_cdev, &wt61p807_isp_fops);
	/* add cdev to device */
	ret = cdev_add(&wt61p807_isp_cdev, devid, 1);
	if (ret) {
		print_err("cdev_add failed with %d\n", ret);
		goto cdev_add_fail;
	}

	wt61p807_isp = &m_isp_dev;

	wt61p807_isp->isp_dev = &wt61p807_isp_cdev;
	wt61p807_isp->micom_isp_major = MAJOR(devid);
	wt61p807_isp->ref_count = 0;

	wt61p807_isp->isp_class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(wt61p807_isp->isp_class)) {
		print_err("failed to create sys class\n");
	} else {
		wt61p807_isp->isp_device = device_create(
				wt61p807_isp->isp_class,
				NULL, devid, NULL, DEV_NAME);
		if (IS_ERR(wt61p807_isp->isp_device)) {
			print_err("failed to create sys device\n");
			class_destroy(wt61p807_isp->isp_class);
		}
	}

	/* dynamic initialization of mutex for device */
	mutex_init(&isp_dev_lock);

	return ret;

	/* cleaning up due to failure. */
cdev_add_fail:
	unregister_chrdev_region(devid, 1);
chrdev_alloc_fail:
	return ret;
}

/* Device exit routine */
static void __exit micom_isp_exit(void)
{
	print_info("called \n");

	mutex_destroy(&isp_dev_lock);

	/* destroy micom isp sysfs device and class */
	if (wt61p807_isp->isp_device != NULL) {
		device_destroy(wt61p807_isp->isp_class,
				MKDEV(wt61p807_isp->micom_isp_major, 0));
	}
	if (wt61p807_isp->isp_class != NULL)
		class_destroy(wt61p807_isp->isp_class);

	unregister_chrdev_region(MKDEV(m_isp_dev.micom_isp_major, 0), 1);
	return;
}

/* define module init/exit, license */
subsys_initcall(micom_isp_init);
module_exit(micom_isp_exit);

MODULE_DESCRIPTION("Micom driver interface for ISP data");
MODULE_AUTHOR("Abhishek Jaiswal <abhishek1.j@samsung.com>");
MODULE_LICENSE("GPL");
