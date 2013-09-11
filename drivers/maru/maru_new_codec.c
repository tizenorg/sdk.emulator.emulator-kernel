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

#define DEVICE_NAME	"brillcodec"

/* vendor, device value for pci.*/
#define PCI_VENDOR_ID_TIZEN_EMUL			0xC9B5
#define PCI_DEVICE_ID_VIRTUAL_BRILL_CODEC	0x1040

/* interrupt identifier */
#define CODEC_IRQ_TASK 0x1f

// DEBUG
#ifndef CODEC_DEBUG
#define DEBUG(fmt, ...) \
	printk(KERN_DEBUG "[%s][%d]: " fmt, DEVICE_NAME, __LINE__, ##__VA_ARGS__)

#define ERROR(fmt, ...) \
	printk(KERN_ERR "[%s][%d]: " fmt, DEVICE_NAME, __LINE__, ##__VA_ARGS__)

#define INFO(fmt, ...) \
	printk(KERN_INFO "[%s][%d]: " fmt, DEVICE_NAME, __LINE__, ##__VA_ARGS__)
#else
#define ERROR(fmt, ...)

#define INFO(fmt, ...)

#define DEBUG(fmt, ...)
#endif

#ifdef CODEC_TIME_LOG
#include <linux/time.h>
#define CODEC_CURRENT_TIME \
{ \
	struct timeval now; \
	do_gettimeofday(&now); \
	printk(KERN_INFO "[%s][%d] current time: %ld.%06ld\n", \
		DEVICE_NAME, __LINE__, (long)now.tv_sec, (long)now.tv_usec); \
}
#else
#define CODEC_CURRENT_TIME
#endif

/* Define i/o and api values.  */

#if 0
enum codec_io_cmd {
//	CODEC_CMD_COPY_TO_DEVICE_MEM = 5,	// user and driver
//	CODEC_CMD_COPY_FROM_DEVICE_MEM,
	CODEC_CMD_API_INDEX = 10,			// driver and device
	CODEC_CMD_CONTEXT_INDEX,
	CODEC_CMD_FILE_INDEX,
	CODEC_CMD_DEVICE_MEM_OFFSET,
	CODEC_CMD_GET_THREAD_STATE,
	CODEC_CMD_GET_QUEUE,
	CODEC_CMD_POP_WRITE_QUEUE,
	CODEC_CMD_RELEASE_AVCONTEXT,
	CODEC_CMD_GET_VERSION = 20,			// user, driver and device
	CODEC_CMD_GET_ELEMENT_INFO,
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
#endif

enum codec_io_cmd {
	CODEC_CMD_API_INDEX = 10,			// driver and device
	CODEC_CMD_CONTEXT_INDEX,
	CODEC_CMD_FILE_INDEX,
	CODEC_CMD_DEVICE_MEM_OFFSET,
	CODEC_CMD_GET_THREAD_STATE,
	CODEC_CMD_GET_CTX_FROM_QUEUE,
	CODEC_CMD_GET_DATA_FROM_QUEUE,
	CODEC_CMD_RELEASE_CONTEXT,
	CODEC_CMD_GET_VERSION = 20,
	CODEC_CMD_GET_ELEMENT,
	CODEC_CMD_GET_CONTEXT_INDEX,
	CODEC_CMD_USE_DEVICE_MEM = 40,		// plugin and driver
	CODEC_CMD_GET_DATA_FROM_SMALL_BUFFER,
	CODEC_CMD_GET_DATA_FROM_MEDIUM_BUFFER,
	CODEC_CMD_GET_DATA_FROM_LARGE_BUFFER,
	CODEC_CMD_SECURE_SMALL_BUFFER,
	CODEC_CMD_SECURE_MEDIUM_BUFFER,
	CODEC_CMD_SECURE_LARGE_BUFFER,
	CODEC_CMD_RELEASE_BUFFER,
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

struct codec_param {
	int32_t api_index;
	int32_t ctx_index;
	int32_t mem_offset;
};

struct codec_mem_info {
	uint32_t index;
	uint32_t offset;
};

/* manage device memory block */
struct device_mem {
	uint32_t blk_id;
	uint32_t mem_offset;
	bool occupied;

	struct list_head entry;
};

struct maru_brill_codec_device {
	struct pci_dev *dev;

	/* I/O and Memory Region */
	unsigned int *ioaddr;

	resource_size_t io_start;
	resource_size_t io_size;
	resource_size_t mem_start;
	resource_size_t mem_size;

	//
	struct device_mem *elem;

	/* task queue */
#if 0
	struct list_head avail_memblk;
	struct list_head used_memblk;
#endif

	struct list_head avail_s_memblk;
	struct list_head used_s_memblk;
	struct list_head avail_m_memblk;
	struct list_head used_m_memblk;
	struct list_head avail_l_memblk;
	struct list_head used_l_memblk;

	spinlock_t lock;

	int version;
};

/* device memory */
#define DEVICE_MEMORY_COUNT	8
#define CODEC_CONTEXT_SIZE	1024

#define CODEC_S_DEVICE_MEM_COUNT	63	// small		(256K)	8M
// #define CODEC_XS_DEVICE_MEM_COUNT	8	// extra small	(1M)	8M
#define CODEC_M_DEVICE_MEM_COUNT	4	// medium		(2M)	8M
#define CODEC_L_DEVICE_MEM_COUNT	2	// large		(4M)	8M

#define CODEC_S_DEVICE_MEM_SIZE		0x40000		// small
// #define CODEC_XS_DEVICE_MEM_SIZE	0x100000	// extra small
#define CODEC_M_DEVICE_MEM_SIZE		0x200000	// medium
#define CODEC_L_DEVICE_MEM_SIZE		0x400000	// large


static struct maru_brill_codec_device *maru_brill_codec;
static int context_flags[CODEC_CONTEXT_SIZE] = { 0, };

// semaphore, mutex
static DEFINE_MUTEX(critical_section);

static DEFINE_MUTEX(s_block_mutex);
static DEFINE_MUTEX(m_block_mutex);
static DEFINE_MUTEX(l_block_mutex);


#if 0
static DEFINE_MUTEX(maru_brill_codec_blk_mutex);

static struct semaphore maru_brill_codec_buffer_mutex =
	__SEMAPHORE_INITIALIZER(maru_brill_codec_buffer_mutex, DEVICE_MEMORY_COUNT);
#endif

#define ENTER_CRITICAL_SECTION	mutex_lock(&critical_section);
#define LEAVE_CRITICAL_SECTION	mutex_unlock(&critical_section);

static struct semaphore s_buffer_sema =
	__SEMAPHORE_INITIALIZER(s_buffer_sema, CODEC_S_DEVICE_MEM_COUNT);

static struct semaphore m_buffer_sema =
	__SEMAPHORE_INITIALIZER(m_buffer_sema, CODEC_M_DEVICE_MEM_COUNT);

static struct semaphore l_buffer_sema =
	__SEMAPHORE_INITIALIZER(l_buffer_sema, CODEC_L_DEVICE_MEM_COUNT);

// bottom-half
static DECLARE_WAIT_QUEUE_HEAD(wait_queue);

static struct workqueue_struct *maru_brill_codec_bh_workqueue;
static void maru_brill_codec_bh_func(struct work_struct *work);
static DECLARE_WORK(maru_brill_codec_bh_work, maru_brill_codec_bh_func);
static void maru_brill_codec_bh(struct maru_brill_codec_device *dev);


static void maru_brill_codec_divide_device_memory(void)
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
		list_add_tail(&elem[index].entry, &maru_brill_codec->avail_s_memblk);
	}

	for (cnt = 0; cnt < CODEC_M_DEVICE_MEM_COUNT; index++, cnt++) {
		elem[index].mem_offset = (16 * 1024 * 1024) + (cnt * CODEC_M_DEVICE_MEM_SIZE);
		elem[index].occupied = false;
		list_add_tail(&elem[index].entry, &maru_brill_codec->avail_m_memblk);
	}

	for (cnt = 0; cnt < CODEC_L_DEVICE_MEM_COUNT; index++, cnt++) {
		elem[index].mem_offset = (16 * 1024 * 1024) + (8 * 1024 * 1024) + (cnt * CODEC_L_DEVICE_MEM_SIZE);
		elem[index].occupied = false;
		list_add_tail(&elem[index].entry, &maru_brill_codec->avail_l_memblk);
	}

	maru_brill_codec->elem = elem;
}

static void maru_brill_codec_bh_func(struct work_struct *work)
{
	uint32_t value;

	DEBUG("%s\n", __func__);
	do {
		value =
			readl(maru_brill_codec->ioaddr + CODEC_CMD_GET_CTX_FROM_QUEUE);
		DEBUG("read a value from device %x.\n", value);
		if (value) {
			context_flags[value] = 1;
			wake_up_interruptible(&wait_queue);
		} else {
			DEBUG("there is no available task\n");
		}
	} while (value);
}

static void maru_brill_codec_bh(struct maru_brill_codec_device *dev)
{
	DEBUG("add bottom-half function to codec_workqueue\n");
	queue_work(maru_brill_codec_bh_workqueue, &maru_brill_codec_bh_work);
}

#if 0
static int lock_buffer(void)
{
	int ret;
	ret = down_interruptible(&maru_brill_codec_buffer_mutex);

	return ret;
}

static void unlock_buffer(void)
{
	up(&maru_brill_codec_buffer_mutex);
}
#endif

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

	if (!list_empty(&maru_brill_codec->avail_s_memblk)) {
		elem =
			list_first_entry(&maru_brill_codec->avail_s_memblk,
							struct device_mem, entry);
		if (!elem) {
			ret = -1;
			up(&s_buffer_sema);
			ERROR("failed to get first entry from avail_s_memblk.\n");
		} else {
			elem->blk_id = blk_id;
			elem->occupied = true;

			list_move_tail(&elem->entry, &maru_brill_codec->used_s_memblk);
			ret = elem->mem_offset;
			DEBUG("get available memory region: 0x%x\n", ret);
		}
	} else {
		ERROR("the number of buffer mutex: %d\n",  s_buffer_sema.count);
		ERROR("no available memory block\n");
		ret = -1;
		up(&s_buffer_sema);
	}

	mutex_unlock(&s_block_mutex);

	return ret;
}

static void release_s_device_memory(uint32_t mem_offset)
{
	struct device_mem *elem = NULL;
	struct list_head *pos, *temp;

	mutex_lock(&s_block_mutex);
	if (!list_empty(&maru_brill_codec->used_s_memblk)) {
		list_for_each_safe(pos, temp, &maru_brill_codec->used_s_memblk) {
			elem = list_entry(pos, struct device_mem, entry);
			if (elem->mem_offset == (uint32_t)mem_offset) {

				elem->blk_id = 0;
				elem->occupied = false;
				list_move_tail(&elem->entry, &maru_brill_codec->avail_s_memblk);

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

	if (!list_empty(&maru_brill_codec->avail_m_memblk)) {
		elem =
			list_first_entry(&maru_brill_codec->avail_m_memblk,
							struct device_mem, entry);
		if (!elem) {
			ret = -1;
			up(&m_buffer_sema);
			ERROR("failed to get first entry from avail_m_memblk. %d\n", m_buffer_sema.count);
		} else {
			elem->blk_id = blk_id;
			elem->occupied = true;

			list_move_tail(&elem->entry, &maru_brill_codec->used_m_memblk);
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
	if (!list_empty(&maru_brill_codec->used_m_memblk)) {
		list_for_each_safe(pos, temp, &maru_brill_codec->used_m_memblk) {
			elem = list_entry(pos, struct device_mem, entry);
			if (elem->mem_offset == (uint32_t)mem_offset) {

				elem->blk_id = 0;
				elem->occupied = false;
				list_move_tail(&elem->entry, &maru_brill_codec->avail_m_memblk);

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

	if (!list_empty(&maru_brill_codec->avail_l_memblk)) {
		elem =
			list_first_entry(&maru_brill_codec->avail_l_memblk,
							struct device_mem, entry);
		if (!elem) {
			ret = -1;
			up(&l_buffer_sema);
			ERROR("failed to get first entry from avail_l_memblk.\n");
		} else {
			elem->blk_id = blk_id;
			elem->occupied = true;

			list_move_tail(&elem->entry, &maru_brill_codec->used_l_memblk);
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

	return ret;
}

static void release_l_device_memory(uint32_t mem_offset)
{
	struct device_mem *elem = NULL;
	struct list_head *pos, *temp;

	mutex_lock(&l_block_mutex);
	if (!list_empty(&maru_brill_codec->used_l_memblk)) {
		list_for_each_safe(pos, temp, &maru_brill_codec->used_l_memblk) {
			elem = list_entry(pos, struct device_mem, entry);
			if (elem->mem_offset == (uint32_t)mem_offset) {

				elem->blk_id = 0;
				elem->occupied = false;
				list_move(&elem->entry, &maru_brill_codec->avail_l_memblk);

				up(&l_buffer_sema);
				DEBUG("up l_buffer_semaphore: %d\n", l_buffer_sema.count);

				break;
			}
		}
	} else {
		DEBUG("there is no used memory block.\n");
	}
	mutex_unlock(&l_block_mutex);
}

#if 0
static int32_t secure_device_memory(uint32_t blk_id)
{
	int ret = 0;
	struct device_mem *elem = NULL;

	lock_buffer();

	// check whether avail_memblk is empty.
	// move mem_blk to used_blk if it is not empty
	// otherwise, the current task will be waiting until a mem_blk is available.
	mutex_lock(&maru_brill_codec_blk_mutex);

	if (!list_empty(&maru_brill_codec->avail_memblk)) {
		elem =
			list_first_entry(&maru_brill_codec->avail_memblk, struct device_mem, entry);
		if (!elem) {
			ERROR("failed to get first entry from avail_memblk\n");
			ret = -1;
		} else {
			elem->blk_id = blk_id;
			elem->occupied = true;

			list_move_tail(&elem->entry, &maru_brill_codec->used_memblk);
			ret = elem->mem_offset;
		}
	} else {
		ERROR("no available memory block\n");
		ERROR("the number of buffer mutex: %d\n", maru_brill_codec_buffer_mutex.count);
		ret = -1;
	}

	mutex_unlock(&maru_brill_codec_blk_mutex);

	return ret;
}

static void release_device_memory(uint32_t mem_offset)
{
	struct device_mem *elem = NULL;
	struct list_head *pos, *temp;

	mutex_lock(&maru_brill_codec_blk_mutex);
	if (!list_empty(&maru_brill_codec->used_memblk)) {
		list_for_each_safe(pos, temp, &maru_brill_codec->used_memblk) {
			elem = list_entry(pos, struct device_mem, entry);
			if (elem->mem_offset == (uint32_t)mem_offset) {
				elem->blk_id = 0;
				elem->occupied = false;
				list_move(&elem->entry, &maru_brill_codec->avail_memblk);

				unlock_buffer();
				break;
			}
		}
	} else {
		DEBUG("there is no used memory block.\n");
	}
	mutex_unlock(&maru_brill_codec_blk_mutex);
}
#endif

static void release_device_memory(uint32_t mem_offset)
{
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

#if 0
static long maru_brill_codec_ioctl(struct file *file,
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
					maru_brill_codec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((uint32_t)file,
					maru_brill_codec->ioaddr + CODEC_CMD_POP_WRITE_QUEUE);
			LEAVE_CRITICAL_SECTION;

			if (copy_to_user((void *)arg, &value, sizeof(uint32_t))) {
				ERROR("ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}
	}
		break;
	case CODEC_CMD_GET_VERSION:
		DEBUG("%s version: %d\n", DEVICE_NAME, maru_brill_codec->version);

		if (copy_to_user((void *)arg, &maru_brill_codec->version, sizeof(int))) {
			ERROR("ioctl: failed to copy data to user\n");
			ret = -EIO;
		}
		break;
	case CODEC_CMD_GET_ELEMENT_INFO:
		DEBUG("request a device to get codec elements\n");

		ENTER_CRITICAL_SECTION;
		value = readl(maru_brill_codec->ioaddr + cmd);
		LEAVE_CRITICAL_SECTION;

		if (value < 0) {
			ERROR("ioctl: failed to get elements. %d\n", (int)value);
			ret = -EINVAL;
		}
		break;
	case CODEC_CMD_GET_CONTEXT_INDEX:
		DEBUG("request a device to get an index of codec context \n");

		value = readl(maru_brill_codec->ioaddr + cmd);
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

		if (mem_offset == maru_brill_codec->mem_size) {
			ERROR("offset of device memory is overflow!! 0x%x\n", mem_offset);
			ret = -EIO;
		} else {
			// notify that codec device can copy data to memory region.
			DEBUG("send a request to pop data from device. %p\n", file);

			ENTER_CRITICAL_SECTION;
			writel((uint32_t)mem_offset,
					maru_brill_codec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((uint32_t)file,
					maru_brill_codec->ioaddr + CODEC_CMD_POP_WRITE_QUEUE);
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

			ENTER_CRITICAL_SECTION;
			writel((uint32_t)value,
					maru_brill_codec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((uint32_t)file,
					maru_brill_codec->ioaddr + CODEC_CMD_POP_WRITE_QUEUE);
			LEAVE_CRITICAL_SECTION;

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
					maru_brill_codec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((uint32_t)file,
					maru_brill_codec->ioaddr + CODEC_CMD_POP_WRITE_QUEUE);
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
					maru_brill_codec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((uint32_t)file,
					maru_brill_codec->ioaddr + CODEC_CMD_POP_WRITE_QUEUE);
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
#endif

static long maru_brill_codec_ioctl(struct file *file,
			unsigned int cmd,
			unsigned long arg)
{
	long value = 0, ret = 0;

	switch (cmd) {
	case CODEC_CMD_GET_VERSION:
		DEBUG("%s version: %d\n", DEVICE_NAME, maru_brill_codec->version);

		if (copy_to_user((void *)arg, &maru_brill_codec->version, sizeof(int))) {
			ERROR("ioctl: failed to copy data to user\n");
			ret = -EIO;
		}
		break;
	case CODEC_CMD_GET_ELEMENT:
		DEBUG("request a device to get codec elements\n");

		ENTER_CRITICAL_SECTION;
		value = readl(maru_brill_codec->ioaddr + cmd);
		LEAVE_CRITICAL_SECTION;

		if (value < 0) {
			ERROR("ioctl: failed to get elements. %d\n", (int)value);
			ret = -EINVAL;
		}
		break;
	case CODEC_CMD_GET_CONTEXT_INDEX:
		DEBUG("request a device to get an index of codec context \n");

		value = readl(maru_brill_codec->ioaddr + cmd);
		if (value < 0 || value > CODEC_CONTEXT_SIZE) {
			ERROR("ioctl: failed to get proper context. %d\n", (int)value);
			ret = -EINVAL;
		} else if (copy_to_user((void *)arg, &value, sizeof(int))) {
			ERROR("ioctl: failed to copy data to user\n");
			ret = -EIO;
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

		if (mem_offset == maru_brill_codec->mem_size) {
			ERROR("offset of device memory is overflow!! 0x%x\n", mem_offset);
			ret = -EIO;
		} else {
			// notify that codec device can copy data to memory region.
			DEBUG("send a request to pop data from device. %p\n", file);

			ENTER_CRITICAL_SECTION;
			writel((uint32_t)mem_offset,
					maru_brill_codec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((uint32_t)file,
					maru_brill_codec->ioaddr + CODEC_CMD_GET_DATA_FROM_QUEUE);
			LEAVE_CRITICAL_SECTION;
		}
	}
		break;
	case CODEC_CMD_GET_DATA_FROM_SMALL_BUFFER:
		DEBUG("read small size of data from device memory\n");

		value =
			secure_s_device_memory((uint32_t)file);
		if (value < 0) {
			ERROR("failed to get available memory\n");
			ret = -EINVAL;
		} else {
			DEBUG("send a request to pop data from device. %p\n", file);

			ENTER_CRITICAL_SECTION;
			writel((uint32_t)value,
					maru_brill_codec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((uint32_t)file,
					maru_brill_codec->ioaddr + CODEC_CMD_GET_DATA_FROM_QUEUE);
			LEAVE_CRITICAL_SECTION;

			if (copy_to_user((void *)arg, &value, sizeof(int32_t))) {
				ERROR("ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}
		break;
	case CODEC_CMD_GET_DATA_FROM_MEDIUM_BUFFER:
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
					maru_brill_codec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((uint32_t)file,
					maru_brill_codec->ioaddr + CODEC_CMD_GET_DATA_FROM_QUEUE);
			LEAVE_CRITICAL_SECTION;

			if (copy_to_user((void *)arg, &value, sizeof(uint32_t))) {
				ERROR("ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}
		break;
	case CODEC_CMD_GET_DATA_FROM_LARGE_BUFFER:
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
					maru_brill_codec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((uint32_t)file,
					maru_brill_codec->ioaddr + CODEC_CMD_GET_DATA_FROM_QUEUE);
			LEAVE_CRITICAL_SECTION;

			if (copy_to_user((void *)arg, &value, sizeof(uint32_t))) {
				ERROR("ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}
		break;
	case CODEC_CMD_SECURE_SMALL_BUFFER:
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
	case CODEC_CMD_SECURE_MEDIUM_BUFFER:
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
	case CODEC_CMD_SECURE_LARGE_BUFFER:
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
	case CODEC_CMD_RELEASE_BUFFER:
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
	default:
		DEBUG("no available command.");
		ret = -EINVAL;
		break;
	}

	return ret;
}


static ssize_t maru_brill_codec_read(struct file *file, char __user *buf,
							size_t count, loff_t *fops)
{
	DEBUG("do nothing.\n");
	return 0;
}

/* Copy data between guest and host using mmap operation. */
static ssize_t maru_brill_codec_write(struct file *file, const char __user *buf,
							size_t count, loff_t *fops)
{
	struct codec_param ioparam = { 0 };
	int api_index, ctx_index;

	if (!maru_brill_codec) {
		ERROR("failed to get codec device info\n");
		return -EINVAL;
	}

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
					maru_brill_codec->ioaddr + CODEC_CMD_FILE_INDEX);
			writel((uint32_t)ioparam.mem_offset,
					maru_brill_codec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((int32_t)ioparam.ctx_index,
					maru_brill_codec->ioaddr + CODEC_CMD_CONTEXT_INDEX);
			writel((int32_t)ioparam.api_index,
					maru_brill_codec->ioaddr + CODEC_CMD_API_INDEX);
			LEAVE_CRITICAL_SECTION;

			wait_event_interruptible(wait_queue, context_flags[ctx_index] != 0);
			context_flags[ctx_index] = 0;
		}
			break;
		case CODEC_DECODE_VIDEO... CODEC_ENCODE_AUDIO:
		{
			ENTER_CRITICAL_SECTION;
			writel((uint32_t)file,
					maru_brill_codec->ioaddr + CODEC_CMD_FILE_INDEX);
			writel((uint32_t)ioparam.mem_offset,
					maru_brill_codec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((int32_t)ioparam.ctx_index,
					maru_brill_codec->ioaddr + CODEC_CMD_CONTEXT_INDEX);
			writel((int32_t)ioparam.api_index,
					maru_brill_codec->ioaddr + CODEC_CMD_API_INDEX);
			LEAVE_CRITICAL_SECTION;

			if (api_index == CODEC_ENCODE_VIDEO) {
				// in case of medium and large size of data
				release_device_memory(ioparam.mem_offset);
			} else {
				// in case of small size of data
				release_s_device_memory(ioparam.mem_offset);
			}

			wait_event_interruptible(wait_queue, context_flags[ctx_index] != 0);
			context_flags[ctx_index] = 0;
		}
			break;
		case CODEC_PICTURE_COPY:
		{
			ENTER_CRITICAL_SECTION;
			writel((uint32_t)file,
					maru_brill_codec->ioaddr + CODEC_CMD_FILE_INDEX);
			writel((uint32_t)ioparam.mem_offset,
					maru_brill_codec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((int32_t)ioparam.ctx_index,
					maru_brill_codec->ioaddr + CODEC_CMD_CONTEXT_INDEX);
			writel((int32_t)ioparam.api_index,
					maru_brill_codec->ioaddr + CODEC_CMD_API_INDEX);
			LEAVE_CRITICAL_SECTION;

			wait_event_interruptible(wait_queue, context_flags[ctx_index] != 0);
			context_flags[ctx_index] = 0;
		}
			break;
		case CODEC_DEINIT:
			ENTER_CRITICAL_SECTION;
			writel((uint32_t)file,
					maru_brill_codec->ioaddr + CODEC_CMD_FILE_INDEX);
			writel((int32_t)ioparam.ctx_index,
					maru_brill_codec->ioaddr + CODEC_CMD_CONTEXT_INDEX);
			writel((int32_t)ioparam.api_index,
					maru_brill_codec->ioaddr + CODEC_CMD_API_INDEX);
			LEAVE_CRITICAL_SECTION;
			break;
		default:
			ERROR("wrong api command: %d", api_index);
	}

	return 0;
}

static int maru_brill_codec_mmap(struct file *file, struct vm_area_struct *vm)
{
	unsigned long off;
	unsigned long phys_addr;
	unsigned long size;
	int ret = -1;

	size = vm->vm_end - vm->vm_start;
	if (size > maru_brill_codec->mem_size) {
		ERROR("over mapping size\n");
		return -EINVAL;
	}
	off = vm->vm_pgoff << PAGE_SHIFT;
	phys_addr = (PAGE_ALIGN(maru_brill_codec->mem_start) + off) >> PAGE_SHIFT;

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

static irqreturn_t maru_brill_codec_irq_handler(int irq, void *dev_id)
{
	struct maru_brill_codec_device *dev = (struct maru_brill_codec_device *)dev_id;
	unsigned long flags = 0;
	int val = 0;

	val = readl(dev->ioaddr + CODEC_CMD_GET_THREAD_STATE);
	if (!(val & CODEC_IRQ_TASK)) {
		return IRQ_NONE;
	}

	spin_lock_irqsave(&dev->lock, flags);

	DEBUG("handle an interrupt from codec device.\n");
	maru_brill_codec_bh(dev);

	spin_unlock_irqrestore(&dev->lock, flags);

	return IRQ_HANDLED;
}

static int maru_brill_codec_open(struct inode *inode, struct file *file)
{
	DEBUG("open! struct file:%p\n", file);

	/* register interrupt handler */
	if (request_irq(maru_brill_codec->dev->irq, maru_brill_codec_irq_handler,
		IRQF_SHARED, DEVICE_NAME, maru_brill_codec)) {
		ERROR("failed to register irq handle\n");
		return -EBUSY;
	}

	try_module_get(THIS_MODULE);

	return 0;
}

static int maru_brill_codec_release(struct inode *inode, struct file *file)
{
	/* free irq */
	if (maru_brill_codec->dev->irq) {
		DEBUG("free registered irq\n");
		free_irq(maru_brill_codec->dev->irq, maru_brill_codec);
	}

	DEBUG("%s. file: %p\n", __func__, file);

#if 0
	/* free resource */
	{
		struct device_mem *elem = NULL;
		struct list_head *pos, *temp;

		mutex_lock(&maru_brill_codec_blk_mutex);

		if (!list_empty(&maru_brill_codec->used_memblk)) {
			list_for_each_safe(pos, temp, &maru_brill_codec->used_memblk) {
				elem = list_entry(pos, struct device_mem, entry);
				if (elem->blk_id == (uint32_t)file) {
					DEBUG("move element(%p) to available memory block.\n", elem);

					elem->blk_id = 0;
					elem->occupied = false;
					list_move(&elem->entry, &maru_brill_codec->avail_memblk);

					unlock_buffer();
				}
			}
		} else {
			DEBUG("there is no used memory block.\n");
		}

		mutex_unlock(&maru_brill_codec_blk_mutex);
	}
#endif

	/* free resource */
	{
		struct device_mem *elem = NULL;
		struct list_head *pos, *temp;

		mutex_lock(&s_block_mutex);

		if (!list_empty(&maru_brill_codec->used_s_memblk)) {
			list_for_each_safe(pos, temp, &maru_brill_codec->used_s_memblk) {
				elem = list_entry(pos, struct device_mem, entry);
				if (elem->blk_id == (uint32_t)file) {
					DEBUG("move element(%p) to available memory block.\n", elem);

					elem->blk_id = 0;
					elem->occupied = false;
					list_move_tail(&elem->entry, &maru_brill_codec->avail_s_memblk);

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
			maru_brill_codec->ioaddr + CODEC_CMD_RELEASE_CONTEXT);
		LEAVE_CRITICAL_SECTION;
	}

	module_put(THIS_MODULE);

	return 0;
}

/* define file opertion for CODEC */
const struct file_operations maru_brill_codec_fops = {
	.owner			 = THIS_MODULE,
	.read			 = maru_brill_codec_read,
	.write			 = maru_brill_codec_write,
	.unlocked_ioctl	 = maru_brill_codec_ioctl,
	.open			 = maru_brill_codec_open,
	.mmap			 = maru_brill_codec_mmap,
	.release		 = maru_brill_codec_release,
};

static struct miscdevice codec_dev = {
	.minor			= MISC_DYNAMIC_MINOR,
	.name			= DEVICE_NAME,
	.fops			= &maru_brill_codec_fops,
	.mode			= S_IRUGO | S_IWUGO,
};

static void maru_brill_codec_get_device_version(void)
{
	maru_brill_codec->version =
		readl(maru_brill_codec->ioaddr + CODEC_CMD_GET_VERSION);

	INFO("device version: %d\n",
		maru_brill_codec->version);
}

static int __devinit maru_brill_codec_probe(struct pci_dev *pci_dev,
	const struct pci_device_id *pci_id)
{
	int ret = 0;

	INFO("%s: driver is probed.\n", DEVICE_NAME);

	maru_brill_codec = kzalloc(sizeof(struct maru_brill_codec_device), GFP_KERNEL);
	if (!maru_brill_codec) {
		ERROR("Failed to allocate memory for codec.\n");
		return -ENOMEM;
	}

	maru_brill_codec->dev = pci_dev;

#if 0
	INIT_LIST_HEAD(&maru_brill_codec->avail_memblk);
	INIT_LIST_HEAD(&maru_brill_codec->used_memblk);
#endif

	INIT_LIST_HEAD(&maru_brill_codec->avail_s_memblk);
	INIT_LIST_HEAD(&maru_brill_codec->used_s_memblk);
	INIT_LIST_HEAD(&maru_brill_codec->avail_m_memblk);
	INIT_LIST_HEAD(&maru_brill_codec->used_m_memblk);
	INIT_LIST_HEAD(&maru_brill_codec->avail_l_memblk);
	INIT_LIST_HEAD(&maru_brill_codec->used_l_memblk);

	maru_brill_codec_divide_device_memory();

	spin_lock_init(&maru_brill_codec->lock);

	if ((ret = pci_enable_device(pci_dev))) {
		ERROR("pci_enable_device failed\n");
		return ret;
	}
	pci_set_master(pci_dev);

	maru_brill_codec->mem_start = pci_resource_start(pci_dev, 0);
	maru_brill_codec->mem_size = pci_resource_len(pci_dev, 0);
	if (!maru_brill_codec->mem_start) {
		ERROR("pci_resource_start failed\n");
		pci_disable_device(pci_dev);
		return -ENODEV;
	}

	if (!request_mem_region(maru_brill_codec->mem_start,
				maru_brill_codec->mem_size,
				DEVICE_NAME)) {
		ERROR("request_mem_region failed\n");
		pci_disable_device(pci_dev);
		return -EINVAL;
	}

	maru_brill_codec->io_start = pci_resource_start(pci_dev, 1);
	maru_brill_codec->io_size = pci_resource_len(pci_dev, 1);
	if (!maru_brill_codec->io_start) {
		ERROR("pci_resource_start failed\n");
		release_mem_region(maru_brill_codec->mem_start, maru_brill_codec->mem_size);
		pci_disable_device(pci_dev);
		return -ENODEV;
	}

	if (!request_mem_region(maru_brill_codec->io_start,
				maru_brill_codec->io_size,
				DEVICE_NAME)) {
		ERROR("request_io_region failed\n");
		release_mem_region(maru_brill_codec->mem_start, maru_brill_codec->mem_size);
		pci_disable_device(pci_dev);
		return -EINVAL;
	}

	maru_brill_codec->ioaddr = ioremap_nocache(maru_brill_codec->io_start, maru_brill_codec->io_size);
	if (!maru_brill_codec->ioaddr) {
		ERROR("ioremap failed\n");
		release_mem_region(maru_brill_codec->io_start, maru_brill_codec->io_size);
		release_mem_region(maru_brill_codec->mem_start, maru_brill_codec->mem_size);
		pci_disable_device(pci_dev);
		return -EINVAL;
	}

	maru_brill_codec_get_device_version();

	if ((ret = misc_register(&codec_dev))) {
		ERROR("cannot register codec as misc\n");
		iounmap(maru_brill_codec->ioaddr);
		release_mem_region(maru_brill_codec->io_start, maru_brill_codec->io_size);
		release_mem_region(maru_brill_codec->mem_start, maru_brill_codec->mem_size);
		pci_disable_device(pci_dev);
		return ret;
	}

	return 0;
}

static void __devinit maru_brill_codec_remove(struct pci_dev *pci_dev)
{
	if (maru_brill_codec) {
		if (maru_brill_codec->ioaddr) {
			iounmap(maru_brill_codec->ioaddr);
			maru_brill_codec->ioaddr = NULL;
		}

		if (maru_brill_codec->io_start) {
			release_mem_region(maru_brill_codec->io_start,
					maru_brill_codec->io_size);
			maru_brill_codec->io_start = 0;
		}

		if (maru_brill_codec->mem_start) {
			release_mem_region(maru_brill_codec->mem_start,
					maru_brill_codec->mem_size);
			maru_brill_codec->mem_start = 0;
		}

		if (maru_brill_codec->elem) {
			kfree(maru_brill_codec->elem);
			maru_brill_codec->elem = NULL;
		}

		kfree(maru_brill_codec);
	}

	misc_deregister(&codec_dev);
	pci_disable_device(pci_dev);
}

static struct pci_device_id maru_brill_codec_pci_table[] __devinitdata = {
	{
		.vendor		= PCI_VENDOR_ID_TIZEN_EMUL,
		.device		= PCI_DEVICE_ID_VIRTUAL_BRILL_CODEC,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	},
	{},
};
MODULE_DEVICE_TABLE(pci, maru_brill_codec_pci_table);

static struct pci_driver driver = {
	.name		= DEVICE_NAME,
	.id_table	= maru_brill_codec_pci_table,
	.probe		= maru_brill_codec_probe,
	.remove		= maru_brill_codec_remove,
};

static int __init maru_brill_codec_init(void)
{
	INFO("driver is initialized.\n");

	maru_brill_codec_bh_workqueue = create_workqueue ("maru_brill_codec");
	if (!maru_brill_codec_bh_workqueue) {
		ERROR("failed to allocate workqueue\n");
		return -ENOMEM;
	}

	return pci_register_driver(&driver);
}

static void __exit maru_brill_codec_exit(void)
{
	INFO("driver is finalized.\n");

	if (maru_brill_codec_bh_workqueue) {
		destroy_workqueue (maru_brill_codec_bh_workqueue);
		maru_brill_codec_bh_workqueue = NULL;
	}
	pci_unregister_driver(&driver);
}
module_init(maru_brill_codec_init);
module_exit(maru_brill_codec_exit);
