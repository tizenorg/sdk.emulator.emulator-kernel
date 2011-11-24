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
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include "avformat.h" 

#define DRIVER_NAME		"codec"
#define CODEC_MAJOR		240

#define CODEC_DEBUG
#ifdef CODEC_DEBUG
#define codec_log(fmt, ...) \
    printk(KERN_INFO "[codec][%s][%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define codec_log(fmt, ...) ((void)0)
#endif

struct _param {
	uint32_t func;
	uint32_t in_args_num;
	uint32_t in_args[20];
	uint32_t ret;
};

enum codec_param_offset {
	CODEC_API_INDEX = 0,
	CODEC_IN_PARAM,
	CODEC_RETURN_VALUE
};

struct codec_buf {
	AVFormatContext *pFormatCtx;
	AVClass *pClass;
	struct AVInputFormat *pInputFmt;
	ByteIOContext *pByteIOCtx;
	AVStream *pStream;

	AVCodecContext *pCodecCtx;
	AVFrame *pFrame;
	AVCodec *pCodec;
};

struct codec_dev {
	struct pci_dev *dev;				/* pci device */
	volatile unsigned int *ioaddr;		/* memory mapped registers */

	resource_size_t mem_start;
	resource_size_t mem_size;

	/* image buffer */
//	uint8_t *img_buf;
};	

static struct pci_device_id slpcodec_pci_table[] __devinitdata = {
	{
	.vendor 	= PCI_VENDOR_ID_SAMSUNG,
	.device		= PCI_DEVICE_ID_VIRTUAL_CODEC,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
};
MODULE_DEVICE_TABLE(pci, slpcodec_pci_table);

static struct codec_dev *slpcodec;

// static void call_workqueue(void *data);

// DECLARE_WAIT_QUEUE_HEAD(waitqueue_read);
// DECLARE_WORK(work_queue, call_workqueue);

static irqreturn_t slpcodec_interrupt (int irq, void *dev_id);

static int slpcodec_open (struct inode *inode, struct file *file)
{
	codec_log("\n");
	try_module_get(THIS_MODULE);

	/* register interrupt handler */
	if (request_irq(slpcodec->dev->irq, slpcodec_interrupt, IRQF_SHARED,
					DRIVER_NAME, slpcodec)) {
		printk(KERN_ERR "[%s] : request_irq failed\n", __func__);
	}		

	return 0;
}

static ssize_t slpcodec_write (struct file *file, const char __user *buf,
								size_t count, loff_t *fops)
{
	struct _param paramInfo;
	AVCodecContext tempCtx;
	uint8_t *ptr;
	int i;
	
	if (!slpcodec) {
		printk(KERN_ERR "[%s] : Fail to get codec device info\n", __func__);
	}

	copy_from_user(&paramInfo, buf, sizeof(struct _param));

	for (i = 0; i < paramInfo.in_args_num; i++) {
		writel(paramInfo.in_args[i], slpcodec->ioaddr + CODEC_IN_PARAM);
	}

	/* guest to host */
	if (paramInfo.apiIndex == 2) {
		AVCodecContext *ctx;
		ctx = (AVCodecContext*)paramInfo.in_args[0];
		memcpy(&tempCtx, ctx, sizeof(AVCodecContext));
		writel((uint32_t)ctx->extradata, slpcodec->ioaddr + CODEC_IN_PARAM);
	} else if (paramInfo.apiIndex == 20) {
		AVCodecContext *ctx;
		ctx = (AVCodecContext*)paramInfo.in_args[0];
		writel((uint32_t)&ctx->frame_number, slpcodec->ioaddr + CODEC_IN_PARAM);
		writel((uint32_t)&ctx->pix_fmt, slpcodec->ioaddr + CODEC_IN_PARAM);
		writel((uint32_t)&ctx->coded_frame, slpcodec->ioaddr + CODEC_IN_PARAM);
		writel((uint32_t)&ctx->sample_aspect_ratio, slpcodec->ioaddr + CODEC_IN_PARAM);
		writel((uint32_t)&ctx->reordered_opaque, slpcodec->ioaddr + CODEC_IN_PARAM);
	} else if (paramInfo.apiIndex == 22) {
		AVCodecContext *ctx;
		uint32_t buf_size;
		ctx = (AVCodecContext*)paramInfo.in_args[0];
		buf_size = *(uint32_t*)paramInfo.in_args[2];
		writel((uint32_t)ctx->coded_frame, slpcodec->ioaddr + CODEC_IN_PARAM);
		ptr = kmalloc(buf_size, GFP_KERNEL);
		writel((uint32_t)ptr, slpcodec->ioaddr + CODEC_IN_PARAM);
	} else if (paramInfo.apiIndex == 24) {
		int width, height;
		int size, size2;
		width = *(int*)paramInfo.in_args[2];
		height = *(int*)paramInfo.in_args[3];
		size = width * height;
		size2 = size / 4;
		ptr = kmalloc(size + size2 * 2, GFP_KERNEL);
		writel((uint32_t)ptr, slpcodec->ioaddr + CODEC_IN_PARAM);
	} 

	// return value
	writel(paramInfo.ret, slpcodec->ioaddr + CODEC_RETURN_VALUE);

	// api index	
	writel((uint32_t)paramInfo.apiIndex, slpcodec->ioaddr + CODEC_API_INDEX);
	
	/* host to guest */
	if (paramInfo.apiIndex == 2) {
		AVCodecContext *avctx;
		avctx = (AVCodecContext*)paramInfo.in_args[0];
#if 1
		avctx->av_class = temp_ctx.av_class;
		avctx->codec = (AVCodec*)paramInfo.in_args[1];
//		avctx->priv_data = temp_ctx.priv_data;
		avctx->extradata = temp_ctx.extradata;
		avctx->opaque = temp_ctx.opaque;
		avctx->get_buffer = temp_ctx.get_buffer;
		avctx->release_buffer = temp_ctx.release_buffer;
		avctx->stats_out = temp_ctx.stats_out;
		avctx->stats_in = temp_ctx.stats_in;
		avctx->rc_override = temp_ctx.rc_override;
		avctx->rc_eq = temp_ctx.rc_eq;
		avctx->slice_offset = temp_ctx.slice_offset;
		avctx->get_format = temp_ctx.get_format;
		avctx->internal_buffer = temp_ctx.internal_buffer;
		avctx->intra_matrix = temp_ctx.intra_matrix;
		avctx->inter_matrix = temp_ctx.inter_matrix;
		avctx->reget_buffer = temp_ctx.reget_buffer;
		avctx->execute = temp_ctx.execute;
		avctx->thread_opaque = temp_ctx.thread_opaque;
		avctx->execute2 = temp_ctx.execute2;
#endif
	} else if (paramInfo.apiIndex == 20) {
		AVCodecContext *ctx;
		AVFrame *frame;
		ctx = (AVCodecContext*)paramInfo.in_args[0];
		frame = (AVFrame*)paramInfo.in_args[1];
		ctx->coded_frame = frame;
	} else if (paramInfo.apiIndex == 22) {
		uint32_t buf_size;
		buf_size = *(uint32_t*)paramInfo.in_args[2];
		copy_to_user(paramInfo.in_args[1], ptr, buf_size);
		kfree(ptr);
    } else if (paramInfo.apiIndex == 24) {
		int width, height;
		int size, size2;
		width = *(int*)paramInfo.in_args[2];
		height = *(int*)paramInfo.in_args[3];
		size = width * height;
		size2 = size / 4;
		copy_to_user(paramInfo.in_args[4], ptr, size + size2 * 2);
		kfree(ptr);
	} 

	return 0;
}

static ssize_t slpcodec_read (struct file *file, char __user *buf,
								size_t count, loff_t *fops)
{
	codec_log("\n");
	if (!slpcodec) {
		printk(KERN_ERR "[%s] : Fail to get codec device info\n", __func__);
	}
//	readl(slpcodec->ioaddr);
	return 0;
}

static int slpcodec_mmap (struct file *file, struct vm_area_struct *vm)
{
	unsigned long phys_addr;
	unsigned long size;

	codec_log("\n");
	phys_addr = vm->vm_pgoff << PAGE_SHIFT;
	size = vm->vm_end - vm->vm_start;

	if (!slpcodec && size > slpcodec->mem_size) {
		codec_log("Over mapping size\n");
		return -EINVAL;
	}

	vm->vm_flags |= VM_IO;
	vm->vm_flags |= VM_RESERVED;

	if (remap_pfn_range(vm, vm->vm_start, phys_addr, size, vm->vm_page_prot)) {
		codec_log("Failed to remap page range\n");
		return -EAGAIN;
	}

	return 0;
}

static int slpcodec_release (struct inode *inode, struct file *file)
{
	if (slpcodec->dev->irq >= 0) {
		free_irq(slpcodec->dev->irq, slpcodec);
	}

/*	if (slpcodec->img_buf) {
		kfree(slpcodec->img_buf);
		slpcodec->img_buf = NULL;
	} */

	module_put(THIS_MODULE);
	codec_log("\n");		
	return 0;
}

// static void call_workqueue(void *data)
//{
//	codec_log("\n");
//}

/*
 *  Interrupt handler
 */
static irqreturn_t slpcodec_interrupt (int irq, void *dev_id)
{
//	codec_log("\n");
	// schedule_work(&work_queue);

	/* need more implementation */
//	return IRQ_HANDLED;
	return IRQ_NONE;
}

struct file_operations codec_fops = {
	.owner		= THIS_MODULE,
	.read		= slpcodec_read,
	.write		= slpcodec_write,
	.open		= slpcodec_open,
	.mmap		= slpcodec_mmap,
	.release	= slpcodec_release,
//	.ioctl		= slpcodec_ioctl,
};

static void __devinit slpcodec_remove (struct pci_dev *pci_dev)
{
	if (slpcodec) {
		iounmap(slpcodec->ioaddr);
		kfree(slpcodec);
	}
	pci_release_regions(pci_dev);
	pci_disable_device(pci_dev);
}

static int __devinit slpcodec_probe (struct pci_dev *pci_dev,
									const struct pci_device_id *pci_id)
{
	int ret;

	slpcodec = (struct codec_dev*)kmalloc(sizeof(struct codec_dev), GFP_KERNEL);
	memset(slpcodec, 0x00, sizeof(struct codec_dev));

	slpcodec->dev = pci_dev;	

	ret = -EIO;	
	if (pci_enable_device(pci_dev)) {
		printk(KERN_ERR "[%s] : pci_enable_device failed\n", __func__);
		return ret;
	}

	ret = pci_request_regions(pci_dev, DRIVER_NAME);
	if (ret) {
		printk(KERN_ERR "[%s] : pci_request_regions failed\n", __func__);
		goto err_out;
	}

	slpcodec->mem_start = pci_resource_start(pci_dev, 1);
	slpcodec->mem_size = pci_resource_len(pci_dev, 1);
	
	slpcodec->ioaddr = ioremap(slpcodec->mem_start, slpcodec->mem_size);
	if (!slpcodec->ioaddr) {
		printk(KERN_ERR "[%s] : ioremap failed\n", __func__);
		goto err_regions;
	}

	// pci_set_drvdata(pci_dev, codec);
	pci_set_master(pci_dev);

	if (register_chrdev(CODEC_MAJOR, DRIVER_NAME, &codec_fops)) {
		printk(KERN_ERR "[%s] : register_chrdev failed\n", __func__);
		goto err_map;
	}

	return 0;

err_map:
	iounmap(slpcodec->ioaddr);
err_regions:
	pci_release_regions(pci_dev) ;
err_out:
	pci_disable_device(pci_dev);
	return ret;
}

static struct pci_driver driver = {
	.name		= DRIVER_NAME,
	.id_table	= slpcodec_pci_table,
	.probe		= slpcodec_probe,
	.remove		= slpcodec_remove,
#ifdef CONFIG_PM
//	.suspend	= slpcodec_suspend,
//	.resume		= slpcodec_resume,
#endif
};

static int __init slpcodec_init (void)
{
	codec_log("Codec accelerator initialized\n");
	return pci_register_driver(&driver);
}

static void __exit slpcodec_exit (void)
{
	pci_unregister_driver(&driver);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kitae KIM <kt920.kim@samsung.com");
MODULE_DESCRIPTION("Virtual Codec Driver for Emulator");

module_init(slpcodec_init);
module_exit(slpcodec_exit);
