/*
 * Maru Virtio Keyboard Device Driver
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 *  SeokYeon Hwang <syeon.hwang@samsung.com>
 *  GiWoong Kim <giwoong.kim@samsung.com>
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

#include "maru_virtio_device.h"

MODULE_LICENSE("GPL2");
MODULE_AUTHOR("SeokYeon Hwangm <syeon.hwang@samsung.com>");
MODULE_DESCRIPTION("Emulator virtio keyboard driver");


#define DRIVER_NAME "virtio-keyboard"

#define VKBD_LOG(log_level, fmt, ...) \
	printk(log_level "%s: " fmt, DRIVER_NAME, ##__VA_ARGS__)

//#define DEBUG

struct keyboard_event
{
	uint16_t code;
	uint16_t value;
};

struct virtio_keyboard
{
	struct maru_virtio_device *mdev;

	struct input_dev *idev;
	struct keyboard_event evtbuf[MAX_BUF_COUNT];
};

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_KEYBOARD, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static void vq_keyboard_callback(struct virtqueue *vq)
{
	unsigned int len; /* not used */
	struct virtio_keyboard *vkbd = vq->vdev->priv;
	struct maru_virtio_device *mdev = vkbd->mdev;
	struct keyboard_event *event;

	unsigned long flags;
	int err;

	spin_lock_irqsave(&mdev->lock, flags);
	while ((event = virtqueue_get_buf(mdev->vq, &len)) != NULL) {
		spin_unlock_irqrestore(&mdev->lock, flags);
#ifdef DEBUG
        printk(KERN_ERR "keyboard code = %d, value = %d\n", event->code,
                event->value);
#endif

		input_event(vkbd->idev, EV_KEY, event->code, event->value);
		input_sync(vkbd->idev);

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

static int input_keyboard_open(struct input_dev *dev)
{
	VKBD_LOG(KERN_DEBUG, "input_keyboard_open\n");
	return 0;
}

static void input_keyboard_close(struct input_dev *dev)
{
	VKBD_LOG(KERN_DEBUG, "input_keyboard_close\n");
}

static int virtio_keyboard_probe(struct virtio_device *vdev)
{
	int ret = 0;
	struct virtio_keyboard *vkbd;

	VKBD_LOG(KERN_INFO, "driver is probed\n");

	vdev->priv = vkbd = kzalloc(sizeof(struct virtio_keyboard), GFP_KERNEL);
	if (!vkbd) {
		return -ENOMEM;
	}
	vkbd->mdev = kzalloc(sizeof(*vkbd->mdev), GFP_KERNEL);
	if (!vkbd->mdev) {
		ret = -ENOMEM;
		goto err3;
	}

	/* register for input device */
	vkbd->idev = input_allocate_device();
	if (!vkbd->idev) {
		VKBD_LOG(KERN_ERR, "failed to allocate a input device.\n");
		ret = -ENOMEM;
		goto err2;
	}

	vkbd->idev->name = "Maru VirtIO Keyboard";
	vkbd->idev->dev.parent = &(vdev->dev);

	input_set_drvdata(vkbd->idev, vkbd);
	vkbd->idev->open = input_keyboard_open;
	vkbd->idev->close = input_keyboard_close;

	/* initialize a device as a keyboard device.
	 * refer to struct input_dev from input.h.  */
	vkbd->idev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP)
				| BIT_MASK(EV_MSC) | BIT_MASK(EV_LED);
	vkbd->idev->ledbit[0] = BIT_MASK(LED_NUML) | BIT_MASK(LED_CAPSL)
				| BIT_MASK(LED_SCROLLL) | BIT_MASK(LED_COMPOSE)
				| BIT_MASK(LED_KANA);
	set_bit(MSC_SCAN, vkbd->idev->mscbit);

	/* set keybit field as xinput keyboard. */
	vkbd->idev->keybit[0] = 0xfffffffe;
	vkbd->idev->keybit[1] = 0xffffffff;
	vkbd->idev->keybit[2] = 0xffefffff;
	vkbd->idev->keybit[3] = 0xfebeffdf;
	vkbd->idev->keybit[4] = 0xc14057ff;
	vkbd->idev->keybit[5] = 0xff9f207a;
	vkbd->idev->keybit[6] = 0x7;
	vkbd->idev->keybit[7] = 0x10000;

	ret = input_register_device(vkbd->idev);
	if (ret) {
		VKBD_LOG(KERN_ERR, "failed to register a input device.\n");
		goto err;
	}

	ret = init_virtio_device(vdev, vkbd->mdev, vq_keyboard_callback,
			vkbd->evtbuf, sizeof(struct keyboard_event));
	if (ret) {
		goto err;
	}

	VKBD_LOG(KERN_INFO, "driver is probed\n");

	return 0;

err:
	input_free_device(vkbd->idev);
err2:
	kfree(vkbd->mdev);
err3:
	kfree(vkbd);
	vdev->priv = NULL;
	return ret;
}

static void virtio_keyboard_remove(struct virtio_device *vdev)
{
	struct virtio_keyboard *vkbd = vdev->priv;

	input_unregister_device(vkbd->idev);
	deinit_virtio_device(vdev);

	kfree(vkbd);

	VKBD_LOG(KERN_INFO, "driver is removed.\n");
}

MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_keyboard_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
	},
	.id_table = id_table,
	.probe = virtio_keyboard_probe,
	.remove = virtio_keyboard_remove,
#if 0
#ifdef CONFIG_PM
	.freeze = virtio_codec_freeze,
	.restore = virtio_codec_restore,
#endif
#endif
};

static int __init virtio_keyboard_init(void)
{
	VKBD_LOG(KERN_INFO, "driver is initialized.\n");
	return register_virtio_driver(&virtio_keyboard_driver);
}

static void __exit virtio_keyboard_exit(void)
{
	VKBD_LOG(KERN_INFO, "driver is destroyed.\n");
	unregister_virtio_driver(&virtio_keyboard_driver);
}

module_init(virtio_keyboard_init);
module_exit(virtio_keyboard_exit);
