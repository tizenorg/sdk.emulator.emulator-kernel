/*
 * Maru Virtio Virtual Modem Device Driver
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 *  Sooyoung Ha <yoosah.ha@samsung.com>
 *  SeokYeon Hwang <syeon.hwang@samsung.com>
 *  Sangho Park <sangho1206.park@samsung.com>
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
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/cdev.h>

#define DRIVER_NAME "VMODEM"

#define LOG(fmt, ...) \
	printk(KERN_ERR "%s: " fmt, DRIVER_NAME, ##__VA_ARGS__)

#define NUM_OF_VMODEM	2
#define DEVICE_NAME	"vmodem"

MODULE_LICENSE("GPL2");
MODULE_AUTHOR("Sooyoung Ha <yoosah.ha@samsung.com>");
MODULE_DESCRIPTION("Emulator Virtio Virtual Modem Device Driver");

#define __MAX_BUF_SIZE	1024

enum
{
	route_qemu = 0,
	route_control_server = 1,
	route_monitor = 2
};

typedef unsigned int CSCliSN;

struct msg_info {
	char buf[__MAX_BUF_SIZE];

	uint32_t route;
	uint32_t use;
	uint16_t count;
	uint16_t index;

	CSCliSN cclisn;
};

#define SIZEOF_MSG_INFO	sizeof(struct msg_info)

struct msg_buf {
	struct msg_info msg;
	struct list_head list;
};

#define SIZEOF_MSG_BUF	sizeof(struct msg_buf)

enum {
	EVID_READ = 0, EVID_WRITE = 1
};

struct virtvmodem_info {

	wait_queue_head_t waitqueue;
	spinlock_t inbuf_lock;
	spinlock_t outvq_lock;

	struct cdev cdev;
	char name[10];

	int index;
	bool guest_connected;

} *pvmodem_info[NUM_OF_VMODEM];

struct virtio_vmodem {
	struct virtio_device* vdev;
	struct virtqueue* rvq;
	struct virtqueue* svq;

	struct msg_info read_msginfo;
	struct msg_info send_msginfo;

	struct list_head read_list;
	struct list_head write_list;

	struct scatterlist sg_read[2];
	struct scatterlist sg_send[2];
};
struct virtio_vmodem *vvmodem;

static struct virtio_device_id id_table[] = { { VIRTIO_ID_VMODEM,
		VIRTIO_DEV_ANY_ID }, { 0 }, };

static dev_t vmodem_dev_number;
static struct class* vmodem_class;


static void* __xmalloc(size_t size)
{
	void* p = kmalloc(size, GFP_KERNEL);
	if (!p) {
		return NULL;
	}
	return p;
}

int make_vmodem_buf_and_kick(void)
{
	int ret;
	memset(&vvmodem->read_msginfo, 0x00, sizeof(vvmodem->read_msginfo));
	ret = virtqueue_add_inbuf(vvmodem->rvq, vvmodem->sg_read, 1, &vvmodem->read_msginfo,
			GFP_ATOMIC );
	if (ret < 0) {
		LOG("failed to add buffer to virtqueue.(%d)\n", ret);
		return ret;
	}

	virtqueue_kick(vvmodem->rvq);

	return 0;
}

static int add_inbuf(struct virtqueue *vq, struct msg_info *msg)
{
	struct scatterlist sg[1];
	int ret;

	sg_init_one(sg, msg, sizeof(struct msg_info));

	ret = virtqueue_add_inbuf(vq, sg, 1, msg, GFP_ATOMIC);
	virtqueue_kick(vq);
	return ret;
}

static bool has_readdata(struct virtvmodem_info *vvinfo)
{
	bool ret;
	unsigned long flags;

	spin_lock_irqsave(&vvinfo->inbuf_lock, flags);

	ret = true;

	if (list_empty(&vvmodem->read_list)) {
		ret = false;
	}

	spin_unlock_irqrestore(&vvinfo->inbuf_lock, flags);

	return ret;
}


static int vmodem_open(struct inode* inode, struct file* filp)
{
	int i, ret;
	struct virtvmodem_info* vmodem_info;
	struct cdev *cdev = inode->i_cdev;

	vmodem_info = NULL;
	LOG("vmodem_open\n");

	for (i = 0; i < NUM_OF_VMODEM; i++) {
		LOG("vmodem info index = %d, cdev dev = %d, inode dev = %d\n",
				i, pvmodem_info[i]->cdev.dev, cdev->dev);

		if (pvmodem_info[i]->cdev.dev == cdev->dev) {
			vmodem_info = pvmodem_info[i];
			break;
		}
	}

	filp->private_data = vmodem_info;

	vmodem_info->guest_connected = true;


	ret = make_vmodem_buf_and_kick();
	if (ret < 0) {
		return ret;
	}

	LOG("vmodem is opened\n");
	return 0;
}

static int vmodem_close(struct inode* i, struct file* filp) {
	struct virtvmodem_info *vvinfo;

	vvinfo = filp->private_data;
	vvinfo->guest_connected = false;

	LOG("vmodem is closed\n");
	return 0;
}

static ssize_t vmodem_read(struct file *filp, char __user *ubuf, size_t len,
		loff_t *f_pos)
{
	struct virtvmodem_info *vvinfo;

	ssize_t ret;
	struct msg_buf* next;
	unsigned long flags;

	vvinfo = filp->private_data;

	if (!has_readdata(vvinfo)) {
		if (filp->f_flags & O_NONBLOCK) {
			LOG("list is empty, return EAGAIN\n");
			return -EAGAIN;
		}
		return -EFAULT;
	}

	next = list_first_entry(&vvmodem->read_list, struct msg_buf, list);
	if (next == NULL) {
		LOG("invliad list entry\n");
		return -EFAULT;
	}

	ret = copy_to_user(ubuf, &next->msg, len);

	list_del(&next->list);
	kfree(next);

	spin_lock_irqsave(&pvmodem_info[EVID_READ]->inbuf_lock, flags);


	if (add_inbuf(vvmodem->rvq, &vvmodem->read_msginfo) < 0) {
		LOG("failed add_buf\n");
	}

	spin_unlock_irqrestore(&pvmodem_info[EVID_READ]->inbuf_lock, flags);

	if (ret < 0) {
		return -EFAULT;
	}

	*f_pos += len;

	return len;
}

static ssize_t vmodem_write(struct file *f, const char __user *ubuf, size_t len,
		loff_t* f_pos)
{
	int err = 0;
	ssize_t ret = 0;

	if (vvmodem == NULL) {
		LOG("invalid vmodem handle\n");
		return 0;
	}

	memset(&vvmodem->send_msginfo, 0, sizeof(vvmodem->send_msginfo));
	ret = copy_from_user(&vvmodem->send_msginfo, ubuf, sizeof(vvmodem->send_msginfo));

	if (ret) {
		LOG("vmodem's copy_from_user is failed\n");
		ret = -EFAULT;
		return ret;
	}


	err = virtqueue_add_outbuf(vvmodem->svq, vvmodem->sg_send, 1,
			&vvmodem->send_msginfo, GFP_ATOMIC);

	if (err < 0) {
		LOG("failed to add buffer to virtqueue (err = %d)\n", err);
		return 0;
	}

	virtqueue_kick(vvmodem->svq);
	LOG("vmodem kick the data to ecs\n");

	return len;
}

static unsigned int vmodem_poll(struct file *filp, poll_table *wait)
{
	struct virtvmodem_info *vvinfo;
	unsigned int ret;

	vvinfo = filp->private_data;
	poll_wait(filp, &vvinfo->waitqueue, wait);

	if (!vvinfo->guest_connected) {
		return POLLHUP;
	}

	ret = 0;

	if (has_readdata(vvinfo)) {
		LOG("POLLIN | POLLRDNORM\n");
		ret |= POLLIN | POLLRDNORM;
	}

	return ret;
}

static struct file_operations vmodem_fops = {
		.owner = THIS_MODULE,
		.open = vmodem_open,
		.release = vmodem_close,
		.read = vmodem_read,
		.write = vmodem_write,
		.poll  = vmodem_poll,
};



static void vmodem_recv_done(struct virtqueue *rvq) {

	unsigned int len;
	unsigned long flags;
	struct msg_info* _msg;
	struct msg_buf* msgbuf;

	_msg = (struct msg_info*) virtqueue_get_buf(vvmodem->rvq, &len);
	if (_msg == NULL ) {
		LOG("failed to virtqueue_get_buf\n");
		return;
	}

	do {
		msgbuf = (struct msg_buf*) __xmalloc(SIZEOF_MSG_BUF);
		memset(msgbuf, 0x00, sizeof(*msgbuf));
		memcpy(&(msgbuf->msg), _msg, sizeof(*_msg));

		spin_lock_irqsave(&pvmodem_info[EVID_READ]->inbuf_lock, flags);

		list_add_tail(&msgbuf->list, &vvmodem->read_list);

		spin_unlock_irqrestore(&pvmodem_info[EVID_READ]->inbuf_lock, flags);

		wake_up_interruptible(&pvmodem_info[EVID_READ]->waitqueue);

		_msg = (struct msg_info*) virtqueue_get_buf(vvmodem->rvq, &len);
		if (_msg == NULL) {
			break;
		}

	} while (true);
}

static void vmodem_send_done(struct virtqueue *svq) {
	unsigned int len = 0;

	LOG("vmodem send done\n");
	virtqueue_get_buf(svq, &len);
}

static int init_vqs(struct virtio_vmodem *v_vmodem) {
	struct virtqueue *vqs[2];
	vq_callback_t *callbacks[] = { vmodem_recv_done, vmodem_send_done };
	const char *names[] = { "vmodem_input", "vmodem_output" };
	int err;

	err = v_vmodem->vdev->config->find_vqs(v_vmodem->vdev, 2, vqs, callbacks, names);
	if (err < 0) {
		LOG("find_vqs of vmodem device is failed\n");
		return err;
	}

	v_vmodem->rvq = vqs[0];
	v_vmodem->svq = vqs[1];

	return 0;
}

int init_vmodem_device(void)
{
	int i, ret;

	if (alloc_chrdev_region(&vmodem_dev_number, 0, NUM_OF_VMODEM, DEVICE_NAME) < 0) {
		LOG("fail to alloc_chrdev_region\n");
		return -1;
	}

	vmodem_class = class_create(THIS_MODULE, DEVICE_NAME);

	if (vmodem_class == NULL ) {
		unregister_chrdev_region(vmodem_dev_number, NUM_OF_VMODEM);
		return -1;
	}

	for (i = 0; i < NUM_OF_VMODEM; i++) {
		pvmodem_info[i] = kmalloc(sizeof(struct virtvmodem_info), GFP_KERNEL);
		if (!pvmodem_info[i]) {
			LOG("pvmodem_info malloc is failed\n");
			return -ENOMEM;
		}

		sprintf(pvmodem_info[i]->name, "%s%d", DEVICE_NAME, i);

		pvmodem_info[i]->index = i;
		pvmodem_info[i]->guest_connected = false;

		cdev_init(&pvmodem_info[i]->cdev, &vmodem_fops);
		pvmodem_info[i]->cdev.owner = THIS_MODULE;
		ret = cdev_add(&pvmodem_info[i]->cdev, (vmodem_dev_number + i), 1);

		init_waitqueue_head(&pvmodem_info[i]->waitqueue);
		spin_lock_init(&pvmodem_info[i]->inbuf_lock);
		spin_lock_init(&pvmodem_info[i]->outvq_lock);

		if (ret == -1) {
			LOG("cdev_add(%d) is failed\n", i);
			return ret;
		}

		device_create(vmodem_class, NULL, (vmodem_dev_number + i), NULL, "%s%d",
				DEVICE_NAME, i);
	}

	return 0;
}


static int vmodem_probe(struct virtio_device* dev) {
	int ret;

	vvmodem = kmalloc(sizeof(struct virtio_vmodem), GFP_KERNEL);

	INIT_LIST_HEAD(&vvmodem->read_list);

	vvmodem->vdev = dev;
	dev->priv = vvmodem;

	ret = init_vmodem_device();
	if (ret) {
		LOG("failed to init_vmodem_device\n");
		return ret;
	}
	ret = init_vqs(vvmodem);
	if (ret) {
		dev->config->del_vqs(dev);
		kfree(vvmodem);
		dev->priv = NULL;

		LOG("failed to init_vqs\n");
		return ret;
	}

	virtqueue_enable_cb(vvmodem->rvq);
	virtqueue_enable_cb(vvmodem->svq);

	memset(&vvmodem->read_msginfo, 0x00, sizeof(vvmodem->read_msginfo));
	sg_set_buf(vvmodem->sg_read, &vvmodem->read_msginfo, sizeof(struct msg_info));

	memset(&vvmodem->send_msginfo, 0x00, sizeof(vvmodem->send_msginfo));
	sg_set_buf(vvmodem->sg_send, &vvmodem->send_msginfo, sizeof(struct msg_info));

	sg_init_one(vvmodem->sg_read, &vvmodem->read_msginfo, sizeof(vvmodem->read_msginfo));
	sg_init_one(vvmodem->sg_send, &vvmodem->send_msginfo, sizeof(vvmodem->send_msginfo));

	LOG("vmodem is probed");
	return 0;
}

static void vmodem_remove(struct virtio_device* dev)
{
	struct virtio_vmodem* _vmodem = dev->priv;
	if (!_vmodem) {
		LOG("vmodem is NULL\n");
		return;
	}

	dev->config->reset(dev);
	dev->config->del_vqs(dev);

	kfree(_vmodem);

	LOG("driver is removed.\n");
}

MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_vmodem_driver = {
		.driver = {
				.name = KBUILD_MODNAME,
				.owner = THIS_MODULE ,
		},
		.id_table = id_table,
		.probe = vmodem_probe,
		.remove = vmodem_remove,
};

static int __init vmodem_init(void)
{
	LOG("VMODEM driver initialized.\n");

	return register_virtio_driver(&virtio_vmodem_driver);
}

static void __exit vmodem_exit(void)
{
	int i;

	unregister_chrdev_region(vmodem_dev_number, NUM_OF_VMODEM);

	for (i = 0; i < NUM_OF_VMODEM; i++) {
		device_destroy(vmodem_class, MKDEV(MAJOR(vmodem_dev_number), i));
		cdev_del(&pvmodem_info[i]->cdev);
		kfree(pvmodem_info[i]);
	}

	class_destroy(vmodem_class);

	unregister_virtio_driver(&virtio_vmodem_driver);

	LOG("VMODEM driver is destroyed.\n");
}

module_init(vmodem_init);
module_exit(vmodem_exit);

