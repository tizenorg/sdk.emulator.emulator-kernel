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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Contributors:
 * - S-Core Co., Ltd
 *
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

MODULE_DESCRIPTION("Virtual Codec Device Driver");
MODULE_AUTHOR("Kitae KIM <kt920.kim@samsung.com");
MODULE_LICENSE("GPL2");

// #define CODEC_DEBUG
#define CODEC_HOST

#ifdef CODEC_DEBUG
#define SVCODEC_LOG(fmt, ...) \
    printk(KERN_INFO "[%s][%s][%d]" fmt, DRIVER_NAME, __func__, __LINE__, ##__VA_ARGS__)
#else
#define SVCODEC_LOG(fmt, ...) ((void)0)
#endif

struct _param {
	uint32_t apiIndex;
	uint32_t in_args_num;
	uint32_t in_args[20];
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
	struct pci_dev *dev;

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
	.vendor 	= PCI_VENDOR_ID_TIZEN,
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
		default:
			size = -1;
	}
	return size;
}

void restore_codec_context(AVCodecContext *dstctx,
						   AVCodecContext *srcctx) {
	dstctx->av_class = srcctx->av_class;
	dstctx->codec = srcctx->codec;
	dstctx->extradata = srcctx->extradata;
	dstctx->opaque = srcctx->opaque;
	dstctx->get_buffer = srcctx->get_buffer;
	dstctx->release_buffer = srcctx->release_buffer;
	dstctx->stats_out = srcctx->stats_out;
	dstctx->stats_in = srcctx->stats_in;
	dstctx->rc_override = srcctx->rc_override;
	dstctx->rc_eq = srcctx->rc_eq;
	dstctx->slice_offset = srcctx->slice_offset;
	dstctx->get_format = srcctx->get_format;
	dstctx->internal_buffer = srcctx->internal_buffer;
	dstctx->intra_matrix = srcctx->intra_matrix;
	dstctx->inter_matrix = srcctx->inter_matrix;
	dstctx->reget_buffer = srcctx->reget_buffer;
	dstctx->execute = srcctx->execute;
	dstctx->thread_opaque = srcctx->thread_opaque;
	dstctx->execute2 = srcctx->execute2;
}

#ifdef CODEC_HOST
static ssize_t svcodec_write (struct file *file, const char __user *buf,
							  size_t count, loff_t *fops)
{
	struct _param paramInfo;
	AVCodecParserContext tempParserCtx;
	AVCodecContext tempCtx;
	int i;
	
	if (!svcodec) {
		printk(KERN_ERR "[%s] : Fail to get codec device info\n", __func__);
	}

	if (copy_from_user(&paramInfo, buf, sizeof(struct _param))) {
		printk(KERN_ERR "[%s]:Fail to copy\n", __func__);
	}

	for (i = 0; i < paramInfo.in_args_num; i++) {
		writel(paramInfo.in_args[i], svcodec->ioaddr + CODEC_IN_PARAM);
	}

	/* guest to host */
	if (paramInfo.apiIndex == 2) {
		AVCodecContext *ctx;
		ctx = (AVCodecContext*)paramInfo.in_args[0];
		if (ctx) {
			memcpy(&tempCtx, ctx, sizeof(AVCodecContext));
			writel((uint32_t)ctx->extradata, svcodec->ioaddr + CODEC_IN_PARAM);
		}
	} else if (paramInfo.apiIndex == 20) {
		AVCodecContext *ctx;
		ctx = (AVCodecContext*)paramInfo.in_args[0];
		memcpy(&tempCtx, ctx, sizeof(AVCodecContext));
/*		writel((uint32_t)&ctx->frame_number, svcodec->ioaddr + CODEC_IN_PARAM);
		writel((uint32_t)&ctx->pix_fmt, svcodec->ioaddr + CODEC_IN_PARAM);
		writel((uint32_t)&ctx->coded_frame, svcodec->ioaddr + CODEC_IN_PARAM);
		writel((uint32_t)&ctx->sample_aspect_ratio, svcodec->ioaddr + CODEC_IN_PARAM);
		writel((uint32_t)&ctx->reordered_opaque, svcodec->ioaddr + CODEC_IN_PARAM); */
	} else if (paramInfo.apiIndex == 22) {
		AVCodecContext *ctx;
		uint32_t buf_size;
		ctx = (AVCodecContext*)paramInfo.in_args[0];
		buf_size = *(uint32_t*)paramInfo.in_args[2];
		writel((uint32_t)ctx->coded_frame, svcodec->ioaddr + CODEC_IN_PARAM);
		svcodec->imgBuf = kmalloc(buf_size, GFP_KERNEL);
		writel((uint32_t)svcodec->imgBuf, svcodec->ioaddr + CODEC_IN_PARAM);
	} else if (paramInfo.apiIndex == 24) {
		int pix_fmt;
		int width, height;
		int size;
		pix_fmt = *(int*)paramInfo.in_args[1];
		width = *(int*)paramInfo.in_args[2];
		height = *(int*)paramInfo.in_args[3];
		size = get_picture_size(pix_fmt, width, height);
		svcodec->imgBuf = kmalloc(size, GFP_KERNEL);
		writel((uint32_t)svcodec->imgBuf, svcodec->ioaddr + CODEC_IN_PARAM);
	} else if (paramInfo.apiIndex == 31) {
		AVCodecParserContext *parserctx;
		AVCodecContext *ctx;
		parserctx = (AVCodecParserContext*)paramInfo.in_args[0];
		ctx = (AVCodecContext*)paramInfo.in_args[1];
		memcpy(&tempParserCtx, parserctx, sizeof(AVCodecParserContext));
		memcpy(&tempCtx, ctx, sizeof(AVCodecContext));
	} 

	// return value
	if (paramInfo.ret != 0)
		writel((uint32_t)paramInfo.ret, svcodec->ioaddr + CODEC_RETURN_VALUE);

	// api index	
	writel((uint32_t)paramInfo.apiIndex, svcodec->ioaddr + CODEC_API_INDEX);
	
	/* host to guest */
	if (paramInfo.apiIndex == 2) {
		AVCodecContext *ctx;
		AVCodec *codec;
		ctx = (AVCodecContext*)paramInfo.in_args[0];
		codec = (AVCodec*)paramInfo.in_args[1];
		if (ctx) {
			restore_codec_context(ctx, &tempCtx);
			ctx->codec = codec;
		} 
	} else if (paramInfo.apiIndex == 20) {
		AVCodecContext *ctx;
		AVFrame *frame;
		ctx = (AVCodecContext*)paramInfo.in_args[0];
		frame = (AVFrame*)paramInfo.in_args[1];
		restore_codec_context(ctx, &tempCtx);
		ctx->coded_frame = frame;
	} else if (paramInfo.apiIndex == 22) {
		uint32_t buf_size;
		buf_size = *(uint32_t*)paramInfo.in_args[2];
		if (copy_to_user((void*)paramInfo.in_args[1], svcodec->imgBuf, buf_size)) {
			printk(KERN_ERR "[%s]:Fail to copy_to_user\n", __func__);
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
		if (copy_to_user((void*)paramInfo.in_args[4], svcodec->imgBuf, size)) {
			printk(KERN_ERR "[%s]:Fail to copy_to_user\n", __func__);
		}
		kfree(svcodec->imgBuf);
		svcodec->imgBuf = NULL;
	} else if (paramInfo.apiIndex == 31) {
		AVCodecParserContext *parserctx;
		AVCodecContext *ctx;
		uint8_t *outbuf;
		int *outbuf_size;

		parserctx = (AVCodecParserContext*)paramInfo.in_args[0];
		ctx = (AVCodecContext*)paramInfo.in_args[1];
		outbuf_size = (int*)paramInfo.in_args[3];
		parserctx->priv_data = tempParserCtx.priv_data;
		parserctx->parser = tempParserCtx.parser;
		restore_codec_context(ctx, &tempCtx);

//		printk(KERN_INFO "before copy outbuf_size :%d\n", *outbuf_size);
//		memcpy_fromio(outbuf_size, svcodec->memaddr, sizeof(int));
/*		if (*outbuf_size > 0) {
			outbuf = kmalloc(*outbuf_size, GFP_KERNEL);
			memcpy_fromio(outbuf, (uint8_t*)svcodec->memaddr + 4, *outbuf_size);
			if (copy_to_user((void*)(paramInfo.in_args[2]), outbuf, *outbuf_size)) {
				printk(KERN_ERR "[%s]:Failed to copy_to_user\n", __func__);
			}
			kfree(outbuf);
		} */
	}

	return 0;
}

#else 
static ssize_t svcodec_write (struct file *file, const char __user *buf,
								size_t count, loff_t *fops)
{
	struct _param paramInfo;
	AVCodecContext tmpCtx;
	
	if (!svcodec) {
		printk(KERN_ERR "[%s]:Fail to get codec device info\n", __func__);
	}

	if (copy_from_user(&paramInfo, buf, sizeof(struct _param)))	{
		printk(KERN_ERR "[%s]:Fail to get codec parameter info from user\n", __func__);
	}

	/* guest to host */
	if (paramInfo.apiIndex == 6) {
		int value = -1;
		value = *(int*)paramInfo.ret;
		memcpy_toio(svcodec->memaddr, &value, sizeof(int));
	} else if (paramInfo.apiIndex == 30) {
		int codec_id;
		codec_id = *(int*)paramInfo.in_args[0];
		memcpy_toio(svcodec->memaddr, &codec_id, sizeof(int));
	} else if (paramInfo.apiIndex == 31) {
		AVCodecParserContext *pctx;
		AVCodecContext *avctx;
		uint8_t *inbuf;
		int inbuf_size;
		int64_t pts;
		int64_t dts;
		int size;

		pctx = (AVCodecParserContext*)paramInfo.in_args[0];
		avctx = (AVCodecContext*)paramInfo.in_args[1];
		inbuf = (uint8_t*)paramInfo.in_args[4];
		inbuf_size = *(int*)paramInfo.in_args[5];
		pts = *(int64_t*)paramInfo.in_args[6];
		dts = *(int64_t*)paramInfo.in_args[7];

		SVCODEC_LOG("AVCodecParserContext Size : %d\n", sizeof(AVCodecParserContext));
		size = sizeof(AVCodecParserContext);
//		memcpy_toio(svcodec->memaddr, pctx, size);
		memcpy_toio((uint8_t*)svcodec->memaddr + size, avctx, sizeof(AVCodecContext));
		size += sizeof(AVCodecContext);

		memcpy_toio((uint8_t*)svcodec->memaddr + size, &inbuf_size, sizeof(int));
		size += sizeof(int);
		memcpy_toio((uint8_t*)svcodec->memaddr + size, inbuf, inbuf_size);
		size += inbuf_size;
		memcpy_toio((uint8_t*)svcodec->memaddr + size, &pts, sizeof(int64_t));
		size += sizeof(int64_t);
		memcpy_toio((uint8_t*)svcodec->memaddr + size, &dts, sizeof(int64_t));
	} 

	// api index	
	writel((uint32_t)paramInfo.apiIndex, svcodec->ioaddr + CODEC_API_INDEX);
	
	/* host to guest */
	if (paramInfo.apiIndex == 3) {
		int *ret;
		ret = (int*)paramInfo.ret;
		memcpy_fromio(ret, svcodec->memaddr, sizeof(int));
	} else if (paramInfo.apiIndex == 31) {
		AVCodecParserContext *pctx;
		AVCodecParserContext tmp_pctx;
		AVCodecContext *avctx;
		AVCodecContext tmp_ctx;
		uint8_t *outbuf;
		int *outbuf_size;
		int size, *ret;

		pctx = (AVCodecParserContext*)paramInfo.in_args[0];
		avctx = (AVCodecContext*)paramInfo.in_args[1];
//		outbuf = (uint8_t*)paramInfo.in_args[2];
		outbuf_size = (int*)paramInfo.in_args[3];
		ret = (int*)paramInfo.ret;

		memcpy(&tmp_pctx, pctx, sizeof(AVCodecParserContext));
		memcpy(&tmp_ctx, avctx, sizeof(AVCodecContext));

		size = sizeof(AVCodecParserContext);
		memcpy_fromio(pctx, svcodec->memaddr, size);
		pctx->priv_data = tmp_pctx.priv_data;
		pctx->parser = tmp_pctx.parser;

		memcpy_fromio(avctx, (uint8_t*)svcodec->memaddr + size, sizeof(AVCodecContext));
		restore_codec_context(avctx, &tmp_ctx);
		size += sizeof(AVCodecContext);

		memcpy_fromio(outbuf_size, (uint8_t*)svcodec->memaddr + size, sizeof(int));
		size += sizeof(int);

		if (*outbuf_size != 0) {
			outbuf = kmalloc(*outbuf_size, GFP_KERNEL);
			memcpy_fromio(outbuf, (uint8_t*)svcodec->memaddr + size, *outbuf_size);
			if (copy_to_user((void*)(paramInfo.in_args[2]), outbuf, *outbuf_size)) {
				printk(KERN_ERR "[%s]:Failed to copy the output buffer of"
					   " av_parser_parse to user\n", __func__);
			}
			kfree(outbuf);
			size += *outbuf_size;
		}
		memcpy_fromio(ret, (uint8_t*)svcodec->memaddr + size, sizeof(int));
	}

	return 0;
}
#endif

static ssize_t svcodec_read (struct file *file, char __user *buf,
								size_t count, loff_t *fops)
{
	SVCODEC_LOG("\n");
	if (!svcodec) {
		printk(KERN_ERR "[%s] : Fail to get codec device info\n", __func__);
	}
	return 0;
}

static void svcodec_vm_open(struct vm_area_struct *vm)
{
}

static void svcodec_vm_close(struct vm_area_struct *vm)
{
}

static const struct vm_operations_struct svcodec_vm_ops = {
	.open	= svcodec_vm_open,
	.close	= svcodec_vm_close,
};

static int svcodec_mmap (struct file *file, struct vm_area_struct *vm)
{
	unsigned long off;
	unsigned long phys_addr;
	unsigned long size;
	int ret = -1;

	off = vm->vm_pgoff << PAGE_SHIFT;
	phys_addr = (PAGE_ALIGN(svcodec->mem_start) + off) >> PAGE_SHIFT;
	size = vm->vm_end - vm->vm_start;
	printk(KERN_DEBUG "svcodec_mmap\n");

	if (size > svcodec->mem_size) {
		printk(KERN_ERR "Over mapping size\n");
		return -EINVAL;
	}

	ret = remap_pfn_range(vm, vm->vm_start, phys_addr, size, vm->vm_page_prot);
	if (ret < 0) {
		printk(KERN_ERR "Failed to remap page range\n");
		return -EAGAIN;
	}

	vm->vm_ops = &svcodec_vm_ops;
	vm->vm_flags |= VM_IO;
	vm->vm_flags |= VM_RESERVED;

	return 0;
}

static int svcodec_release (struct inode *inode, struct file *file)
{
/*	if (svcodec->dev->irq > 0) {
		free_irq(svcodec->dev->irq, svcodec);
	} */

	printk(KERN_DEBUG "[%s]\n", __func__);
	if (svcodec->imgBuf) {
		kfree(svcodec->imgBuf);
		svcodec->imgBuf = NULL;
		printk(KERN_DEBUG "[%s]release codec device module\n", __func__);
	}
	module_put(THIS_MODULE);
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

	svcodec->ioaddr = ioremap_nocache(svcodec->io_start, svcodec->io_size);
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
};

static int __init svcodec_init (void)
{
	printk(KERN_INFO "svcodec device is initialized.\n");
	return pci_register_driver(&driver);
}

static void __exit svcodec_exit (void)
{
	pci_unregister_driver(&driver);
}
module_init(svcodec_init);
module_exit(svcodec_exit);
