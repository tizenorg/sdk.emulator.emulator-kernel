/*
 * Maru Virtual Virtio Touchscreen device driver
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact: 
 *  GiWoong Kim <giwoong.kim@samsung.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Contributors:
 * - S-Core Co., Ltd
 *
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
//#include <linux/pci.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/kthread.h>


#define DEVICE_NAME "virtio-touchscreen"

/* This structure must match the qemu definitions */
typedef struct EmulTouchState {
    uint16_t x, y, z;
    uint8_t state;
} EmulTouchState;

typedef struct virtio_touchscreen
{
    struct virtio_device *vdev;
    struct virtqueue *vq;

    /* The thread servicing the touchscreen */
	struct task_struct *thread;
} virtio_touchscreen;


#define MAX_TRKID 6


static struct virtio_device_id id_table[] = {
    { VIRTIO_ID_TOUCHSCREEN, VIRTIO_DEV_ANY_ID },
    { 0 },
};

static int run_touchscreen(void *_vtouchscreen)
{
	virtio_touchscreen *vt = _vtouchscreen;
    int count = 0;

    struct scatterlist sg;
    EmulTouchState *touch = NULL;
    EmulTouchState *buf = kzalloc(sizeof(EmulTouchState), GFP_KERNEL);
    buf->x = MAX_TRKID;

    while (!kthread_should_stop()) {
        /* publish the real size of the buffer */
        sg_init_one(&sg, buf, sizeof(EmulTouchState));

        if (virtqueue_add_buf(vt->vq, &sg, 0, 1, (void*)buf, GFP_ATOMIC) >= 0) {
            virtqueue_kick(vt->vq);

            while (!(touch = virtqueue_get_buf(vt->vq, &count))) {
                cpu_relax();
            }

            if (touch == NULL) {
                printk(KERN_INFO "touch is null\n");
            } else {
                printk(KERN_INFO "x=%d, y=%d, z=%d, state=%d\n", touch->x, touch->y, touch->z, touch->state);
            }

            printk(KERN_INFO "virtio touchscreen remaining capacity of queue = %d\n", count);
        }

    }

    printk(KERN_INFO "stop run_touchscreen\n");
    kfree(buf);

    return 0;
}

static void vq_touchscreen_callback(struct virtqueue *vq)
{
    printk(KERN_INFO "vq touchscreen callback\n");
}

static int virtio_touchscreen_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "virtio touchscreen device is opened\n");
    return 0;
}

static int virtio_touchscreen_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "virtio touchscreen device is closed\n");
    return 0;
}

struct file_operations virtio_touchscreen_fops = {
    .owner      = THIS_MODULE,
    .write      = NULL,
    .mmap       = NULL,
    .open       = virtio_touchscreen_open,
    .release    = virtio_touchscreen_release,
};

static struct miscdevice virtio_touchscreen_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .fops = &virtio_touchscreen_fops,
};

static int virtio_touchscreen_probe(struct virtio_device *vdev)
{
    struct virtio_touchscreen *vt;
    int ret = 0;

    printk(KERN_INFO "virtio touchscreen driver is probed.\n");

    vdev->priv = vt = kmalloc(sizeof(*vt), GFP_KERNEL);
    if (!vt) {
		return -ENOMEM;
	}

    vt->vdev = vdev;

    vt->vq = virtio_find_single_vq(vt->vdev, NULL, "virtio-touchscreen-vq");
    if (IS_ERR(vt->vq)) {
        return PTR_ERR(vt->vq);
    }

    ret = misc_register(&virtio_touchscreen_dev);
    if (ret) {
        printk(KERN_ERR "virtio touchscreen cannot register device as misc\n");
        return -ENODEV;
    }


    // TODO:
    vt->vq->callback = vq_touchscreen_callback;
    virtqueue_enable_cb(vt->vq);

    /* thread */
    vt->thread = kthread_run(run_touchscreen, vt, "vtouchscreen");
    if (IS_ERR(vt->thread)) {
        kfree(vt);
		return PTR_ERR(vt->thread);
	}


    return 0;
}

static void __devexit virtio_touchscreen_remove(struct virtio_device *vdev)
{
    virtio_touchscreen *vt;

    printk(KERN_INFO "virtio touchscreen driver is removed.\n");

    vt = vdev->priv;

    kthread_stop(vt->thread);

    vdev->config->reset(vdev); // reset device
    misc_deregister(&virtio_touchscreen_dev);
    vdev->config->del_vqs(vdev); // clean up the queues

    kfree(vt);
}

static struct virtio_driver virtio_touchscreen_driver = {
    //.feature_table = features,
    //.feature_table_size = ARRAY_SIZE(features),
    .driver.name = KBUILD_MODNAME,
    .driver.owner = THIS_MODULE,
    .id_table = id_table,
    .probe = virtio_touchscreen_probe,
    .remove = virtio_touchscreen_remove,
#if 0
    .config_changed = virtballoon_changed,
#ifdef CONFIG_PM
    .freeze =   
    .restore =
#endif
#endif
};

static int __init virtio_touchscreen_init(void)
{
    printk(KERN_INFO "virtio touchscreen device is initialized.\n");
    return register_virtio_driver(&virtio_touchscreen_driver);
}

static void __exit virtio_touchscreen_exit(void)
{
    printk(KERN_INFO "virtio touchscreen device is destroyed.\n");
    unregister_virtio_driver(&virtio_touchscreen_driver);
}

module_init(virtio_touchscreen_init);
module_exit(virtio_touchscreen_exit);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_AUTHOR("GiWoong Kim <giwoong.kim@samsung.com>");
MODULE_DESCRIPTION("Emulator Virtio Touchscreen driver");
MODULE_LICENSE("GPL2");
