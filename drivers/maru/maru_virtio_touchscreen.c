/*
 * Maru Virtio Touchscreen Device Driver
 *
 * Copyright (c) 2012 - 2013 Samsung Electronics Co., Ltd. All rights reserved.
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
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/kthread.h>

MODULE_LICENSE("GPL2");
MODULE_AUTHOR("GiWoong Kim <giwoong.kim@samsung.com>");
MODULE_DESCRIPTION("Emulator Virtio Touchscreen driver");


#define DEVICE_NAME "virtio-touchscreen"

/* This structure must match the qemu definitions */
typedef struct EmulTouchEvent {
    uint16_t x, y, z;
    uint8_t state;
} EmulTouchEvent;
EmulTouchEvent *event;

typedef struct virtio_touchscreen
{
    struct virtio_device *vdev;
    struct virtqueue *vq;
    struct input_dev *idev;

    /* The thread servicing the touchscreen */
    struct task_struct *thread;
} virtio_touchscreen;
virtio_touchscreen *vt;


#define MAX_TRKID 10
#define TOUCHSCREEN_RESOLUTION_X 5040
#define TOUCHSCREEN_RESOLUTION_Y 3780
#define ABS_PRESSURE_MAX 255

#define MAX_BUF_COUNT MAX_TRKID
struct scatterlist sg[MAX_BUF_COUNT];
EmulTouchEvent vbuf[MAX_BUF_COUNT];

static struct virtio_device_id id_table[] = {
    { VIRTIO_ID_TOUCHSCREEN, VIRTIO_DEV_ANY_ID },
    { 0 },
};


#if 0
/**
 * @brief : event polling
 */
static int run_touchscreen(void *_vtouchscreen)
{
    virtio_touchscreen *vt = NULL;
    int err = 0;
    unsigned int len = 0; /* not used */
    unsigned int index = 0;
    unsigned int recv_index = 0;
    unsigned int id = 0; /* finger id */

    struct input_dev *input_dev = NULL;
    EmulTouchEvent *event = NULL;

    vt = (virtio_touchscreen *)_vtouchscreen;
    input_dev = vt->idev;

    sg_init_table(sg, MAX_BUF_COUNT);

    for (index = 0; index < MAX_BUF_COUNT; index++) {
        sg_set_buf(&sg[index], &vbuf[index], sizeof(EmulTouchEvent));

        err = virtqueue_add_buf(vt->vq, sg, 0, index + 1, (void *)index + 1, GFP_ATOMIC);
        if (err < 0) {
            printk(KERN_ERR "failed to add buf\n");
        }
    }
    virtqueue_kick(vt->vq);

    index = 0;

    while (!kthread_should_stop())
    {
        while ((recv_index = (unsigned int)virtqueue_get_buf(vt->vq, &len)) == 0) {
            cpu_relax();
        }

        do {
            event = &vbuf[recv_index - 1];
#if 0
            printk(KERN_INFO "touch x=%d, y=%d, z=%d, state=%d, recv_index=%d\n",
                event->x, event->y, event->z, event->state, recv_index);
#endif

            id = event->z;

            /* Multi-touch Protocol is B */
            if (event->state != 0)
            { /* pressed */
                input_mt_slot(input_dev, id);
                input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, true);
                input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, 10);
                input_report_abs(input_dev, ABS_MT_POSITION_X, event->x);
                input_report_abs(input_dev, ABS_MT_POSITION_Y, event->y);
            }
            else
            { /* released */
                input_mt_slot(input_dev, id);
                input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false);
            }

            input_sync(input_dev);

            /* expose buffer to other end */
            err = virtqueue_add_buf(vt->vq, sg, 0, recv_index, (void *)recv_index, GFP_ATOMIC);
            if (err < 0) {
                printk(KERN_ERR "failed to add buf\n");
            }

            recv_index = (unsigned int)virtqueue_get_buf(vt->vq, &len);
            if (recv_index == 0) {
                break;
            }
        } while(true);

        virtqueue_kick(vt->vq);
    }

    printk(KERN_INFO "virtio touchscreen thread is stopped\n");

    return 0;
}
#endif


int err = 0;
unsigned int len = 0; /* not used */
unsigned int index = 0;
unsigned int recv_index = 0;
unsigned int finger_id = 0; /* finger id */

/**
* @brief : callback for virtqueue
*/
static void vq_touchscreen_callback(struct virtqueue *vq)
{
#if 0
    printk(KERN_INFO "vq touchscreen callback\n");
#endif

    recv_index = (unsigned int)virtqueue_get_buf(vt->vq, &len);
    if (recv_index == 0) {
        printk(KERN_ERR "failed to get buffer\n");
        return;
    }

    do {
        event = &vbuf[recv_index - 1];

#if 0
        printk(KERN_INFO "touch x=%d, y=%d, z=%d, state=%d, recv_index=%d\n",
            event->x, event->y, event->z, event->state, recv_index);
#endif

        finger_id = event->z;

        if (finger_id < MAX_TRKID) {
            /* Multi-touch Protocol is B */

            if (event->state != 0)
            { /* pressed */
                input_mt_slot(vt->idev, finger_id);
                input_mt_report_slot_state(vt->idev, MT_TOOL_FINGER, true);
                input_report_abs(vt->idev, ABS_MT_TOUCH_MAJOR, 10);
                input_report_abs(vt->idev, ABS_MT_POSITION_X, event->x);
                input_report_abs(vt->idev, ABS_MT_POSITION_Y, event->y);
            }
            else
            { /* released */
                input_mt_slot(vt->idev, finger_id);
                input_mt_report_slot_state(vt->idev, MT_TOOL_FINGER, false);
            }

            input_sync(vt->idev);
        } else {
            printk(KERN_ERR "%d is an invalid finger id!\n", finger_id);
        }

        /* expose buffer to other end */
        err = virtqueue_add_buf(vt->vq, sg, 0,
            recv_index, (void *)recv_index, GFP_ATOMIC);

        if (err < 0) {
            printk(KERN_ERR "failed to add buffer!\n");
        }

        recv_index = (unsigned int)virtqueue_get_buf(vt->vq, &len);
        if (recv_index == 0) {
            break;
        }
    } while(true);

    virtqueue_kick(vt->vq);
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

static int input_touchscreen_open(struct input_dev *dev)
{
    printk(KERN_INFO "input touchscreen device is opened\n");
    return 0;
}

static void input_touchscreen_close(struct input_dev *dev)
{
    printk(KERN_INFO "input touchscreen device is closed\n");
}

struct file_operations virtio_touchscreen_fops = {
    .owner   = THIS_MODULE,
    .open    = virtio_touchscreen_open,
    .release = virtio_touchscreen_release,
};

static int virtio_touchscreen_probe(struct virtio_device *vdev)
{
    int ret = 0;

    printk(KERN_INFO "virtio touchscreen driver is probed\n");

    /* init virtio */
    vdev->priv = vt = kmalloc(sizeof(*vt), GFP_KERNEL);
    if (!vt) {
        return -ENOMEM;
    }

    vt->vdev = vdev;

    vt->vq = virtio_find_single_vq(vt->vdev,
        vq_touchscreen_callback, "virtio-touchscreen-vq");
    if (IS_ERR(vt->vq)) {
        ret = PTR_ERR(vt->vq);

        kfree(vt);
        vdev->priv = NULL;
        return ret;
    }

    /* enable callback */
    virtqueue_enable_cb(vt->vq);

    sg_init_table(sg, MAX_BUF_COUNT);

    /* prepare the buffers */
    for (index = 0; index < MAX_BUF_COUNT; index++) {
        sg_set_buf(&sg[index], &vbuf[index], sizeof(EmulTouchEvent));

        err = virtqueue_add_buf(vt->vq, sg, 0,
            index + 1, (void *)index + 1, GFP_ATOMIC);

        if (err < 0) {
            printk(KERN_ERR "failed to add buffer\n");

            kfree(vt);
            vdev->priv = NULL;
            return ret;
        }
    }

    /* register for input device */
    vt->idev = input_allocate_device();
    if (!vt->idev) {
        printk(KERN_ERR "failed to allocate a input touchscreen device\n");
        ret = -1;

        kfree(vt);
        vdev->priv = NULL;
        return ret;
    }

    vt->idev->name = "Maru Virtio Touchscreen";
    vt->idev->dev.parent = &(vdev->dev);

    input_set_drvdata(vt->idev, vt);
    vt->idev->open = input_touchscreen_open;
    vt->idev->close = input_touchscreen_close;

    vt->idev->evbit[0] |= BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
    vt->idev->absbit[BIT_WORD(ABS_MISC)] |= BIT_MASK(ABS_MISC);
    vt->idev->keybit[BIT_WORD(BTN_TOUCH)] |= BIT_MASK(BTN_TOUCH);

    input_mt_init_slots(vt->idev, MAX_TRKID);

    input_set_abs_params(vt->idev, ABS_X, 0,
        /*TOUCHSCREEN_RESOLUTION_X*/0, 0, 0); //TODO:
    input_set_abs_params(vt->idev, ABS_Y, 0,
        /*TOUCHSCREEN_RESOLUTION_Y*/0, 0, 0); //TODO:
    input_set_abs_params(vt->idev, ABS_MT_TRACKING_ID, 0,
        MAX_TRKID, 0, 0);
    input_set_abs_params(vt->idev, ABS_MT_TOUCH_MAJOR, 0,
        ABS_PRESSURE_MAX, 0, 0);
    input_set_abs_params(vt->idev, ABS_MT_POSITION_X, 0,
        TOUCHSCREEN_RESOLUTION_X, 0, 0);
    input_set_abs_params(vt->idev, ABS_MT_POSITION_Y, 0,
        TOUCHSCREEN_RESOLUTION_Y, 0, 0);

    ret = input_register_device(vt->idev);
    if (ret) {
        printk(KERN_ERR "input touchscreen driver cannot registered\n");
        ret = -1;

        input_mt_destroy_slots(vt->idev);
        input_free_device(vt->idev);
        kfree(vt);
        vdev->priv = NULL;
        return ret;
    }

#if 0 /* using a thread */

    /* Responses from the hypervisor occur through the get_buf function */
    vt->thread = kthread_run(run_touchscreen, vt, "vtouchscreen");
    if (IS_ERR(vt->thread)) {
        printk(KERN_ERR "unable to start the virtio touchscreen thread\n");
        ret = PTR_ERR(vt->thread);

        input_mt_destroy_slots(vt->idev);
        input_free_device(vt->idev);
        kfree(vt);
        vdev->priv = NULL;
        return ret;
    }
#else /* using a callback */

    virtqueue_kick(vt->vq);

    index = 0;

#endif

    return 0;
}

static void __devexit virtio_touchscreen_remove(struct virtio_device *vdev)
{
    virtio_touchscreen *vts = NULL;

    printk(KERN_INFO "virtio touchscreen driver is removed\n");

    vts = vdev->priv;

    kthread_stop(vts->thread);

    vdev->config->reset(vdev); /* reset device */
    vdev->config->del_vqs(vdev); /* clean up the queues */

    input_unregister_device(vts->idev);
    input_mt_destroy_slots(vts->idev);

    kfree(vts);
}

MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_touchscreen_driver = {
    .driver.name = KBUILD_MODNAME,
    .driver.owner = THIS_MODULE,
    .id_table = id_table,
    .probe = virtio_touchscreen_probe,
    .remove = __devexit_p(virtio_touchscreen_remove),
#if 0
    .feature_table = features,
    .feature_table_size = ARRAY_SIZE(features),
    .config_changed =
#ifdef CONFIG_PM
    .freeze =   
    .restore =
#endif
#endif
};

static int __init virtio_touchscreen_init(void)
{
    printk(KERN_INFO "virtio touchscreen device is initialized\n");
    return register_virtio_driver(&virtio_touchscreen_driver);
}

static void __exit virtio_touchscreen_exit(void)
{
    printk(KERN_INFO "virtio touchscreen device is destroyed\n");
    unregister_virtio_driver(&virtio_touchscreen_driver);
}

module_init(virtio_touchscreen_init);
module_exit(virtio_touchscreen_exit);

