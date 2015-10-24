/*
 * Maru Virtio Keyboard Device Driver
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 *  Kitae Kim <kitae.kim@samsung.com>
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
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>

MODULE_LICENSE("GPL2");
MODULE_AUTHOR("Kitae Kim <kt920.kim@samsung.com>");
MODULE_DESCRIPTION("Emulator Virtio Keyboard Driver");

#define DRIVER_NAME "virtio-keyboard"
#define VKBD_LOG(log_level, fmt, ...) \
	printk(log_level "%s: " fmt, DRIVER_NAME, ##__VA_ARGS__)

#define KBD_BUF_SIZE 100
static int vqidx = 0;

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

	struct EmulKbdEvent kbdevt[KBD_BUF_SIZE];
	struct scatterlist sg[KBD_BUF_SIZE];

	struct mutex event_mutex;
};

struct virtio_keyboard *vkbd;

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_KEYBOARD, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static void vq_keyboard_handle(struct virtqueue *vq)
{
	int err = 0, len = 0;
	void *data;
	struct EmulKbdEvent kbdevent;

	VKBD_LOG(KERN_DEBUG, "virtqueue callback.\n");
	data = virtqueue_get_buf(vq, &len);
	if (!data) {
		VKBD_LOG(KERN_ERR, "there is no available buffer.\n");
		return;
	}

	VKBD_LOG(KERN_DEBUG, "vqidx: %d\n", vqidx);
	while (1) {
		memcpy(&kbdevent, &vkbd->kbdevt[vqidx], sizeof(kbdevent));
#if 1
		if (kbdevent.code == 0) {
			break;
		}
#endif
		/* how to get keycode and value. */
		input_event(vkbd->idev, EV_KEY, kbdevent.code, kbdevent.value);
		input_sync(vkbd->idev);
		printk(KERN_ERR "input_event code = %d, value = %d\n", kbdevent.code, kbdevent.value);
		memset(&vkbd->kbdevt[vqidx], 0x00, sizeof(kbdevent));
		vqidx++;
		if (vqidx == KBD_BUF_SIZE) {
			vqidx = 0;
		}
	}
	err = virtqueue_add_inbuf(vq, vkbd->sg, KBD_BUF_SIZE, (void *)KBD_BUF_SIZE, GFP_ATOMIC);
	if (err < 0) {
		VKBD_LOG(KERN_ERR, "failed to add buffer to virtqueue.\n");
		return;
	}

	virtqueue_kick(vkbd->vq);
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

#if 0
static int virtio_keyboard_open(struct inode *inode, struct file *file)
{
	VKBD_LOG(KERN_DEBUG, "opened.\n");
	return 0;
}

static int virtio_keyboard_release(struct inode *inode, struct file *file)
{
	VKBD_LOG(KERN_DEBUG, "closed\n");
	return 0;
}

struct file_operations virtio_keyboard_fops = {
	.owner   = THIS_MODULE,
	.open	= virtio_keyboard_open,
	.release = virtio_keyboard_release,
};
#endif

static int virtio_keyboard_probe(struct virtio_device *vdev)
{
	int ret = 0;
	int index = 0;

	VKBD_LOG(KERN_INFO, "driver is probed\n");
	vqidx = 0;

	vdev->priv = vkbd = kmalloc(sizeof(struct virtio_keyboard), GFP_KERNEL);
	if (!vkbd) {
		return -ENOMEM;
	}
	memset(&vkbd->kbdevt, 0x00, sizeof(vkbd->kbdevt));

	vkbd->vdev = vdev;
	mutex_init(&vkbd->event_mutex);

	vkbd->vq = virtio_find_single_vq(vkbd->vdev, vq_keyboard_handle, "virtio-keyboard-vq");
	if (IS_ERR(vkbd->vq)) {
		ret = PTR_ERR(vkbd->vq);
		kfree(vkbd);
		vdev->priv = NULL;
		return ret;
	}

	for (; index < KBD_BUF_SIZE; index++) {
		sg_set_buf(&vkbd->sg[index],
				&vkbd->kbdevt[index],
				sizeof(struct EmulKbdEvent));
	}

	ret = virtqueue_add_inbuf(vkbd->vq, vkbd->sg, KBD_BUF_SIZE, (void *)KBD_BUF_SIZE, GFP_ATOMIC);
	if (ret < 0) {
		VKBD_LOG(KERN_ERR, "failed to add buffer to virtqueue.\n");
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

	for (; index < KBD_BUF_SIZE; index++) {
		sg_set_buf(&vkbd->sg[index],
				&vkbd->kbdevt[index],
				sizeof(struct EmulKbdEvent));
	}

	virtqueue_kick(vkbd->vq);

	return 0;
}

static void virtio_keyboard_remove(struct virtio_device *vdev)
{
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
