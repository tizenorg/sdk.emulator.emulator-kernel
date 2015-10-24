/*
 * Maru Virtio Hwkey Device Driver
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 *  Sungmin Ha <sungmin82.ha@samsung.com>
 *  Sangjin Kim <sangjin3.kim@samsung.com>
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
#include <linux/kthread.h>

MODULE_LICENSE("GPL2");
MODULE_AUTHOR("Sungmin Ha <sungmin82.ha@samsung.com>");
MODULE_DESCRIPTION("Emulator Virtio Hwkey driver");

#define DEVICE_NAME "virtio-hwkey"
#define MAX_BUF_COUNT 64
static int vqidx = 0;

/* This structure must match the qemu definitions */
typedef struct EmulHwkeyEvent {
	uint8_t event_type;
	uint32_t keycode;
} EmulHwkeyEvent;

typedef struct virtio_hwkey
{
	struct virtio_device *vdev;
	struct virtqueue *vq;
	struct input_dev *idev;

	struct scatterlist sg[MAX_BUF_COUNT];
	struct EmulHwkeyEvent vbuf[MAX_BUF_COUNT];

	struct mutex event_mutex;
} virtio_hwkey;

virtio_hwkey *vh;

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_HWKEY, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

/* keep it consistent with emulator-skin definition */
enum {
	KEY_PRESSED = 1,
	KEY_RELEASED = 2,
};

static int err = 0;
static unsigned int index = 0;

/**
 * @brief : callback for virtqueue
 */
static void vq_hwkey_callback(struct virtqueue *vq)
{
	struct EmulHwkeyEvent hwkey_event;
	unsigned int len = 0;
	void *token = NULL;
#if 0
	printk(KERN_INFO "vq hwkey callback\n");
#endif
	while (1) {
		memcpy(&hwkey_event, &vh->vbuf[vqidx], sizeof(hwkey_event));
		if (hwkey_event.event_type == 0) {
			break;
		}
		printk(KERN_INFO "keycode: %d, event_type: %d, vqidx: %d\n", hwkey_event.keycode, hwkey_event.event_type, vqidx);
		if (hwkey_event.event_type == KEY_PRESSED) {
			input_event(vh->idev, EV_KEY, hwkey_event.keycode, true);
		}
		else if (hwkey_event.event_type == KEY_RELEASED) {
			input_event(vh->idev, EV_KEY, hwkey_event.keycode, false);
		}
		else {
			printk(KERN_ERR "Unknown event type\n");
		}

		input_sync(vh->idev);
		memset(&vh->vbuf[vqidx], 0x00, sizeof(hwkey_event));
		token = virtqueue_get_buf(vh->vq, &len);
		if (len > 0) {
			err = virtqueue_add_inbuf(vh->vq, vh->sg, MAX_BUF_COUNT, token, GFP_ATOMIC);
		}

		vqidx++;
		if (vqidx == MAX_BUF_COUNT) {
			vqidx = 0;
		}
	}

	virtqueue_kick(vh->vq);
}

static int virtio_hwkey_open(struct inode *inode, struct file *file)
{
	printk(KERN_INFO "virtio hwkey device is opened\n");
	return 0;
}

static int virtio_hwkey_release(struct inode *inode, struct file *file)
{
	printk(KERN_INFO "virtio hwkey device is closed\n");
	return 0;
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

struct file_operations virtio_hwkey_fops = {
	.owner   = THIS_MODULE,
	.open    = virtio_hwkey_open,
	.release = virtio_hwkey_release,
};

static int virtio_hwkey_probe(struct virtio_device *vdev)
{
	int ret = 0;
	vqidx = 0;

	printk(KERN_INFO "virtio hwkey driver is probed\n");

	/* init virtio */
	vdev->priv = vh = kmalloc(sizeof(*vh), GFP_KERNEL);
	if (!vh) {
		return -ENOMEM;
	}
	memset(&vh->vbuf, 0x00, sizeof(vh->vbuf));

	vh->vdev = vdev;

	vh->vq = virtio_find_single_vq(vh->vdev, vq_hwkey_callback, "virtio-hwkey-vq");
	if (IS_ERR(vh->vq)) {
		ret = PTR_ERR(vh->vq);

		kfree(vh);
		vdev->priv = NULL;
		return ret;
	}

	/* enable callback */
	virtqueue_enable_cb(vh->vq);

	sg_init_table(vh->sg, MAX_BUF_COUNT);

	/* prepare the buffers */
	for (index = 0; index < MAX_BUF_COUNT; index++) {
		sg_set_buf(&vh->sg[index], &vh->vbuf[index], sizeof(EmulHwkeyEvent));

		if (err < 0) {
			printk(KERN_ERR "failed to add buffer\n");

			kfree(vh);
			vdev->priv = NULL;
			return ret;
		}
	}

	err = virtqueue_add_inbuf(vh->vq, vh->sg,
			MAX_BUF_COUNT, (void *)MAX_BUF_COUNT, GFP_ATOMIC);

	/* register for input device */
	vh->idev = input_allocate_device();
	if (!vh->idev) {
		printk(KERN_ERR "failed to allocate a input hwkey device\n");
		ret = -1;

		kfree(vh);
		vdev->priv = NULL;
		return ret;
	}

	vh->idev->name = "Maru Virtio Hwkey";
	vh->idev->dev.parent = &(vdev->dev);

	input_set_drvdata(vh->idev, vh);
	vh->idev->open = input_hwkey_open;
	vh->idev->close = input_hwkey_close;

	vh->idev->evbit[0] = BIT_MASK(EV_KEY);
	/* to support any keycode */
	memset(vh->idev->keybit, 0xffffffff, sizeof(unsigned long) * BITS_TO_LONGS(KEY_CNT));

	ret = input_register_device(vh->idev);
	if (ret) {
		printk(KERN_ERR "input hwkey driver cannot registered\n");
		ret = -1;

		input_free_device(vh->idev);
		kfree(vh);
		vdev->priv = NULL;
		return ret;
	}

	virtqueue_kick(vh->vq);
	index = 0;

	return 0;
}

static void virtio_hwkey_remove(struct virtio_device *vdev)
{
	virtio_hwkey *vhk = NULL;

	printk(KERN_INFO "virtio hwkey driver is removed\n");

	vhk = vdev->priv;

	vdev->config->reset(vdev); /* reset device */
	vdev->config->del_vqs(vdev); /* clean up the queues */

	input_unregister_device(vhk->idev);

	kfree(vhk);
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

