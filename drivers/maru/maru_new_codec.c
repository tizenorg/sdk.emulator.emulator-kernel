/*
 * Virtual Codec PCI device driver
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 *  Kitae Kim <kt920.kim@samsung.com>
 *  SeokYeon Hwang <syeon.hwang@samsung.com>
 *  YeongKyoon Lee <yeongkyoon.lee@samsung.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 *
 * Contributors:
 * - S-Core Co., Ltd
 *
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/slab.h>


MODULE_DESCRIPTION("Virtual New Codec Device Driver");
MODULE_AUTHOR("Kitae KIM <kt920.kim@samsung.com");
MODULE_LICENSE("GPL2");

#define DEVICE_NAME	 "newcodec"

/* vendor, device value for PCI.*/
#define PCI_VENDOR_ID_TIZEN_EMUL			0xC9B5
#define PCI_DEVICE_ID_VIRTUAL_NEW_CODEC		0x1040

#ifndef CODEC_DEBUG
#define CODEC_LOG(fmt, ...) \
	printk(KERN_DEBUG "[%s][%d]: " fmt, DEVICE_NAME, __LINE__, ##__VA_ARGS__)
#else
#define CODEC_LOG(fmt, ...)
#endif

#define CODEC_IRQ_TASK 0x1f
#define CODEC_DEVICE_MEM_COUNT 8

//#define TIME_CHECK
#ifdef TIME_CHECK
#include <linux/time.h>
#define NEWCODEC_TIME_CHECK \
{ \
	struct timeval now; \
	do_gettimeofday(&now); \
	printk(KERN_INFO "%ld.%06ld: irq handler.\n", (long)now.tv_sec, (long)now.tv_usec); \
}
#endif

struct codec_param {
	int32_t api_index;
	int32_t ctx_index;
	int32_t mem_offset;
	int32_t mem_type;
};

struct codec_mem_info {
	uint32_t index;
	uint32_t offset;
};

enum codec_io_cmd {
	CODEC_CMD_COPY_TO_DEVICE_MEM = 5,	// plugin and driver
	CODEC_CMD_COPY_FROM_DEVICE_MEM,
	CODEC_CMD_API_INDEX = 10,			// driver and device
	CODEC_CMD_CONTEXT_INDEX,
	CODEC_CMD_FILE_INDEX,
	CODEC_CMD_DEVICE_MEM_OFFSET,
	CODEC_CMD_GET_THREAD_STATE,
	CODEC_CMD_GET_QUEUE,
	CODEC_CMD_POP_WRITE_QUEUE,
	CODEC_CMD_RESET_AVCONTEXT,
	CODEC_CMD_GET_VERSION = 20,			// plugin, driver and device
	CODEC_CMD_GET_ELEMENT,
	CODEC_CMD_GET_CONTEXT_INDEX,
	CODEC_CMD_SECURE_MEMORY= 30,
	CODEC_CMD_RELEASE_MEMORY,
	CODEC_CMD_COPY_FROM_DEVICE_MEM2,
};

enum codec_api_index {
    CODEC_INIT = 0,
    CODEC_DECODE_VIDEO,
    CODEC_ENCODE_VIDEO,
    CODEC_DECODE_AUDIO,
    CODEC_ENCODE_AUDIO,
    CODEC_PICTURE_COPY,
    CODEC_DEINIT,
};

struct device_mem {
	uint32_t blk_id;
	uint32_t mem_offset;
	bool occupied;

	struct list_head entry;
};


struct newcodec_device {
	struct pci_dev *dev;

	/* I/O and Memory Region */
	unsigned int *ioaddr;

	resource_size_t io_start;
	resource_size_t io_size;
	resource_size_t mem_start;
	resource_size_t mem_size;

	/* task queue */
	struct list_head avail_memblk;
	struct list_head used_memblk;

	spinlock_t lock;

	int version;
};

static struct newcodec_device *newcodec;
static int context_flags[1024] = { 0, };

static DEFINE_MUTEX(critical_section);
static DEFINE_MUTEX(newcodec_blk_mutex);

static struct semaphore newcodec_buffer_mutex =
	__SEMAPHORE_INITIALIZER(newcodec_buffer_mutex, CODEC_DEVICE_MEM_COUNT);

static DECLARE_WAIT_QUEUE_HEAD(wait_queue);

// static void newcodec_add_task(struct list_head *entry, uint32_t file);

static struct workqueue_struct *newcodec_bh_workqueue;
static void newcodec_bh_func(struct work_struct *work);
static DECLARE_WORK(newcodec_bh_work, newcodec_bh_func);
static void newcodec_bh(struct newcodec_device *dev);

#define	ENTER_CRITICAL_SECTION	mutex_lock(&critical_section);
#define LEAVE_CRITICAL_SECTION	mutex_unlock(&critical_section);

static void newcodec_bh_func(struct work_struct *work)
{
	uint32_t value;

	CODEC_LOG("%s\n", __func__);
	do {
		value = readl(newcodec->ioaddr + CODEC_CMD_GET_QUEUE);
		CODEC_LOG("read a value from device %x.\n", value);
		if (value) {
//			newcodec_add_task(&newcodec->req_task, value);
            context_flags[value] = 1;
            wake_up_interruptible(&wait_queue);
		} else {
			CODEC_LOG("there is no available task\n");
		}
	} while (value);
}

static void newcodec_bh(struct newcodec_device *dev)
{
	CODEC_LOG("add bottom-half function to codec_workqueue\n");
	queue_work(newcodec_bh_workqueue, &newcodec_bh_work);
}

static int lock_buffer(void)
{
	int ret;
	ret = down_interruptible(&newcodec_buffer_mutex);

//	CODEC_LOG("lock buffer_mutex: %d\n", newcodec_buffer_mutex.count);
	return ret;
}

static void unlock_buffer(void)
{
    up(&newcodec_buffer_mutex);
//	CODEC_LOG("unlock buffer_mutex: %d\n", newcodec_buffer_mutex.count);
}

static void release_device_memory(uint32_t mem_offset)
{
//	struct device_mem_mgr *mem_mgr = NULL;
//	int index;

#if 0
	for (index = 0; index < CODEC_DEVICE_MEM_COUNT; index++) {
		mem_mgr = &newcodec->mem_mgr[index];
		if (mem_mgr->mem_offset == mem_offset) {
			mem_mgr->occupied = false;
			mem_mgr->context_id = 0;
			break;
		}
	}
#endif
	{
		struct device_mem *elem = NULL;
		struct list_head *pos, *temp;

//		printk(KERN_INFO "release the memory offset: 0x%x", mem_offset);
		mutex_lock(&newcodec_blk_mutex);

		if (!list_empty(&newcodec->used_memblk)) {
			list_for_each_safe(pos, temp, &newcodec->used_memblk) {
				elem = list_entry(pos, struct device_mem, entry);
				if (elem->mem_offset == (uint32_t)mem_offset) {
//					printk(KERN_INFO "move %p to avail_memblk, mem_offset: 0x%x", elem, mem_offset);

					elem->blk_id = 0;
					elem->occupied = false;
					list_move(&elem->entry, &newcodec->avail_memblk);

					unlock_buffer();
					break;
				}
			}
		} else {
			CODEC_LOG("there is no used memory block.\n");
		}

		mutex_unlock(&newcodec_blk_mutex);
	}
}


static int32_t secure_device_memory(uint32_t blk_id)
{
//	struct device_mem_mgr *mem_mgr = NULL;
//	int index, ret = -1;
	int ret = -1;

#if 0
	if (!newcodec->mem_mgr) {
		printk(KERN_ERR "Invalid access to mem_mgr variable.\n");
		return ret;
	}
#endif

//	CODEC_LOG("try to lock the buffer_mutex. %x, %d\n",
//			blk_id, newcodec_buffer_mutex.count);

	if (lock_buffer()) {
        // TODO: need some care...
    }

	{
		// check whether avail_memblk is empty.
		// move mem_blk to used_blk if it is not empty
		// otherwise, the current task will be waiting until a mem_blk is available.
		struct device_mem *elem = NULL;

		mutex_lock(&newcodec_blk_mutex);

		if (!list_empty(&newcodec->avail_memblk)) {
			elem =
				list_first_entry(&newcodec->avail_memblk, struct device_mem, entry);
			if (!elem) {
				printk(KERN_ERR "failed to get first entry from avail_memblk\n");
				return ret;
			}
			elem->blk_id = blk_id;
			elem->occupied = true;

//			printk(KERN_INFO "selected memblk: %p, offset: 0x%x", elem, elem->mem_offset);
//			printk(KERN_INFO "avail_memblk is not empty. 0x%x", elem->mem_offset);

			list_move_tail(&elem->entry, &newcodec->used_memblk);
			ret = elem->mem_offset;

			elem =
				list_first_entry(&newcodec->avail_memblk, struct device_mem, entry);
//			printk(KERN_INFO "head of avail_memblk %p", elem);

		} else {
			printk(KERN_ERR "[%s][%d]: the number of buffer mutex: %d\n",
					DEVICE_NAME, __LINE__, newcodec_buffer_mutex.count);
			printk(KERN_ERR "[%s][%d]: no available memory block\n",
					DEVICE_NAME, __LINE__);
		}

		mutex_unlock(&newcodec_blk_mutex);
	}

//	printk(KERN_ERR "secure_memory, mem_offset: 0x%x\n", ret);

#if 0
	for (index = 0; index < CODEC_DEVICE_MEM_COUNT; index++) {
		mem_mgr = &newcodec->mem_mgr[index];

	    CODEC_LOG("mem_mgr[%d] : %d, 0x%x\n", index, mem_mgr->occupied, mem_mgr->mem_offset);

		if (!mem_mgr->occupied) {
			mem_mgr->occupied = true;
			mem_mgr->context_id = file;

			ret = index;
            break;
		}
	}
#endif

#if 0
	if (ret == newcodec->mem_size) {
		printk(KERN_ERR "mem_offset overflow: 0x%x\n", ret);
	}
#endif

	return ret;
}

static long newcodec_ioctl(struct file *file,
			unsigned int cmd,
			unsigned long arg)
{
	long value = 0, ret = 0;

	switch (cmd) {
#if 0
	case CODEC_CMD_REMOVE_TASK_QUEUE:
    {
		uint32_t mem_offset;

		if (copy_from_user(&mem_offset, (void *)arg, sizeof(uint32_t))) {
			printk(KERN_ERR "ioctl: failed to copy data to user\n");
			ret = -EIO;
			break;
		}
		release_device_memory(mem_offset);
		break;
    }
#endif
	case CODEC_CMD_COPY_TO_DEVICE_MEM:
	{
		CODEC_LOG("copy data to device memory\n");
		value =
			secure_device_memory((uint32_t)file);
		if (value < 0) {
			CODEC_LOG(KERN_ERR "failed to get available memory\n");
		} else {
			if (copy_to_user((void *)arg, &value, sizeof(uint32_t))) {
				printk(KERN_ERR "ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}
	}
		break;

	case CODEC_CMD_COPY_FROM_DEVICE_MEM:
	{
		CODEC_LOG("copy data from device memory. %p\n", file);
		value =
			secure_device_memory((uint32_t)file);
		if (value < 0) {
			CODEC_LOG(KERN_ERR "failed to get available memory\n");
		} else {
			CODEC_LOG("send a request to pop data from device. %p\n", file);

			ENTER_CRITICAL_SECTION;
			writel((uint32_t)value,
					newcodec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((uint32_t)file,
					newcodec->ioaddr + CODEC_CMD_POP_WRITE_QUEUE);
			LEAVE_CRITICAL_SECTION;

			if (copy_to_user((void *)arg, &value, sizeof(uint32_t))) {
				printk(KERN_ERR "ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}
	}
		break;
#if 0
	case CODEC_CMD_RELEASE_DEVICE_MEM:
	{
		uint32_t mem_offset;

		if (copy_from_user(&mem_offset, (void *)arg, sizeof(uint32_t))) {
			printk(KERN_ERR "ioctl: failed to copy data to user\n");
			ret = -EIO;
			break;
		}

		release_device_memory(mem_offset);
	}
		break;
#endif
	case CODEC_CMD_GET_VERSION:
		CODEC_LOG("return codec device version: %d\n", newcodec->version);

		if (copy_to_user((void *)arg, &newcodec->version, sizeof(int))) {
			printk(KERN_ERR "ioctl: failed to copy data to user\n");
			ret = -EIO;
	    }
		break;
	case CODEC_CMD_GET_ELEMENT:
		CODEC_LOG("request a device to get codec element\n");

		ENTER_CRITICAL_SECTION;
		readl(newcodec->ioaddr + cmd);
		LEAVE_CRITICAL_SECTION;
		break;
	case CODEC_CMD_GET_CONTEXT_INDEX:
		CODEC_LOG("request a device to get an index of codec context \n");

		value = readl(newcodec->ioaddr + cmd);

		if (copy_to_user((void *)arg, &value, sizeof(int))) {
			printk(KERN_ERR "ioctl: failed to copy data to user\n");
			ret = -EIO;
	    }
		break;
    case CODEC_CMD_SECURE_MEMORY:
		value =
			secure_device_memory((uint32_t)file);
		if (value < 0) {
			CODEC_LOG(KERN_ERR "failed to get available memory\n");
		} else {
			if (copy_to_user((void *)arg, &value, sizeof(uint32_t))) {
				printk(KERN_ERR "ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}
        break;
    case CODEC_CMD_RELEASE_MEMORY:
    {
		uint32_t mem_offset;

		if (copy_from_user(&mem_offset, (void *)arg, sizeof(uint32_t))) {
			printk(KERN_ERR "ioctl: failed to copy data to user\n");
			ret = -EIO;
			break;
		}
		release_device_memory(mem_offset);
    }
        break;
    case CODEC_CMD_COPY_FROM_DEVICE_MEM2:
	{
		uint32_t mem_offset;

		if (copy_from_user(&mem_offset, (void *)arg, sizeof(uint32_t))) {
			printk(KERN_ERR "ioctl: failed to copy data to user\n");
			ret = -EIO;
			break;
		}

//		printk(KERN_INFO "copy data from device memory2. 0x%x\n", mem_offset);

		if (mem_offset == newcodec->mem_size) {
			printk(KERN_ERR "offset of device memory is overflow!! 0x%x\n", mem_offset);
		}
		// notify that codec device can copy data to memory region.
		CODEC_LOG("send a request to pop data from device. %p\n", file);

		ENTER_CRITICAL_SECTION;
		writel((uint32_t)mem_offset,
					newcodec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
		writel((uint32_t)file,
			newcodec->ioaddr + CODEC_CMD_POP_WRITE_QUEUE);
		LEAVE_CRITICAL_SECTION;
	}
		break;

#if 0
    case CODEC_CMD_COPY_FROM_DEVICE_MEM3:
	{
		uint32_t mem_offset;

		if (copy_from_user(&mem_offset, (void *)arg, sizeof(uint32_t))) {
			printk(KERN_ERR "ioctl: failed to copy data to user\n");
			ret = -EIO;
			break;
		}

//		printk(KERN_INFO "DEVICE_MEM3. memory_offset for decoded picture: 0x%x\n", mem_offset);
	}
		break;
#endif
	default:
		CODEC_LOG("no available command.");
		break;
	}

	return ret;
}

static ssize_t newcodec_read(struct file *file, char __user *buf,
							size_t count, loff_t *fops)
{
	CODEC_LOG("do nothing.\n");
	return 0;
}

/* Copy data between guest and host using mmap operation. */
static ssize_t newcodec_write(struct file *file, const char __user *buf, size_t count, loff_t *fops)
{
	struct codec_param param_info;
	int api_index;

	if (!newcodec) {
		printk(KERN_ERR "failed to get codec device info\n");
		return -EINVAL;
	}

	memset (&param_info, 0x00, sizeof(struct codec_param));
	if (copy_from_user(&param_info, buf, sizeof(struct codec_param))) {
		printk(KERN_ERR
			"failed to get codec parameter info from user\n");
		return -EIO;
    }

	CODEC_LOG("enter %s. %p\n", __func__, file);

	api_index = param_info.api_index;
	switch (api_index) {
	case CODEC_INIT:
		{
			int ctx_index;

			ENTER_CRITICAL_SECTION;
			writel((uint32_t)file,
					newcodec->ioaddr + CODEC_CMD_FILE_INDEX);
			writel((uint32_t)param_info.mem_offset,
					newcodec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
#if 1
			writel((int32_t)param_info.ctx_index,
					newcodec->ioaddr + CODEC_CMD_CONTEXT_INDEX);
			writel((int32_t)param_info.api_index,
					newcodec->ioaddr + CODEC_CMD_API_INDEX);
#endif
			LEAVE_CRITICAL_SECTION;

			release_device_memory(param_info.mem_offset);

            ctx_index = param_info.ctx_index;
			CODEC_LOG("context index: %d\n", ctx_index);

	        wait_event_interruptible(wait_queue, context_flags[ctx_index] != 0);
            context_flags[ctx_index] = 0;
		}
			break;
		case CODEC_DECODE_VIDEO... CODEC_ENCODE_AUDIO:
		{
            int ctx_index;

			ENTER_CRITICAL_SECTION;
			writel((uint32_t)file,
					newcodec->ioaddr + CODEC_CMD_FILE_INDEX);
			writel((uint32_t)param_info.mem_offset,
					newcodec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
#if 1
			writel((int32_t)param_info.ctx_index,
					newcodec->ioaddr + CODEC_CMD_CONTEXT_INDEX);
			writel((int32_t)param_info.api_index,
					newcodec->ioaddr + CODEC_CMD_API_INDEX);
#endif
			LEAVE_CRITICAL_SECTION;

			release_device_memory(param_info.mem_offset);

            ctx_index = param_info.ctx_index;
	        wait_event_interruptible(wait_queue, context_flags[ctx_index] != 0);
            context_flags[ctx_index] = 0;
		}
			break;

		case CODEC_PICTURE_COPY:
		{
			int ctx_index;

			ENTER_CRITICAL_SECTION;
			writel((uint32_t)file,
					newcodec->ioaddr + CODEC_CMD_FILE_INDEX);
			writel((uint32_t)param_info.mem_offset,
					newcodec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((int32_t)param_info.ctx_index,
					newcodec->ioaddr + CODEC_CMD_CONTEXT_INDEX);
			writel((int32_t)param_info.api_index,
					newcodec->ioaddr + CODEC_CMD_API_INDEX);
			LEAVE_CRITICAL_SECTION;

            ctx_index = param_info.ctx_index;
	        wait_event_interruptible(wait_queue, context_flags[ctx_index] != 0);
            context_flags[ctx_index] = 0;
		}
			break;

		case CODEC_DEINIT:
			ENTER_CRITICAL_SECTION;
			writel((uint32_t)file,
					newcodec->ioaddr + CODEC_CMD_FILE_INDEX);
//			writel((uint32_t)param_info.mem_offset,
//					newcodec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((int32_t)param_info.ctx_index,
					newcodec->ioaddr + CODEC_CMD_CONTEXT_INDEX);
			writel((int32_t)param_info.api_index,
					newcodec->ioaddr + CODEC_CMD_API_INDEX);
			LEAVE_CRITICAL_SECTION;
			break;
		default:
			printk(KERN_ERR "wrong api command: %d", api_index);
	}

	return 0;
}

static int newcodec_mmap(struct file *file, struct vm_area_struct *vm)
{
	unsigned long off;
	unsigned long phys_addr;
	unsigned long size;
	int ret = -1;

	size = vm->vm_end - vm->vm_start;
	if (size > newcodec->mem_size) {
		printk(KERN_ERR "over mapping size\n");
		return -EINVAL;
	}
	off = vm->vm_pgoff << PAGE_SHIFT;
	phys_addr = (PAGE_ALIGN(newcodec->mem_start) + off) >> PAGE_SHIFT;

	ret = remap_pfn_range(vm, vm->vm_start, phys_addr,
			size, vm->vm_page_prot);
	if (ret < 0) {
		printk(KERN_ERR "failed to remap page range\n");
		return -EAGAIN;
	}

	vm->vm_flags |= VM_IO;
	vm->vm_flags |= VM_RESERVED;

	return 0;
}

static irqreturn_t newcodec_irq_handler(int irq, void *dev_id)
{
	struct newcodec_device *dev = (struct newcodec_device *)dev_id;
	unsigned long flags = 0;
	int val = 0;

	val = readl(dev->ioaddr + CODEC_CMD_GET_THREAD_STATE);
	if (!(val & CODEC_IRQ_TASK)) {
		return IRQ_NONE;
	}

	spin_lock_irqsave(&dev->lock, flags);

	CODEC_LOG("handle an interrupt from codec device.\n");
	newcodec_bh(dev);

	spin_unlock_irqrestore(&dev->lock, flags);

	return IRQ_HANDLED;
}

static int newcodec_open(struct inode *inode, struct file *file)
{
	CODEC_LOG("open! struct file:%p\n", file);

	/* register interrupt handler */
	if (request_irq(newcodec->dev->irq, newcodec_irq_handler,
		IRQF_SHARED, DEVICE_NAME, newcodec)) {
		printk(KERN_ERR "failed to register irq handle\n");
		return -EBUSY;
	}

	try_module_get(THIS_MODULE);

	return 0;
}

static int newcodec_release(struct inode *inode, struct file *file)
{
	/* free irq */
	if (newcodec->dev->irq) {
		CODEC_LOG("free registered irq\n");
		free_irq(newcodec->dev->irq, newcodec);
	}

	CODEC_LOG("%s. file: %p\n", __func__, file);

	/* free resource */
	{
		struct device_mem *elem = NULL;
		struct list_head *pos, *temp;

		mutex_lock(&newcodec_blk_mutex);

		if (!list_empty(&newcodec->used_memblk)) {
			list_for_each_safe(pos, temp, &newcodec->used_memblk) {
				elem = list_entry(pos, struct device_mem, entry);
				if (elem->blk_id == (uint32_t)file) {
					CODEC_LOG("move element(%p) to available memory block.\n", elem);

					elem->blk_id = 0;
					elem->occupied = false;
					list_move(&elem->entry, &newcodec->avail_memblk);

					unlock_buffer();
				}
			}
		} else {
			CODEC_LOG("there is no used memory block.\n");
		}

		mutex_unlock(&newcodec_blk_mutex);
	}

	/* notify closing codec device of qemu. */
	if (file) {
		ENTER_CRITICAL_SECTION;
		writel((int32_t)file,
			newcodec->ioaddr + CODEC_CMD_RESET_AVCONTEXT);
		LEAVE_CRITICAL_SECTION;
	}

	module_put(THIS_MODULE);

	return 0;
}

/* define file opertion for CODEC */
const struct file_operations newcodec_fops = {
	.owner			 = THIS_MODULE,
	.read			 = newcodec_read,
	.write			 = newcodec_write,
	.unlocked_ioctl	 = newcodec_ioctl,
	.open			 = newcodec_open,
	.mmap			 = newcodec_mmap,
	.release		 = newcodec_release,
};

static struct miscdevice codec_dev = {
	.minor			= MISC_DYNAMIC_MINOR,
	.name			= DEVICE_NAME,
	.fops			= &newcodec_fops,
	.mode			= S_IRUGO | S_IWUGO,
};

static void newcodec_get_device_version(void)
{
	newcodec->version =
		readl(newcodec->ioaddr + CODEC_CMD_GET_VERSION);

	printk(KERN_INFO "codec device version: %d\n",
		newcodec->version);
}

static int __devinit newcodec_probe(struct pci_dev *pci_dev,
	const struct pci_device_id *pci_id)
{
	int ret = 0;

	printk(KERN_INFO "%s: driver is probed.\n", DEVICE_NAME);

	newcodec = kzalloc(sizeof(struct newcodec_device), GFP_KERNEL);
	if (!newcodec) {
		printk(KERN_ERR "Failed to allocate memory for codec.\n");
		return -ENOMEM;
	}

	newcodec->dev = pci_dev;

	INIT_LIST_HEAD(&newcodec->avail_memblk);
	INIT_LIST_HEAD(&newcodec->used_memblk);

	{
		struct device_mem *elem = NULL;
		int index;

		elem =
			kzalloc(sizeof(struct device_mem) * CODEC_DEVICE_MEM_COUNT, GFP_KERNEL);
		if (!elem) {
			printk(KERN_ERR "Falied to allocate memory!!\n");
			return -ENOMEM;
		}

		for (index = 0; index < CODEC_DEVICE_MEM_COUNT; index++) {
			elem[index].mem_offset = index * 0x200000;
			elem[index].occupied = false;
			list_add_tail(&elem[index].entry, &newcodec->avail_memblk);
		}
	}

	spin_lock_init(&newcodec->lock);

	if ((ret = pci_enable_device(pci_dev))) {
		printk(KERN_ERR "pci_enable_device failed\n");
		return ret;
	}
	pci_set_master(pci_dev);

	newcodec->mem_start = pci_resource_start(pci_dev, 0);
	newcodec->mem_size = pci_resource_len(pci_dev, 0);
	if (!newcodec->mem_start) {
		printk(KERN_ERR "pci_resource_start failed\n");
		pci_disable_device(pci_dev);
		return -ENODEV;
	}

	if (!request_mem_region(newcodec->mem_start,
				newcodec->mem_size,
				DEVICE_NAME)) {
		printk(KERN_ERR "request_mem_region failed\n");
		pci_disable_device(pci_dev);
		return -EINVAL;
	}

	newcodec->io_start = pci_resource_start(pci_dev, 1);
	newcodec->io_size = pci_resource_len(pci_dev, 1);
	if (!newcodec->io_start) {
		printk(KERN_ERR "pci_resource_start failed\n");
		release_mem_region(newcodec->mem_start, newcodec->mem_size);
		pci_disable_device(pci_dev);
		return -ENODEV;
	}

	if (!request_mem_region(newcodec->io_start,
				newcodec->io_size,
				DEVICE_NAME)) {
		printk(KERN_ERR "request_io_region failed\n");
		release_mem_region(newcodec->mem_start, newcodec->mem_size);
		pci_disable_device(pci_dev);
		return -EINVAL;
	}

	newcodec->ioaddr = ioremap_nocache(newcodec->io_start, newcodec->io_size);
	if (!newcodec->ioaddr) {
		printk(KERN_ERR "ioremap failed\n");
		release_mem_region(newcodec->io_start, newcodec->io_size);
		release_mem_region(newcodec->mem_start, newcodec->mem_size);
		pci_disable_device(pci_dev);
		return -EINVAL;
	}

	newcodec_get_device_version();

	if ((ret = misc_register(&codec_dev))) {
		printk(KERN_ERR "cannot register codec as misc\n");
		iounmap(newcodec->ioaddr);
		release_mem_region(newcodec->io_start, newcodec->io_size);
		release_mem_region(newcodec->mem_start, newcodec->mem_size);
		pci_disable_device(pci_dev);
		return ret;
	}

	return 0;
}

static void __devinit newcodec_remove(struct pci_dev *pci_dev)
{
	if (newcodec) {
		if (newcodec->ioaddr) {
			iounmap(newcodec->ioaddr);
			newcodec->ioaddr = NULL;
		}

		if (newcodec->io_start) {
			release_mem_region(newcodec->io_start,
					newcodec->io_size);
			newcodec->io_start = 0;
		}

		if (newcodec->mem_start) {
			release_mem_region(newcodec->mem_start,
					newcodec->mem_size);
			newcodec->mem_start = 0;
		}

		kfree(newcodec);
	}

	misc_deregister(&codec_dev);
	pci_disable_device(pci_dev);
}

static struct pci_device_id newcodec_pci_table[] __devinitdata = {
	{
		.vendor		= PCI_VENDOR_ID_TIZEN_EMUL,
		.device		= PCI_DEVICE_ID_VIRTUAL_NEW_CODEC,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	},
	{},
};
MODULE_DEVICE_TABLE(pci, newcodec_pci_table);

static struct pci_driver driver = {
	.name		= DEVICE_NAME,
	.id_table	= newcodec_pci_table,
	.probe		= newcodec_probe,
	.remove		= newcodec_remove,
};

static int __init newcodec_init(void)
{
	printk(KERN_INFO "%s: driver is initialized.\n", DEVICE_NAME);

	newcodec_bh_workqueue = create_workqueue ("newcodec");
	if (!newcodec_bh_workqueue) {
		printk(KERN_ERR "failed to allocate workqueue\n");
		return -ENOMEM;
	}

	return pci_register_driver(&driver);
}

static void __exit newcodec_exit(void)
{
	printk(KERN_INFO "device is finalized.\n");

	if (newcodec_bh_workqueue) {
		destroy_workqueue (newcodec_bh_workqueue);
		newcodec_bh_workqueue = NULL;
	}
	pci_unregister_driver(&driver);
}
module_init(newcodec_init);
module_exit(newcodec_exit);
