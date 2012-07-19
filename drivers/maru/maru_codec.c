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
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define DRIVER_NAME     "codec"
#define CODEC_MAJOR     240

MODULE_DESCRIPTION("Virtual Codec Device Driver");
MODULE_AUTHOR("Kitae KIM <kt920.kim@samsung.com");
MODULE_LICENSE("GPL2");

#define USABLE_MMAP_MAX_SIZE    4
#define CODEC_ERR_LOG(fmt, ...) \
	printk(KERN_ERR "%s: " fmt, DRIVER_NAME, ##__VA_ARGS__)


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
    CODEC_FILE_INDEX,
    CODEC_CLOSED,
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
    EMUL_GET_MMAP_INDEX,
    EMUL_GET_CODEC_VER = 50,
};

typedef struct _svcodec_dev {
    struct pci_dev *dev;

    volatile unsigned int *ioaddr;
    volatile unsigned int *memaddr;

    resource_size_t io_start;
    resource_size_t io_size;
    resource_size_t mem_start;
    resource_size_t mem_size;

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
    { 0, },
};
MODULE_DEVICE_TABLE(pci, svcodec_pci_table);

/* Another way to copy data between guest and host. */
/* Copy data between guest and host using mmap operation. */
static ssize_t svcodec_write (struct file *file, const char __user *buf,
                              size_t count, loff_t *fops)
{
    struct _param paramInfo;

    mutex_lock(&codec_mutex);

    if (!svcodec) {
        CODEC_ERR_LOG("fail to get codec device info\n");
        return -EINVAL;
    }

    if (copy_from_user(&paramInfo, buf, sizeof(struct _param))) {
        CODEC_ERR_LOG("fail to get codec parameter info from user\n");
    }

    if (paramInfo.apiIndex == EMUL_GET_MMAP_INDEX) {
        int i = 0;
        int max_size = USABLE_MMAP_MAX_SIZE;
        int *mmapIndex = (int*)paramInfo.ret;
        uint8_t *useMmapSize = &svcodec->useMmap[max_size];

        printk(KERN_DEBUG "%s: before available useMmap count:%d\n", DRIVER_NAME, (max_size - *useMmapSize));

        for (; i < max_size; i++) {
            if (svcodec->useMmap[i] == 1) {
                svcodec->useMmap[i] = 0;
                (*useMmapSize)++;
                file->private_data = &svcodec->useMmap[i];
                printk(KERN_DEBUG "%s: useMmap[%d]=%d\n", DRIVER_NAME, i, svcodec->useMmap[i]);
                printk(KERN_DEBUG "%s: file:%p private_data:%p\n", DRIVER_NAME, file, file->private_data);
                printk(KERN_DEBUG "%s: after available useMmap count:%d\n", DRIVER_NAME, (max_size - *useMmapSize));
                printk(KERN_DEBUG "%s: return %d as the index of mmap\n", DRIVER_NAME, i);
                break;
            }
        }

        if (i == max_size) {
            printk(KERN_DEBUG "%s: Usable mmap is none! struct file:%p\n", DRIVER_NAME, file);
            i = -1;
        }

        if (copy_to_user((void*)mmapIndex, &i, sizeof(int))) {
            printk(KERN_ERR "%s: fail to copy the index value to user.\n", DRIVER_NAME);
        }
        mutex_unlock(&codec_mutex);

        return 0;
    }

    if (paramInfo.apiIndex == EMUL_AVCODEC_ALLOC_CONTEXT) {
        writel((uint32_t)file, svcodec->ioaddr + CODEC_FILE_INDEX);
    }

    writel((uint32_t)paramInfo.ctxIndex, svcodec->ioaddr + CODEC_CONTEXT_INDEX);
    writel((uint32_t)paramInfo.mmapOffset, svcodec->ioaddr + CODEC_MMAP_OFFSET);
    writel((uint32_t)paramInfo.apiIndex, svcodec->ioaddr + CODEC_API_INDEX);

    mutex_unlock(&codec_mutex);
     
    return 0;
}

static ssize_t svcodec_read (struct file *file, char __user *buf,
                             size_t count, loff_t *fops)
{
    printk(KERN_INFO "%s: do nothing in the read operation \n", DRIVER_NAME);
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

    if (size > svcodec->mem_size) {
        CODEC_ERR_LOG("over mapping size\n");
        return -EINVAL;
    }

    ret = remap_pfn_range(vm, vm->vm_start, phys_addr, size, vm->vm_page_prot);
    if (ret < 0) {
        CODEC_ERR_LOG("failed to remap page range\n");
        return -EAGAIN;
    }

    vm->vm_flags |= VM_IO;
    vm->vm_flags |= VM_RESERVED;

    return 0;
}

static int svcodec_open (struct inode *inode, struct file *file)
{
    int i;
    int max_size = USABLE_MMAP_MAX_SIZE;

    mutex_lock(&codec_mutex);

    printk(KERN_DEBUG "%s: open! struct file:%p\n", DRIVER_NAME, file);
    if (svcodec->useMmap[max_size] == 0) {
        for (i = 0; i < max_size; i++) {
            svcodec->useMmap[i] = 1;
            printk(KERN_DEBUG "%s: reset useMmap[%d]=%d\n", DRIVER_NAME, i, svcodec->useMmap[i]);
        }
    }

    try_module_get(THIS_MODULE);
    mutex_unlock(&codec_mutex);

    return 0;
}

static int svcodec_release (struct inode *inode, struct file *file)
{
    int max_size = USABLE_MMAP_MAX_SIZE;
    uint8_t *useMmap;

    mutex_lock(&codec_mutex);

    useMmap  = (uint8_t*)file->private_data;
    printk(KERN_DEBUG "%s: close!! struct file:%p, priv_data:%p\n", DRIVER_NAME, file, useMmap);

    if (file && file->private_data) {
        if (svcodec->useMmap[max_size] > 0) {
            (svcodec->useMmap[max_size])--;
        }
        *useMmap = 1;
        printk(KERN_DEBUG "%s: available useMmap count:%d\n",
                DRIVER_NAME, (max_size - svcodec->useMmap[max_size]));

        /* notify closing codec device of qemu. */
        writel((uint32_t)file, svcodec->ioaddr + CODEC_CLOSED);
    }

#ifdef CODEC_DEBUG
    int i;
    for (i = 0; i < max_size; i++) {
        printk(KERN_DEBUG "%s: useMmap[%d]=%d\n", DRIVER_NAME, i, svcodec->useMmap[i]);
    }
#endif
    module_put(THIS_MODULE);
    mutex_unlock(&codec_mutex);

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
    svcodec = (svcodec_dev*)kmalloc(sizeof(svcodec_dev), GFP_KERNEL);
    memset(svcodec, 0x00, sizeof(svcodec_dev));

    svcodec->dev = pci_dev;

    if (pci_enable_device(pci_dev)) {
        CODEC_ERR_LOG("pci_enable_device failed\n");
        goto err_rel;
    }

    pci_set_master(pci_dev);

    svcodec->mem_start = pci_resource_start(pci_dev, 0);
    svcodec->mem_size = pci_resource_len(pci_dev, 0);

    if (!svcodec->mem_start) {
        CODEC_ERR_LOG("pci_resource_start failed:mem\n");
        goto err_out;
    }
    
    if (!request_mem_region(svcodec->mem_start, svcodec->mem_size, DRIVER_NAME)) {
        CODEC_ERR_LOG("request_mem_region failed:mem\n");
        goto err_out;
    }

    svcodec->io_start = pci_resource_start(pci_dev, 1);
    svcodec->io_size = pci_resource_len(pci_dev, 1);

    if (!svcodec->io_start) {
        CODEC_ERR_LOG("pci_resource_start failed:io\n");
        goto err_mem_region;
    }

    if (!request_mem_region(svcodec->io_start, svcodec->io_size, DRIVER_NAME)) {
        CODEC_ERR_LOG("request_io_region failed:io\n");
        goto err_mem_region;
    }

#if 0
    svcodec->memaddr = ioremap(svcodec->mem_start, svcodec->mem_size);
    if (!svcodec->memaddr) {
        CODEC_ERR_LOG( "[%s] : ioremap failed\n", __func__);
        goto err_io_region;
    }
#endif

    svcodec->ioaddr = ioremap_nocache(svcodec->io_start, svcodec->io_size);
    if (!svcodec->ioaddr) {
        CODEC_ERR_LOG("ioremap failed\n");
        goto err_io_region;
    }
    if (register_chrdev(CODEC_MAJOR, DRIVER_NAME, &svcodec_fops)) {
        CODEC_ERR_LOG("register_chrdev failed\n");
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

static struct pci_driver driver = {
    .name       = DRIVER_NAME,
    .id_table   = svcodec_pci_table,
    .probe      = svcodec_probe,
    .remove     = svcodec_remove,
};

static int __init svcodec_init (void)
{
    printk(KERN_INFO "%s: device is initialized.\n", DRIVER_NAME);
    return pci_register_driver(&driver);
}

static void __exit svcodec_exit (void)
{
    pci_unregister_driver(&driver);
}
module_init(svcodec_init);
module_exit(svcodec_exit);
