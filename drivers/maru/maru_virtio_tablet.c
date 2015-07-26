/*
 * Maru Virtio Tablet Device Driver
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 *  Sungmin Ha <sungmin82.ha@samsung.com>
 *  Sangho Park <sangho.p@samsung.com>
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
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/kthread.h>

MODULE_LICENSE("GPL2");
MODULE_AUTHOR("Sungmin Ha <sungmin82.ha@samsung.com>");
MODULE_DESCRIPTION("Emulator Virtio Tablet driver");

#define DEVICE_NAME "virtio-tablet"
#define MAX_BUF_COUNT 256
static int vqidx = 0;

/* This structure must match the qemu definitions */
typedef struct EmulTabletEvent {
	uint8_t event_type;
	uint32_t x;
	uint32_t y;
	uint32_t btn;
	uint32_t btn_status;
} EmulTabletEvent;

typedef struct virtio_tablet
{
	struct virtio_device *vdev;
	struct virtqueue *vq;
	struct input_dev *idev;

	struct scatterlist sg[MAX_BUF_COUNT];
	struct EmulTabletEvent vbuf[MAX_BUF_COUNT];

	struct mutex event_mutex;
} virtio_tablet;

virtio_tablet *vtb;

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_TABLET, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

/* keep it consistent with emulator-skin definition */
enum {
	INPUT_MOVE = 1,
	INPUT_BTN = 2,
};

typedef enum InputButton
{
	INPUT_BUTTON_LEFT = 0,
	INPUT_BUTTON_MIDDLE = 1,
	INPUT_BUTTON_RIGHT = 2,
	INPUT_BUTTON_WHEEL_UP = 3,
	INPUT_BUTTON_WHEEL_DOWN = 4,
	INPUT_BUTTON_MAX = 5,
} InputButton;

static int err = 0;
static unsigned int index = 0;

/**
* @brief : callback for virtqueue
*/
static void vq_tablet_callback(struct virtqueue *vq)
{
	struct EmulTabletEvent tablet_event;

	while (1) {
		memcpy(&tablet_event, &vtb->vbuf[vqidx],
			sizeof(tablet_event));
		if (tablet_event.event_type == 0) {
			break;
		}

		if (tablet_event.event_type == INPUT_BTN) {
			/* TODO: Implementation for
			 * the remaining events are required. */
			if (tablet_event.btn == INPUT_BUTTON_LEFT) {
				/* 0x90001 is scan code.
				 * (logitech left click) */
				input_event(vtb->idev, EV_MSC, MSC_SCAN,
							0x90001);
				input_event(vtb->idev, EV_KEY, BTN_LEFT,
						tablet_event.btn_status);
				input_sync(vtb->idev);
			}
		} else if (tablet_event.event_type == INPUT_MOVE) {
			input_event(vtb->idev, EV_ABS, ABS_X,
						tablet_event.x);
			input_event(vtb->idev, EV_ABS, ABS_Y,
						tablet_event.y);
			input_sync(vtb->idev);
		} else {
			printk(KERN_ERR "Unknown event type\n");
			return;
		}

		memset(&vtb->vbuf[vqidx], 0x00,
				sizeof(tablet_event));
		vqidx++;
		if (vqidx == MAX_BUF_COUNT) {
			vqidx = 0;
		}
	}

	virtqueue_kick(vtb->vq);
}

static int virtio_tablet_open(struct inode *inode,
				struct file *file)
{
	printk(KERN_INFO "virtio tablet device is opened\n");
	return 0;
}

static int virtio_tablet_release(struct inode *inode,
				struct file *file)
{
	printk(KERN_INFO "virtio tablet device is closed\n");
	return 0;
}

static int input_tablet_open(struct input_dev *dev)
{
	printk(KERN_INFO "input tablet device is opened\n");
	return 0;
}

static void input_tablet_close(struct input_dev *dev)
{
	printk(KERN_INFO "input tablet device is closed\n");
}

struct file_operations virtio_tablet_fops = {
	.owner   = THIS_MODULE,
	.open    = virtio_tablet_open,
	.release = virtio_tablet_release,
};

static int virtio_tablet_probe(struct virtio_device *vdev)
{
	int ret = 0;
	vqidx = 0;

	printk(KERN_INFO "virtio tablet driver is probed\n");

	/* init virtio */
	vdev->priv = vtb = kmalloc(sizeof(*vtb), GFP_KERNEL);
	if (!vtb) {
		return -ENOMEM;
	}

	memset(&vtb->vbuf, 0x00, sizeof(vtb->vbuf));
	vtb->vdev = vdev;
	vtb->vq = virtio_find_single_vq(vtb->vdev,
			vq_tablet_callback, "virtio-tablet-vq");
	if (IS_ERR(vtb->vq)) {
		ret = PTR_ERR(vtb->vq);
		kfree(vtb);
		vdev->priv = NULL;
		return ret;
	}

	/* enable callback */
	virtqueue_enable_cb(vtb->vq);

	sg_init_table(vtb->sg, MAX_BUF_COUNT);

	/* prepare the buffers */
	for (index = 0; index < MAX_BUF_COUNT; index++) {
		sg_set_buf(&vtb->sg[index], &vtb->vbuf[index],
				sizeof(EmulTabletEvent));

		if (err < 0) {
			printk(KERN_ERR "failed to add buffer\n");
			kfree(vtb);
			vdev->priv = NULL;
			return ret;
		}
	}

	err = virtqueue_add_inbuf(vtb->vq, vtb->sg,
			MAX_BUF_COUNT, (void *)MAX_BUF_COUNT,
			GFP_ATOMIC);

	/* register for input device */
	vtb->idev = input_allocate_device();
	if (!vtb->idev) {
		printk(KERN_ERR "failed to allocate a input tablet device\n");
		ret = -1;
		kfree(vtb);
		vdev->priv = NULL;
		return ret;
	}

	vtb->idev->name = "Maru VirtIO Tablet";
	vtb->idev->dev.parent = &(vdev->dev);

	input_set_drvdata(vtb->idev, vtb);
	vtb->idev->open = input_tablet_open;
	vtb->idev->close = input_tablet_close;

	vtb->idev->evbit[0] = BIT_MASK(EV_KEY)
				| BIT_MASK(EV_REL)
				| BIT_MASK(EV_ABS)
				| BIT_MASK(EV_MSC);

	/* 32767 is max size of usbdevice tablet. */
	input_abs_set_max(vtb->idev, ABS_X, 32767);
	input_abs_set_max(vtb->idev, ABS_Y, 32767);

	set_bit(BTN_LEFT, vtb->idev->keybit);
	set_bit(BTN_RIGHT, vtb->idev->keybit);
	set_bit(BTN_MIDDLE, vtb->idev->keybit);
	set_bit(REL_WHEEL, vtb->idev->relbit);
	set_bit(ABS_X, vtb->idev->absbit);
	set_bit(ABS_Y, vtb->idev->absbit);
	set_bit(MSC_SCAN, vtb->idev->mscbit);

	ret = input_register_device(vtb->idev);
	if (ret) {
		printk(KERN_ERR "input tablet driver cannot registered\n");
		ret = -1;
		input_free_device(vtb->idev);
		kfree(vtb);
		vdev->priv = NULL;
		return ret;
	}

	virtqueue_kick(vtb->vq);
	index = 0;

	return 0;
}

static void virtio_tablet_remove(struct virtio_device *vdev)
{
	virtio_tablet *vtk = NULL;

	printk(KERN_INFO "virtio tablet driver is removed\n");

	vtk = vdev->priv;

	vdev->config->reset(vdev);	/* reset device */
	vdev->config->del_vqs(vdev);	/* clean up the queues */

	input_unregister_device(vtk->idev);

	kfree(vtk);
}

MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_tablet_driver = {
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = id_table,
	.probe = virtio_tablet_probe,
	.remove = virtio_tablet_remove,
};

static int __init virtio_tablet_init(void)
{
	printk(KERN_INFO "virtio tablet device is initialized\n");
	return register_virtio_driver(&virtio_tablet_driver);
}

static void __exit virtio_tablet_exit(void)
{
	printk(KERN_INFO "virtio tablet device is destroyed\n");
	unregister_virtio_driver(&virtio_tablet_driver);
}

module_init(virtio_tablet_init);
module_exit(virtio_tablet_exit);
