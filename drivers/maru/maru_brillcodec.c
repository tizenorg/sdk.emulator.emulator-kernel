/*
 * Virtual Codec Device Driver
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 *	Kitae Kim <kt920.kim@samsung.com>
 *	SeokYeon Hwang <syeon.hwang@samsung.com>
 *	YeongKyoon Lee <yeongkyoon.lee@samsung.com>
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
#include <linux/spinlock.h>


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
#ifdef CODEC_DEBUG
#define DEBUG(fmt, ...) \
	printk(KERN_DEBUG "[%s][%d]: " fmt, DEVICE_NAME, __LINE__, ##__VA_ARGS__)

#define INFO(fmt, ...) \
	printk(KERN_INFO "[%s][%d]: " fmt, DEVICE_NAME, __LINE__, ##__VA_ARGS__)
#else
#define DEBUG(fmt, ...)

#define INFO(fmt, ...)
#endif

#define ERROR(fmt, ...) \
	printk(KERN_ERR "[%s][%d]: " fmt, DEVICE_NAME, __LINE__, ##__VA_ARGS__)

/* Define i/o and api values.  */
enum codec_io_cmd {
	CODEC_CMD_API_INDEX = 10,				// driver and device
	CODEC_CMD_CONTEXT_INDEX,
	CODEC_CMD_DEVICE_MEM_OFFSET = 13,
	CODEC_CMD_GET_THREAD_STATE,
	CODEC_CMD_GET_CTX_FROM_QUEUE,
	CODEC_CMD_GET_DATA_FROM_QUEUE,
	CODEC_CMD_RELEASE_CONTEXT,
	CODEC_CMD_GET_VERSION = 20,				// plugin, driver and device
	CODEC_CMD_GET_ELEMENT,
	CODEC_CMD_GET_CONTEXT_INDEX,
	CODEC_CMD_GET_ELEMENT_DATA,
	CODEC_CMD_PUT_DATA_INTO_BUFFER = 40,	// plugin and driver
	CODEC_CMD_SECURE_BUFFER,
	CODEC_CMD_TRY_SECURE_BUFFER,
	CODEC_CMD_RELEASE_BUFFER,
	CODEC_CMD_INVOKE_API_AND_RELEASE_BUFFER,
};

enum codec_api_index {
	CODEC_INIT = 0,
	CODEC_DECODE_VIDEO,
	CODEC_ENCODE_VIDEO,
	CODEC_DECODE_AUDIO,
	CODEC_ENCODE_AUDIO,
	CODEC_PICTURE_COPY,
	CODEC_DEINIT,
	CODEC_FLUSH_BUFFERS,
};

struct codec_param {
	int32_t api_index;
	int32_t ctx_index;
	int32_t mem_offset;
};

struct codec_element {
	void	*buf;
	uint32_t buf_size;
};

struct codec_buffer_id {
	uint32_t buffer_index;
	uint32_t buffer_size;
};

struct context_id {
	uint32_t id;

	struct list_head node;
};

struct user_process_id {
	uint32_t id;
	struct list_head ctx_id_mgr;

	struct list_head pid_node;
};

/* manage device memory block */
struct device_mem {
	uint32_t ctx_id;
	uint32_t mem_offset;

	struct list_head entry;
};

struct memory_block {
	uint32_t unit_size;
	uint32_t n_units;

	uint32_t start_offset;
	uint32_t end_offset;

	bool last_buf_secured;

	struct device_mem *units;

	struct list_head available;
	struct list_head occupied;

	struct semaphore semaphore;
	struct semaphore last_buf_semaphore;
	struct mutex access_mutex;
};

struct maru_brill_codec_device {
	struct pci_dev *dev;

	/* I/O and Memory Region */
	unsigned int *ioaddr;

	resource_size_t io_start;
	resource_size_t io_size;
	resource_size_t mem_start;
	resource_size_t mem_size;

	struct list_head user_pid_mgr;

	/* task queue */
	struct memory_block memory_blocks[3];

	spinlock_t lock;

	int version;
	bool codec_elem_cached;
	struct codec_element codec_elem;
};

/* device memory */
#define CODEC_CONTEXT_SIZE	1024

#define CODEC_S_DEVICE_MEM_COUNT	15	// small		(256K)	4M
#define CODEC_M_DEVICE_MEM_COUNT	8	// medium		(2M)	16M
#define CODEC_L_DEVICE_MEM_COUNT	3	// large		(4M)	12M

#define CODEC_S_DEVICE_MEM_SIZE		0x40000		// small
#define CODEC_M_DEVICE_MEM_SIZE		0x200000	// medium
#define CODEC_L_DEVICE_MEM_SIZE		0x400000	// large

enum block_size { SMALL, MEDIUM, LARGE };

static struct maru_brill_codec_device *maru_brill_codec;
static int context_flags[CODEC_CONTEXT_SIZE] = { 0, };

// define critical section
DEFINE_SPINLOCK(critical_section);

#define ENTER_CRITICAL_SECTION(flags)	spin_lock_irqsave(&critical_section, flags);
#define LEAVE_CRITICAL_SECTION(flags)	spin_unlock_irqrestore(&critical_section, flags);

// bottom-half
static DECLARE_WAIT_QUEUE_HEAD(wait_queue);

static struct workqueue_struct *maru_brill_codec_bh_workqueue;
static void maru_brill_codec_bh_func(struct work_struct *work);
static DECLARE_WORK(maru_brill_codec_bh_work, maru_brill_codec_bh_func);
static void maru_brill_codec_bh(struct maru_brill_codec_device *dev);

static void maru_brill_codec_context_add(uint32_t user_pid, uint32_t ctx_id);
static int maru_brill_codec_invoke_api_and_release_buffer(void *opaque);

static void maru_brill_codec_divide_device_memory(void)
{
	int i = 0, cnt = 0;
	// First small memory block reserved for meta data
	int offset = CODEC_S_DEVICE_MEM_SIZE;
	for (i = 0; i < 3; ++i) {
		struct memory_block *block = &maru_brill_codec->memory_blocks[i];
		block->start_offset = offset;
		for (cnt = 0; cnt < block->n_units; cnt++) {
			block->units[cnt].mem_offset = offset;
			list_add_tail(&block->units[cnt].entry, &block->available);

			offset += block->unit_size;
		}
		block->end_offset = offset;
	}
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


static int secure_device_memory(uint32_t ctx_id, uint32_t buf_size,
		int non_blocking, uint32_t* offset)
{
	int ret = 0;
	struct device_mem *unit = NULL;
	enum block_size index = SMALL;
	struct memory_block* block = NULL;

	if (buf_size < CODEC_S_DEVICE_MEM_SIZE) {
		index = SMALL;
	} else if (buf_size < CODEC_M_DEVICE_MEM_SIZE) {
		index = MEDIUM;
	} else if (buf_size < CODEC_L_DEVICE_MEM_SIZE) {
		index = LARGE;
	} else {
		ERROR("invalid buffer size: %x\n", buf_size);
	}

	block = &maru_brill_codec->memory_blocks[index];

	// decrease buffer_semaphore
	DEBUG("before down buffer_sema: %d\n", block->semaphore.count);

	if (non_blocking) {
		if (down_trylock(&block->semaphore)) { // if 1
			DEBUG("buffer is not available now\n");
			return -1;
		}
	} else {
		if (down_trylock(&block->semaphore)) { // if 1
			if (down_interruptible(&block->last_buf_semaphore)) { // if -EINTR
				DEBUG("down_interruptible interrupted\n");
				return -1;
			}
			block->last_buf_secured = 1; // protected under last_buf_semaphore
			ret = 1;
			DEBUG("lock last buffer semaphore.\n");
		}
	}

	DEBUG("after down buffer_sema: %d\n", block->semaphore.count);

	mutex_lock(&block->access_mutex);
	unit = list_first_entry(&block->available, struct device_mem, entry);
	if (!unit) {
		// available unit counts are protected under semaphore.
		// so can not enter here...
		ret = -1;
		if (block->last_buf_secured) {
			up(&block->last_buf_semaphore);
		} else {
			up(&block->semaphore);
		}
		ERROR("failed to get memory block.\n");
	} else {
		unit->ctx_id = ctx_id;
		list_move_tail(&unit->entry, &block->occupied);
		*offset = unit->mem_offset;
		DEBUG("get available memory region: 0x%x\n", ret);
	}
	mutex_unlock(&block->access_mutex);

	return ret;
}

static void release_device_memory(uint32_t mem_offset)
{
	struct device_mem *unit = NULL;
	enum block_size index = SMALL;
	struct memory_block *block = NULL;
	bool found = false;

	struct list_head *pos, *temp;

	if (mem_offset < maru_brill_codec->memory_blocks[0].end_offset)	{
		index = SMALL;
	} else if (mem_offset < maru_brill_codec->memory_blocks[1].end_offset) {
		index = MEDIUM;
	} else if (mem_offset < maru_brill_codec->memory_blocks[2].end_offset) {
		index = LARGE;
	} else {
		// error
	}

	block = &maru_brill_codec->memory_blocks[index];

	mutex_lock(&block->access_mutex);
	if (!list_empty(&block->occupied)) {
		list_for_each_safe(pos, temp, &block->occupied) {
			unit = list_entry(pos, struct device_mem, entry);
			if (unit->mem_offset == (uint32_t)mem_offset) {
				unit->ctx_id = 0;
				list_move_tail(&unit->entry, &block->available);

				if (block->last_buf_secured) {
					block->last_buf_secured = 0;
					up(&block->last_buf_semaphore);
					DEBUG("unlock last buffer semaphore.\n");
				} else {
					up(&block->semaphore);
					DEBUG("unlock semaphore: %d.\n", block->semaphore.count);
				}

				found = true;

				break;
			}
		}
		if (!found) {
			// can not enter here...
			ERROR("there is no used memory block. offset = 0x%x.\n", (uint32_t)mem_offset);
		}
	} else {
		// can not enter here...
		ERROR("there is no used memory block.\n");
	}

	mutex_unlock(&block->access_mutex);
}

static void dispose_device_memory(uint32_t context_id)
{
	struct device_mem *unit = NULL;
	int index = 0;
	struct memory_block *block = NULL;

	struct list_head *pos, *temp;

	for (index = SMALL; index <= LARGE; index++) {
		block = &maru_brill_codec->memory_blocks[index];

		mutex_lock(&block->access_mutex);
		if (!list_empty(&block->occupied)) {
			list_for_each_safe(pos, temp, &block->occupied) {
				unit = list_entry(pos, struct device_mem, entry);
				if (unit->ctx_id == context_id) {
					unit->ctx_id = 0;
					list_move_tail(&unit->entry, &block->available);
					INFO("dispose memory block: %x", unit->mem_offset);
				}
			}
		}
		mutex_unlock(&block->access_mutex);
	}
}

static void maru_brill_codec_info_cache(void)
{
	void __iomem *memaddr = NULL;
	void *codec_info = NULL;
	uint32_t codec_info_len = 0;

	memaddr = ioremap(maru_brill_codec->mem_start,
						maru_brill_codec->mem_size);
	if (!memaddr) {
		ERROR("ioremap failed\n");
		return;
	}

	codec_info_len = *(uint32_t *)memaddr;

	codec_info =
		kzalloc(codec_info_len, GFP_KERNEL);
	if (!codec_info) {
		ERROR("falied to allocate codec_info memory!\n");
		return;
	}

	memcpy(codec_info, (uint8_t *)memaddr + sizeof(uint32_t), codec_info_len);
	iounmap(memaddr);

	maru_brill_codec->codec_elem.buf = codec_info;
	maru_brill_codec->codec_elem.buf_size = codec_info_len;
	maru_brill_codec->codec_elem_cached = true;
}

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
	{
		uint32_t len = 0;
		unsigned long flags;

		DEBUG("request a device to get codec elements\n");

		ENTER_CRITICAL_SECTION(flags);
		if (!maru_brill_codec->codec_elem_cached) {
			value = readl(maru_brill_codec->ioaddr + cmd);
			if (value < 0) {
				ERROR("ioctl: failed to get elements. %d\n", (int)value);
				ret = -EINVAL;
			}
			maru_brill_codec_info_cache();
		}
		len = maru_brill_codec->codec_elem.buf_size;
		LEAVE_CRITICAL_SECTION(flags);

		if (copy_to_user((void *)arg, &len, sizeof(uint32_t))) {
			ERROR("ioctl: failed to copy data to user\n");
			ret = -EIO;
		}
	}
		break;

	case CODEC_CMD_GET_ELEMENT_DATA:
	{
		void *codec_elem = NULL;
		uint32_t elem_len = maru_brill_codec->codec_elem.buf_size;

		DEBUG("request codec elements.\n");

		codec_elem = maru_brill_codec->codec_elem.buf;
		if (!codec_elem) {
			ERROR("ioctl: codec elements is empty\n");
			ret = -EIO;
		} else if (copy_to_user((void *)arg, codec_elem, elem_len)) {
			ERROR("ioctl: failed to copy data to user\n");
			ret = -EIO;
		}
	}
		break;
	case CODEC_CMD_GET_CONTEXT_INDEX:
	{
		DEBUG("request a device to get an index of codec context \n");

		value = readl(maru_brill_codec->ioaddr + cmd);
		if (value < 1 || value > (CODEC_CONTEXT_SIZE - 1)) {
			ERROR("ioctl: failed to get proper context. %d\n", (int)value);
			ret = -EINVAL;
		} else {
			// task_id & context_id
			DEBUG("add context. ctx_id: %d\n", (int)value);
			maru_brill_codec_context_add((uint32_t)file, value);

			if (copy_to_user((void *)arg, &value, sizeof(int))) {
				ERROR("ioctl: failed to copy data to user\n");
				ret = -EIO;
			}
		}
	}
		break;
	case CODEC_CMD_PUT_DATA_INTO_BUFFER:
	{
		uint32_t offset = 0;
		unsigned long flags;
		struct codec_buffer_id opaque;

		DEBUG("read data into small buffer\n");
		if (copy_from_user(&opaque, (void *)arg, sizeof(struct codec_buffer_id))) {
			ERROR("ioctl: failed to copy data from user\n");
			ret = -EIO;
			break;
		}

		value = secure_device_memory(opaque.buffer_index, opaque.buffer_size, 0, &offset);
		if (value < 0) {
			DEBUG("failed to get available memory\n");
			ret = -EINVAL;
		} else {
			DEBUG("send a request to pop data from device. %d\n", opaque.buffer_index);

			ENTER_CRITICAL_SECTION(flags);
			writel((uint32_t)offset,
					maru_brill_codec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
			writel((uint32_t)opaque.buffer_index,
					maru_brill_codec->ioaddr + CODEC_CMD_GET_DATA_FROM_QUEUE);
			LEAVE_CRITICAL_SECTION(flags);

			opaque.buffer_size = offset;
			if (copy_to_user((void *)arg, &opaque, sizeof(struct codec_buffer_id))) {
				ERROR("ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}

		/* 1 means that only an available buffer is left at the moment.
		 * gst-plugins-emulator will allocate heap buffer to store
		   output buffer of codec.
		 */
		if (value == 1) {
			ret = 1;
		}
	}
		break;
	case CODEC_CMD_SECURE_BUFFER:
	{
		uint32_t offset = 0;
		struct codec_buffer_id opaque;

		DEBUG("read data into small buffer\n");
		if (copy_from_user(&opaque, (void *)arg, sizeof(struct codec_buffer_id))) {
			ERROR("ioctl: failed to copy data from user\n");
			ret = -EIO;
			break;
		}

		value = secure_device_memory(opaque.buffer_index, opaque.buffer_size, 0, &offset);
		if (value < 0) {
			DEBUG("failed to get available memory\n");
			ret = -EINVAL;
		} else {
			opaque.buffer_size = offset;
			if (copy_to_user((void *)arg, &opaque, sizeof(struct codec_buffer_id))) {
				ERROR("ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}
	}
		break;
	case CODEC_CMD_TRY_SECURE_BUFFER:
	{
		uint32_t offset = 0;
		struct codec_buffer_id opaque;

		DEBUG("read data into small buffer\n");
		if (copy_from_user(&opaque, (void *)arg, sizeof(struct codec_buffer_id))) {
			ERROR("ioctl: failed to copy data from user\n");
			ret = -EIO;
			break;
		}

		value = secure_device_memory(opaque.buffer_index, opaque.buffer_size, 1, &offset);
		if (value < 0) {
			DEBUG("failed to get available memory\n");
			ret = -EINVAL;
		} else {
			opaque.buffer_size = offset;
			if (copy_to_user((void *)arg, &opaque, sizeof(struct codec_buffer_id))) {
				ERROR("ioctl: failed to copy data to user.\n");
				ret = -EIO;
			}
		}
		break;
	}
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
	case CODEC_CMD_INVOKE_API_AND_RELEASE_BUFFER:
	{
		struct codec_param ioparam = { 0 };

		if (copy_from_user(&ioparam, (void *)arg, sizeof(struct codec_param))) {
			ERROR("failed to get codec parameter info from user\n");
			return -EIO;
		}

		ret = maru_brill_codec_invoke_api_and_release_buffer(&ioparam);
	}
		break;
	default:
		DEBUG("no available command.");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int maru_brill_codec_invoke_api_and_release_buffer(void *opaque)
{
	struct codec_param *ioparam = (struct codec_param *)opaque;
	int api_index, ctx_index;
	unsigned long flags;

	DEBUG("enter %s\n", __func__);

	api_index = ioparam->api_index;
	ctx_index = ioparam->ctx_index;

	switch (api_index) {
	case CODEC_INIT:
	{
		ENTER_CRITICAL_SECTION(flags);
		writel((uint32_t)ioparam->mem_offset,
				maru_brill_codec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
		writel((int32_t)ioparam->ctx_index,
				maru_brill_codec->ioaddr + CODEC_CMD_CONTEXT_INDEX);
		writel((int32_t)ioparam->api_index,
				maru_brill_codec->ioaddr + CODEC_CMD_API_INDEX);
		LEAVE_CRITICAL_SECTION(flags);

		wait_event_interruptible(wait_queue, context_flags[ctx_index] != 0);
		context_flags[ctx_index] = 0;
	}
		break;
	case CODEC_DECODE_VIDEO ... CODEC_ENCODE_AUDIO:
	{
		ENTER_CRITICAL_SECTION(flags);
		writel((uint32_t)ioparam->mem_offset,
				maru_brill_codec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
		writel((int32_t)ioparam->ctx_index,
				maru_brill_codec->ioaddr + CODEC_CMD_CONTEXT_INDEX);
		writel((int32_t)ioparam->api_index,
				maru_brill_codec->ioaddr + CODEC_CMD_API_INDEX);
		LEAVE_CRITICAL_SECTION(flags);

		release_device_memory(ioparam->mem_offset);

		wait_event_interruptible(wait_queue, context_flags[ctx_index] != 0);
		context_flags[ctx_index] = 0;
	}
		break;
	case CODEC_PICTURE_COPY:
	{
		ENTER_CRITICAL_SECTION(flags);
		writel((uint32_t)ioparam->mem_offset,
				maru_brill_codec->ioaddr + CODEC_CMD_DEVICE_MEM_OFFSET);
		writel((int32_t)ioparam->ctx_index,
				maru_brill_codec->ioaddr + CODEC_CMD_CONTEXT_INDEX);
		writel((int32_t)ioparam->api_index,
				maru_brill_codec->ioaddr + CODEC_CMD_API_INDEX);
		LEAVE_CRITICAL_SECTION(flags);

		wait_event_interruptible(wait_queue, context_flags[ctx_index] != 0);
		context_flags[ctx_index] = 0;
	}
		break;
	case CODEC_DEINIT:
		ENTER_CRITICAL_SECTION(flags);
		writel((int32_t)ioparam->ctx_index,
				maru_brill_codec->ioaddr + CODEC_CMD_CONTEXT_INDEX);
		writel((int32_t)ioparam->api_index,
				maru_brill_codec->ioaddr + CODEC_CMD_API_INDEX);
		LEAVE_CRITICAL_SECTION(flags);

		dispose_device_memory(ioparam->ctx_index);
		break;
	case CODEC_FLUSH_BUFFERS:
		ENTER_CRITICAL_SECTION(flags);
		writel((int32_t)ioparam->ctx_index,
				maru_brill_codec->ioaddr + CODEC_CMD_CONTEXT_INDEX);
		writel((int32_t)ioparam->api_index,
				maru_brill_codec->ioaddr + CODEC_CMD_API_INDEX);
		LEAVE_CRITICAL_SECTION(flags);
		break;
	default:
		DEBUG("invalid API commands: %d", api_index);
		break;
	}

	DEBUG("leave %s\n", __func__);

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

static void maru_brill_codec_context_add(uint32_t user_pid, uint32_t ctx_id)
{
	struct list_head *pos, *temp;
	struct user_process_id *pid_elem = NULL;
	struct context_id *cid_elem = NULL;
	unsigned long flags;

	DEBUG("enter: %s\n", __func__);

	DEBUG("before inserting context. user_pid: %x, ctx_id: %d\n",
			user_pid, ctx_id);

	ENTER_CRITICAL_SECTION(flags);
	if (!list_empty(&maru_brill_codec->user_pid_mgr)) {
		list_for_each_safe(pos, temp, &maru_brill_codec->user_pid_mgr) {
			pid_elem = list_entry(pos, struct user_process_id, pid_node);

			DEBUG("add context. pid_elem: %p\n", pid_elem);
			if (pid_elem && pid_elem->id == user_pid) {

				DEBUG("add context. user_pid: %x, ctx_id: %d\n",
						user_pid, ctx_id);

				cid_elem = kzalloc(sizeof(struct context_id), GFP_KERNEL);
				if (!cid_elem) {
					ERROR("failed to allocate context_mgr memory\n");
					return;
				}

				INIT_LIST_HEAD(&cid_elem->node);

				DEBUG("add context. user_pid: %x, pid_elem: %p, cid_elem: %p, node: %p\n",
						user_pid, pid_elem, cid_elem, &cid_elem->node);

				cid_elem->id = ctx_id;
				list_add_tail(&cid_elem->node, &pid_elem->ctx_id_mgr);
			}
		}
	} else {
		DEBUG("user_pid_mgr is empty\n");
	}
	LEAVE_CRITICAL_SECTION(flags);

	DEBUG("leave: %s\n", __func__);
}

static void maru_brill_codec_context_remove(struct user_process_id *pid_elem)
{
	struct list_head *pos, *temp;
	struct context_id *cid_elem = NULL;

	DEBUG("enter: %s\n", __func__);

	if (!list_empty(&pid_elem->ctx_id_mgr)) {
		list_for_each_safe(pos, temp, &pid_elem->ctx_id_mgr) {
			cid_elem = list_entry(pos, struct context_id, node);
			if (cid_elem) {
				if (cid_elem->id > 0 && cid_elem->id < CODEC_CONTEXT_SIZE) {
					DEBUG("remove context. ctx_id: %d\n", cid_elem->id);
					writel(cid_elem->id,
							maru_brill_codec->ioaddr + CODEC_CMD_RELEASE_CONTEXT);
					dispose_device_memory(cid_elem->id);
				}

				DEBUG("delete node from ctx_id_mgr. %p\n", &cid_elem->node);
				__list_del_entry(&cid_elem->node);
				DEBUG("release cid_elem. %p\n", cid_elem);
				kfree(cid_elem);
			} else {
				DEBUG("no context in the pid_elem\n");
			}
		}
	} else {
		DEBUG("ctx_id_mgr is empty. user_pid: %x\n", pid_elem->id);
	}
	DEBUG("leave: %s\n", __func__);
}

static void maru_brill_codec_task_add(uint32_t user_pid)
{
	struct user_process_id *pid_elem = NULL;
	unsigned long flags;

	DEBUG("enter: %s\n", __func__);

	ENTER_CRITICAL_SECTION(flags);
	pid_elem = kzalloc(sizeof(struct user_process_id), GFP_KERNEL);
	if (!pid_elem) {
		ERROR("failed to allocate user_process memory\n");
		return;
	}

	INIT_LIST_HEAD(&pid_elem->pid_node);
	INIT_LIST_HEAD(&pid_elem->ctx_id_mgr);

	DEBUG("add task. user_pid: %x, pid_elem: %p, pid_node: %p\n",
		user_pid, pid_elem, &pid_elem->pid_node);
	pid_elem->id = user_pid;
	list_add_tail(&pid_elem->pid_node, &maru_brill_codec->user_pid_mgr);
	LEAVE_CRITICAL_SECTION(flags);

	DEBUG("leave: %s\n", __func__);
}

static void maru_brill_codec_task_remove(uint32_t user_pid)
{
	struct list_head *pos, *temp;
	struct user_process_id *pid_elem = NULL;
	unsigned long flags;

	DEBUG("enter: %s\n", __func__);

	ENTER_CRITICAL_SECTION(flags);
	if (!list_empty(&maru_brill_codec->user_pid_mgr)) {
		list_for_each_safe(pos, temp, &maru_brill_codec->user_pid_mgr) {
			pid_elem = list_entry(pos, struct user_process_id, pid_node);
			if (pid_elem) {
				if (pid_elem->id == user_pid) {
					// remove task and codec contexts that is running in the task.
					DEBUG("remove task. user_pid: %x, pid_elem: %p\n",
							user_pid, pid_elem);
					maru_brill_codec_context_remove(pid_elem);
				}

				DEBUG("move pid_node from user_pid_mgr. %p\n", &pid_elem->pid_node);
				__list_del_entry(&pid_elem->pid_node);
				DEBUG("release pid_elem. %p\n", pid_elem);
				kfree(pid_elem);
			} else {
				DEBUG("no task in the user_pid_mgr\n");
			}
		}
	} else {
		DEBUG("user_pid_mgr is empty\n");
	}
	LEAVE_CRITICAL_SECTION(flags);

	DEBUG("leave: %s\n", __func__);
}


static int maru_brill_codec_open(struct inode *inode, struct file *file)
{
	DEBUG("open! struct file: %p\n", file);

	/* register interrupt handler */
	if (request_irq(maru_brill_codec->dev->irq, maru_brill_codec_irq_handler,
		IRQF_SHARED, DEVICE_NAME, maru_brill_codec)) {
		ERROR("failed to register irq handle\n");
		return -EBUSY;
	}

	maru_brill_codec_task_add((uint32_t)file);

	try_module_get(THIS_MODULE);

	return 0;
}

static int maru_brill_codec_release(struct inode *inode, struct file *file)
{
	DEBUG("close! struct file: %p\n", file);

	/* free irq */
	if (maru_brill_codec->dev->irq) {
		DEBUG("free registered irq\n");
		free_irq(maru_brill_codec->dev->irq, maru_brill_codec);
	}

	DEBUG("before removing task: %x\n", (uint32_t)file);
	/* free resource */
	maru_brill_codec_task_remove((uint32_t)file);

	module_put(THIS_MODULE);

	return 0;
}

/* define file opertion for CODEC */
const struct file_operations maru_brill_codec_fops = {
	.owner			 = THIS_MODULE,
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

	printk(KERN_INFO "%s: device version: %d\n",
		DEVICE_NAME, maru_brill_codec->version);
}

static int __devinit maru_brill_codec_probe(struct pci_dev *pci_dev,
	const struct pci_device_id *pci_id)
{
	int ret = 0;
	int index = 0;

	printk(KERN_INFO "%s: driver is probed.\n", DEVICE_NAME);

	maru_brill_codec =
		kzalloc(sizeof(struct maru_brill_codec_device), GFP_KERNEL);
	if (!maru_brill_codec) {
		ERROR("Failed to allocate memory for codec.\n");
		return -ENOMEM;
	}

	maru_brill_codec->dev = pci_dev;

	INIT_LIST_HEAD(&maru_brill_codec->user_pid_mgr);

	// initialize memory block structures
	maru_brill_codec->memory_blocks[0].unit_size = CODEC_S_DEVICE_MEM_SIZE;
	maru_brill_codec->memory_blocks[0].n_units = CODEC_S_DEVICE_MEM_COUNT;
	maru_brill_codec->memory_blocks[1].unit_size = CODEC_M_DEVICE_MEM_SIZE;
	maru_brill_codec->memory_blocks[1].n_units = CODEC_M_DEVICE_MEM_COUNT;
	maru_brill_codec->memory_blocks[2].unit_size = CODEC_L_DEVICE_MEM_SIZE;
	maru_brill_codec->memory_blocks[2].n_units = CODEC_L_DEVICE_MEM_COUNT;

	for (index = 0; index < 3; ++index) {
		struct memory_block *block = &maru_brill_codec->memory_blocks[index];
		block->units =
			kzalloc(sizeof(struct device_mem) * block->n_units, GFP_KERNEL);

		block->last_buf_secured = 0;

		INIT_LIST_HEAD(&block->available);
		INIT_LIST_HEAD(&block->occupied);
		sema_init(&block->semaphore, block->n_units - 1);
		sema_init(&block->last_buf_semaphore, 1);
		mutex_init(&block->access_mutex);
	}

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

	maru_brill_codec->ioaddr =
		ioremap_nocache(maru_brill_codec->io_start, maru_brill_codec->io_size);
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

/*
		if (maru_brill_codec->units) {
// FIXME
//			kfree(maru_brill_codec->elem);
			maru_brill_codec->units= NULL;
		}
*/

		if (maru_brill_codec->codec_elem.buf) {
			kfree(maru_brill_codec->codec_elem.buf);
			maru_brill_codec->codec_elem.buf = NULL;
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
	printk(KERN_INFO "%s: driver is initialized.\n", DEVICE_NAME);

	maru_brill_codec_bh_workqueue = create_workqueue ("maru_brill_codec");
	if (!maru_brill_codec_bh_workqueue) {
		ERROR("failed to allocate workqueue\n");
		return -ENOMEM;
	}

	return pci_register_driver(&driver);
}

static void __exit maru_brill_codec_exit(void)
{
	printk(KERN_INFO "%s: driver is finalized.\n", DEVICE_NAME);

	if (maru_brill_codec_bh_workqueue) {
		destroy_workqueue (maru_brill_codec_bh_workqueue);
		maru_brill_codec_bh_workqueue = NULL;
	}
	pci_unregister_driver(&driver);
}
module_init(maru_brill_codec_init);
module_exit(maru_brill_codec_exit);
