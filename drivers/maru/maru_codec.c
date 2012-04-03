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

#define DRIVER_NAME     "codec"
#define CODEC_MAJOR     240

MODULE_DESCRIPTION("Virtual Codec Device Driver");
MODULE_AUTHOR("Kitae KIM <kt920.kim@samsung.com");
MODULE_LICENSE("GPL2");

#ifdef CODEC_DEBUG
#define MARU_CODEC_LOG(fmt, ...) \
    printk(KERN_INFO "[%s][%s][%d]" fmt, DRIVER_NAME, __func__, __LINE__, ##__VA_ARGS__)
#else
#define MARU_CODEC_LOG(fmt, ...) ((void)0)
#endif

#define USABLE_MMAP_MAX_SIZE 4

struct _param {
    uint32_t apiIndex;
    uint32_t ctxIndex;
    uint32_t mmapOffset;
    uint32_t inArgsNum;
    uint32_t inArgs[20];
    uint32_t ret;
};

enum svodec_param_offset {
    CODEC_API_INDEX = 0,
    CODEC_IN_PARAM,
    CODEC_RETURN_VALUE,
    CODEC_CONTEXT_INDEX,
    CODEC_MMAP_OFFSET,
};

enum svcodec_param_apiindex {
    EMUL_AV_REGISTER_ALL = 1,
    EMUL_AVCODEC_ALLOC_CONTEXT,
    EMUL_AVCODEC_ALLOC_FRAME,
    EMUL_AVCODEC_OPEN,
    EMUL_AVCODEC_CLOSE,
    EMUL_AV_FREE_CONTEXT,
    EMUL_AV_FREE_PICTURE,
    EMUL_AV_FREE_PALCTRL,
    EMUL_AV_FREE_EXTRADATA,
    EMUL_AVCODEC_FLUSH_BUFFERS,
    EMUL_AVCODEC_DECODE_VIDEO,
    EMUL_AVCODEC_ENCODE_VIDEO,
    EMUL_AVCODEC_DECODE_AUDIO,
    EMUL_AVCODEC_ENCODE_AUDIO,
    EMUL_AV_PICTURE_COPY,
    EMUL_AV_PARSER_INIT,
    EMUL_AV_PARSER_PARSE,
    EMUL_AV_PARSER_CLOSE,
    EMUL_INIT_MMAP_INDEX,
    EMUL_GET_MMAP_INDEX,
    EMUL_RESET_MMAP_INDEX,
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
    uint8_t useMmap[USABLE_MMAP_MAX_SIZE + 1];
} svcodec_dev;

static svcodec_dev *svcodec;
DEFINE_MUTEX(codec_mutex);

static struct pci_device_id svcodec_pci_table[] __devinitdata = {
    {
    .vendor     = PCI_VENDOR_ID_TIZEN,
    .device     = PCI_DEVICE_ID_VIRTUAL_CODEC,
    .subvendor  = PCI_ANY_ID,
    .subdevice  = PCI_ANY_ID,
    },
};
MODULE_DEVICE_TABLE(pci, svcodec_pci_table);

static int svcodec_open (struct inode *inode, struct file *file)
{
    printk(KERN_DEBUG "[%s]\n", __func__);
    try_module_get(THIS_MODULE);

    return 0;
}

#ifdef CODEC_HOST
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
#endif

/* Another way to copy data between guest and host. */
#ifdef CODEC_HOST
static ssize_t svcodec_write (struct file *file, const char __user *buf,
                              size_t count, loff_t *fops)
{
    struct _param paramInfo;
    AVCodecParserContext tempParserCtx;
    AVCodecContext tempCtx;
    int i;

	mutex_lock(&codec_mutex);

    if (!svcodec) {
        printk(KERN_ERR "[%s]:Fail to get codec device info\n", __func__);
    }

    if (copy_from_user(&paramInfo, buf, sizeof(struct _param))) {
        printk(KERN_ERR "[%s]:Fail to copy\n", __func__);
    }

    for (i = 0; i < paramInfo.inArgsNum; i++) {
        writel(paramInfo.inArgs[i], svcodec->ioaddr + CODEC_IN_PARAM);
    }

    /* guest to host */
    if (paramInfo.apiIndex == EMUL_AVCODEC_OPEN) {
        AVCodecContext *ctx;

        ctx = (AVCodecContext*)paramInfo.inArgs[0];
        if (ctx) {
            memcpy(&tempCtx, ctx, sizeof(AVCodecContext));
            writel((uint32_t)ctx->extradata, svcodec->ioaddr + CODEC_IN_PARAM);
        }
    } else if (paramInfo.apiIndex == EMUL_AVCODEC_DECODE_VIDEO) {
        AVCodecContext *ctx;

        ctx = (AVCodecContext*)paramInfo.inArgs[0];
        memcpy(&tempCtx, ctx, sizeof(AVCodecContext));
    } else if (paramInfo.apiIndex == EMUL_AVCODEC_ENCODE_VIDEO) {
        AVCodecContext *ctx;
        uint32_t buf_size;

        ctx = (AVCodecContext*)paramInfo.inArgs[0];
        buf_size = *(uint32_t*)paramInfo.inArgs[2];
        writel((uint32_t)ctx->coded_frame, svcodec->ioaddr + CODEC_IN_PARAM);
        svcodec->imgBuf = kmalloc(buf_size, GFP_KERNEL);
        writel((uint32_t)svcodec->imgBuf, svcodec->ioaddr + CODEC_IN_PARAM);
    } else if (paramInfo.apiIndex == EMUL_AV_PICTURE_COPY) {
        int pix_fmt;
        int width, height;
        int size;

        pix_fmt = *(int*)paramInfo.inArgs[1];
        width = *(int*)paramInfo.inArgs[2];
        height = *(int*)paramInfo.inArgs[3];
        size = get_picture_size(pix_fmt, width, height);
        svcodec->imgBuf = kmalloc(size, GFP_KERNEL);
        writel((uint32_t)svcodec->imgBuf, svcodec->ioaddr + CODEC_IN_PARAM);
    } else if (paramInfo.apiIndex == EMUL_AV_PARSER_PARSE) {
        AVCodecParserContext *parserctx;
        AVCodecContext *ctx;

        parserctx = (AVCodecParserContext*)paramInfo.inArgs[0];
        ctx = (AVCodecContext*)paramInfo.inArgs[1];
        memcpy(&tempParserCtx, parserctx, sizeof(AVCodecParserContext));
        memcpy(&tempCtx, ctx, sizeof(AVCodecContext));
    } 

    /* return value */
    if (paramInfo.ret != 0) {
        writel((uint32_t)paramInfo.ret, svcodec->ioaddr + CODEC_RETURN_VALUE);
	}

    /* context index */
    writel((uint32_t)paramInfo.ctxIndex, svcodec->ioaddr + CODEC_CONTEXT_INDEX);

    /* api index */
    writel((uint32_t)paramInfo.apiIndex, svcodec->ioaddr + CODEC_API_INDEX);
    
    /* host to guest */
    if (paramInfo.apiIndex == EMUL_AVCODEC_OPEN) {
        AVCodecContext *ctx;
        AVCodec *codec;
        ctx = (AVCodecContext*)paramInfo.inArgs[0];
        codec = (AVCodec*)paramInfo.inArgs[1];
        if (ctx) {
            restore_codec_context(ctx, &tempCtx);
            ctx->codec = codec;
        } 
    } else if (paramInfo.apiIndex == EMUL_AVCODEC_DECODE_VIDEO) {
        AVCodecContext *ctx;
        AVFrame *frame;
        ctx = (AVCodecContext*)paramInfo.inArgs[0];
        frame = (AVFrame*)paramInfo.inArgs[1];
        restore_codec_context(ctx, &tempCtx);
        ctx->coded_frame = frame;
    } else if (paramInfo.apiIndex == EMUL_AVCODEC_ENCODE_VIDEO) {
        uint32_t buf_size;
        buf_size = *(uint32_t*)paramInfo.inArgs[2];
        if (copy_to_user((void*)paramInfo.inArgs[1], svcodec->imgBuf, buf_size)) {
            printk(KERN_ERR "[%s]:Fail to copy_to_user\n", __func__);
        }
        kfree(svcodec->imgBuf);
        svcodec->imgBuf = NULL;
    } else if (paramInfo.apiIndex == EMUL_AV_PICTURE_COPY) {
        int pix_fmt;
        int width, height;
        int size;
        pix_fmt = *(int*)paramInfo.inArgs[1];
        width = *(int*)paramInfo.inArgs[2];
        height = *(int*)paramInfo.inArgs[3];
        size = get_picture_size(pix_fmt, width, height);
        if (copy_to_user((void*)paramInfo.inArgs[4], svcodec->imgBuf, size)) {
            printk(KERN_ERR "[%s]:Fail to copy_to_user\n", __func__);
        }
        kfree(svcodec->imgBuf);
        svcodec->imgBuf = NULL;
    } else if (paramInfo.apiIndex == EMUL_AV_PARSER_PARSE) {
        AVCodecParserContext *parserctx;
        AVCodecContext *ctx;
        uint8_t *outbuf;
        int *outbuf_size;

        parserctx = (AVCodecParserContext*)paramInfo.inArgs[0];
        ctx = (AVCodecContext*)paramInfo.inArgs[1];
        outbuf_size = (int*)paramInfo.inArgs[3];
        parserctx->priv_data = tempParserCtx.priv_data;
        parserctx->parser = tempParserCtx.parser;
        restore_codec_context(ctx, &tempCtx);
    }

	mutex_unlock(&codec_mutex);

    return 0;
}

#else
/* Copy data between guest and host using mmap operation. */
 
static ssize_t svcodec_write (struct file *file, const char __user *buf,
                              size_t count, loff_t *fops)
{
    struct _param paramInfo;

    mutex_lock(&codec_mutex);

    if (!svcodec) {
        printk(KERN_ERR "[%s]:Fail to get codec device info\n", __func__);
    }

    if (copy_from_user(&paramInfo, buf, sizeof(struct _param))) {
        printk(KERN_ERR "[%s]:Fail to get codec parameter info from user\n", __func__);
    }

    if (paramInfo.apiIndex == EMUL_INIT_MMAP_INDEX) {
        int i;
        if (svcodec->useMmap[USABLE_MMAP_MAX_SIZE] == 0) {
            for (i = 0; i < USABLE_MMAP_MAX_SIZE; i++) {
                svcodec->useMmap[i] = 1;
                printk(KERN_DEBUG "Reset useMmap[%d]=%d\n", i, svcodec->useMmap[i]);
            }
        }
    } else if (paramInfo.apiIndex == EMUL_GET_MMAP_INDEX) {
        int i;
        int *mmapIndex;

        mmapIndex = (int*)paramInfo.ret;

        for (i = 0; i < USABLE_MMAP_MAX_SIZE; i++) {
            printk(KERN_DEBUG "useMmap[%d]=%d\n", i, svcodec->useMmap[i]);
            if (svcodec->useMmap[i] == 1) {
                break;
            }
        }

        if (i == USABLE_MMAP_MAX_SIZE) {
            i = -1;
        } else {
            svcodec->useMmap[i] = 0;
            (svcodec->useMmap[USABLE_MMAP_MAX_SIZE])++;

            printk(KERN_DEBUG "available useMmap count:%d\n",
                   (USABLE_MMAP_MAX_SIZE - svcodec->useMmap[USABLE_MMAP_MAX_SIZE]));
        }

        printk(KERN_DEBUG "useMmap[%d]=%d\n", i, svcodec->useMmap[i]);

        if (copy_to_user((void*)mmapIndex, &i, sizeof(int))) {
            printk(KERN_ERR "[%s]:Fail to copy_to_user\n", __func__);
        }
        mutex_unlock(&codec_mutex);

        return 0;
    } else if (paramInfo.apiIndex == EMUL_RESET_MMAP_INDEX) {
        int index, i;
        index = paramInfo.mmapOffset;
        svcodec->useMmap[index] = 1;
        (svcodec->useMmap[USABLE_MMAP_MAX_SIZE])--;
        printk(KERN_DEBUG "useMmap[%d] is available from now\n", index);
        printk(KERN_DEBUG "Available useMmap count:%d\n", svcodec->useMmap[USABLE_MMAP_MAX_SIZE]);
#ifdef CODEC_DEBUG
        for (i = 0; i < USABLE_MMAP_MAX_SIZE; i++) {
            printk(KERN_DEBUG "useMmap[%d]=%d\n", i, svcodec->useMmap[i]);
        }
#endif
        mutex_unlock(&codec_mutex);

        return 0;
    }

    writel((uint32_t)paramInfo.ctxIndex, svcodec->ioaddr + CODEC_CONTEXT_INDEX);

    writel((uint32_t)paramInfo.mmapOffset, svcodec->ioaddr + CODEC_MMAP_OFFSET);

    writel((uint32_t)paramInfo.apiIndex, svcodec->ioaddr + CODEC_API_INDEX);

    mutex_unlock(&codec_mutex);
     
    return 0;
}
#endif

static ssize_t svcodec_read (struct file *file, char __user *buf,
                                size_t count, loff_t *fops)
{
    if (!svcodec) {
        printk(KERN_ERR "[%s] : Fail to get codec device info\n", __func__);
    }
    return 0;
}

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

    vm->vm_flags |= VM_IO;
    vm->vm_flags |= VM_RESERVED;

    return 0;
}

static int svcodec_release (struct inode *inode, struct file *file)
{
    printk(KERN_DEBUG "[%s]\n", __func__);
    if (svcodec->imgBuf) {
        kfree(svcodec->imgBuf);
        svcodec->imgBuf = NULL;
        printk(KERN_DEBUG "[%s]release codec device module\n", __func__);
    }
    module_put(THIS_MODULE);
    return 0;
}

struct file_operations svcodec_fops = {
    .owner      = THIS_MODULE,
    .read       = svcodec_read,
    .write      = svcodec_write,
    .open       = svcodec_open,
    .mmap       = svcodec_mmap,
    .release    = svcodec_release,
};

static void __devinit svcodec_remove (struct pci_dev *pci_dev)
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
            release_mem_region(svcodec->io_start, svcodec->io_size);
            svcodec->io_start = 0;
        }

        if (svcodec->mem_start) {
            release_mem_region(svcodec->mem_start, svcodec->mem_size);
            svcodec->mem_start = 0;
        }

        kfree(svcodec);
    }
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

#if 0
	svcodec->memaddr = ioremap(svcodec->mem_start, svcodec->mem_size);
    if (!svcodec->memaddr) {
        printk(KERN_ERR "[%s] : ioremap failed\n", __func__);
        goto err_io_region;
    }
#endif

    svcodec->ioaddr = ioremap_nocache(svcodec->io_start, svcodec->io_size);
    if (!svcodec->ioaddr) {
        printk(KERN_ERR "[%s] : ioremap failed\n", __func__);
		goto err_io_region;
    }
    if (register_chrdev(CODEC_MAJOR, DRIVER_NAME, &svcodec_fops)) {
        printk(KERN_ERR "[%s] : register_chrdev failed\n", __func__);
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
    return ret;
}

static struct pci_driver driver = {
    .name       = DRIVER_NAME,
    .id_table   = svcodec_pci_table,
    .probe      = svcodec_probe,
    .remove     = svcodec_remove,
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
