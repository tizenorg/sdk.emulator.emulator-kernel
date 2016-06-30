/*
 * MARU MICOM MSG
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

/* internal Release1 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/platform_device.h>

#define DRIVER_NAME                     "msg-wt61p807"
#define DEV_NAME                        "micom-msg"

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off maru micom msg debugging (default:off).");

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

/* no of retries */
#define ACK_RETRY_MAX                   4

/* static mutexes for micom msg device */
DEFINE_MUTEX(dev_msg_lock);

/* list of micom msg file operations prototypes. */
static int micom_msg_open(struct inode *inode, struct file *filp);
static int micom_msg_release(struct inode *inode, struct file *filp);
static long micom_msg_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg);
static ssize_t show_jack_ident(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t show_scart_lv_1(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t show_scart_lv_2(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t show_jack_ident_ready(struct device *dev,
		struct device_attribute *attr, char *buf);

/* file operations for micom msg device */
const struct file_operations wt61p807_msg_fops = {
	.owner          = THIS_MODULE,
	.open           = micom_msg_open,
	.unlocked_ioctl = micom_msg_ioctl,
	.release        = micom_msg_release,
};

struct wt61p807_msg_data {
	struct cdev *msg_dev;
	struct class *msg_class;
	struct device *msg_device;
	int micom_msg_major;
	int ref_count;

	int jack_ident;
	int jack_ident_ready;
	int scart_lv_1;
	int scart_lv_2;
};

struct wt61p807_msg_data *wt61p807_msg;

/* micom msg device specific data */
struct wt61p807_msg_data m_msg_dev;

/* micom msg cdev */
struct cdev wt61p807_msg_cdev;

static DEVICE_ATTR(jack_ident, S_IRUGO, show_jack_ident, NULL);
static DEVICE_ATTR(scart_lv_1, S_IRUGO, show_scart_lv_1, NULL);
static DEVICE_ATTR(scart_lv_2, S_IRUGO, show_scart_lv_2, NULL);
static DEVICE_ATTR(jack_ident_ready, S_IRUGO,
		show_jack_ident_ready, NULL);

/*
 *
 *   @fn        static int micom_msg_open(struct inode *inode, \
 *                              struct file *filp);
 *   @brief     opens micom msg device and returns file descriptor
 *   @details   opens micom msg device and increments m_msg_dev_p->ref_count.
 *
 *   @param     inode   pointer to device node's inode structure
 *              filp    pointer to device node file
 *
 *   @return    returns file descriptor if device is opened successfully
 */
static int micom_msg_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct wt61p807_msg_data *m_msg_dev_p = &m_msg_dev;

	/* acquire lock before opening device.*/
	mutex_lock(&dev_msg_lock);

	m_msg_dev_p->ref_count++;

	print_dbg(1, "MSG device is opened. ref_count[%d]\n",
			m_msg_dev_p->ref_count);

	/* Release lock */
	mutex_unlock(&dev_msg_lock);

	return ret;
}

/*
 *
 *   @fn        static int micom_msg_release(struct inode *inode, \
 *                              struct file *filp);
 *   @brief     closes micom msg device and returns status
 *   @details
 *
 *   @param     inode   pointer to device node's inode structure
 *              filp    pointer to device node file
 *
 *   @return    returns zero if device is closed
 */
static int micom_msg_release(struct inode *inode, struct file *filp)
{

	int ret = 0;
	struct wt61p807_msg_data *m_msg_dev_p = &m_msg_dev;

	/* acquire lock before closing device.*/
	mutex_lock(&dev_msg_lock);

	m_msg_dev_p->ref_count--;

	print_dbg(1, "MSG device is closed. ref_count[%d]\n",
			m_msg_dev_p->ref_count);

	/* Release lock*/
	mutex_unlock(&dev_msg_lock);

	return ret;
}

/*
 *
 *   @fn        static long micom_msg_ioctl(struct file *filp, \
 *                              unsigned int cmd, unsigned long arg);
 *   @brief     handles IOCTLs addressed to micom msg device and returns status
 *   @details   valid IOCTLs:
 *                      MICOM_MSG_IOCTL_SEND_MSG: Used to send messages
 *                      containing normal data to micom device. It expects
 *                      acknowledgement from the device.
 *                      MICOM_MSG_IOCTL_SEND_MSG_NO_ACK: Used to send messages
 *                      containing normal buffer data without expecting any
 *                      acknowledgement from micom msg device.
 *
 *   @param     filp    pointer to device node file
 *              cmd     IOCTL command.
 *              arg     argument to ioctl command (struct sdp_micom_usr_msg).
 *
 *   @return    returns status of IOCTL
 *                      -EINVAL: if null arg is passed from user.
 *                      -EFAULT: if copy_from_user() fails to copy
 *                      -ERANGE: if micom command sent from user exceeds the
 *                      defined max value (0xFF)
 *                      zero:   if suceess
 */
static long micom_msg_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	long status = 0;

	print_dbg(1, "MSG device ioctl \n");

	return status;
}

static ssize_t show_jack_ident(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	print_dbg(1, "\n");
	return snprintf(buf, sizeof(int), "%d", m_msg_dev.jack_ident);
}

static ssize_t show_scart_lv_1(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	print_dbg(1, "\n");
	return snprintf(buf, sizeof(int), "%d", m_msg_dev.scart_lv_1);
}

static ssize_t show_scart_lv_2(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	print_dbg(1, "\n");
	return snprintf(buf, sizeof(int), "%d", m_msg_dev.scart_lv_2);
}

static ssize_t show_jack_ident_ready(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	print_dbg(1, "\n");
	return snprintf(buf, sizeof(int), "%d", m_msg_dev.jack_ident_ready);
}

/* Device initialization routine */
static int __init micom_msg_init(void)
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
	cdev_init(&wt61p807_msg_cdev, &wt61p807_msg_fops);
	/* add cdev to device */
	ret = cdev_add(&wt61p807_msg_cdev, devid, 1);
	if (ret) {
		print_err("cdev_add failed with %d\n", ret);
		goto cdev_add_fail;
	}

	wt61p807_msg = &m_msg_dev;

	wt61p807_msg->msg_dev = &wt61p807_msg_cdev;
	wt61p807_msg->micom_msg_major = MAJOR(devid);
	wt61p807_msg->ref_count = 0;

	wt61p807_msg->msg_class = class_create(THIS_MODULE, DEV_NAME);
	if (IS_ERR(wt61p807_msg->msg_class)) {
		print_err("failed to create sys class\n");
	} else {
		wt61p807_msg->msg_device = device_create(
				wt61p807_msg->msg_class,
				NULL, devid, NULL, DEV_NAME);
		if (IS_ERR(wt61p807_msg->msg_device)) {
			print_err("failed to create sys device\n");
			class_destroy(wt61p807_msg->msg_class);
		}
	}

	ret = device_create_file(wt61p807_msg->msg_device,
			&dev_attr_jack_ident);
	if (ret) {
		print_err("failed to create sysfs files (ret = %d) \n", ret);
		goto dev_fail;
	}
	ret = device_create_file(wt61p807_msg->msg_device,
			&dev_attr_scart_lv_1);
	if (ret) {
		print_err("failed to create sysfs files (ret = %d) \n", ret);
		goto dev_fail;
	}
	ret = device_create_file(wt61p807_msg->msg_device,
			&dev_attr_scart_lv_2);
	if (ret) {
		print_err("failed to create sysfs files (ret = %d) \n", ret);
		goto dev_fail;
	}
	ret = device_create_file(wt61p807_msg->msg_device,
			&dev_attr_jack_ident_ready);
	if (ret) {
		print_err("failed to create sysfs files (ret = %d) \n", ret);
		goto dev_fail;
	}

	/* dynamic initialization of mutex for device */
	mutex_init(&dev_msg_lock);

	return ret;

dev_fail:
cdev_add_fail:
	unregister_chrdev_region(devid, 1);
chrdev_alloc_fail:
	return ret;
}

/* Device exit routine */
static void __exit micom_msg_exit(void)
{
	print_info("called \n");

	mutex_destroy(&dev_msg_lock);

	/* destroy micom msg sysfs device and class */
	if (wt61p807_msg->msg_device != NULL) {
		device_destroy(wt61p807_msg->msg_class,
				MKDEV(wt61p807_msg->micom_msg_major, 0));
	}
	if (wt61p807_msg->msg_class != NULL)
		class_destroy(wt61p807_msg->msg_class);

	unregister_chrdev_region(MKDEV(wt61p807_msg->micom_msg_major, 0), 1);
	return;
}

/* define module init/exit, license */
subsys_initcall(micom_msg_init);
module_exit(micom_msg_exit);

MODULE_DESCRIPTION("Micom driver interface for Normal buffer data");
MODULE_AUTHOR("Abhishek Jaiswal <abhishek1.j@samsung.com>");
MODULE_LICENSE("GPL");
