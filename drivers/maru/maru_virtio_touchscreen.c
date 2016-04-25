/*
 * Maru Virtio Touchscreen Device Driver
 *
 * Copyright (c) 2012 - 2013 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 *  GiWoong Kim <giwoong.kim@samsung.com>
 *  SeokYeon Hwang <syeon.hwang@samsung.com>
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

#define MAX_TRKID 10
#define ABS_PRESSURE_MAX 255

#define MAX_BUF_COUNT MAX_TRKID

//#define DEBUG_TS

/* This structure must match the qemu definitions */
struct touch_event
{
	uint16_t x, y, z;
	uint8_t state;
};

struct virtio_touchscreen
{
	struct virtio_device *vdev;
	struct input_dev *idev;

	struct virtqueue *vq;
	struct scatterlist sg[1];
	struct touch_event evtbuf[MAX_BUF_COUNT];

	spinlock_t lock;
};

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_TOUCHSCREEN, VIRTIO_DEV_ANY_ID },
	{ 0 },
};


/**
 * @brief : callback for virtqueue
 */
static void vq_touchscreen_callback(struct virtqueue *vq)
{
	unsigned int len; /* not used */
	unsigned int finger_id; /* finger id */
	struct virtio_touchscreen *vt = vq->vdev->priv;
	struct touch_event *event;

	unsigned long flags;
	int err;

#ifdef DEBUG_TS
	printk(KERN_INFO "vq touchscreen callback\n");
#endif

	spin_lock_irqsave(&vt->lock, flags);
	while ((event = virtqueue_get_buf(vt->vq, &len)) != NULL) {
		spin_unlock_irqrestore(&vt->lock, flags);
#ifdef DEBUG_TS
		printk(KERN_INFO "touch x=%d, y=%d, z=%d, state=%d, len=%d\n",
				event->x, event->y, event->z, event->state, len);
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

		// add new buffer
		spin_lock_irqsave(&vt->lock, flags);
		sg_init_one(vt->sg, event, sizeof(*event));
		err = virtqueue_add_inbuf(vt->vq, vt->sg,
				1, event, GFP_ATOMIC);

		if (err < 0) {
			printk(KERN_ERR "failed to add buffer!\n");
		}
	};
	virtqueue_kick(vt->vq);
	spin_unlock_irqrestore(&vt->lock, flags);
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

extern char *saved_command_line;
#define VM_RESOLUTION_KEY "vm_resolution="

static int virtio_touchscreen_probe(struct virtio_device *vdev)
{
	unsigned long width = 0;
	unsigned long height = 0;
	char *cmdline = NULL;
	char *value = NULL;
	char *tmp = NULL;
	int err = 0;
	int ret = 0;

	unsigned long flags;
	int index;
	struct virtio_touchscreen *vt;

	printk(KERN_INFO "virtio touchscreen driver is probed\n");

	/* init virtio */
	vdev->priv = vt = kzalloc(sizeof(*vt), GFP_KERNEL);
	if (!vt) {
		return -ENOMEM;
	}

	spin_lock_init(&vt->lock);

	vt->vdev = vdev;

	vt->vq = virtio_find_single_vq(vt->vdev,
			vq_touchscreen_callback, "virtio-touchscreen-vq");
	if (IS_ERR(vt->vq)) {
		ret = PTR_ERR(vt->vq);

		kfree(vt);
		vdev->priv = NULL;
		return ret;
	}

	cmdline = kzalloc(strlen(saved_command_line) + 1, GFP_KERNEL);
	if (cmdline) {
		/* get VM resolution */
		strcpy(cmdline, saved_command_line);
		tmp = strstr(cmdline, VM_RESOLUTION_KEY);

		if (tmp != NULL) {
			tmp += strlen(VM_RESOLUTION_KEY);

			value = strsep(&tmp, "x");
			err = kstrtoul(value, 10, &width);
			if (err) {
				printk(KERN_WARNING "vm width option is not defined\n");
				width = 0;
			}

			value = strsep(&tmp, " ");
			err = kstrtoul(value, 10, &height);
			if (err) {
				printk(KERN_WARNING "vm height option is not defined\n");
				height = 0;
			}
		}

		kfree(cmdline);
	}

	if (width != 0 && height != 0) {
		printk(KERN_INFO "emul resolution : %lux%lu\n", width, height);
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

	input_mt_init_slots(vt->idev, MAX_TRKID, 0);

	input_set_abs_params(vt->idev, ABS_X, 0,
			width, 0, 0);
	input_set_abs_params(vt->idev, ABS_Y, 0,
			height, 0, 0);
	input_set_abs_params(vt->idev, ABS_MT_TRACKING_ID, 0,
			MAX_TRKID, 0, 0);
	input_set_abs_params(vt->idev, ABS_MT_TOUCH_MAJOR, 0,
			ABS_PRESSURE_MAX, 0, 0);
	input_set_abs_params(vt->idev, ABS_MT_POSITION_X, 0,
			width, 0, 0);
	input_set_abs_params(vt->idev, ABS_MT_POSITION_Y, 0,
			height, 0, 0);

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

	spin_lock_irqsave(&vt->lock, flags);
	/* prepare the buffers */
	for (index = 0; index < MAX_BUF_COUNT; index++) {
		sg_init_one(vt->sg, &vt->evtbuf[index], sizeof(&vt->evtbuf[index]));

		err = virtqueue_add_inbuf(vt->vq, vt->sg,
				1, &vt->evtbuf[index], GFP_ATOMIC);

		if (err < 0) {
			printk(KERN_ERR "failed to add buffer\n");

			kfree(vt);
			vdev->priv = NULL;
			return err;
		}
	}
	spin_unlock_irqrestore(&vt->lock, flags);

	virtqueue_kick(vt->vq);
	index = 0;

	return 0;
}

static void virtio_touchscreen_remove(struct virtio_device *vdev)
{
	struct virtio_touchscreen *vt = NULL;

	printk(KERN_INFO "virtio touchscreen driver is removed\n");

	vt = vdev->priv;

	vdev->config->reset(vdev); /* reset device */
	vdev->config->del_vqs(vdev); /* clean up the queues */

	input_unregister_device(vt->idev);
	input_mt_destroy_slots(vt->idev);

	kfree(vt);
}

MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_touchscreen_driver = {
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = id_table,
	.probe = virtio_touchscreen_probe,
	.remove = virtio_touchscreen_remove,
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
