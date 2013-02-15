/*
 * Maru Virtio EmulatorStatusMedium Device Driver
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
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
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/cdev.h>

MODULE_LICENSE("GPL2");
MODULE_AUTHOR("SeokYeon Hwang <syeon.hwang@samsung.com>");
MODULE_DESCRIPTION("Emulator Virtio EmulatorStatusMedium Driver");

#define DRIVER_NAME "VirtIO EmulatorStatusMedium "
#define VESM_LOG(log_level, fmt, ...) \
	printk(log_level "%s: " fmt, DRIVER_NAME, ##__VA_ARGS__)

struct progress_info
{
	uint16_t percentage;
};

struct virtio_esm
{
	struct virtio_device *vdev;
	struct virtqueue *vq;

	struct progress_info progress;
	struct scatterlist sg[1];
};

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_ESM, VIRTIO_DEV_ANY_ID },
	{ 0 },
};


struct virtio_esm *vesm;

static dev_t first;
static struct class *cl;
static struct cdev c_dev;

static void vq_esm_callback(struct virtqueue *vq)
{
	int len = 0;

	VESM_LOG(KERN_DEBUG, "virtqueue callback.\n");
	// get buffer and drop it.
	virtqueue_get_buf(vq, &len);
}

static int esm_open(struct inode *i, struct file *f)
{
	VESM_LOG(KERN_DEBUG, "esm_open\n");
	return 0;
}

static int esm_close(struct inode *i, struct file *f)
{
	VESM_LOG(KERN_DEBUG, "esm_close\n");
	return 0;
}

static ssize_t esm_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	VESM_LOG(KERN_DEBUG, "esm_read\n");
	return 0;
}

static ssize_t esm_write(struct inode *i, const char __user *ubuf, size_t len, loff_t *off)
{
	int err = 0;
	ssize_t ret = 0;
	char buf[4];

	if(vesm == NULL) {
		VESM_LOG(KERN_ERR, "Device or driver is malfunction.\n");
		return 0;
	}

	len = min((size_t)4, len);

	ret = copy_from_user(buf, ubuf, len);
	if (ret) {
		ret = -EFAULT;
		return ret;
	}
	buf[len - 1] = '\0';

	kstrtou16(buf, 10, &vesm->progress.percentage);

	VESM_LOG(KERN_DEBUG, "boot up progress is [%u] percent done.\n", vesm->progress.percentage);

	sg_init_one(vesm->sg, &vesm->progress, sizeof(vesm->progress));
	err = virtqueue_add_buf (vesm->vq, vesm->sg, 1, 0, &vesm->progress, GFP_ATOMIC);
	if (err < 0) {
		VESM_LOG(KERN_ERR, "%d\n", err);
		VESM_LOG(KERN_ERR, "failed to add buffer to virtqueue.\n");
		return 0;
	}

	virtqueue_kick(vesm->vq);

	VESM_LOG(KERN_DEBUG, "esm_write\n");
	return len;
}

static struct file_operations esm_fops =
{
	.owner = THIS_MODULE,
	.open = esm_open,
	.release = esm_close,
	.read = esm_read,
	.write = esm_write,
};

static int virtio_esm_probe(struct virtio_device *vdev)
{
	int ret = 0;

	VESM_LOG(KERN_INFO, "driver is probed\n");

	if(device_create(cl, NULL, first, NULL, "esm") == NULL)
	{
		class_destroy(cl);
		unregister_chrdev_region(first, 1);
		return -1;
	}

	cdev_init(&c_dev, &esm_fops);
	if(cdev_add(&c_dev, first, 1) == -1)
	{
		device_destroy(cl, first);
		class_destroy(cl);
		unregister_chrdev_region(first, 1);
		return -1;
	}

	vdev->priv = vesm = kmalloc(sizeof(struct virtio_esm), GFP_KERNEL);
	if (!vesm) {
		return -ENOMEM;
	}

	vesm->vdev = vdev;
	vesm->vq = virtio_find_single_vq(vesm->vdev, vq_esm_callback, "virtio-esm-vq");
	if (IS_ERR(vesm->vq)) {
		ret = PTR_ERR(vesm->vq);
		kfree(vesm);
		vdev->priv = NULL;

		return ret;
	}

	memset(&vesm->progress, 0x00, sizeof(vesm->progress));

	sg_set_buf(vesm->sg, &vesm->progress, sizeof(struct progress_info));

	return 0;
}

static void __devexit virtio_esm_remove(struct virtio_device *vdev)
{
	struct virtio_esm *_vesm = vdev->priv;

	VESM_LOG(KERN_INFO, "driver is removed.\n");
	if (!_vesm) {
		VESM_LOG(KERN_ERR, "vesm is NULL.\n");
		return;
	}

	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);

	kfree(_vesm);
	_vesm = NULL;
}

MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_esm_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
	},
	.id_table = id_table,
	.probe = virtio_esm_probe,
	.remove = virtio_esm_remove,
#if 0
#ifdef CONFIG_PM
	.freeze = virtio_esm_freeze,
	.restore = virtio_esm_restore,
#endif
#endif
};

static int __init virtio_esm_init(void)
{
	if(alloc_chrdev_region(&first, 0, 1, "virtio-esm") < 0)
	{
		return -1;
	}

	if((cl = class_create(THIS_MODULE, "esm_chardrv")) == NULL)
	{
		unregister_chrdev_region(first, 1);
		return -1;
	}

	VESM_LOG(KERN_INFO, "driver is initialized.\n");
	return register_virtio_driver(&virtio_esm_driver);
}

static void __exit virtio_esm_exit(void)
{
	cdev_del(&c_dev);
	device_destroy(cl, first);
	class_destroy(cl);
	unregister_chrdev_region(first, 1);
	unregister_virtio_driver(&virtio_esm_driver);

	VESM_LOG(KERN_INFO, "driver is destroyed.\n");
}

module_init(virtio_esm_init);
module_exit(virtio_esm_exit);
