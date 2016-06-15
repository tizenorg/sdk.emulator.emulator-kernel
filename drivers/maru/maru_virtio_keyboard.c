/*
 * Maru Virtio Keyboard Device Driver
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd. All rights reserved.
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
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>


MODULE_LICENSE("GPL2");
MODULE_AUTHOR("SeokYeon Hwangm <syeon.hwang@samsung.com>");
MODULE_DESCRIPTION("Emulator virtio keyboard driver");


#define DRIVER_NAME "virtio-keyboard"

#define VKBD_LOG(log_level, fmt, ...) \
	printk(log_level "%s: " fmt, DRIVER_NAME, ##__VA_ARGS__)

#define MAX_BUF_COUNT 100

//#define DEBUG

struct EmulKbdEvent
{
	uint16_t code;
	uint16_t value;
};

struct virtio_keyboard
{
	struct virtio_device *vdev;
	struct virtqueue *vq;
	struct input_dev *idev;

	struct scatterlist sg[1];
	struct EmulKbdEvent evtbuf[MAX_BUF_COUNT];

	spinlock_t lock;
};

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_KEYBOARD, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static void vq_keyboard_handle(struct virtqueue *vq)
{
	unsigned int len; /* not used */
	struct virtio_keyboard *vkbd = vq->vdev->priv;
	struct EmulKbdEvent *event;

	unsigned long flags;
	int err;

	spin_lock_irqsave(&vkbd->lock, flags);
	while ((event = virtqueue_get_buf(vkbd->vq, &len)) != NULL) {
		spin_unlock_irqrestore(&vkbd->lock, flags);
#ifdef DEBUG
        printk(KERN_ERR "keyboard code = %d, value = %d\n", event->code,
                event->value);
#endif
		input_event(vkbd->idev, EV_KEY, event->code, event->value);
		input_sync(vkbd->idev);

		// add new buffer
		spin_lock_irqsave(&vkbd->lock, flags);
		sg_init_one(vkbd->sg, event, sizeof(*event));
		err = virtqueue_add_inbuf(vkbd->vq, vkbd->sg,
				1, event, GFP_ATOMIC);

		if (err < 0) {
			printk(KERN_ERR "failed to add buffer!\n");
		}
	}

	virtqueue_kick(vkbd->vq);
	spin_unlock_irqrestore(&vkbd->lock, flags);
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
	int index = 0;
	int err = 0;
	unsigned long flags;
	struct virtio_keyboard *vkbd;

	VKBD_LOG(KERN_INFO, "driver is probed\n");

	vdev->priv = vkbd = kzalloc(sizeof(struct virtio_keyboard), GFP_KERNEL);
	if (!vkbd) {
		return -ENOMEM;
	}

	vkbd->vdev = vdev;

	vkbd->vq = virtio_find_single_vq(vkbd->vdev, vq_keyboard_handle, "virtio-keyboard-vq");
	if (IS_ERR(vkbd->vq)) {
		ret = PTR_ERR(vkbd->vq);
		kfree(vkbd);
		vdev->priv = NULL;
		return ret;
	}

	/* register for input device */
	vkbd->idev = input_allocate_device();
	if (!vkbd->idev) {
		VKBD_LOG(KERN_ERR, "failed to allocate a input device.\n");
		kfree(vkbd);
		vdev->priv = NULL;
		return -ENOMEM;
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
		input_free_device(vkbd->idev);
		kfree(vkbd);
		vdev->priv = NULL;
		return ret;
	}

	spin_lock_irqsave(&vkbd->lock, flags);
	for (index = 0; index < MAX_BUF_COUNT; index++) {
		sg_init_one(vkbd->sg, &vkbd->evtbuf[index], sizeof(&vkbd->evtbuf[index]));

		err = virtqueue_add_inbuf(vkbd->vq, vkbd->sg,
				1, &vkbd->evtbuf[index], GFP_ATOMIC);

		if (err < 0) {
			printk(KERN_ERR "failed to add buffer\n");

			kfree(vkbd);
			vdev->priv = NULL;
			return err;
		}
	}
	spin_unlock_irqrestore(&vkbd->lock, flags);

	virtqueue_kick(vkbd->vq);

	return 0;
}

static void virtio_keyboard_remove(struct virtio_device *vdev)
{
	struct virtio_keyboard *vkbd = vdev->priv;

	VKBD_LOG(KERN_INFO, "driver is removed.\n");
	if (!vkbd) {
		VKBD_LOG(KERN_ERR, "vkbd is NULL.\n");
		return;
	}

	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);

	input_unregister_device(vkbd->idev);

	kfree(vkbd);
	vkbd = NULL;
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
