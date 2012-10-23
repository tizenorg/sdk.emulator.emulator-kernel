/*
 * Virtual Codec PCI device driver
 *
 * Copyright (c) 2011-2012 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 *  Kitae KIM <kt920.kim@samsung.com>
 *  SeokYeon Hwang <syeon.hwang@samsung.com>
 *  DongKyun Yun <dk77.yun@samsung.com>
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

#define DEVICE_NAME	 "codec"
#define CODEC_MAJOR	 240

MODULE_DESCRIPTION("Virtual Codec Device Driver");
MODULE_AUTHOR("Kitae KIM <kt920.kim@samsung.com");
MODULE_LICENSE("GPL2");

#define CODEC_LOG(log_level, fmt, ...) \
	printk(log_level "%s: " fmt, DEVICE_NAME, ##__VA_ARGS__)

#define CODEC_IRQ 0x7f

/* Clean up source. : _param, svcodec_param_offset, paramindex */
struct codec_param {
	uint32_t api_index;
	uint32_t ctx_index;
	uint32_t mmap_offset;
	uint32_t ret;
};

enum codec_io_cmd {
	CODEC_API_INDEX = 0,
	CODEC_QUERY_STATE,
	CODEC_CONTEXT_INDEX,
	CODEC_MMAP_OFFSET,
	CODEC_FILE_INDEX,
	CODEC_CLOSED,
};

enum codec_api_index {
	EMUL_AV_REGISTER_ALL = 1,
	EMUL_AVCODEC_ALLOC_CONTEXT,
	EMUL_AVCODEC_OPEN,
	EMUL_AVCODEC_CLOSE,
	EMUL_AV_FREE,
	EMUL_AVCODEC_FLUSH_BUFFERS,
	EMUL_AVCODEC_DECODE_VIDEO,
	EMUL_AVCODEC_ENCODE_VIDEO,
	EMUL_AVCODEC_DECODE_AUDIO,
	EMUL_AVCODEC_ENCODE_AUDIO,
	EMUL_AV_PICTURE_COPY,
	EMUL_AV_PARSER_INIT,
	EMUL_AV_PARSER_PARSE,
	EMUL_AV_PARSER_CLOSE,
	EMUL_GET_CODEC_VER = 50,
	EMUL_LOCK_MEM_REGION,
	EMUL_UNLOCK_MEM_REGION,
};

struct svcodec_device {
	struct pci_dev *dev;

	/* I/O and Memory Region */
	unsigned int *ioaddr;
	unsigned int *memaddr;

	resource_size_t io_start;
	resource_size_t io_size;
	resource_size_t mem_start;
	resource_size_t mem_size;

	/* irq handler */
	wait_queue_head_t codec_wq;
	spinlock_t lock;
	int sleep_flag;

	int availableMem;
};

static struct svcodec_device *svcodec;
DEFINE_MUTEX(codec_mutex);

static long svcodec_ioctl(struct file *file,
			unsigned int cmd,
			unsigned long arg)
{
	mutex_lock(&codec_mutex);

	if (cmd == EMUL_LOCK_MEM_REGION) {
		if (svcodec->availableMem == 0)
			svcodec->availableMem = 1;
		else
			svcodec->availableMem = -1;

	} else if (cmd == EMUL_UNLOCK_MEM_REGION) {
		svcodec->availableMem = 0;
	} else {
		CODEC_LOG(KERN_ERR, "there is no command.\n");
	}

	if (copy_to_user((void *)arg,
			&svcodec->availableMem,
			sizeof(int)))
		CODEC_LOG(KERN_ERR, "failed to copy data to user\n");

	mutex_unlock(&codec_mutex);

	return 0;
}

static ssize_t svcodec_read(struct file *file, char __user *buf,
			size_t count, loff_t *fops)
{
	CODEC_LOG(KERN_INFO, "do nothing in the read operation.\n");
	return 0;
}

/* Copy data between guest and host using mmap operation. */
static ssize_t svcodec_write(struct file *file, const char __user *buf,
			size_t count, loff_t *fops)
{
	struct codec_param paramInfo;

	mutex_lock(&codec_mutex);

	if (!svcodec) {
		CODEC_LOG(KERN_ERR, "failed to get codec device info\n");
		return -EINVAL;
	}

	if (copy_from_user(&paramInfo, buf, sizeof(struct codec_param)))
		CODEC_LOG(KERN_ERR,
			"failed to get codec parameter info from user\n");

	if (paramInfo.api_index == EMUL_AVCODEC_ALLOC_CONTEXT)
		writel((uint32_t)file, svcodec->ioaddr + CODEC_FILE_INDEX);

	writel((uint32_t)paramInfo.ctx_index,
		svcodec->ioaddr + CODEC_CONTEXT_INDEX);
	writel((uint32_t)paramInfo.mmap_offset,
		svcodec->ioaddr + CODEC_MMAP_OFFSET);
	writel((uint32_t)paramInfo.api_index,
		svcodec->ioaddr + CODEC_API_INDEX);

	/* wait decoding or encoding job */
	if (paramInfo.api_index >= EMUL_AVCODEC_DECODE_VIDEO &&
		paramInfo.api_index <= EMUL_AVCODEC_ENCODE_AUDIO) {
		wait_event_interruptible(svcodec->codec_wq,
					svcodec->sleep_flag != 0);
		svcodec->sleep_flag = 0;
	}

	mutex_unlock(&codec_mutex);

	return 0;
}

static int svcodec_mmap(struct file *file, struct vm_area_struct *vm)
{
	unsigned long off;
	unsigned long phys_addr;
	unsigned long size;
	int ret = -1;

	off = vm->vm_pgoff << PAGE_SHIFT;
	phys_addr = (PAGE_ALIGN(svcodec->mem_start) + off) >> PAGE_SHIFT;
	size = vm->vm_end - vm->vm_start;

	if (size > svcodec->mem_size) {
		CODEC_LOG(KERN_ERR, "over mapping size\n");
		return -EINVAL;
	}

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

static irqreturn_t svcodec_irq_handler (int irq, void *dev_id)
{
	struct svcodec_device *dev = (struct svcodec_device *)dev_id;
	int val = 0;
	unsigned long flags;

/*	spin_lock_irqsave(&dev->lock, flags); */

	val = readl(dev->ioaddr + CODEC_QUERY_STATE);
	if (!(val & CODEC_IRQ)) {
/*		spin_unlock_irqrestore(&dev->lock, flags); */
		return IRQ_NONE;
	}

	spin_lock_irqsave(&dev->lock, flags);

	dev->sleep_flag = 1;
	wake_up_interruptible(&dev->codec_wq);

	spin_unlock_irqrestore(&dev->lock, flags);

	return IRQ_HANDLED;
}

static int svcodec_open(struct inode *inode, struct file *file)
{
	mutex_lock(&codec_mutex);
	CODEC_LOG(KERN_DEBUG, "open! struct file:%p\n", file);

	svcodec->sleep_flag = 0;

	/* register interrupt handler */
	if (request_irq(svcodec->dev->irq, svcodec_irq_handler,
		IRQF_SHARED, DEVICE_NAME, svcodec)) {
		CODEC_LOG(KERN_ERR, "failed to register irq handle\n");
		return -EBUSY;
	}

	try_module_get(THIS_MODULE);
	mutex_unlock(&codec_mutex);

	return 0;
}

static int svcodec_release(struct inode *inode, struct file *file)
{
	mutex_lock(&codec_mutex);

	/* free irq */
	if (svcodec->dev->irq) {
		CODEC_LOG(KERN_DEBUG, "free registered irq\n");
		free_irq(svcodec->dev->irq, svcodec);
	}
	svcodec->availableMem = 0;

	/* notify closing codec device of qemu. */
	if (file)
		writel((uint32_t)file, svcodec->ioaddr + CODEC_CLOSED);

	module_put(THIS_MODULE);
	mutex_unlock(&codec_mutex);

	return 0;
}

/* define file opertion for CODEC */
const struct file_operations svcodec_fops = {
	.owner			 = THIS_MODULE,
	.read			 = svcodec_read,
	.write			 = svcodec_write,
	.unlocked_ioctl	 = svcodec_ioctl,
	.open			 = svcodec_open,
	.mmap			 = svcodec_mmap,
	.release		 = svcodec_release,
};

static struct miscdevice codec_dev = {
	.minor			= MISC_DYNAMIC_MINOR,
	.name			= DEVICE_NAME,
	.fops			= &svcodec_fops,
	.mode			= S_IRUGO | S_IWUGO,
};

static int __devinit svcodec_probe(struct pci_dev *pci_dev,
	const struct pci_device_id *pci_id)
{
	int ret;

	svcodec = kmalloc(sizeof(struct svcodec_device), GFP_KERNEL);
	memset(svcodec, 0x00, sizeof(struct svcodec_device));

	init_waitqueue_head(&svcodec->codec_wq);
	spin_lock_init(&svcodec->lock);

	svcodec->dev = pci_dev;

	if (pci_enable_device(pci_dev)) {
		CODEC_LOG(KERN_ERR, "pci_enable_device failed\n");
		goto err_rel;
	}

	pci_set_master(pci_dev);

	svcodec->mem_start = pci_resource_start(pci_dev, 0);
	svcodec->mem_size = pci_resource_len(pci_dev, 0);

	if (!svcodec->mem_start) {
		CODEC_LOG(KERN_ERR, "pci_resource_start failed\n");
		goto err_out;
	}

	if (!request_mem_region(svcodec->mem_start,
				svcodec->mem_size,
				DEVICE_NAME)) {
		CODEC_LOG(KERN_ERR, "request_mem_region failed\n");
		goto err_out;
	}

	svcodec->io_start = pci_resource_start(pci_dev, 1);
	svcodec->io_size = pci_resource_len(pci_dev, 1);

	if (!svcodec->io_start) {
		CODEC_LOG(KERN_ERR, "pci_resource_start failed\n");
		goto err_mem_region;
	}

	if (!request_mem_region(svcodec->io_start,
				svcodec->io_size,
				DEVICE_NAME)) {
		CODEC_LOG(KERN_ERR, "request_io_region failed\n");
		goto err_mem_region;
	}

#if 0
	svcodec->memaddr = ioremap(svcodec->mem_start, svcodec->mem_size);
	if (!svcodec->memaddr) {
		CODEC_LOG(KERN_ERR, "[%s] : ioremap failed\n", __func__);
		goto err_io_region;
	}
#endif

	svcodec->ioaddr = ioremap_nocache(svcodec->io_start, svcodec->io_size);
	if (!svcodec->ioaddr) {
		CODEC_LOG(KERN_ERR, "ioremap failed\n");
		goto err_io_region;
	}

#if 0
	/* register chrdev */
	if (register_chrdev(CODEC_MAJOR, DEVICE_NAME, &svcodec_fops)) {
		CODEC_LOG(KERN_ERR, "register_chrdev failed\n");
		goto err_io_unmap;
	}
#endif
	ret = misc_register(&codec_dev);
	if (ret) {
		CODEC_LOG(KERN_ERR, "cannot register codec as misc\n");
		goto err_io_unmap;
	}

	return 0;

err_io_unmap:
	iounmap(svcodec->ioaddr);
#if 0
err_mem_unmap:
	iounmap(svcodec->memaddr);
#endif
err_io_region:
	release_mem_region(svcodec->io_start, svcodec->io_size);
err_mem_region:
	release_mem_region(svcodec->mem_start, svcodec->mem_size);
err_out:
	pci_disable_device(pci_dev);
err_rel:
	return -EIO;
}

static void __devinit svcodec_remove(struct pci_dev *pci_dev)
{
	if (svcodec) {
		if (svcodec->ioaddr) {
			iounmap(svcodec->ioaddr);
			svcodec->ioaddr = 0;
		}

#if 0
		if (svcodec->memaddr) {
			iounmap(svcodec->memaddr);
			svcodec->memaddr = 0;
		}
#endif

		if (svcodec->io_start) {
			release_mem_region(svcodec->io_start,
					svcodec->io_size);
			svcodec->io_start = 0;
		}

		if (svcodec->mem_start) {
			release_mem_region(svcodec->mem_start,
					svcodec->mem_size);
			svcodec->mem_start = 0;
		}

		kfree(svcodec);
	}

	misc_deregister(&codec_dev);
	pci_disable_device(pci_dev);
}

static struct pci_device_id svcodec_pci_table[] __devinitdata = {
	{
		.vendor = PCI_VENDOR_ID_TIZEN,
		.device = PCI_DEVICE_ID_VIRTUAL_CODEC,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
	},
	{},
};
MODULE_DEVICE_TABLE(pci, svcodec_pci_table);

/* define PCI Driver for CODEC */
static struct pci_driver driver = {
	.name = DEVICE_NAME,
	.id_table = svcodec_pci_table,
	.probe = svcodec_probe,
	.remove = svcodec_remove,
};

static int __init svcodec_init(void)
{
	CODEC_LOG(KERN_INFO, "device is initialized.\n");
	return pci_register_driver(&driver);
}

static void __exit svcodec_exit(void)
{
	pci_unregister_driver(&driver);
}
module_init(svcodec_init);
module_exit(svcodec_exit);
