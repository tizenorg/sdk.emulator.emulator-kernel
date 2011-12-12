/*
 * Virtual Codec driver
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd All Rights Reserved
 *
 * Contact : Kitae KIM <kt920.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the BSD Licence, GNU General Public License
 * as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include "avformat.h" 

#define DRIVER_NAME		"svcodec"
#define CODEC_MAJOR		240

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kitae KIM <kt920.kim@samsung.com");
MODULE_DESCRIPTION("Virtual Codec Driver for Emulator");

// #define CODEC_DEBUG

#ifdef CODEC_DEBUG
#define SVCODEC_LOG(fmt, ...) \
    printk(KERN_INFO "[%s][%s][%d]" fmt, DRIVER_NAME, __func__, __LINE__, ##__VA_ARGS__)
#else
#define SVCODEC_LOG(fmt, ...) ((void)0)
#endif

struct _param {
	uint32_t apiIndex;
	uint32_t in_args_num;
	uint32_t in_args[10];
	uint32_t ret;
};

enum svodec_param_offset {
	CODEC_API_INDEX = 0,
	CODEC_IN_PARAM,
	CODEC_RETURN_VALUE,
	CODEC_READY_TO_GET_DATA,
	CODEC_GET_RESULT_DATA,
};

typedef struct _svcodec_dev {
	struct pci_dev *dev;				/* pci device */

	volatile unsigned int *ioaddr;
	volatile unsigned int *memaddr;

	resource_size_t io_start;
	resource_size_t io_size;
	resource_size_t mem_start;
	resource_size_t mem_size;

	uint8_t *imgBuf;
/*	struct semaphore sem;
	int wake_up; */
} svcodec_dev;	

static struct pci_device_id svcodec_pci_table[] __devinitdata = {
	{
	.vendor 	= PCI_VENDOR_ID_SAMSUNG,
	.device		= PCI_DEVICE_ID_VIRTUAL_CODEC,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
};
MODULE_DEVICE_TABLE(pci, svcodec_pci_table);

static svcodec_dev *svcodec;

#if 0
static void call_workqueue(void *data);
DECLARE_WAIT_QUEUE_HEAD(waitqueue_read);
DECLARE_WORK(work_queue, call_workqueue);
#endif

static int svcodec_open (struct inode *inode, struct file *file)
{
	SVCODEC_LOG("\n");
	try_module_get(THIS_MODULE);

	/* register interrupt handler */
/*	if (request_irq(svcodec->dev->irq, svcodec_irq_handler, IRQF_SHARED,
					DRIVER_NAME, svcodec)) {
		printk(KERN_ERR "[%s] : request_irq failed\n", __func__);
		return -EBUSY;
	}
	init_MUTEX(&svcodec->sem); */

	return 0;
}

static int get_picture_size (int pix_fmt, int width, int height)
{
	int size;

	switch (pix_fmt) {
		case PIX_FMT_YUV420P:
		case PIX_FMT_YUV422P:
		case PIX_FMT_YUV444P:
		case PIX_FMT_YUV410P:
		case PIX_FMT_YUV411P:
		case PIX_FMT_YUVJ420P:
		case PIX_FMT_YUVJ422P:
		case PIX_FMT_YUVJ444P:
			size = (width * height * 3) / 2;
			break;
		case PIX_FMT_RGB24:
		case PIX_FMT_BGR24:
			size = width * height * 3;
			break;
/*		case PIX_FMT_RGB32:
			size = width * height * 4;
			break;
		case PIX_FMT_RGB555:
		case PIX_FMT_RGB565:
		case PIX_FMT_YUYV422:
		case PIX_FMT_UYVY422:
			size = widht * height * 2;
			break; */
		default:
			size = -1;
	}
	return size;
}


static ssize_t svcodec_write (struct file *file, const char __user *buf,
								size_t count, loff_t *fops)
{
	struct _param paramInfo;
	AVCodecContext tempCtx;
	
	if (!svcodec) {
		printk(KERN_ERR "[%s]:Fail to get codec device info\n", __func__);
	}

	if (copy_from_user(&paramInfo, buf, sizeof(struct _param)))	{
		printk(KERN_ERR "[%s]:Fail to get codec parameter info from user\n", __func__);
	}

	/* guest to host */
	if (paramInfo.apiIndex == 2) {
		AVCodecContext *ctx;
		AVCodec *codec;
		int size, size1;
		ctx = (AVCodecContext*)paramInfo.in_args[0];
		codec = (AVCodec*)paramInfo.in_args[1];

		size = sizeof(AVCodecContext);
		printk(KERN_DEBUG "[%s]:AVCodecContext size:%d\n", sizeof(AVCodecContext));
		memcpy(&tempCtx, ctx, size);

		memcpy_toio(svcodec->memaddr, ctx, size);
		memcpy_toio((uint8_t*)svcodec->memaddr + size, codec, sizeof(AVCodec));

		size1 = size + sizeof(AVCodec);
		memcpy_toio((uint8_t*)svcodec->memaddr + size1, ctx->extradata, ctx->extradata_size);
	} else if (paramInfo.apiIndex == 6) {
		int value = -1;
		value = *(int*)paramInfo.ret;
		memcpy_toio(svcodec->memaddr, &value, sizeof(int));
	} else if (paramInfo.apiIndex == 20) {
		AVCodecContext *ctx;
		AVFrame* frame;
		int size, buf_size;
		uint8_t *buf;
		ctx = (AVCodecContext*)paramInfo.in_args[0];
		frame = (AVFrame*)paramInfo.in_args[1];
		buf = (uint8_t*)paramInfo.in_args[3];
		buf_size = *(int*)paramInfo.in_args[4];

		SVCODEC_LOG("AVCODEC_DECODE_VIDEO\n");

		size = sizeof(AVCodecContext);
		memcpy(&tempCtx, ctx, size);
	
//		memcpy_toio(svcodec->memaddr, ctx, size);
		memcpy_toio((uint8_t*)svcodec->memaddr + size, &buf_size, sizeof(int));

		if (buf_size > 0) {
			size += sizeof(int);
			memcpy_toio((uint8_t*)svcodec->memaddr + size, buf, buf_size);
		}
	} else if (paramInfo.apiIndex == 22) {
		AVCodecContext *ctx = NULL;
		AVFrame *pict = NULL;
		int buf_size = 0;
		int size = 0;
		int pict_buf_size = 0;
		uint8_t *buf = NULL;
		uint8_t *pict_buf = NULL;

		ctx = (AVCodecContext*)paramInfo.in_args[0];
		buf = (uint8_t*)paramInfo.in_args[1];
		buf_size = *(int*)paramInfo.in_args[2];
		pict = (AVFrame*)paramInfo.in_args[3];
		pict_buf = (uint8_t*)paramInfo.in_args[4];
		pict_buf_size = (ctx->height * ctx->width) * 3 / 2;

		svcodec->imgBuf = kmalloc(buf_size, GFP_KERNEL);
		if (!svcodec->imgBuf) {
			printk(KERN_ERR "[%s]Failed to allocate image buffer\n", __func__);
		}

		memcpy_toio(svcodec->memaddr, &buf_size, sizeof(int));
		memcpy_toio((uint8_t*)svcodec->memaddr + 4, buf, buf_size);
		size = buf_size + 4;

		if (pict) {
			int pict_temp = 1;
			memcpy_toio((uint8_t*)svcodec->memaddr + size, &pict_temp, sizeof(int));
			size += 4;
			memcpy_toio((uint8_t*)svcodec->memaddr + size, pict, sizeof(AVFrame));
			size += sizeof(AVFrame);
			SVCODEC_LOG("AVCODEC_ENCODE_VIDEO 1\n");
			memcpy_toio((uint8_t*)svcodec->memaddr + size, pict_buf, pict_buf_size);
		} else {
			int pict_temp = 0;
			memcpy_toio((uint8_t*)svcodec->memaddr + size, &pict_temp, sizeof(int));
			SVCODEC_LOG("AVCODEC_ENCODE_VIDEO 2\n");
		}
	} else if (paramInfo.apiIndex == 24) {
		int pix_fmt;
		int width, height;
		int size;
		pix_fmt = *(int*)paramInfo.in_args[1];
		width = *(int*)paramInfo.in_args[2];
		height = *(int*)paramInfo.in_args[3];
		size = get_picture_size(pix_fmt, width, height);
		svcodec->imgBuf = kmalloc(size, GFP_KERNEL);
		if (!svcodec->imgBuf) {
			printk(KERN_ERR "[%s]Failed to allocate image buffer\n", __func__);
		}
	} 

	// api index	
	writel((uint32_t)paramInfo.apiIndex, svcodec->ioaddr + CODEC_API_INDEX);
	
	/* host to guest */
	if (paramInfo.apiIndex == 2) {
		AVCodecContext *avctx;
		AVCodec *codec;
		int *ret, size;
		avctx = (AVCodecContext*)paramInfo.in_args[0];
		codec = (AVCodec*)paramInfo.in_args[1];
		ret = (int*)paramInfo.ret;
		size = sizeof(AVCodecContext);
		memcpy_fromio(avctx, svcodec->memaddr, size);
		memcpy_fromio(ret, (uint8_t*)svcodec->memaddr + size, sizeof(int));

		avctx->av_class = tempCtx.av_class;
		avctx->codec = codec;
//		avctx->priv_data = tempCtx.priv_data;
		avctx->extradata = tempCtx.extradata;
		avctx->opaque = tempCtx.opaque;
		avctx->get_buffer = tempCtx.get_buffer;
		avctx->release_buffer = tempCtx.release_buffer;
		avctx->stats_out = tempCtx.stats_out;
		avctx->stats_in = tempCtx.stats_in;
		avctx->rc_override = tempCtx.rc_override;
		avctx->rc_eq = tempCtx.rc_eq;
		avctx->slice_offset = tempCtx.slice_offset;
		avctx->get_format = tempCtx.get_format;
		avctx->internal_buffer = tempCtx.internal_buffer;
		avctx->intra_matrix = tempCtx.intra_matrix;
		avctx->inter_matrix = tempCtx.inter_matrix;
		avctx->reget_buffer = tempCtx.reget_buffer;
		avctx->execute = tempCtx.execute;
		avctx->thread_opaque = tempCtx.thread_opaque;
		avctx->execute2 = tempCtx.execute2;

		if (copy_to_user((void*)(paramInfo.in_args[0]), avctx, sizeof(AVCodecContext))) {
			printk(KERN_ERR "[%s]:Fail to copy AVCodecContext to user\n", __func__);
		}
	} else if (paramInfo.apiIndex == 3) {
		int *ret;
		ret = (int*)paramInfo.ret;
		memcpy_fromio(ret, svcodec->memaddr, sizeof(int));
	} else if (paramInfo.apiIndex == 20) {
		AVCodecContext *avctx;
		AVFrame *frame;
		int size;
		int *got_picture_ptr;
		int *ret;
		int buf_size;

		avctx = (AVCodecContext*)paramInfo.in_args[0];
		frame = (AVFrame*)paramInfo.in_args[1];
		got_picture_ptr = (int*)paramInfo.in_args[2];
		buf_size = *(int*)paramInfo.in_args[4];
		ret = (int*)paramInfo.ret;

		if (buf_size > 0) {
			size = sizeof(AVCodecContext);
			memcpy_fromio(avctx, svcodec->memaddr, size);
			memcpy_fromio(frame, (uint8_t*)svcodec->memaddr + size, sizeof(AVFrame));

			size += sizeof(AVFrame);
			memcpy_fromio(got_picture_ptr, (uint8_t*)svcodec->memaddr + size, sizeof(int));
			size += sizeof(int);
			memcpy_fromio(ret, (uint8_t*)svcodec->memaddr + size , sizeof(int));
		} else {
			*got_picture_ptr = 0;
			memcpy_fromio(ret, svcodec->memaddr, sizeof(int));
		}
		avctx->coded_frame = frame;
		avctx->av_class = tempCtx.av_class;
		avctx->codec = tempCtx.codec;
		avctx->extradata = tempCtx.extradata;
		avctx->opaque = tempCtx.opaque;
		avctx->get_buffer = tempCtx.get_buffer;
		avctx->release_buffer = tempCtx.release_buffer;
		avctx->stats_out = tempCtx.stats_out;
		avctx->stats_in = tempCtx.stats_in;
		avctx->rc_override = tempCtx.rc_override;
		avctx->rc_eq = tempCtx.rc_eq;
		avctx->slice_offset = tempCtx.slice_offset;
		avctx->get_format = tempCtx.get_format;
		avctx->internal_buffer = tempCtx.internal_buffer;
		avctx->intra_matrix = tempCtx.intra_matrix;
		avctx->inter_matrix = tempCtx.inter_matrix;
		avctx->reget_buffer = tempCtx.reget_buffer;
		avctx->execute = tempCtx.execute;
		avctx->thread_opaque = tempCtx.thread_opaque;
		avctx->execute2 = tempCtx.execute2;
		if (copy_to_user((void*)(paramInfo.in_args[0]), avctx, sizeof(AVCodecContext))) {
			printk(KERN_ERR "[%s]:Fail to copy AVCodecContext to user\n", __func__);
		}
	} else if (paramInfo.apiIndex == 22) {
		uint32_t buf_size;
		int *ret;
		buf_size = *(uint32_t*)paramInfo.in_args[2];
		ret = (int*)paramInfo.ret;
		if (buf_size > 0) {
			memcpy_fromio(svcodec->imgBuf, svcodec->memaddr, buf_size);
			if (copy_to_user((void *)(paramInfo.in_args[1]), svcodec->imgBuf, buf_size)) {
				printk(KERN_ERR "[%s]:Fail to copy image buffers to user\n", __func__);
			}
			memcpy_fromio(ret, (uint8_t*)svcodec->memaddr + buf_size , sizeof(int));
		} else {
			memcpy_fromio(ret, svcodec->memaddr , sizeof(int));
		}
		kfree(svcodec->imgBuf);
		svcodec->imgBuf = NULL;
    } else if (paramInfo.apiIndex == 24) {
		int pix_fmt;
		int width, height;
		int size;
		pix_fmt = *(int*)paramInfo.in_args[1];
		width = *(int*)paramInfo.in_args[2];
		height = *(int*)paramInfo.in_args[3];
		size = get_picture_size(pix_fmt, width, height);
		memcpy_fromio(svcodec->imgBuf, svcodec->memaddr, size);
		if (copy_to_user((void*)paramInfo.in_args[4], svcodec->imgBuf, size)) {
			printk(KERN_ERR "[%s]:Fail to copy image buffers to user\n", __func__);
		}
		kfree(svcodec->imgBuf);
		svcodec->imgBuf = NULL;
	} 

	return 0;
}

static ssize_t svcodec_read (struct file *file, char __user *buf,
								size_t count, loff_t *fops)
{
	SVCODEC_LOG("\n");
	if (!svcodec) {
		printk(KERN_ERR "[%s] : Fail to get codec device info\n", __func__);
	}
	return 0;
}

static int svcodec_mmap (struct file *file, struct vm_area_struct *vm)
{
	unsigned long phys_addr;
	unsigned long size;

	SVCODEC_LOG("\n");
	phys_addr = vm->vm_pgoff << PAGE_SHIFT;
	size = vm->vm_end - vm->vm_start;

	if (!svcodec && size > svcodec->mem_size) {
		SVCODEC_LOG("Over mapping size\n");
		return -EINVAL;
	}

	vm->vm_flags |= VM_IO;
	vm->vm_flags |= VM_RESERVED;

	if (remap_pfn_range(vm, vm->vm_start, phys_addr, size, vm->vm_page_prot)) {
		SVCODEC_LOG("Failed to remap page range\n");
		return -EAGAIN;
	}

	return 0;
}

static int svcodec_release (struct inode *inode, struct file *file)
{
/*	if (svcodec->dev->irq > 0) {
		free_irq(svcodec->dev->irq, svcodec);
	} */

	if (svcodec->imgBuf) {
		kfree(svcodec->imgBuf);
		svcodec->imgBuf = NULL;
	}
	module_put(THIS_MODULE);
	SVCODEC_LOG("\n");		
	return 0;
}

#if 0
static void call_workqueue(void *data)
{
	SVCODEC_LOG("\n");
}

static irqreturn_t svcodec_irq_handler (int irq, void *dev_id)
{
	int ret = -1;
	svcodec_dev *dev = (svcodec_dev*)dev_id;
	
	ret = ioread32(dev->ioaddr + CODEC_READY_TO_GET_DATA);
	if (ret == 0) {
		return IRQ_NONE;
	}

	SVCODEC_LOG("\n");
	dev->wake_up = ret;
	wake_up_interruptible(&waitqueue);
	schedule_work(&work_queue);
	iowrite32(0, dev->ioaddr + CODEC_GET_RESULT_DATA);
	/* need more implementation */
	return IRQ_HANDLED;
}
#endif

struct file_operations codec_fops = {
	.owner		= THIS_MODULE,
	.read		= svcodec_read,
	.write		= svcodec_write,
	.open		= svcodec_open,
	.mmap		= svcodec_mmap,
	.release	= svcodec_release,
};

static void __devinit svcodec_remove (struct pci_dev *pci_dev)
{
	if (svcodec) {
		if (svcodec->ioaddr) {
			iounmap(svcodec->ioaddr);
			svcodec->ioaddr = 0;
		}

		if (svcodec->memaddr) {
			iounmap(svcodec->memaddr);
			svcodec->memaddr = 0;
		}

		if (svcodec->io_start) {
			release_mem_region(svcodec->io_start, svcodec->io_size);
			svcodec->io_start = 0;
		}

		if (svcodec->mem_start) {
			release_mem_region(svcodec->mem_start, svcodec->mem_size);
			svcodec->mem_start = 0;
		}

		kfree(svcodec);
	}
//	pci_release_regions(pci_dev);

	pci_disable_device(pci_dev);
}

static int __devinit svcodec_probe (struct pci_dev *pci_dev,
									const struct pci_device_id *pci_id)
{
	int ret;

	svcodec = (svcodec_dev*)kmalloc(sizeof(svcodec_dev), GFP_KERNEL);
	memset(svcodec, 0x00, sizeof(svcodec_dev));

	svcodec->dev = pci_dev;	

	if (pci_enable_device(pci_dev)) {
		printk(KERN_ERR "[%s] : pci_enable_device failed\n", __func__);
		goto err_rel;
	}

	pci_set_master(pci_dev);

	ret = -EIO;	
/*	ret = pci_request_regions(pci_dev, DRIVER_NAME);
	if (ret) {
		printk(KERN_ERR "[%s] : pci_request_regions failed\n", __func__);
		goto err_out;
	} */

	svcodec->mem_start = pci_resource_start(pci_dev, 0);
	svcodec->mem_size = pci_resource_len(pci_dev, 0);

	if (!svcodec->mem_start) {
		printk(KERN_ERR "[%s] : pci_resource_start failed\n", __func__);
		goto err_out;
	}
	
	if (!request_mem_region(svcodec->mem_start, svcodec->mem_size, DRIVER_NAME)) {
		printk(KERN_ERR "[%s] : request_mem_region failed\n", __func__);
		goto err_out;
	}

	svcodec->io_start = pci_resource_start(pci_dev, 1);
	svcodec->io_size = pci_resource_len(pci_dev, 1);

	if (!svcodec->io_start) {
		printk(KERN_ERR "[%s] : pci_resource_start failed\n", __func__);
		goto err_mem_region;
	}

	if (!request_mem_region(svcodec->io_start, svcodec->io_size, DRIVER_NAME)) {
		printk(KERN_ERR "[%s] : request_io_region failed\n", __func__);
		goto err_mem_region;
	}

	svcodec->memaddr = ioremap(svcodec->mem_start, svcodec->mem_size);
	if (!svcodec->memaddr) {
		printk(KERN_ERR "[%s] : ioremap failed\n", __func__);
		goto err_io_region;
	}

	svcodec->ioaddr = ioremap(svcodec->io_start, svcodec->io_size);
	if (!svcodec->ioaddr) {
		printk(KERN_ERR "[%s] : ioremap failed\n", __func__);
		goto err_mem_unmap;
	}
//	pci_set_drvdata(pci_dev, svcodec);

	if (register_chrdev(CODEC_MAJOR, DRIVER_NAME, &codec_fops)) {
		printk(KERN_ERR "[%s] : register_chrdev failed\n", __func__);
		goto err_io_unmap;
	}

	return 0;

err_io_unmap:
	iounmap(svcodec->ioaddr);
err_mem_unmap:
	iounmap(svcodec->memaddr);
err_io_region:
	release_mem_region(svcodec->io_start, svcodec->io_size);
err_mem_region:
	release_mem_region(svcodec->mem_start, svcodec->mem_size);
err_out:
	pci_disable_device(pci_dev);
err_rel:
	return ret;
}

static struct pci_driver driver = {
	.name		= DRIVER_NAME,
	.id_table	= svcodec_pci_table,
	.probe		= svcodec_probe,
	.remove		= svcodec_remove,
#ifdef CONFIG_PM
//	.suspend	= svcodec_suspend,
//	.resume		= svcodec_resume,
#endif
};

static int __init svcodec_init (void)
{
	SVCODEC_LOG("SVCODEC initialized\n");
	return pci_register_driver(&driver);
}

static void __exit svcodec_exit (void)
{
	pci_unregister_driver(&driver);
}
module_init(svcodec_init);
module_exit(svcodec_exit);
