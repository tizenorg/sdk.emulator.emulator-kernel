/*
 * MARU EEPROM
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 * Dongkyun Yun <dk77.yun@samsung.com>
 * Munkyu Im <munkyu.im@samsung.com>
 * Sangho Park <sangho.p@samsung.com>
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
 * Boston, MA  02110-1301, USA.
 *
 * Contributors:
 * - S-Core Co., Ltd
 *
 */

#include <asm/current.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/version.h>
#include <linux/moduleparam.h>
#include <linux/cdev.h>
#include <linux/file.h>
#include "maru_eeprom.h"
#include <linux/vmalloc.h>

//#define EEP_DEBUG

#ifdef EEP_DEBUG
#define eep_log(fmt, arg...) \
	printk(KERN_INFO "[%s](%s:%d)[%s:%d]: " fmt, EEPROM_DEV_NAME, \
			__func__, __LINE__, current->comm, current->pid, ##arg);
#else
#define eep_log(fmt, arg...)
#endif

#define eep_err(fmt, arg...) \
	printk(KERN_ERR "[%s](%s)[%s:%d]: " fmt, EEPROM_DEV_NAME, __func__, \
			current->comm, current->pid, ##arg);
#define eep_warn(fmt, arg...) \
	printk(KERN_WARNING "[%s](%s)[%s:%d]: " fmt, EEPROM_DEV_NAME, __func__, \
			current->comm, current->pid, ##arg);
#define eep_info(fmt, arg...) \
	printk(KERN_INFO "[%s](%s:%d)[%s:%d]: " fmt, EEPROM_DEV_NAME, __func__, __LINE__,  \
			current->comm, current->pid, ##arg);



#define FILENAME	"/boot/eeprom"

#define true 1
#define false 0

/* device protocol */
enum req_cmd {
	req_eeprom_get = 1,
	req_eeprom_set,
	req_eeprom_reply
};

/* eeprom specific macros */
#define EEP_BLOCK_SIZE  4096
#define EEP_BLOCK_COUNT  16
#define EEPROM_MAX_SIZE (EEP_BLOCK_SIZE * EEP_BLOCK_COUNT)
#define MAX_ATTR_BYTES 6

/* 10 milliseconds of delay is required after each i2c write operation */
//#define DELAY 10000

/* upper/lower 8-bit word address macros */
#define HI_BYTE(x) (((x) >> 8) & (0x00FF))
#define LO_BYTE(x) ((x) & (0x00FF))

/* define global mutex statically ***/
static DEFINE_MUTEX(eep_mutex);

/* global variables for this module ***/
static int counter;
static const unsigned int eeprom_size = EEPROM_MAX_SIZE;
static int eeprom_dev_major;
//static struct i2c_client *eep_client;

/* write protection for eeprom */
static int g_protect;

static struct file *g_file;

static ssize_t eeprom_write_file(const unsigned char __user *buf, int len, loff_t pos)
{
	ssize_t ret = 0;
	mm_segment_t old_fs;

	if (g_file == NULL)
		return -ENODEV;

	if (g_protect) {
		eep_info("memory is protected.\n");
		return ret;
	}
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	ret = vfs_write(g_file, buf, len, &pos);
	if (ret != len) {
		eep_warn("len mismatch len: %d, ret: %d, pos: %lld\n", ret, len, pos);
	}
	set_fs(old_fs);

	return ret;
}

static int eeprom_init_file(void)
{
	unsigned int i;
	loff_t pos = 0;
	int ret = 0;
	int reminder_pos = 0;
	unsigned char *buf = vmalloc(EEP_BLOCK_SIZE);
	if (!buf) {
		eep_warn("memory not allocated for buffer.\n");
		ret = -ENOMEM;
		goto out_ext;
	}
	memset(buf, 0xff, EEP_BLOCK_SIZE);

	for (i = 0; i < EEP_BLOCK_COUNT; i++) {
		ret = eeprom_write_file(buf, EEP_BLOCK_SIZE, pos);
		if (ret < 0) {
			eep_warn("failed to initialize. ret: %d\n", ret);
			goto out_ext;
		} else if (ret < EEP_BLOCK_SIZE) {
			eep_info("write reminder.\n");
			reminder_pos = ret;
			do {
				ret = eeprom_write_file(buf, EEP_BLOCK_SIZE - reminder_pos, pos + reminder_pos);
				if (ret <= 0) {
					eep_warn("failed to write reminder. ret: %d\n", ret);
					goto out_ext;
				}
				reminder_pos += ret;
			} while (reminder_pos != EEP_BLOCK_SIZE);
		}
		pos += EEP_BLOCK_SIZE;
	}
	ret = pos;
	vfree(buf);
	eep_info("file initialized: %s\n", FILENAME);

out_ext:
	return ret;
}

static int eeprom_open_file(void)
{
	struct file *file;
	eep_log("eeprom_open_file\n");

	file = filp_open(FILENAME, O_CREAT|O_RDWR|O_EXCL|O_SYNC, 0666);
	if (IS_ERR(file)) {
		eep_info("file already exists: %s\n", FILENAME);
		file = filp_open(FILENAME, O_CREAT|O_RDWR|O_SYNC, 0666);
		if (IS_ERR(file)) {
			eep_warn("filp_open failed.\n");
			return -ENOENT;
		}
		g_file = file;
		return 0;
	}
	g_file = file;

	return eeprom_init_file();
}

static ssize_t eeprom_read_file(unsigned char __user *buf, int len, loff_t pos)
{
	ssize_t ret = 0;
	mm_segment_t old_fs;

	if (g_file == NULL)
		return -ENODEV;

	eep_log("pos(%lld) len(%d) \n", pos, len);

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	ret = vfs_read(g_file, buf, len, &pos);
	if (ret != len) {
		eep_warn("len mismatch len: %d, ret: %d, pos: %lld\n", ret, len, pos);
	}
	set_fs(old_fs);

	return ret;
}

static int eeprom_usr_write(const char __user *buf, size_t count, unsigned long addr)
{
	int ret = -EFAULT;

	eep_log("eep write.\n");
	if (!buf) {
		eep_err("Invalid User buffer.\n");
	} else if (count > 0) {
		if (addr + count > eeprom_size) {
			eep_err("overflow!!.\n");
			goto out_err;
		}
		ret = eeprom_write_file(buf, count, addr);
		if (ret < 0) {
			eep_warn("Write failed.\n");
		}
	} else {
		eep_warn("inavalid count.\n");
	}

out_err:
	return ret;
}

static ssize_t eeprom_usr_read(char __user *buf, size_t count, unsigned long addr)
{
	int ret = -EFAULT;

	eep_log("eep read.\n");
	if (!buf) {
		eep_err("User buf has no memory allocation.\n");
	} else if (count > 0) {
		if (count + addr > eeprom_size) {
			eep_err("overflow!!.\n");
			goto out_err;
		}
		ret = eeprom_read_file(buf, count, addr);
		if (ret < 0) {
			eep_warn("read failed.\n");
		}
	} else {
		eep_warn("invalid count.\n");
	}

out_err:
	return ret;
}




/* for using sysfs. (class, device, device attribute) */

/** @brief This function inhibits the write operation on eeprom.
 *
 * @param [in] protect value(0 or 1).
 * 1 - To protect memory.
 * 0 - To unprotect memory.
 * @return On success Returns 0.
 *         On failure Returns negative error number.
 */
int eep_set_wp(int protect)
{
	int ret = 0;

	if (protect != 0 && protect != 1) {
		eep_warn("invalid input.\n");
		return -EINVAL;
	}

	g_protect = protect;
	eep_log("eeprom => %d (1:locked, 0:unlocked)\n", g_protect);

	return ret;
}
EXPORT_SYMBOL(eep_set_wp);

/**
 * @brief Returns the status of eeprom write protection.
 *
 * @param [in] void It takes no arguments.
 *
 * @return On success Returns 0.
 *         On failure Returns negative error number.
 */
int eep_get_wp()
{
	int ret = 0;

	ret = g_protect;
	eep_log("eeprom => %d (1:locked, 0:unlocked)\n", g_protect);

	return ret;
}
EXPORT_SYMBOL(eep_get_wp);

/**
 * @brief This function resets the whole eeprom chip.
 *
 * @parms [in] void It takes no arguments.
 *
 * @return On success Returns 0.
 *         On failure Returns negative error number.
 */
static int eep_reset(void)
{
	return eeprom_init_file();
}

/**
 * @brief This function opens the eeprom device.
 *
 * @param inode inode.
 * @param [in] fp File pointer points to the file descriptor.
 *
 * @return On success Returns 0.
 *         On failure Returns negative error number.
 */
int eep_open(struct inode *inode, struct file *fp)
{
	int ret = 0;

	mutex_lock(&eep_mutex);
	if (counter == 0) {
		eeprom_open_file();
	} else if (g_file == NULL) {
		eep_warn("g_file is NULL\n");
		ret = -ENODEV;
		goto out;
	} else {
		//Increase reference count of g_file
		atomic_long_inc_not_zero(&g_file->f_count);
	}

	counter++;
	eep_log("Open: #:%d\n" , counter);
	//eep_log("major number=%d, minor number=%d \n", imajor(inode), iminor(inode));
out:
	mutex_unlock(&eep_mutex);

	return ret;
}

/**
 * @brief This function undo the all open call operations.
 *
 * @param inode inode.
 * @param [in] fp File pointer points to the file descriptor.
 *
 * @return On success Returns 0.
 *         On failure Returns negative error number.
 */
int eep_close(struct inode *inode, struct file *fp)
{
	int ret = 0;

	eep_log("close.\n");

	mutex_lock(&eep_mutex);
	BUG_ON(g_file == NULL);
	fput(g_file);
	counter--;
	mutex_unlock(&eep_mutex);
	return ret;
}

/**
 * @brief The lseek method is used to change the current read/write position
 *        in a file
 *
 * @param [in] fp File pointer points to the file descriptor.
 * @param [in] offset Offset.
 * @param [in] origin Origin.
 *
 * @return On success The new position(resulting offset) is returned.
 *         On failure Returns negative error number.
 */
loff_t eep_lseek(struct file *fp, loff_t offset, int origin)
{
	loff_t current_offset = 0;

	eep_log("lseek.\n");

	mutex_lock(&eep_mutex);

	switch (origin) {
	case SEEK_SET:
		current_offset = offset;
		break;
	case SEEK_CUR:
		current_offset = fp->f_pos + offset;
		break;
	case SEEK_END:
		current_offset = eeprom_size - offset;
		break;
	default:
		break;
	}
	if (current_offset >= eeprom_size) {
		eep_err("offset overflow!\n");
		current_offset = eeprom_size - 1;
	} else if (current_offset < 0) {
		eep_err("offset underflow!\n");
		current_offset = 0;
	}

	fp->f_pos = current_offset;

	mutex_unlock(&eep_mutex);
	return current_offset;
}

/**
 * @brief This function reads the data from eeprom device.
 *
 * @param [in] fp File pointer points to the file descriptor.
 * @param [in] buf Pointer to the user space buffer.
 * @param [in] count Number of bytes application intend to read.
 * @param [in/out] pos Current file offset position.
 *
 * @return On success Returns count(number of read bytes).
 *         On failure Returns negative error number.
 */
ssize_t eep_read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	int ret = 0;

	mutex_lock(&eep_mutex);
	ret = eeprom_usr_read(buf, count, fp->f_pos);
	if (ret < 0) {
		eep_warn("eep_read failed. ret: %d", ret);
	}
	mutex_unlock(&eep_mutex);

	return ret;
}

/**
 * @brief This function writes the data to eeprom device.
 *
 * @param [in] fp File pointer points to the file descriptor.
 * @param [in] buf Pointer to the user space buffer.
 * @param [in] count Number of bytes application intend to write.
 * @param [in/out] pos Current file offset position.
 *
 * @return On success Returns count(the number of bytes successfully written).
 *         On failure Returns negative error number.
 */
ssize_t eep_write(struct file *fp, const char __user *buf,
		size_t count, loff_t *pos)
{
	int ret = 0;

	mutex_lock(&eep_mutex);
	ret = eeprom_usr_write(buf, count, fp->f_pos);
	if (ret < 0) {
		eep_warn("eep_write failed. ret: %d", ret);
	}
	mutex_unlock(&eep_mutex);

	return ret;
}


/**
 * @brief This function performs control operations on eeprom.
 *
 * @param [in] fp File pointer points to the file descriptor.
 * @param [in] cmd Request command.
 * @param [in/out] args The arguments based on request command.
 *
 * @return On Success Returns zero.
 *         On failure Returns negative error number.
 */
long eep_ioctl(struct file *fp, unsigned int cmd, unsigned long args)
{
	int protect = 0, size;
	long ret = 0;

	eep_log("ioctl.\n");

	/*verify args*/
	if (_IOC_TYPE(cmd) != EEPROM_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > EEPROM_MAX_CMDS)
		return -ENOTTY;
	if (_IOC_DIR(cmd) & _IOC_READ)
		if (!access_ok(VERIFY_WRITE, (void *)args, _IOC_SIZE(cmd)))
			return -EFAULT;
	if (_IOC_DIR(cmd) & _IOC_WRITE)
		if (!access_ok(VERIFY_READ, (void *)args, _IOC_SIZE(cmd)))
			return -EFAULT;
	mutex_lock(&eep_mutex);
	switch (cmd) {
	case EEPROM_RESET:
		eep_reset();
		break;
	case EEPROM_SET_WP:
		if (copy_from_user(&protect, (int *)args, sizeof(int))) {
			eep_warn("failed copy_from_user.\n");
			return -EFAULT;
		}
		eep_set_wp(protect);
		break;
	case EEPROM_GET_WP:
		protect = eep_get_wp();
		if (copy_to_user((int *)args, &protect, sizeof(int))) {
			eep_warn("failed copy_to_user.\n");
			ret =  -EFAULT;
		}
		break;
	case EEPROM_GET_SIZE:
		size = eeprom_size;
		if (copy_to_user((int *)args, &size, sizeof(int))) {
			eep_warn("failed copy_to_user.\n");
			ret = -EFAULT;
		}
		break;
	case EEPROM_WRITE_DATA:
	case EEPROM_READ_DATA:
		{
			struct eeprom_io_pkt *pkt = (struct eeprom_io_pkt *)args;
			if (cmd == EEPROM_WRITE_DATA)
				ret = eeprom_usr_write(pkt->wbuf, pkt->size, pkt->addr);
			else
				ret = eeprom_usr_read(pkt->rbuf, pkt->size, pkt->addr);
			if (ret < 0) {
				eep_warn("failed handle eeprom data: %ld", ret);
			}
		}
		break;
	default:
		eep_warn("invalid cmd %d \n", cmd);
		ret = -EFAULT;
		break;
	}
	mutex_unlock(&eep_mutex);
	return ret;
}

/**
 * @brief This function will be called when the user read from sysfs.
 *
 * @param [in] dev device.
 * @param [in] attr device attributes.
 * @param [in/out] buf buffer.
 *
 * @return On success Returns maxlen characters pointed to by buf.
 *         On failure Returns negative error number.
 */
static ssize_t nvram_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	eep_log("called.\n");

	mutex_lock(&eep_mutex);
	snprintf(buf, sizeof(int), "%d", eeprom_size);
	mutex_unlock(&eep_mutex);
	return strnlen(buf, MAX_ATTR_BYTES);
}

/* for using sysfs. (class, device, device attribute) */
static struct class *eep_class;
static struct device *eep_dev;
static DEVICE_ATTR_RO(nvram_size);

static const struct of_device_id eepdev_of_match[] = {
	{ .compatible = "sii,s24c512c" },
	{},
};
MODULE_DEVICE_TABLE(of, eepdev_of_match);

/**
 * Devices are identified using device id
 * of the chip
 */
static const struct i2c_device_id eep_i2c_id[] = {
	{ "s24c512c", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, eep_i2c_id);

/* device number */
dev_t eeprom_dev_num;

/* function pointers for file operations */
static const struct file_operations eep_drv_fops = {
	.llseek = eep_lseek,
	.read = eep_read,
	.open = eep_open,
	.release = eep_close,
	.write = eep_write,
	.unlocked_ioctl = eep_ioctl,
	.owner = THIS_MODULE,
};


/* Device initialization routine */
static int __init eep_init(void)
{
	int res = 0;

	eep_info("init!!\n");

	counter = 0;
	eeprom_dev_major = 0;
	eeprom_dev_num = 0;
	eep_dev = NULL;
	eep_class = NULL;
	//eep_client = NULL;

	/* register device with file operation mappings
	 * for dynamic allocation of major number
	 */
	res = register_chrdev(eeprom_dev_major, EEPROM_DEV_NAME, &eep_drv_fops);
	if (res < 0) {
		eep_err("failed to get major number.\n");
		goto out;
	}
	/* if the allocation is dynamic ?*/
	if (res != 0)
		eeprom_dev_major = res;

	eeprom_dev_num = MKDEV(eeprom_dev_major, 0);

	/* create class. (/sys/class/eep_class) */
	eep_class = class_create(THIS_MODULE, "eep_class");
	if (IS_ERR(eep_class)) {
		res = PTR_ERR(eep_class);
		goto out_unreg_chrdev;
	}
	/* create class device. (/sys/class/eep_class/eeprom) */
	eep_dev = device_create(eep_class, NULL, eeprom_dev_num, NULL,
			"eeprom");

	if (IS_ERR(eep_dev)) {
		res = PTR_ERR(eep_dev);
		goto out_unreg_class;
	}
	/* create sysfs file. (/sys/class/eep_class/eeprom/nvram_size) */
	res = device_create_file(eep_dev, &dev_attr_nvram_size);
	if (res) {
		eep_err("failed to create sysfs.\n");
		goto out_unreg_device;
	}

	return res;

	//out_unreg_sysfs:
	//	device_remove_file(eep_dev, &dev_attr_nvram_size);
out_unreg_device:
	device_destroy(eep_class, eeprom_dev_num);
out_unreg_class:
	class_destroy(eep_class);
out_unreg_chrdev:
	unregister_chrdev(eeprom_dev_major, EEPROM_DEV_NAME);
out:
	return res;
}

/* Device exit routine */
static void __exit eep_exit(void)
{
	eep_info("Exit!!\n");

	/* remove sysfs file */
	device_remove_file(eep_dev, &dev_attr_nvram_size);
	/* remove class device */
	device_destroy(eep_class, eeprom_dev_num);
	/* remove class */
	class_destroy(eep_class);
	//i2c_del_driver(&eep_i2c_driver);
	/* unregister device */
	unregister_chrdev(eeprom_dev_major, EEPROM_DEV_NAME);

	/* for emulator */
}

/* define module init/exit, license */
subsys_initcall(eep_init);
module_exit(eep_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dronamraju Santosh Pavan Kumar, <dronamraj.k@samsung.com>");
