/*
 * Virtual Codec Device Driver
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


MODULE_DESCRIPTION("Virtual Codec Device Driver");
MODULE_AUTHOR("Kitae KIM <kt920.kim@samsung.com");
MODULE_LICENSE("GPL2");

#define DEVICE_NAME	"newcodec"

/* vendor, device value for PCI.*/
#define PCI_VENDOR_ID_TIZEN_EMUL			0xC9B5
#define PCI_DEVICE_ID_VIRTUAL_NEW_CODEC		0x1040

/* interrupt identifier for NEWCODEC */
#define CODEC_IRQ_TASK	0x1f

//
#define DEVICE_MEMORY_COUNT	8
#define CODEC_CONTEXT_SIZE	1024

// DEBUG
#ifndef CODEC_DEBUG
#define DEBUG(fmt, ...) \
	printk(KERN_DEBUG "[%s][%d]: " fmt, DEVICE_NAME, __LINE__, ##__VA_ARGS__)
#else
#define DEBUG(fmt, ...)
#endif

#define ERROR(fmt, ...) \
	printk(KERN_ERR "[%s][%d]: " fmt, DEVICE_NAME, __LINE__, ##__VA_ARGS__)

#define INFO(fmt, ...) \
	printk(KERN_INFO "[%s][%d]: " fmt, DEVICE_NAME, __LINE__, ##__VA_ARGS__)


#ifdef CODEC_TIME_LOG
#include <linux/time.h>
#define NEWCODEC_CURRENT_TIME \
{ \
	struct timeval now; \
	do_gettimeofday(&now); \
	printk(KERN_INFO "[%s][%d] current time: %ld.%06ld\n", DEVICE_NAME, __LINE__, (long)now.tv_sec, (long)now.tv_usec); \
}
#else
#define NEWCODEC_CURRENT_TIME
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
	CODEC_CMD_COPY_TO_DEVICE_MEM = 5,	// user and driver
	CODEC_CMD_COPY_FROM_DEVICE_MEM,
	CODEC_CMD_API_INDEX = 10,			// driver and device
	CODEC_CMD_CONTEXT_INDEX,
	CODEC_CMD_FILE_INDEX,
	CODEC_CMD_DEVICE_MEM_OFFSET,
	CODEC_CMD_GET_THREAD_STATE,
	CODEC_CMD_GET_QUEUE,
	CODEC_CMD_POP_WRITE_QUEUE,
	CODEC_CMD_RESET_AVCONTEXT,
	CODEC_CMD_GET_VERSION = 20,			// user, driver and device
	CODEC_CMD_GET_ELEMENT,
	CODEC_CMD_GET_CONTEXT_INDEX,
	CODEC_CMD_SECURE_MEMORY= 30,
	CODEC_CMD_RELEASE_MEMORY,
	CODEC_CMD_USE_DEVICE_MEM,
	CODEC_CMD_REQ_FROM_SMALL_MEMORY,
	CODEC_CMD_REQ_FROM_MEDIUM_MEMORY,
	CODEC_CMD_REQ_FROM_LARGE_MEMORY,
	CODEC_CMD_S_SECURE_BUFFER,
	CODEC_CMD_M_SECURE_BUFFER,
	CODEC_CMD_L_SECURE_BUFFER,
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

	struct list_head avail_s_memblk;
	struct list_head used_s_memblk;
	struct list_head avail_m_memblk;
	struct list_head used_m_memblk;
	struct list_head avail_l_memblk;
	struct list_head used_l_memblk;

	spinlock_t lock;

	int version;
};

static struct newcodec_device *newcodec;
static int context_flags[CODEC_CONTEXT_SIZE] = { 0, };

// semaphore, mutex
static DEFINE_MUTEX(critical_section);
static DEFINE_MUTEX(newcodec_blk_mutex);

static struct semaphore newcodec_buffer_mutex =
	__SEMAPHORE_INITIALIZER(newcodec_buffer_mutex, DEVICE_MEMORY_COUNT);

#define ENTER_CRITICAL_SECTION	mutex_lock(&critical_section);
#define LEAVE_CRITICAL_SECTION	mutex_unlock(&critical_section);

#define CODEC_S_DEVICE_MEM_COUNT	63	// small		(256K)	8M
#define CODEC_XS_DEVICE_MEM_COUNT	8	// extra small	(1M)	8M
#define CODEC_M_DEVICE_MEM_COUNT	4	// medium		(2M)	8M
#define CODEC_L_DEVICE_MEM_COUNT	2	// large		(4M)	8M

#define CODEC_S_DEVICE_MEM_SIZE		0x40000		// small
#define CODEC_XS_DEVICE_MEM_SIZE	0x100000	// extra small
#define CODEC_M_DEVICE_MEM_SIZE		0x200000	// medium
#define CODEC_L_DEVICE_MEM_SIZE		0x400000	// large

static DEFINE_MUTEX(s_block_mutex);
static DEFINE_MUTEX(m_block_mutex);
static DEFINE_MUTEX(l_block_mutex);

static struct semaphore s_buffer_sema =
	__SEMAPHORE_INITIALIZER(s_buffer_sema, CODEC_S_DEVICE_MEM_COUNT);

static struct semaphore m_buffer_sema =
	__SEMAPHORE_INITIALIZER(m_buffer_sema, CODEC_M_DEVICE_MEM_COUNT);

static struct semaphore l_buffer_sema =
	__SEMAPHORE_INITIALIZER(l_buffer_sema, CODEC_L_DEVICE_MEM_COUNT);

// bottom-half
static DECLARE_WAIT_QUEUE_HEAD(wait_queue);

static struct workqueue_struct *newcodec_bh_workqueue;
static void newcodec_bh_func(struct work_struct *work);
static DECLARE_WORK(newcodec_bh_work, newcodec_bh_func);
static void newcodec_bh(struct newcodec_device *dev);


// internal function
static void newcodec_write_data(int cmd1, int cmd2,
								uint32_t value1, uint32_t value2);


static void newcodec_divide_device_memory(void)
{
	struct device_mem *elem = NULL;
	int index = 0, cnt;

	elem =
		kzalloc(sizeof(struct device_mem) * (63 + 4 + 2), GFP_KERNEL);
	if (!elem) {
		ERROR("falied to allocate memory!!\n");
		return;
	}

	for (cnt = 0; cnt < CODEC_S_DEVICE_MEM_COUNT; index++, cnt++) {
		elem[index].mem_offset = (index + 1) * CODEC_S_DEVICE_MEM_SIZE;
		elem[index].occupied = false;
		list_add_tail(&elem[index].entry, &newcodec->avail_s_memblk);
	}

	for (cnt = 0; cnt < CODEC_M_DEVICE_MEM_COUNT; index++, cnt++) {
		elem[index].mem_offset = (16 * 1024 * 1024) + (cnt * CODEC_M_DEVICE_MEM_SIZE);
		elem[index].occupied = false;
		list_add_tail(&elem[index].entry, &newcodec->avail_m_memblk);
	}

#if 1
	for (cnt = 0; cnt < CODEC_L_DEVICE_MEM_COUNT; index++, cnt++) {
		elem[index].mem_offset = (16 * 1024 * 1024) + (8 * 1024 * 1024) + (cnt * CODEC_L_DEVICE_MEM_SIZE);
		elem[index].occupied = false;
		list_add_tail(&elem[index].entry, &newcodec->avail_l_memblk);
	}
#endif
}

static void newcodec_bh_func(struct work_struct *work)
{
	uint32_t value;

	DEBUG("%s\n", __func__);
	do {
		value = readl(newcodec->ioaddr + CODEC_CMD_GET_QUEUE);
		DEBUG("read a value from device %x.\n", value);
		if (value) {
			context_flags[value] = 1;
			wake_up_interruptible(&wait_queue);
		} else {
			DEBUG("there is no available task\n");
		}
	} while (value);
}

static void newcodec_bh(struct newcodec_device *dev)
{
	DEBUG("add bottom-half function to codec_workqueue\n");
	queue_work(newcodec_bh_workqueue, &newcodec_bh_work);
}

static int lock_buffer(void)
{
	int ret;
	ret = down_interruptible(&newcodec_buffer_mutex);

//	DEBUG("lock buffer_mutex: %d\n", newcodec_buffer_mutex.count);
	return ret;
}

static void unlock_buffer(void)
{
	up(&newcodec_buffer_mutex);
//	DEBUG("unlock buffer_mutex: %d\n", newcodec_buffer_mutex.count);
}

static int secure_s_device_memory(uint32_t blk_id)
{
	int ret = -1;
	struct device_mem *elem = NULL;

	// decrease s_buffer_semaphore
	DEBUG("before down s_buffer_sema: %d\n", s_buffer_sema.count);
	ret = down_interruptible(&s_buffer_sema);
	DEBUG("after down s_buffer_sema: %d\n", s_buffer_sema.count);
	if (ret < 0) {
		ERROR("no available memory block\n");
		return ret;
	}

	mutex_lock(&s_block_mutex);

	NEWCODEC_CURRENT_TIME

	if (!list_empty(&newcodec->avail_s_memblk)) {
		elem =
			list_first_entry(&newcodec->avail_s_memblk,
							struct device_mem, entry);
		if (!elem) {
			ret = -1;
			up(&s_buffer_sema);
			ERROR("failed to get first entry from avail_s_memblk.\n");
		} else {
			elem->blk_id = blk_id;
			elem->occupied = true;

			list_move_tail(&elem->entry, &newcodec->used_s_memblk);
			ret = elem->mem_offset;
			DEBUG("get available memory region: 0x%x\n", ret);
		}
	} else {
		ERROR("the number of buffer mutex: %d\n",  s_buffer_sema.count);
		ERROR("no available memory block\n");
		ret = -1;
		up(&s_buffer_sema);
	}

	NEWCODEC_CURRENT_TIME

	mutex_unlock(&s_block_mutex);

	return ret;
}

static void release_s_device_memory(uint32_t mem_offset)
{
	struct device_mem *elem = NULL;
	struct list_head *pos, *temp;

	mutex_lock(&s_block_mutex);
	if (!list_empty(&newcodec->used_s_memblk)) {
		list_for_each_safe(pos, temp, &newcodec->used_s_memblk) {
			elem = list_entry(pos, struct device_mem, entry);
			if (elem->mem_offset == (uint32_t)mem_offset) {

				elem->blk_id = 0;
				elem->occupied = false;
				list_move_tail(&elem->entry, &newcodec->avail_s_memblk);

				up(&s_buffer_sema);
				DEBUG("unlock s_buffer_sema: %d\n", s_buffer_sema.count);

				break;
			}
		}
	} else {
		DEBUG("there is no used memory block.\n");
	}
	mutex_unlock(&s_block_mutex);
}

static int secure_m_device_memory(uint32_t blk_id)
{
	int ret = -1;
	struct device_mem *elem = NULL;

	// decrease m_buffer_semaphore
	ret = down_interruptible(&m_buffer_sema);
	if (ret < 0) {
		ERROR("m_buffer_sema: %d\n", m_buffer_sema.count);
		ERROR("no available memory block\n");
		return ret;
	}

	mutex_lock(&m_block_mutex);

	if (!list_empty(&newcodec->avail_m_memblk)) {
		elem =
			list_first_entry(&newcodec->avail_m_memblk,
							struct device_mem, entry);
		if (!elem) {
			ret = -1;
			up(&m_buffer_sema);
			ERROR("failed to get first entry from avail_m_memblk. %d\n", m_buffer_sema.count);
		} else {
			elem->blk_id = blk_id;
			elem->occupied = true;

			list_move_tail(&elem->entry, &newcodec->used_m_memblk);
			ret = elem->mem_offset;
			DEBUG("get available memory region: 0x%x\n", ret);
		}
	} else {
		ERROR("no available memory block\n");
		ret = -1;
		up(&m_buffer_sema);
		ERROR("the number of buffer mutex: %d\n",  m_buffer_sema.count);
	}

	mutex_unlock(&m_block_mutex);

	return ret;
}

static void release_m_device_memory(uint32_t mem_offset)
{
	struct device_mem *elem = NULL;
	struct list_head *pos, *temp;

	mutex_lock(&m_block_mutex);
	if (!list_empty(&newcodec->used_m_memblk)) {
		list_for_each_safe(pos, temp, &newcodec->used_m_memblk) {
			elem = list_entry(pos, struct device_mem, entry);
			if (elem->mem_offset == (uint32_t)mem_offset) {

				elem->blk_id = 0;
				elem->occupied = false;
				list_move_tail(&elem->entry, &newcodec->avail_m_memblk);

				up(&m_buffer_sema);
				break;
			}
		}
	} else {
		DEBUG("there is no used memory block.\n");
	}
	mutex_unlock(&m_block_mutex);
}


static int32_t secure_l_device_memory(uint32_t blk_id)
{
	int ret = -1;
	struct device_mem *elem = NULL;

#if 1
	// decrease m_buffer_semaphore
	DEBUG("before down l_buffer_semaphore: %d\n", l_buffer_sema.count);
	ret = down_interruptible(&l_buffer_sema);
	DEBUG("after down l_buffer_semaphore: %d\n", l_buffer_sema.count);
	if (ret < 0) {
		ERROR("l_buffer_semaphore: %d\n", l_buffer_sema.count);
		ERROR("no available memory block\n");
		return ret;
	}

	mutex_lock(&l_block_mutex);

	if (!list_empty(&newcodec->avail_l_memblk)) {
		elem =
			list_first_entry(&newcodec->avail_l_memblk,
							struct device_mem, entry);
		if (!elem) {
			ret = -1;
			up(&l_buffer_sema);
			ERROR("failed to get first entry from avail_l_memblk.\n");
		} else {
			elem->blk_id = blk_id;
			elem->occupied = true;

			list_move_tail(&elem->entry, &newcodec->used_l_memblk);
			ret = elem->mem_offset;
			DEBUG("get available memory region: 0x%x\n", ret);
		}
	} else {
		ERROR("the number of buffer mutex: %d\n",  l_buffer_sema.count);
		ERROR("no available memory block\n");
		ret = -1;
		up(&l_buffer_sema);
	}

	mutex_unlock(&l_block_mutex);
#endif

	return ret;
}

static void release_l_device_memory(uint32_t mem_offset)
{
	struct device_mem *elem = NULL;
	struct list_head *pos, *temp;

#if 1
	mutex_lock(&l_block_mutex);
	if (!list_empty(&newcodec->used_l_memblk)) {
		list_for_each_safe(pos, temp, &newcodec->used_l_memblk) {
			elem = list_entry(pos, struct device_mem, entry);
			if (elem->mem_offset == (uint32_t)mem_offset) {

				elem->blk_id = 0;
				elem->occupied = false;
				list_move(&elem->entry, &newcodec->avail_l_memblk);

				up(&l_buffer_sema);
				DEBUG("up l_buffer_semaphore: %d\n", l_buffer_sema.count);

				break;
			}
		}
	} else {
		DEBUG("there is no used memory block.\n");
	}
	mutex_unlock(&l_block_mutex);
#endif
}

static int32_t secure_device_memory(uint32_t blk_id)
{
	int ret = 0;

	lock_buffer();

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
				ERROR("failed to get first entry from avail_memblk\n");
				ret = -1;
			} else {
				elem->blk_id = blk_id;
				elem->occupied = true;

				list_move_tail(&elem->entry, &newcodec->used_memblk);
				ret = elem->mem_offset;
			}
		} else {
			ERROR("no available memory block\n");
			ERROR("the number of buffer mutex: %d\n", newcodec_buffer_mutex.count);
			ret = -1;
		}

		mutex_unlock(&newcodec_blk_mutex);
	}

	return ret;
}

static void release_device_memory(uint32_t mem_offset)
{

#if 0
	struct device_mem *elem = NULL;
	struct list_head *pos, *temp;

	mutex_lock(&newcodec_blk_mutex);
	if (!list_empty(&newcodec->used_memblk)) {
		list_for_each_safe(pos, temp, &newcodec->used_memblk) {
			elem = list_entry(pos, struct device_mem, entry);
			if (elem->mem_offset == (uint32_t)mem_offset) {
				elem->blk_id = 0;
				elem->occupied = false;
				list_move(&elem->entry, &newcodec->avail_memblk);

				unlock_buffer();
				break;
			}
		}
	} else {
		DEBUG("there is no used memory block.\n");
	}
	mutex_unlock(&newcodec_blk_mutex);
#endif

	if (mem_offset < (16 * 1024 * 1024)) {
		DEBUG("release small size of memory\n");
		release_s_device_memory(mem_offset);
	} else if (mem_offset - (24 * 1024 * 1024)) {
		DEBUG("release medium size of memory\n");
		release_m_device_memory(mem_offset);
	} else {
		DEBUG("release large size of memory\n");
		release_l_device_memory(mem_offset);
	}
}

static long newcodec_ioctl(struct file *file,
			unsigned int cmd,
			unsigned long arg)
{
	long value = 0, ret = 0;

	switch (cmd) {
	case CODEC_CMD_COPY_TO_DEVICE_MEM:
	{
		DEBUG("copy data to device memory\n");
		value =
			secure_device_memory((uint32_t)file);
		if (value < 0) {
			ERROR("failed to get available memory\n");
			ret = -EINVAL;
		} else {
			if (copy_to_user((void *)arg, &value, sizeof(uint32_t))) {
				ERROR("ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}
	}
		break;
	case CODEC_CMD_COPY_FROM_DEVICE_MEM:
	{
		DEBUG("copy data from device memory. %p\n", file);
		value =
			secure_device_memory((uint32_t)file);
		if (value < 0) {
			ERROR("failed to get available memory\n");
			ret = -EINVAL;
		} else {
			DEBUG("send a request to pop data from device. %p\n", file);

			ENTER_CRITICAL_SECTION;
			writel((uint32_t)value,
					newcodec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((uint32_t)file,
					newcodec->ioaddr + CODEC_CMD_POP_WRITE_QUEUE);
			LEAVE_CRITICAL_SECTION;

			if (copy_to_user((void *)arg, &value, sizeof(uint32_t))) {
				ERROR("ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}
	}
		break;

	case CODEC_CMD_GET_VERSION:
		DEBUG("%s version: %d\n", DEVICE_NAME, newcodec->version);

		if (copy_to_user((void *)arg, &newcodec->version, sizeof(int))) {
			ERROR("ioctl: failed to copy data to user\n");
			ret = -EIO;
		}
		break;
	case CODEC_CMD_GET_ELEMENT:
		DEBUG("request a device to get codec elements\n");

		ENTER_CRITICAL_SECTION;
		value = readl(newcodec->ioaddr + cmd);
		LEAVE_CRITICAL_SECTION;

		if (value < 0) {
			ERROR("ioctl: failed to get elements. %d\n", (int)value);
			ret = -EINVAL;
		}
		break;
	case CODEC_CMD_GET_CONTEXT_INDEX:
		DEBUG("request a device to get an index of codec context \n");

		value = readl(newcodec->ioaddr + cmd);
		if (value < 0 || value > CODEC_CONTEXT_SIZE) {
			ERROR("ioctl: failed to get proper context. %d\n", (int)value);
			ret = -EINVAL;
		} else if (copy_to_user((void *)arg, &value, sizeof(int))) {
			ERROR("ioctl: failed to copy data to user\n");
			ret = -EIO;
		}
		break;
	case CODEC_CMD_SECURE_MEMORY:
		value =
			secure_device_memory((uint32_t)file);
		if (value < 0) {
			ERROR("failed to get available memory\n");
			ret = -EINVAL;
		} else {
			if (copy_to_user((void *)arg, &value, sizeof(uint32_t))) {
				ERROR("ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}
		break;
	case CODEC_CMD_RELEASE_MEMORY:
	{
		uint32_t mem_offset;

		if (copy_from_user(&mem_offset, (void *)arg, sizeof(uint32_t))) {
			ERROR("ioctl: failed to copy data from user\n");
			ret = -EIO;
			break;
		}
		release_device_memory(mem_offset);
	}
		break;
	case CODEC_CMD_USE_DEVICE_MEM:
	{
		uint32_t mem_offset;

		if (copy_from_user(&mem_offset, (void *)arg, sizeof(uint32_t))) {
			ERROR("ioctl: failed to copy data from user\n");
			ret = -EIO;
			break;
		}

		if (mem_offset == newcodec->mem_size) {
			ERROR("offset of device memory is overflow!! 0x%x\n", mem_offset);
			ret = -EIO;
		} else {
			// notify that codec device can copy data to memory region.
			DEBUG("send a request to pop data from device. %p\n", file);

			ENTER_CRITICAL_SECTION;
			writel((uint32_t)mem_offset,
					newcodec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((uint32_t)file,
					newcodec->ioaddr + CODEC_CMD_POP_WRITE_QUEUE);
			LEAVE_CRITICAL_SECTION;
		}
	}
		break;
	case CODEC_CMD_REQ_FROM_SMALL_MEMORY:
		DEBUG("read small size of data from device memory\n");

		value =
			secure_s_device_memory((uint32_t)file);
		if (value < 0) {
			ERROR("failed to get available memory\n");
			ret = -EINVAL;
		} else {
			DEBUG("send a request to pop data from device. %p\n", file);

			newcodec_write_data(CODEC_CMD_DEVICE_MEM_OFFSET,
								CODEC_CMD_POP_WRITE_QUEUE,
								(uint32_t)value, (uint32_t)file);

			if (copy_to_user((void *)arg, &value, sizeof(int32_t))) {
				ERROR("ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}
		break;
	case CODEC_CMD_REQ_FROM_MEDIUM_MEMORY:
		DEBUG("read large size of data from device memory\n");

		value =
			secure_m_device_memory((uint32_t)file);
		if (value < 0) {
			ERROR("failed to get available memory\n");
			ret = -EINVAL;
		} else {
			DEBUG("send a request to pop data from device. %p\n", file);

			ENTER_CRITICAL_SECTION;
			writel((uint32_t)value,
					newcodec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((uint32_t)file,
					newcodec->ioaddr + CODEC_CMD_POP_WRITE_QUEUE);
			LEAVE_CRITICAL_SECTION;

			if (copy_to_user((void *)arg, &value, sizeof(uint32_t))) {
				ERROR("ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}
		break;
	case CODEC_CMD_REQ_FROM_LARGE_MEMORY:
		DEBUG("read large size of data from device memory\n");

		value =
			secure_l_device_memory((uint32_t)file);
		if (value < 0) {
			ERROR("failed to get available memory\n");
			ret = -EINVAL;

		} else {
			DEBUG("send a request to pop data from device. %p\n", file);

			ENTER_CRITICAL_SECTION;
			writel((uint32_t)value,
					newcodec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((uint32_t)file,
					newcodec->ioaddr + CODEC_CMD_POP_WRITE_QUEUE);
			LEAVE_CRITICAL_SECTION;

			if (copy_to_user((void *)arg, &value, sizeof(uint32_t))) {
				ERROR("ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}
		break;
	case CODEC_CMD_S_SECURE_BUFFER:
		value =
			secure_s_device_memory((uint32_t)file);
		if (value < 0) {
			ERROR("failed to get available memory\n");
			ret = -EINVAL;
		} else {
			if (copy_to_user((void *)arg, &value, sizeof(uint32_t))) {
				ERROR("ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}
		break;
	case CODEC_CMD_M_SECURE_BUFFER:
		value =
			secure_m_device_memory((uint32_t)file);
		if (value < 0) {
			ERROR("failed to get available memory\n");
			ret = -EINVAL;
		} else {
			if (copy_to_user((void *)arg, &value, sizeof(uint32_t))) {
				ERROR("ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}
		break;
	case CODEC_CMD_L_SECURE_BUFFER:
		value =
			secure_l_device_memory((uint32_t)file);
		if (value < 0) {
			ERROR("failed to get available memory\n");
			ret = -EINVAL;
		} else {
			if (copy_to_user((void *)arg, &value, sizeof(uint32_t))) {
				ERROR("ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}
		break;
	default:
		DEBUG("no available command.");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static ssize_t newcodec_read(struct file *file, char __user *buf,
							size_t count, loff_t *fops)
{
	DEBUG("do nothing.\n");
	return 0;
}

static void newcodec_write_data(int cmd1, int cmd2,
								uint32_t value1, uint32_t value2)
{
	ENTER_CRITICAL_SECTION;
	writel(value1, newcodec->ioaddr + cmd1);
	writel(value2, newcodec->ioaddr + cmd2);
	LEAVE_CRITICAL_SECTION;
}

static void newcodec_func(struct codec_param *param, uint32_t file_index)
{
	int ctx_index, api_index;

	ctx_index = param->ctx_index;
	api_index = param->api_index;
	DEBUG("context index: %d\n", ctx_index);

	switch (api_index) {
		case CODEC_INIT:

			NEWCODEC_CURRENT_TIME

			newcodec_write_data(CODEC_CMD_FILE_INDEX,
								CODEC_CMD_CONTEXT_INDEX,
								file_index, ctx_index);
		
			NEWCODEC_CURRENT_TIME

//			wait_event_interruptible(wait_queue, context_flags[ctx_index] != 0);
//			context_flags[ctx_index] = 0;
			break;
		case CODEC_DECODE_VIDEO... CODEC_ENCODE_AUDIO:

			NEWCODEC_CURRENT_TIME

			newcodec_write_data(CODEC_CMD_FILE_INDEX,
								CODEC_CMD_CONTEXT_INDEX,
								file_index, ctx_index);

			if (api_index == CODEC_ENCODE_VIDEO) {
				// in case of medium and large size of data
				release_device_memory(param->mem_offset);
			} else {
				// in case of small size of data
//				INFO("release small size of data\n");
				release_s_device_memory(param->mem_offset);
			}
			wait_event_interruptible(wait_queue, context_flags[ctx_index] != 0);
			context_flags[ctx_index] = 0;
			break;
		case CODEC_PICTURE_COPY:
			newcodec_write_data(CODEC_CMD_FILE_INDEX,
								CODEC_CMD_CONTEXT_INDEX,
								file_index, ctx_index);
			wait_event_interruptible(wait_queue, context_flags[ctx_index] != 0);
			context_flags[ctx_index] = 0;
			break;
		case CODEC_DEINIT:
			newcodec_write_data(CODEC_CMD_FILE_INDEX,
								CODEC_CMD_CONTEXT_INDEX,
								file_index, ctx_index);

			context_flags[ctx_index] = 0;
//			INFO("deinit. reset context_flags[%d]: %d\n", ctx_index, context_flags[ctx_index]);
			break;
		default:
			ERROR("undefined function index: %d", api_index);
	}
}


#if 0
static ssize_t newcodec_write(struct file *file, const char __user *buf, size_t count, loff_t *fops)
{
	struct codec_param ioparam = { 0 };

	DEBUG("enter %s. %p\n", __func__, file);

	if (!newcodec) {
		ERROR("failed to get codec device info\n");
		return -EINVAL;
	}

	if (copy_from_user(&ioparam, buf, sizeof(struct codec_param))) {
		ERROR("failed to get codec parameter from user\n");
		return -EIO;
	}

	newcodec_func(&ioparam, (uint32_t)file);

	DEBUG("leave %s. %p\n", __func__, file);
	return 0;
}
#endif

#if 1
/* Copy data between guest and host using mmap operation. */
static ssize_t newcodec_write(struct file *file, const char __user *buf, size_t count, loff_t *fops)
{
	struct codec_param ioparam = { 0 };
	int api_index, ctx_index;

	if (!newcodec) {
		ERROR("failed to get codec device info\n");
		return -EINVAL;
	}

//	memset (&ioparam, 0x00, sizeof(struct codec_param));
	if (copy_from_user(&ioparam, buf, sizeof(struct codec_param))) {
		ERROR("failed to get codec parameter info from user\n");
		return -EIO;
	}

	DEBUG("enter %s. %p\n", __func__, file);

	api_index = ioparam.api_index;
	ctx_index = ioparam.ctx_index;

	switch (api_index) {
	case CODEC_INIT:
		{
			ENTER_CRITICAL_SECTION;
			writel((uint32_t)file,
					newcodec->ioaddr + CODEC_CMD_FILE_INDEX);
			writel((uint32_t)ioparam.mem_offset,
					newcodec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((int32_t)ioparam.ctx_index,
					newcodec->ioaddr + CODEC_CMD_CONTEXT_INDEX);
			writel((int32_t)ioparam.api_index,
					newcodec->ioaddr + CODEC_CMD_API_INDEX);
			LEAVE_CRITICAL_SECTION;

//			release_device_memory(ioparam.mem_offset);
			DEBUG("context index: %d\n", ctx_index);

			wait_event_interruptible(wait_queue, context_flags[ctx_index] != 0);

			DEBUG("wakeup. %d\n", ctx_index);

			context_flags[ctx_index] = 0;
		}
			break;
		case CODEC_DECODE_VIDEO... CODEC_ENCODE_AUDIO:
		{
			ENTER_CRITICAL_SECTION;
			writel((uint32_t)file,
					newcodec->ioaddr + CODEC_CMD_FILE_INDEX);
			writel((uint32_t)ioparam.mem_offset,
					newcodec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((int32_t)ioparam.ctx_index,
					newcodec->ioaddr + CODEC_CMD_CONTEXT_INDEX);
			writel((int32_t)ioparam.api_index,
					newcodec->ioaddr + CODEC_CMD_API_INDEX);
			LEAVE_CRITICAL_SECTION;


			if (api_index == CODEC_ENCODE_VIDEO) {
				// in case of medium and large size of data
				release_device_memory(ioparam.mem_offset);
			} else {
				// in case of small size of data
				release_s_device_memory(ioparam.mem_offset);
			}

//			release_device_memory(ioparam.mem_offset);

			wait_event_interruptible(wait_queue, context_flags[ctx_index] != 0);
			context_flags[ctx_index] = 0;
		}
			break;

		case CODEC_PICTURE_COPY:
		{
			ENTER_CRITICAL_SECTION;
			writel((uint32_t)file,
					newcodec->ioaddr + CODEC_CMD_FILE_INDEX);
			writel((uint32_t)ioparam.mem_offset,
					newcodec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((int32_t)ioparam.ctx_index,
					newcodec->ioaddr + CODEC_CMD_CONTEXT_INDEX);
			writel((int32_t)ioparam.api_index,
					newcodec->ioaddr + CODEC_CMD_API_INDEX);
			LEAVE_CRITICAL_SECTION;

			wait_event_interruptible(wait_queue, context_flags[ctx_index] != 0);
			context_flags[ctx_index] = 0;
		}
			break;

		case CODEC_DEINIT:
			ENTER_CRITICAL_SECTION;
			writel((uint32_t)file,
					newcodec->ioaddr + CODEC_CMD_FILE_INDEX);
//			writel((uint32_t)ioparam.mem_offset,
//					newcodec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((int32_t)ioparam.ctx_index,
					newcodec->ioaddr + CODEC_CMD_CONTEXT_INDEX);
			writel((int32_t)ioparam.api_index,
					newcodec->ioaddr + CODEC_CMD_API_INDEX);
			LEAVE_CRITICAL_SECTION;
			break;
		default:
			ERROR("wrong api command: %d", api_index);
	}

	return 0;
}
#endif

static int newcodec_mmap(struct file *file, struct vm_area_struct *vm)
{
	unsigned long off;
	unsigned long phys_addr;
	unsigned long size;
	int ret = -1;

	size = vm->vm_end - vm->vm_start;
	if (size > newcodec->mem_size) {
		ERROR("over mapping size\n");
		return -EINVAL;
	}
	off = vm->vm_pgoff << PAGE_SHIFT;
	phys_addr = (PAGE_ALIGN(newcodec->mem_start) + off) >> PAGE_SHIFT;

	ret = remap_pfn_range(vm, vm->vm_start, phys_addr,
			size, vm->vm_page_prot);
	if (ret < 0) {
		ERROR("failed to remap page range\n");
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

	DEBUG("handle an interrupt from codec device.\n");
	NEWCODEC_CURRENT_TIME
	newcodec_bh(dev);

	spin_unlock_irqrestore(&dev->lock, flags);

	return IRQ_HANDLED;
}

static int newcodec_open(struct inode *inode, struct file *file)
{
	DEBUG("open! struct file:%p\n", file);

	/* register interrupt handler */
	if (request_irq(newcodec->dev->irq, newcodec_irq_handler,
		IRQF_SHARED, DEVICE_NAME, newcodec)) {
		ERROR("failed to register irq handle\n");
		return -EBUSY;
	}

	try_module_get(THIS_MODULE);

	return 0;
}

static int newcodec_release(struct inode *inode, struct file *file)
{
	/* free irq */
	if (newcodec->dev->irq) {
		DEBUG("free registered irq\n");
		free_irq(newcodec->dev->irq, newcodec);
	}

	DEBUG("%s. file: %p\n", __func__, file);

#if 0
	/* free resource */
	{
		struct device_mem *elem = NULL;
		struct list_head *pos, *temp;

		mutex_lock(&newcodec_blk_mutex);

		if (!list_empty(&newcodec->used_memblk)) {
			list_for_each_safe(pos, temp, &newcodec->used_memblk) {
				elem = list_entry(pos, struct device_mem, entry);
				if (elem->blk_id == (uint32_t)file) {
					DEBUG("move element(%p) to available memory block.\n", elem);

					elem->blk_id = 0;
					elem->occupied = false;
					list_move(&elem->entry, &newcodec->avail_memblk);

					unlock_buffer();
				}
			}
		} else {
			DEBUG("there is no used memory block.\n");
		}

		mutex_unlock(&newcodec_blk_mutex);
	}
#endif

	/* free resource */
	{
		struct device_mem *elem = NULL;
		struct list_head *pos, *temp;

		mutex_lock(&s_block_mutex);

		if (!list_empty(&newcodec->used_s_memblk)) {
			list_for_each_safe(pos, temp, &newcodec->used_s_memblk) {
				elem = list_entry(pos, struct device_mem, entry);
				if (elem->blk_id == (uint32_t)file) {
					DEBUG("move element(%p) to available memory block.\n", elem);

					elem->blk_id = 0;
					elem->occupied = false;
					list_move_tail(&elem->entry, &newcodec->avail_s_memblk);

					up(&s_buffer_sema);
				}
			}
		} else {
			DEBUG("there is no used memory block.\n");
		}

		mutex_unlock(&s_block_mutex);
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

	INFO("device version: %d\n",
		newcodec->version);
}

static int __devinit newcodec_probe(struct pci_dev *pci_dev,
	const struct pci_device_id *pci_id)
{
	int ret = 0;

	INFO("%s: driver is probed.\n", DEVICE_NAME);

	newcodec = kzalloc(sizeof(struct newcodec_device), GFP_KERNEL);
	if (!newcodec) {
		ERROR("Failed to allocate memory for codec.\n");
		return -ENOMEM;
	}

	newcodec->dev = pci_dev;

	INIT_LIST_HEAD(&newcodec->avail_memblk);
	INIT_LIST_HEAD(&newcodec->used_memblk);

	INIT_LIST_HEAD(&newcodec->avail_s_memblk);
	INIT_LIST_HEAD(&newcodec->used_s_memblk);
	INIT_LIST_HEAD(&newcodec->avail_m_memblk);
	INIT_LIST_HEAD(&newcodec->used_m_memblk);
	INIT_LIST_HEAD(&newcodec->avail_l_memblk);
	INIT_LIST_HEAD(&newcodec->used_l_memblk);

#if 0
	{
		struct device_mem *elem = NULL;
		int index;

		elem =
			kzalloc(sizeof(struct device_mem) * DEVICE_MEMORY_COUNT, GFP_KERNEL);
		if (!elem) {
			ERROR("Falied to allocate memory!!\n");
			return -ENOMEM;
		}

		for (index = 0; index < DEVICE_MEMORY_COUNT; index++) {
			elem[index].mem_offset = index * 0x200000;
			elem[index].occupied = false;
			list_add_tail(&elem[index].entry, &newcodec->avail_memblk);
		}
	}
#endif
	newcodec_divide_device_memory();

	spin_lock_init(&newcodec->lock);

	if ((ret = pci_enable_device(pci_dev))) {
		ERROR("pci_enable_device failed\n");
		return ret;
	}
	pci_set_master(pci_dev);

	newcodec->mem_start = pci_resource_start(pci_dev, 0);
	newcodec->mem_size = pci_resource_len(pci_dev, 0);
	if (!newcodec->mem_start) {
		ERROR("pci_resource_start failed\n");
		pci_disable_device(pci_dev);
		return -ENODEV;
	}

	if (!request_mem_region(newcodec->mem_start,
				newcodec->mem_size,
				DEVICE_NAME)) {
		ERROR("request_mem_region failed\n");
		pci_disable_device(pci_dev);
		return -EINVAL;
	}

	newcodec->io_start = pci_resource_start(pci_dev, 1);
	newcodec->io_size = pci_resource_len(pci_dev, 1);
	if (!newcodec->io_start) {
		ERROR("pci_resource_start failed\n");
		release_mem_region(newcodec->mem_start, newcodec->mem_size);
		pci_disable_device(pci_dev);
		return -ENODEV;
	}

	if (!request_mem_region(newcodec->io_start,
				newcodec->io_size,
				DEVICE_NAME)) {
		ERROR("request_io_region failed\n");
		release_mem_region(newcodec->mem_start, newcodec->mem_size);
		pci_disable_device(pci_dev);
		return -EINVAL;
	}

	newcodec->ioaddr = ioremap_nocache(newcodec->io_start, newcodec->io_size);
	if (!newcodec->ioaddr) {
		ERROR("ioremap failed\n");
		release_mem_region(newcodec->io_start, newcodec->io_size);
		release_mem_region(newcodec->mem_start, newcodec->mem_size);
		pci_disable_device(pci_dev);
		return -EINVAL;
	}

	newcodec_get_device_version();

	if ((ret = misc_register(&codec_dev))) {
		ERROR("cannot register codec as misc\n");
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
	INFO("driver is initialized.\n");

	newcodec_bh_workqueue = create_workqueue ("newcodec");
	if (!newcodec_bh_workqueue) {
		ERROR("failed to allocate workqueue\n");
		return -ENOMEM;
	}

	return pci_register_driver(&driver);
}

static void __exit newcodec_exit(void)
{
	INFO("driver is finalized.\n");

	if (newcodec_bh_workqueue) {
		destroy_workqueue (newcodec_bh_workqueue);
		newcodec_bh_workqueue = NULL;
	}
	pci_unregister_driver(&driver);
}
module_init(newcodec_init);
module_exit(newcodec_exit);
