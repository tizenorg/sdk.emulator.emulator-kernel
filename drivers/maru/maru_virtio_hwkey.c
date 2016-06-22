/*
 * Maru Virtio Hwkey Device Driver
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 *  SeokYeon Hwang <syeon.hwang@samsung.com>
 *  Sungmin Ha <sungmin82.ha@samsung.com>
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

#include "maru_virtio_device.h"

MODULE_LICENSE("GPL2");
MODULE_AUTHOR("SeokYeon Hwangm <syeon.hwang@samsung.com>");
MODULE_DESCRIPTION("Emulator Virtio Hwkey driver");

#define DEVICE_NAME "virtio-hwkey"

/* This structure must match the qemu definitions */
struct hwkey_event {
	uint8_t event_type;
	uint32_t keycode;
};

struct virtio_hwkey
{
	struct maru_virtio_device *mdev;

	struct input_dev *idev;
	struct hwkey_event evtbuf[MAX_BUF_COUNT];
};

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_HWKEY, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

/* keep it consistent with emulator-skin definition */
enum {
	KEY_PRESSED = 1,
	KEY_RELEASED = 2,
};

/**
 * @brief : callback for virtqueue
 */
static void vq_hwkey_callback(struct virtqueue *vq)
{
	unsigned int len; /* not used */
	struct virtio_hwkey *vh = vq->vdev->priv;
	struct maru_virtio_device *mdev = vh->mdev;
	struct hwkey_event *event;

	unsigned long flags;
	int err;

	spin_lock_irqsave(&mdev->lock, flags);
	while ((event = virtqueue_get_buf(mdev->vq, &len)) != NULL) {
		spin_unlock_irqrestore(&mdev->lock, flags);

		printk(KERN_INFO "keycode: %d, event_type: %d\n",
				event->keycode, event->event_type);
		if (event->event_type == KEY_PRESSED) {
			input_event(vh->idev, EV_KEY, event->keycode, true);
		}
		else if (event->event_type == KEY_RELEASED) {
			input_event(vh->idev, EV_KEY, event->keycode, false);
		}
		else {
			printk(KERN_ERR "Unknown event type\n");
		}

		input_sync(vh->idev);

		// add new buffer
		spin_lock_irqsave(&mdev->lock, flags);
		err = add_new_buf(mdev, event, sizeof(*event));
		if (err < 0) {
			printk(KERN_ERR "failed to add buffer\n");
		}
	}

	virtqueue_kick(mdev->vq);
	spin_unlock_irqrestore(&mdev->lock, flags);
}

static int input_hwkey_open(struct input_dev *dev)
{
	printk(KERN_INFO "input hwkey device is opened\n");
	return 0;
}

static void input_hwkey_close(struct input_dev *dev)
{
	printk(KERN_INFO "input hwkey device is closed\n");
}

static int virtio_hwkey_probe(struct virtio_device *vdev)
{
	int ret = 0;
	struct virtio_hwkey *vh;

	/* init virtio */
	vdev->priv = vh = kzalloc(sizeof(*vh), GFP_KERNEL);
	if (!vh) {
		return -ENOMEM;
	}
	vh->mdev = kzalloc(sizeof(*vh->mdev), GFP_KERNEL);
	if (!vh->mdev) {
		ret = -ENOMEM;
		goto err3;
	}

	/* register for input device */
	vh->idev = input_allocate_device();
	if (!vh->idev) {
		printk(KERN_ERR "failed to allocate a input hwkey device\n");
		ret = -ENOMEM;
		goto err2;
	}

	vh->idev->name = "Maru Virtio Hwkey";
	vh->idev->dev.parent = &(vdev->dev);

	input_set_drvdata(vh->idev, vh);
	vh->idev->open = input_hwkey_open;
	vh->idev->close = input_hwkey_close;

	vh->idev->evbit[0] = BIT_MASK(EV_KEY);
	/* to support any keycode */
	memset(vh->idev->keybit, 0xffffffff,
			sizeof(unsigned long) * BITS_TO_LONGS(KEY_CNT));

	ret = input_register_device(vh->idev);
	if (ret) {
		printk(KERN_ERR "input hwkey driver cannot registered\n");
		ret = -1;
		goto err;
	}

	ret = init_virtio_device(vdev, vh->mdev, vq_hwkey_callback,
			vh->evtbuf, sizeof(struct hwkey_event));
	if (ret) {
		goto err;
	}

	printk(KERN_INFO "virtio hwkey driver is probed\n");

	return 0;

err:
	input_free_device(vh->idev);
err2:
	kfree(vh->mdev);
err3:
	kfree(vh);
	vdev->priv = NULL;
	return ret;
}

static void virtio_hwkey_remove(struct virtio_device *vdev)
{
	struct virtio_hwkey *vh = vdev->priv;

	input_unregister_device(vh->idev);
	deinit_virtio_device(vdev);

	kfree(vh);

	printk(KERN_INFO "virtio hwkey driver is removed\n");
}

MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_hwkey_driver = {
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = id_table,
	.probe = virtio_hwkey_probe,
	.remove = virtio_hwkey_remove,
};

static int __init virtio_hwkey_init(void)
{
	printk(KERN_INFO "virtio hwkey device is initialized\n");
	return register_virtio_driver(&virtio_hwkey_driver);
}

static void __exit virtio_hwkey_exit(void)
{
	printk(KERN_INFO "virtio hwkey device is destroyed\n");
	unregister_virtio_driver(&virtio_hwkey_driver);
}

module_init(virtio_hwkey_init);
module_exit(virtio_hwkey_exit);
