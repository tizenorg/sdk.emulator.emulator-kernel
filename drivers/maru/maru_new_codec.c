/*
 * Virtual Codec PCI device driver
 *
 * Copyright (c) 2011-2012 Samsung Electronics Co., Ltd. All rights reserved.
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
// #include <linux/time.h>

#define DEVICE_NAME	 "newcodec"

MODULE_DESCRIPTION("Virtual New Codec Device Driver");
MODULE_AUTHOR("Kitae KIM <kt920.kim@samsung.com");
MODULE_LICENSE("GPL2");

#define CODEC_LOG(log_level, fmt, ...) \
	printk(log_level "[%s][%d]: " fmt, DEVICE_NAME, __LINE__, ##__VA_ARGS__)

#define NEWCODEC_IRQ_SHARED_TASK	0x1f
#define NEWCODEC_IRQ_FIXED_TASK		0x2f

#define NEWCODEC_FIXED_DEV_MEM_MAX	24 * 1024 * 1024
#define NEWCODEC_SHARED_DEV_MEM_MAX	 8 * 1024 * 1024

#if 0
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
	uint32_t mem_offset;
	uint32_t mem_type;
};

struct codec_mem_info {
	uint8_t type;
	uint32_t index;
	uint32_t offset;
};

enum codec_io_cmd {
	CODEC_CMD_GET_DEVICE_MEM_INFO = 0,	// user and driver
	CODEC_CMD_RELEASE_DEVICE_MEM,
	CODEC_CMD_ADD_TASK_QUEUE,
	CODEC_CMD_REMOVE_TASK_QUEUE,
	CODEC_CMD_COPY_FROM_DEVICE_MEM,
	CODEC_CMD_COPY_TO_DEVICE_MEM,
	CODEC_CMD_WAIT_TASK_QUEUE,
	CODEC_CMD_API_INDEX = 10,			// driver and device
	CODEC_CMD_CONTEXT_INDEX,
	CODEC_CMD_FILE_INDEX,
	CODEC_CMD_DEVICE_MEM_OFFSET,
	CODEC_CMD_DEVICE_MEM_TYPE,
	CODEC_CMD_GET_THREAD_STATE,
	CODEC_CMD_GET_SHARED_QUEUE,
	CODEC_CMD_GET_FIXED_QUEUE,
	CODEC_CMD_POP_WRITE_QUEUE,
	CODEC_CMD_RESET_CODEC_INFO,
	CODEC_CMD_GET_VERSION = 20,			// user, driver and device
	CODEC_CMD_GET_CONTEXT_INDEX,
};

enum codec_api_index {
    CODEC_ELEMENT_QUERY = 1,
    CODEC_INIT,
    CODEC_DECODE_VIDEO,
    CODEC_ENCODE_VIDEO,
    CODEC_DECODE_AUDIO,
    CODEC_ENCODE_AUDIO,
    CODEC_PICTURE_COPY,
    CODEC_DEINIT,
};

enum codec_mem_state {
	CODEC_MEM_UNLOCK = 0,
	CODEC_MEM_LOCK,
};

enum codec_mem_type {
	CODEC_FIXED_DEVICE_MEM = 0,
	CODEC_SHARED_DEVICE_MEM,
};

struct newcodec_task {
	int32_t id;
	struct list_head entry;
};

struct newcodec_mmapmgr {
	int32_t id;
	uint32_t offset;
};

struct newcodec_device {
	struct pci_dev *dev;

	/* I/O and Memory Region */
	unsigned int *ioaddr;

	resource_size_t io_start;
	resource_size_t io_size;
	resource_size_t mem_start;
	resource_size_t mem_size;

	/* Decoding/Encoding queue */
	struct list_head req_task;
	struct list_head old_task;
	struct list_head irq_task;
#if 0
	uint32_t irq_task[128];
	uint32_t irq_task_cnt;
	uint32_t irq_task_index;
#endif

	/* Device memory manager */
	struct newcodec_mmapmgr *mem_mgr;
	uint32_t mmapmgr_size;
	uint32_t mmapmgr_idx;
	uint32_t mmapmgr_offset;
	uint32_t used_mem_size;

	spinlock_t lock;
};

static struct newcodec_device *newcodec;
static DEFINE_MUTEX(newcodec_mutex);
static DEFINE_MUTEX(newcodec_bh_mutex);

static void newcodec_add_task(struct list_head *entry, int32_t file);
static struct workqueue_struct *newcodec_bh_workqueue;

static void newcodec_shared_bh_func(struct work_struct *work);
static void newcodec_fixed_bh_func(struct work_struct *work);
static DECLARE_WORK(newcodec_shared_bh_work, newcodec_shared_bh_func);
static DECLARE_WORK(newcodec_fixed_bh_work, newcodec_fixed_bh_func);
static void newcodec_shared_bh(struct newcodec_device *dev);
static void newcodec_fixed_bh(struct newcodec_device *dev);

static void newcodec_shared_bh_func(struct work_struct *work)
{
	int32_t value;

	CODEC_LOG(KERN_DEBUG, "shared_bh func.\n");
	do {
		value = readl(newcodec->ioaddr + CODEC_CMD_GET_SHARED_QUEUE);
		CODEC_LOG(KERN_DEBUG, "file value of head task: %x.\n", value);
		if (value) {
			newcodec_add_task(&newcodec->req_task, value);
		} else {
			CODEC_LOG(KERN_DEBUG, "there is no available task\n");
		}
	} while (value);
}

static void newcodec_shared_bh(struct newcodec_device *dev)
{
	CODEC_LOG(KERN_DEBUG, "request bottom-half operation.\n");
	queue_work(newcodec_bh_workqueue, &newcodec_shared_bh_work);
}

static void newcodec_fixed_bh_func(struct work_struct *work)
{
	uint32_t value;

	CODEC_LOG(KERN_DEBUG, "fixed_bh func.\n");

	do {
		value = readl(newcodec->ioaddr + CODEC_CMD_GET_FIXED_QUEUE);
		CODEC_LOG(KERN_DEBUG, "file value of head task: %x.\n", value);
		if (value) {
			newcodec_add_task(&newcodec->irq_task, value);
		} else {
			CODEC_LOG(KERN_DEBUG, "there is no available task\n");
		}
	} while (value);
}

static void newcodec_fixed_bh(struct newcodec_device *dev)
{
	CODEC_LOG(KERN_DEBUG, "request bottom-half operation.\n");
	queue_work(newcodec_bh_workqueue, &newcodec_fixed_bh_work);
}

static void newcodec_add_task(struct list_head *head, int32_t file)
{
	struct newcodec_task *temp = NULL;

	temp = kzalloc(sizeof(struct newcodec_task), GFP_KERNEL);
	if (!temp) {
		CODEC_LOG(KERN_ERR, "Failed to allocate memory.\n");
		return;
	}

	CODEC_LOG(KERN_DEBUG, "add task. file: %x\n", file);
	temp->id = file;

	INIT_LIST_HEAD(&temp->entry);

	mutex_lock(&newcodec_bh_mutex);
	list_add_tail(&temp->entry, head);
	mutex_unlock(&newcodec_bh_mutex);
}

static void newcodec_release_task_entry(struct list_head *head, int32_t value)
{
	struct list_head *pos, *temp;
	struct newcodec_task *node;

	list_for_each_safe(pos, temp, head) {
		node = list_entry(pos, struct newcodec_task, entry);
		if (node->id == value) {
			CODEC_LOG(KERN_DEBUG, "release task resource. :%x\n", node->id);
			list_del(pos);
			kfree(node);
		}
	}
}

static uint32_t newcodec_manage_dev_mem(struct codec_mem_info *mem_info, int32_t file)
{
	uint32_t req_mem_size = mem_info->index;
	uint32_t used_mem_size = newcodec->used_mem_size;
//	uint32_t mmapmgr_offset = newcodec->mmapmgr_offset;
	struct newcodec_mmapmgr *mem_mgr;

	mem_mgr = &newcodec->mem_mgr[newcodec->mmapmgr_idx];
	CODEC_LOG(KERN_DEBUG, "[file: %x] mem index: %d\n",
		file, newcodec->mmapmgr_idx);

	if ((used_mem_size + req_mem_size) < NEWCODEC_FIXED_DEV_MEM_MAX) {
#if 0
		if (used_mem_size == 0) {
			newcodec->mem_mgr[0].id = file;
			newcodec->mem_mgr[0].offset = 0;
			newcodec->mmapmgr_offset = req_mem_size;
		} else {
#endif
			mem_mgr->id = file;
			mem_mgr->offset = newcodec->mmapmgr_offset;
			newcodec->mmapmgr_offset = mem_mgr->offset + req_mem_size;
//		}

		mutex_lock(&newcodec_mutex);
		newcodec->used_mem_size += req_mem_size;
		mutex_unlock(&newcodec_mutex);

		mem_info->type = CODEC_FIXED_DEVICE_MEM;
	} else {
		mem_mgr->id = file;
		mem_mgr->offset = NEWCODEC_FIXED_DEV_MEM_MAX;

		mem_info->type = CODEC_SHARED_DEVICE_MEM;
	}

	mem_info->index = newcodec->mmapmgr_idx;
	mem_info->offset = mem_mgr->offset;

	newcodec->mmapmgr_idx++;
	// TODO: twice size or return to 0
	if (newcodec->mmapmgr_idx == newcodec->mmapmgr_size) {
		newcodec->mmapmgr_idx = 0;
	}

	return 0;
//	return ret;
}


static long newcodec_ioctl(struct file *file,
			unsigned int cmd,
			unsigned long arg)
{
	long value = 0, ret = 0;

	switch (cmd) {
	case CODEC_CMD_ADD_TASK_QUEUE:
		newcodec_add_task(&newcodec->req_task, (int32_t)file);
		break;
	case CODEC_CMD_REMOVE_TASK_QUEUE:
	{
		struct newcodec_task *head_task = NULL;

		head_task =
			list_first_entry(&newcodec->req_task, struct newcodec_task, entry);
		if (!head_task) {
			CODEC_LOG(KERN_DEBUG, "[file: %p] head_task is NULL\n", file);
		} else {
			CODEC_LOG(KERN_DEBUG,
				"[file: %p] head_task of req: %x into old_task\n",
				file, head_task->id);

			mutex_lock(&newcodec_mutex);
			list_move_tail(&head_task->entry, &newcodec->old_task);
			mutex_unlock(&newcodec_mutex);

			CODEC_LOG(KERN_DEBUG, "release old_task resource.\n");
			newcodec_release_task_entry(&newcodec->old_task, (int32_t)file);
		}
	}
		break;
	case CODEC_CMD_COPY_TO_DEVICE_MEM:
	{
		struct newcodec_task *head_task = NULL;

		CODEC_LOG(KERN_DEBUG, "[file: %p] COPY_TO_DEV_MEM\n", file);
		if (!list_empty(&newcodec->req_task)) {
			head_task =
				list_first_entry(&newcodec->req_task, struct newcodec_task, entry);

			if (!head_task) {
				CODEC_LOG(KERN_DEBUG, "[file: %p] head_task is NULL\n", file);
				value = CODEC_MEM_LOCK;
				if (copy_to_user((void *)arg, &value, sizeof(int))) {
					CODEC_LOG(KERN_DEBUG, "ioctl: failed to copy data to user.\n");
				}
				break;
			}

			CODEC_LOG(KERN_DEBUG, "[file: %p] COPY_TO head_task: %x\n", file, head_task->id);

			if (head_task->id != (int32_t)file) {
				CODEC_LOG(KERN_DEBUG,	"[file: %p] different file btw head and file\n", file);
				value = CODEC_MEM_LOCK;
			} else {
				CODEC_LOG(KERN_DEBUG, "[file: %p] handle head_task: %x\n", file, head_task->id);
				CODEC_LOG(KERN_DEBUG, "[file: %p] COPY_TO_DEV is accept.\n", file);
				value = CODEC_MEM_UNLOCK;
			}
		} else {
			mutex_unlock(&newcodec_mutex);
			CODEC_LOG(KERN_DEBUG, "[file: %p] COPY_TO_DEV_MEM req_task is empty\n", file);
			value = CODEC_MEM_LOCK;
		}

		if (copy_to_user((void *)arg, &value, sizeof(int))) {
			CODEC_LOG(KERN_ERR, "ioctl: failed to copy data to user.\n");
		}
	}
		break;

	case CODEC_CMD_COPY_FROM_DEVICE_MEM:
	{
		struct newcodec_task *head_task = NULL;

		CODEC_LOG(KERN_DEBUG, "[file: %p] COPY_FROM_DEV_MEM\n", file);

		mutex_lock(&newcodec_mutex);
		if (!list_empty(&newcodec->req_task)) {
			mutex_unlock(&newcodec_mutex);

			if (!(head_task =
				list_first_entry(&newcodec->req_task, struct newcodec_task, entry))) {
				value = CODEC_MEM_LOCK;
				if (copy_to_user((void *)arg, &value, sizeof(int))) {
					CODEC_LOG(KERN_DEBUG,
						"ioctl: failed to copy data to user.\n");
				}
				break;
			}

			CODEC_LOG(KERN_DEBUG,
				"[file: %p] COPY_FROM head_task: %x\n",
				file, head_task->id);

			if (head_task->id != (int32_t)file) {
				CODEC_LOG(KERN_DEBUG,
					"[file: %p] different file btw head and file\n", file);
				value = CODEC_MEM_LOCK;
			} else {
				CODEC_LOG(KERN_DEBUG,
					"[file: %p] pop data %x from codec_wq.\n",
					file, head_task->id);
				value = CODEC_MEM_UNLOCK;

				writel(head_task->id,
						newcodec->ioaddr + CODEC_CMD_POP_WRITE_QUEUE);
			}
		} else {
			mutex_unlock(&newcodec_mutex);
			CODEC_LOG(KERN_DEBUG,
				"[file: %p] COPY_FROM_DEV_MEM req_task is empty\n", file);
			value = CODEC_MEM_LOCK;
		}

		if (copy_to_user((void *)arg, &value, sizeof(int))) {
			CODEC_LOG(KERN_DEBUG, "ioctl: failed to copy data to user.\n");
		}
	}
		break;
	case CODEC_CMD_GET_DEVICE_MEM_INFO:
	{
		int ret;
		struct codec_mem_info mem_info;

		if (copy_from_user(&mem_info, (void *)arg, sizeof(mem_info))) {
			CODEC_LOG(KERN_DEBUG, "ioctl: failed to copy data to user\n");
			return -EIO;
		}

		CODEC_LOG(KERN_DEBUG, "request memory size: %d\n", mem_info.index);

		ret = newcodec_manage_dev_mem(&mem_info, (int32_t)file);

		if (copy_to_user((void *)arg, &mem_info, sizeof(mem_info))) {
			CODEC_LOG(KERN_DEBUG,
				"ioctl: failed to copy data to user.\n");
		}
	}
		break;
	case CODEC_CMD_RELEASE_DEVICE_MEM:
	{
		struct newcodec_mmapmgr *mem_mgr;
		struct codec_mem_info mem_info;

		if (copy_from_user(&mem_info, (void *)arg, sizeof(mem_info))) {
			CODEC_LOG(KERN_DEBUG, "ioctl: failed to copy data to user\n");
			return -EIO;
		}

		CODEC_LOG(KERN_DEBUG, "release memory size: %d\n", mem_info.index);
		mem_mgr = &newcodec->mem_mgr[mem_info.index];

		if (mem_mgr->id == (int32_t)file &&
			mem_mgr->offset == mem_info.offset) {
			memset(mem_mgr, 0x00, sizeof(struct newcodec_mmapmgr));
		}
	}
		break;
	case CODEC_CMD_WAIT_TASK_QUEUE:
	{
		value = CODEC_MEM_LOCK;

		if (!list_empty(&newcodec->irq_task)) {
			struct newcodec_task *head_task = NULL;

			head_task =
				list_first_entry(&newcodec->irq_task, struct newcodec_task, entry);
			if (!head_task) {
				CODEC_LOG(KERN_DEBUG,
					"[file: %p] head_task is NULL\n", file);
			} else {
				CODEC_LOG(KERN_DEBUG,
					"[file: %p] head_task id: %x\n", file, head_task->id);
				if (head_task->id == (int32_t)file) {
					value = CODEC_MEM_UNLOCK;
					list_del(&head_task->entry);
					kfree(head_task);
				}
			}
		} else {
			CODEC_LOG(KERN_DEBUG, "[file: %p] irq_task is empty\n", file);
		}

		if (copy_to_user((void *)arg, &value, sizeof(int))) {
			CODEC_LOG(KERN_ERR, "ioctl: failed to copy data to user\n");
	    }
	}
		break;
	case CODEC_CMD_GET_VERSION:
	case CODEC_CMD_GET_CONTEXT_INDEX:
		value = readl(newcodec->ioaddr + cmd);
		if (copy_to_user((void *)arg, &value, sizeof(int))) {
			CODEC_LOG(KERN_ERR, "ioctl: failed to copy data to user\n");
	    }
		break;
	default:
		CODEC_LOG(KERN_DEBUG, "no available command.");
		break;
	}

	return ret;
}

static ssize_t newcodec_read(struct file *file, char __user *buf,
			size_t count, loff_t *fops)
{
	CODEC_LOG(KERN_DEBUG, "do nothing in the read operation.\n");
	return 0;
}

/* Copy data between guest and host using mmap operation. */
static ssize_t newcodec_write(struct file *file, const char __user *buf, size_t count, loff_t *fops)
{
	struct codec_param param_info;

	if (!newcodec) {
		CODEC_LOG(KERN_ERR, "failed to get codec device info\n");
		return -EINVAL;
	}

	memset (&param_info, 0x00, sizeof(struct codec_param));
	if (copy_from_user(&param_info, buf, sizeof(struct codec_param))) {
		CODEC_LOG(KERN_ERR,
			"failed to get codec parameter info from user\n");
		return -EIO;
    }

	mutex_lock(&newcodec_mutex);
	if (param_info.api_index == CODEC_ELEMENT_QUERY) {
		writel((int32_t)param_info.api_index,
				newcodec->ioaddr + CODEC_CMD_API_INDEX);
	} else {
		writel((int32_t)file,
				newcodec->ioaddr + CODEC_CMD_FILE_INDEX);
		writel((uint32_t)param_info.mem_offset,
				newcodec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
		writel((uint32_t)param_info.mem_type,
				newcodec->ioaddr + CODEC_CMD_DEVICE_MEM_TYPE);
		writel((int32_t)param_info.ctx_index,
				newcodec->ioaddr + CODEC_CMD_CONTEXT_INDEX);
		writel((int32_t)param_info.api_index,
				newcodec->ioaddr + CODEC_CMD_API_INDEX);

		if (param_info.mem_type == CODEC_SHARED_DEVICE_MEM) {
			if (param_info.api_index > CODEC_ELEMENT_QUERY &&
					param_info.api_index < CODEC_DEINIT) {
				struct newcodec_task *head_task = NULL;

				head_task =
					list_first_entry(&newcodec->req_task, struct newcodec_task, entry);
				if (!head_task) {
					CODEC_LOG(KERN_DEBUG, "[file: %p] head_task is NULL\n", file);
				} else {
					CODEC_LOG(KERN_DEBUG, "move head_task: %x into old_task\n", head_task->id);
					list_move_tail(&head_task->entry, &newcodec->old_task);
				}
			}
		}
	}
	mutex_unlock(&newcodec_mutex);

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
		CODEC_LOG(KERN_ERR, "over mapping size\n");
		return -EINVAL;
	}
	off = vm->vm_pgoff << PAGE_SHIFT;
	phys_addr = (PAGE_ALIGN(newcodec->mem_start) + off) >> PAGE_SHIFT;

	ret = remap_pfn_range(vm, vm->vm_start, phys_addr,
			size, vm->vm_page_prot);
	if (ret < 0) {
		CODEC_LOG(KERN_ERR, "failed to remap page range\n");
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
	if (!(val & NEWCODEC_IRQ_SHARED_TASK ||
			val & NEWCODEC_IRQ_FIXED_TASK)) {
		return IRQ_NONE;
	}

	spin_lock_irqsave(&dev->lock, flags);

	if (val == NEWCODEC_IRQ_SHARED_TASK) {
		CODEC_LOG(KERN_DEBUG, "handle shared_task irq\n");
		newcodec_shared_bh(dev);
	} else if (val == NEWCODEC_IRQ_FIXED_TASK) {
		CODEC_LOG(KERN_DEBUG, "handle fixed_task irq\n");
		newcodec_fixed_bh(dev);
	}

	spin_unlock_irqrestore(&dev->lock, flags);

	return IRQ_HANDLED;
}

static int newcodec_open(struct inode *inode, struct file *file)
{
	CODEC_LOG(KERN_DEBUG, "open! struct file:%p\n", file);

	/* register interrupt handler */
	if (request_irq(newcodec->dev->irq, newcodec_irq_handler,
		IRQF_SHARED, DEVICE_NAME, newcodec)) {
		CODEC_LOG(KERN_ERR, "failed to register irq handle\n");
		return -EBUSY;
	}

	try_module_get(THIS_MODULE);

	return 0;
}

static int newcodec_release(struct inode *inode, struct file *file)
{
	/* free irq */
	if (newcodec->dev->irq) {
		CODEC_LOG(KERN_DEBUG, "free registered irq\n");
		free_irq(newcodec->dev->irq, newcodec);
	}

	/* free old_task resource */
	CODEC_LOG(KERN_DEBUG, "release old_task resource.\n");
	newcodec_release_task_entry(&newcodec->old_task, (int32_t)file);

	CODEC_LOG(KERN_DEBUG, "release req_task resource.\n");
	newcodec_release_task_entry(&newcodec->req_task, (int32_t)file);

	CODEC_LOG(KERN_DEBUG, "release irq_task resource.\n");
	newcodec_release_task_entry(&newcodec->irq_task, (int32_t)file);

#if 0
	{
		struct list_head *pos, *temp;
		struct newcodec_task *node;

		list_for_each_safe(pos, temp, &newcodec->old_task) {
			node = list_entry(pos, struct newcodec_task, entry);
			if (node->id == (int32_t)file) {
				CODEC_LOG(KERN_INFO,
					"release old_task resource. :%x\n", node->id);
				list_del(pos);
				kfree(node);
			}
		}

		list_for_each_safe(pos, temp, &newcodec->req_task) {
			node = list_entry(pos, struct newcodec_task, entry);
			if (node->id == (int32_t)file) {
				CODEC_LOG(KERN_DEBUG, "release req_task resource. :%x\n", node->id);
				list_del(pos);
				kfree(node);
			}
		}
	}
#endif

	/* notify closing codec device of qemu. */
	if (file) {
		writel((int32_t)file,
			newcodec->ioaddr + CODEC_CMD_RESET_CODEC_INFO);
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

static int __devinit newcodec_probe(struct pci_dev *pci_dev,
	const struct pci_device_id *pci_id)
{
	int ret = 0;

	newcodec = kzalloc(sizeof(struct newcodec_device), GFP_KERNEL);
	if (!newcodec) {
		CODEC_LOG(KERN_ERR, "Failed to allocate memory for codec.\n");
		return -EIO;
	}
	newcodec->dev = pci_dev;

	newcodec->mmapmgr_size = 32;
	newcodec->mem_mgr =
		kzalloc(sizeof(struct newcodec_mmapmgr) *
				newcodec->mmapmgr_size, GFP_KERNEL);
	if (!newcodec->mem_mgr) {
		CODEC_LOG(KERN_ERR, "Failed to allocate memory.\n");
		return -EIO;
	}

	INIT_LIST_HEAD(&newcodec->req_task);
	INIT_LIST_HEAD(&newcodec->old_task);
	INIT_LIST_HEAD(&newcodec->irq_task);

//	init_waitqueue_head(&newcodec->codec_wq);
	spin_lock_init(&newcodec->lock);

	if ((ret = pci_enable_device(pci_dev))) {
		CODEC_LOG(KERN_ERR, "pci_enable_device failed\n");
		return ret;
	}

	pci_set_master(pci_dev);

	newcodec->mem_start = pci_resource_start(pci_dev, 0);
	newcodec->mem_size = pci_resource_len(pci_dev, 0);
	if (!newcodec->mem_start) {
		CODEC_LOG(KERN_ERR, "pci_resource_start failed\n");
		pci_disable_device(pci_dev);
		return -ENODEV;
	}

	if (!request_mem_region(newcodec->mem_start,
				newcodec->mem_size,
				DEVICE_NAME)) {
		CODEC_LOG(KERN_ERR, "request_mem_region failed\n");
		pci_disable_device(pci_dev);
		return -EINVAL;
	}

	newcodec->io_start = pci_resource_start(pci_dev, 1);
	newcodec->io_size = pci_resource_len(pci_dev, 1);
	if (!newcodec->io_start) {
		CODEC_LOG(KERN_ERR, "pci_resource_start failed\n");
		release_mem_region(newcodec->mem_start, newcodec->mem_size);
		pci_disable_device(pci_dev);
		return -ENODEV;
	}

	if (!request_mem_region(newcodec->io_start,
				newcodec->io_size,
				DEVICE_NAME)) {
		CODEC_LOG(KERN_ERR, "request_io_region failed\n");
		release_mem_region(newcodec->mem_start, newcodec->mem_size);
		pci_disable_device(pci_dev);
		return -EINVAL;
	}

	newcodec->ioaddr = ioremap_nocache(newcodec->io_start, newcodec->io_size);
	if (!newcodec->ioaddr) {
		CODEC_LOG(KERN_ERR, "ioremap failed\n");
		release_mem_region(newcodec->io_start, newcodec->io_size);
		release_mem_region(newcodec->mem_start, newcodec->mem_size);
		pci_disable_device(pci_dev);
		return -EINVAL;
	}

	ret = misc_register(&codec_dev);
	if (ret) {
		CODEC_LOG(KERN_ERR, "cannot register codec as misc\n");
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

#define PCI_VENDOR_ID_TIZEN_EMUL			0xC9B5
#define PCI_DEVICE_ID_VIRTUAL_NEW_CODEC		0x1024

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

/* define PCI Driver for CODEC */
static struct pci_driver driver = {
	.name		= DEVICE_NAME,
	.id_table	= newcodec_pci_table,
	.probe		= newcodec_probe,
	.remove		= newcodec_remove,
};

static int __init newcodec_init(void)
{
	CODEC_LOG(KERN_INFO, "device is initialized.\n");
	newcodec_bh_workqueue = create_workqueue ("newcodec");
	if (!newcodec_bh_workqueue) {
		CODEC_LOG(KERN_ERR, "failed to allocate workqueue\n");
		return -ENOMEM;
	}

	return pci_register_driver(&driver);
}

static void __exit newcodec_exit(void)
{
	CODEC_LOG(KERN_INFO, "device is finalized.\n");
	if (newcodec_bh_workqueue) {
		destroy_workqueue (newcodec_bh_workqueue);
		newcodec_bh_workqueue = NULL;
	}
	pci_unregister_driver(&driver);
}
module_init(newcodec_init);
module_exit(newcodec_exit);
