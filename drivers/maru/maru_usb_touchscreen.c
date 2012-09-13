/*
 * Maru USB Touchscreen Device Driver
 * Based on drivers/input/tablet/wacom_sys.c:
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact: 
 *  GiWoong Kim <giwoong.kim@samsung.com>
 *  HyunJun Son <hj79.son@samsung.com>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/input/mt.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("s-core");
MODULE_DESCRIPTION("Emulator Touchscreen driver for x86");

/* touchscreen device features */
#define MAX_TRKID 6
#define EMUL_TOUCHSCREEN_PACKET_LEN 7
#define TOUCHSCREEN_RESOLUTION_X 5040
#define TOUCHSCREEN_RESOLUTION_Y 3780
#define ABS_PRESSURE_MAX 255

struct emul_touchscreen {
    dma_addr_t data_dma;
    struct input_dev *emuldev;
    struct usb_device *usbdev;
    struct usb_interface *intf;
    struct urb *irq;
    unsigned char *data;
    struct mutex lock;
    unsigned int open:1;
    char phys[32];
};

/* This structure must match the qemu definitions */
typedef struct USBEmulTouchscreenPacket {
    uint16_t x, y, z;
    uint8_t state;
} USBEmulTouchscreenPacket;


static void emul_touchscreen_sys_irq(struct urb *urb)
{
    int retval = 0;
    struct emul_touchscreen *usb_ts = urb->context;
    struct input_dev *input_dev = usb_ts->emuldev;
    USBEmulTouchscreenPacket *packet = (USBEmulTouchscreenPacket *)usb_ts->data;
    int id = packet->z;

    switch (urb->status) {
        case 0:
            /* success */
            break;
        case -ECONNRESET:
        case -ENOENT:
        case -ESHUTDOWN:
            /* this urb is terminated, clean up */
            dbg("%s - urb shutting down with status: %d", __func__, urb->status);
            return;
        default:
            dbg("%s - nonzero urb status received: %d", __func__, urb->status);
            goto exit;
    }

    /* Multi-touch Protocol B */
    if (packet->state != 0)
    { /* pressed */
        input_mt_slot(input_dev, id);
        input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, true);
        input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, 5);
        input_report_abs(input_dev, ABS_MT_POSITION_X, packet->x);
        input_report_abs(input_dev, ABS_MT_POSITION_Y, packet->y);
        //printk(KERN_INFO "!!pressed x=%d, y=%d, z=%d",
            //packet->x, packet->y, packet->z);
    }
    else
    { /* release */
        input_mt_slot(input_dev, id);
        input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false);
        //printk(KERN_INFO "!!released x=%d, y=%d, z=%d",
            //packet->x, packet->y, packet->z);
    }

    input_sync(input_dev);

 exit:
    usb_mark_last_busy(usb_ts->usbdev);
    retval = usb_submit_urb(urb, GFP_ATOMIC);
    if (retval) {
        err("%s - usb_submit_urb failed with result %d", __func__, retval);
    }
}

static int emul_touchscreen_open(struct input_dev *dev)
{
    struct emul_touchscreen *usb_ts = input_get_drvdata(dev);

    printk(KERN_INFO "usb touchscreen device is opened\n");

    mutex_lock(&usb_ts->lock);
    usb_ts->irq->dev = usb_ts->usbdev;

    if (usb_autopm_get_interface(usb_ts->intf) < 0) {
        mutex_unlock(&usb_ts->lock);
        return -EIO;
    }

    if (usb_submit_urb(usb_ts->irq, GFP_KERNEL)) {
        usb_autopm_put_interface(usb_ts->intf);
        mutex_unlock(&usb_ts->lock);
        return -EIO;
    }

    usb_ts->open = 1;
    usb_ts->intf->needs_remote_wakeup = 1;

    mutex_unlock(&usb_ts->lock);
    return 0;
}

static void emul_touchscreen_close(struct input_dev *dev)
{
    struct emul_touchscreen *usb_ts = input_get_drvdata(dev);

    printk(KERN_INFO "usb touchscreen device is closed\n");

    mutex_lock(&usb_ts->lock);
    usb_kill_urb(usb_ts->irq);
    usb_ts->open = 0;
    usb_ts->intf->needs_remote_wakeup = 0;
    mutex_unlock(&usb_ts->lock);

    input_mt_destroy_slots(usb_ts->emuldev);
}

static int emul_touchscreen_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    struct usb_endpoint_descriptor *endpoint;
    struct emul_touchscreen *usb_ts;
    int error = -ENOMEM;

    printk(KERN_INFO "usb touchscreen driver is probed\n");

    usb_ts = kzalloc(sizeof(struct emul_touchscreen), GFP_KERNEL);
    if (!usb_ts) {
        goto fail1;
    }

    usb_ts->usbdev = interface_to_usbdev(intf);
    usb_ts->data = usb_alloc_coherent(usb_ts->usbdev, 10, GFP_KERNEL, &usb_ts->data_dma);
    if (!usb_ts->data) {
        goto fail1;
    }

    usb_ts->irq = usb_alloc_urb(0, GFP_KERNEL);
    if (!usb_ts->irq) {
        goto fail2;
    }

    usb_ts->emuldev = input_allocate_device();
    if (!usb_ts->emuldev) {
        goto fail1;
    }
    
    usb_ts->intf = intf;

    mutex_init(&usb_ts->lock);
    usb_make_path(usb_ts->usbdev, usb_ts->phys, sizeof(usb_ts->phys));
    strlcat(usb_ts->phys, "/input0", sizeof(usb_ts->phys));

    usb_ts->emuldev->name = "Maru USB Touchscreen";
    usb_to_input_id(usb_ts->usbdev, &usb_ts->emuldev->id);

    usb_ts->emuldev->dev.parent = &intf->dev;

    input_set_drvdata(usb_ts->emuldev, usb_ts);

    usb_ts->emuldev->open = emul_touchscreen_open;
    usb_ts->emuldev->close = emul_touchscreen_close;

    endpoint = &intf->cur_altsetting->endpoint[0].desc;

    usb_ts->emuldev->evbit[0] |= BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
    usb_ts->emuldev->absbit[BIT_WORD(ABS_MISC)] |= BIT_MASK(ABS_MISC);
    usb_ts->emuldev->keybit[BIT_WORD(BTN_TOUCH)] |= BIT_MASK(BTN_TOUCH);

    input_set_abs_params(usb_ts->emuldev,
        ABS_X, 0, TOUCHSCREEN_RESOLUTION_X, 4, 0);
    input_set_abs_params(usb_ts->emuldev,
        ABS_Y, 0, TOUCHSCREEN_RESOLUTION_Y, 4, 0);

    /* for multitouch */
    input_mt_init_slots(usb_ts->emuldev, MAX_TRKID);
    input_set_abs_params(usb_ts->emuldev,
        ABS_MT_TRACKING_ID, 0, MAX_TRKID, 0, 0);
    input_set_abs_params(usb_ts->emuldev,
        ABS_MT_TOUCH_MAJOR, 0, ABS_PRESSURE_MAX, 0, 0);
    input_set_abs_params(usb_ts->emuldev,
        ABS_MT_POSITION_X, 0, TOUCHSCREEN_RESOLUTION_X, 0, 0);
    input_set_abs_params(usb_ts->emuldev,
        ABS_MT_POSITION_Y, 0, TOUCHSCREEN_RESOLUTION_Y, 0, 0);

    usb_fill_int_urb(usb_ts->irq, usb_ts->usbdev,
             usb_rcvintpipe(usb_ts->usbdev, endpoint->bEndpointAddress),
             usb_ts->data, EMUL_TOUCHSCREEN_PACKET_LEN,
             emul_touchscreen_sys_irq, usb_ts, endpoint->bInterval);
    usb_ts->irq->transfer_dma = usb_ts->data_dma;
    usb_ts->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    error = input_register_device(usb_ts->emuldev);
    if (error) {
        goto fail3;
    }

    usb_set_intfdata(intf, usb_ts);
    return 0;

 fail3:    usb_free_urb(usb_ts->irq);
 fail2:    usb_free_coherent(usb_ts->usbdev, 10, usb_ts->data, usb_ts->data_dma);
 fail1:    input_free_device(usb_ts->emuldev);
    kfree(usb_ts);
    return error;
}

static void emul_touchscreen_disconnect(struct usb_interface *intf)
{
    struct emul_touchscreen *usb_ts = usb_get_intfdata(intf);

    printk(KERN_INFO "usb touchscreen device is disconnected\n");

    usb_set_intfdata(intf, NULL);
    if (usb_ts) {
        usb_kill_urb(usb_ts->irq);
        input_unregister_device(usb_ts->emuldev);
        usb_free_urb(usb_ts->irq);
        usb_free_coherent(interface_to_usbdev(intf), 10, usb_ts->data, usb_ts->data_dma);
        kfree(usb_ts);
    }
}

static int emul_touchscreen_suspend(struct usb_interface *intf, pm_message_t message)
{
    struct emul_touchscreen *usb_ts = usb_get_intfdata(intf);

    printk(KERN_INFO "usb touchscreen device is suspended\n");

    mutex_lock(&usb_ts->lock);
    usb_kill_urb(usb_ts->irq);
    mutex_unlock(&usb_ts->lock);

    return 0;
}

static int emul_touchscreen_resume(struct usb_interface *intf)
{
    struct emul_touchscreen *usb_ts = usb_get_intfdata(intf);
    int rv = 0;

    printk(KERN_INFO "usb touchscreen device is resumed\n");

    mutex_lock(&usb_ts->lock);
    if (usb_ts->open) {
        rv = usb_submit_urb(usb_ts->irq, GFP_NOIO);
    } else {
        rv = 0;
    }
    mutex_unlock(&usb_ts->lock);

    return rv;
}

static struct usb_device_id emul_usb_touchscreen_table[] = {
    { USB_DEVICE(0x056a, 0x00) },
    { } /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, emul_usb_touchscreen_table);

static struct usb_driver emul_touchscreen_driver = {
    .name                 = "usb-emul-touchscreen",
    .id_table             = emul_usb_touchscreen_table,
    .probe                = emul_touchscreen_probe,
    .disconnect           = emul_touchscreen_disconnect,
    .suspend              = emul_touchscreen_suspend,
    .resume               = emul_touchscreen_resume,
    .reset_resume         = emul_touchscreen_resume,
    .supports_autosuspend = 1,
};

static int __init emul_touchscreen_init(void)
{
    int result = 0;

    printk(KERN_INFO "usb touchscreen device is initialized\n");

    result = usb_register(&emul_touchscreen_driver);
    if (result != 0) {
        printk(KERN_ERR "emul_touchscreen_init: usb_register=%d\n", result);
    }

    return result;
}

static void __exit emul_touchscreen_exit(void)
{
    printk(KERN_INFO "usb touchscreen device is destroyed\n");
    usb_deregister(&emul_touchscreen_driver);
}

module_init(emul_touchscreen_init);
module_exit(emul_touchscreen_exit);
