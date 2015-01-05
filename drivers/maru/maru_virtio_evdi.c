/*
 * Maru Virtio EmulatorVritualDeviceInterface Device Driver
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 *  DaiYoung Kim <daiyoung777.kim@samsung.com>
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

#define DRIVER_NAME "EVDI"

#define LOGDEBUG(fmt, ...) \
	printk(KERN_DEBUG "%s: " fmt, DRIVER_NAME, ##__VA_ARGS__)

#define LOGERR(fmt, ...) \
	printk(KERN_ERR "%s: " fmt, DRIVER_NAME, ##__VA_ARGS__)

#define NUM_OF_EVDI	2
#define DEVICE_NAME		"evdi"

/* device protocol */
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

/* device protocol */

#define SIZEOF_MSG_INFO	sizeof(struct msg_info)

struct msg_buf {
	struct msg_info msg;
	struct list_head list;
};

#define SIZEOF_MSG_BUF	sizeof(struct msg_buf)

enum {
	EVID_READ = 0, EVID_WRITE = 1
};

struct virtevdi_info {

	wait_queue_head_t waitqueue;
	spinlock_t inbuf_lock;
	spinlock_t outvq_lock;

	struct cdev cdev;
	char name[10];

	int index;
	bool guest_connected;

} *pevdi_info[NUM_OF_EVDI];

struct virtio_evdi {
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

struct virtio_evdi *vevdi;

static struct virtio_device_id id_table[] = { { VIRTIO_ID_EVDI,
		VIRTIO_DEV_ANY_ID }, { 0 }, };

static dev_t evdi_dev_number;
static struct class* evdi_class;


static void* __xmalloc(size_t size)
{
	void* p = kmalloc(size, GFP_KERNEL);
	if (!p)
		return NULL;
	return p;
}

int _make_buf_and_kick(void)
{
	int ret;
	memset(&vevdi->read_msginfo, 0x00, sizeof(vevdi->read_msginfo));
	ret = virtqueue_add_inbuf(vevdi->rvq, vevdi->sg_read,
	    1, &vevdi->read_msginfo, GFP_ATOMIC);
	if (ret < 0) {
		LOGERR("failed to add buffer to virtqueue.(%d)\n", ret);
		return ret;
	}

	virtqueue_kick(vevdi->rvq);

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

static bool has_readdata(struct virtevdi_info *evdi)
{
	bool ret;
	unsigned long flags;

	spin_lock_irqsave(&evdi->inbuf_lock, flags);

	ret = true;

	if (list_empty(&vevdi->read_list))
		ret = false;

	spin_unlock_irqrestore(&evdi->inbuf_lock, flags);

	return ret;
}

#define HEADER_SIZE	4
#define ID_SIZE	10
#define GUEST_CONNECTION_CATEGORY	"guest"
static void send_guest_connected_msg(bool connected)
{
	int err;
	struct msg_info* _msg;
	char connect = (char)connected;
	if (vevdi == NULL) {
		LOGERR("invalid evdi handle\n");
		return;
	}

	_msg = &vevdi->send_msginfo;

	memset(_msg, 0, sizeof(vevdi->send_msginfo));

	memcpy(_msg->buf, GUEST_CONNECTION_CATEGORY, 7);
	memcpy(_msg->buf + ID_SIZE + 3, &connect, 1);
	_msg->route = route_control_server;
	_msg->use = ID_SIZE + HEADER_SIZE;
	_msg->count = 1;
	_msg->index = 0;
	_msg->cclisn = 0;

	err = virtqueue_add_outbuf(vevdi->svq, vevdi->sg_send, 1,
			_msg, GFP_ATOMIC);

	LOGERR("send guest connection message to qemu with (%d)\n", connected);

	if (err < 0) {
		LOGERR("failed to add buffer to virtqueue (err = %d)\n", err);
		return;
	}

	virtqueue_kick(vevdi->svq);
}


static int evdi_open(struct inode* inode, struct file* filp)
{
	int i, ret;
	struct virtevdi_info* evdi_info;
	struct cdev *cdev = inode->i_cdev;

	evdi_info = NULL;
	LOGDEBUG("evdi_open\n");

	for (i = 0; i < NUM_OF_EVDI; i++)
	{
		LOGDEBUG("evdi info index = %d, cdev dev = %d, inode dev = %d\n",
				i, pevdi_info[i]->cdev.dev, cdev->dev);

		if (pevdi_info[i]->cdev.dev == cdev->dev)
		{
			evdi_info = pevdi_info[i];
			break;
		}
	}

	filp->private_data = evdi_info;

	evdi_info->guest_connected = true;

	//send_guest_connected_msg(true);

	ret = _make_buf_and_kick();
	if (ret < 0)
		return ret;


	LOGDEBUG("evdi_opened\n");
	return 0;
}

static int evdi_close(struct inode* i, struct file* filp) {
	struct virtevdi_info *evdi_info;

	evdi_info = filp->private_data;
	evdi_info->guest_connected = false;

	send_guest_connected_msg(false);

	LOGDEBUG("evdi_closed\n");
	return 0;
}



static ssize_t evdi_read(struct file *filp, char __user *ubuf, size_t len,
		loff_t *f_pos)
{
	struct virtevdi_info *evdi;

	ssize_t ret;
	struct msg_buf* next;
	unsigned long flags;

	evdi = filp->private_data;

	if (!has_readdata(evdi))
	{
		if (filp->f_flags & O_NONBLOCK)
		{
			LOGERR("list is empty, return EAGAIN\n");
			return -EAGAIN;
		}
		return -EFAULT;
	}


	next = list_first_entry(&vevdi->read_list, struct msg_buf, list);
	if (next == NULL) {
		LOGERR("invliad list entry\n");
		return -EFAULT;
	}

	ret = copy_to_user(ubuf, &next->msg, len);

	list_del(&next->list);
	kfree(next);

	spin_lock_irqsave(&pevdi_info[EVID_READ]->inbuf_lock, flags);


	if (add_inbuf(vevdi->rvq, &vevdi->read_msginfo) < 0)
	{
		LOGERR("failed add_buf\n");
	}

	spin_unlock_irqrestore(&pevdi_info[EVID_READ]->inbuf_lock, flags);

	if (ret < 0)
		return -EFAULT;



	*f_pos += len;

	return len;
}

static ssize_t evdi_write(struct file *f, const char __user *ubuf, size_t len,
		loff_t* f_pos)
{
	int err = 0;
	ssize_t ret = 0;

	if (vevdi == NULL) {
		LOGERR("invalid evdi handle\n");
		return 0;
	}

	memset(&vevdi->send_msginfo, 0, sizeof(vevdi->send_msginfo));
	ret = copy_from_user(&vevdi->send_msginfo, ubuf, sizeof(vevdi->send_msginfo));

	LOGDEBUG("copy_from_user ret = %d, msg = %s", ret, vevdi->send_msginfo.buf);

	if (ret) {
		ret = -EFAULT;
		return ret;
	}


	err = virtqueue_add_outbuf(vevdi->svq, vevdi->sg_send, 1,
			&vevdi->send_msginfo, GFP_ATOMIC);

	/*
	err = virtqueue_add_buf(vevdi->svq, vevdi->sg_send, 1, 0,
				&_msg, GFP_ATOMIC);*/

	if (err < 0) {
		LOGERR("failed to add buffer to virtqueue (err = %d)\n", err);
		return 0;
	}

	virtqueue_kick(vevdi->svq);

	//LOG("send to host\n");

	return len;
}

static unsigned int evdi_poll(struct file *filp, poll_table *wait)
{
	struct virtevdi_info *evdi;
	unsigned int ret;

	evdi = filp->private_data;
	poll_wait(filp, &evdi->waitqueue, wait);

	if (!evdi->guest_connected) {
		/* evdi got unplugged */
		return POLLHUP;
	}

	ret = 0;

	if (has_readdata(evdi))
	{
		LOGDEBUG("POLLIN | POLLRDNORM\n");
		ret |= POLLIN | POLLRDNORM;
	}

	return ret;
}

static struct file_operations evdi_fops = {
		.owner = THIS_MODULE,
		.open = evdi_open,
		.release = evdi_close,
		.read = evdi_read,
		.write = evdi_write,
		.poll  = evdi_poll,
};



static void evdi_recv_done(struct virtqueue *rvq) {

	unsigned int len;
	unsigned long flags;
	struct msg_info* _msg;
	struct msg_buf* msgbuf;



	/* TODO : check if guest has been connected. */

	_msg = (struct msg_info*) virtqueue_get_buf(vevdi->rvq, &len);
	if (_msg == NULL ) {
		LOGERR("failed to virtqueue_get_buf\n");
		return;
	}

	do {
		//LOG("msg use = %d\n", _msg->use);
		//LOG("msg data = %s\n", _msg->buf);

		/* insert into queue */
		msgbuf = (struct msg_buf*) __xmalloc(SIZEOF_MSG_BUF);
		memset(msgbuf, 0x00, sizeof(*msgbuf));
		memcpy(&(msgbuf->msg), _msg, sizeof(*_msg));

		//LOG("copied msg data = %s, %s\n", msgbuf->msg.buf, _msg->buf);

		spin_lock_irqsave(&pevdi_info[EVID_READ]->inbuf_lock, flags);

		list_add_tail(&msgbuf->list, &vevdi->read_list);
		//LOG("== wake_up_interruptible = %d!\n", ++g_wake_up_interruptible_count);

		spin_unlock_irqrestore(&pevdi_info[EVID_READ]->inbuf_lock, flags);

		wake_up_interruptible(&pevdi_info[EVID_READ]->waitqueue);

		_msg = (struct msg_info*) virtqueue_get_buf(vevdi->rvq, &len);
		if (_msg == NULL) {
			break;
		}

	} while (true);


	/*
	if (add_inbuf(vevdi->rvq, &vevdi->read_msginfo) < 0)
	{
		LOG("failed add_buf\n");
	}
	*/
}

static void evdi_send_done(struct virtqueue *svq) {
	unsigned int len = 0;

	virtqueue_get_buf(svq, &len);
}

/*
 *
 */

static int init_vqs(struct virtio_evdi *evdi) {
	struct virtqueue *vqs[2];
	vq_callback_t *callbacks[] = { evdi_recv_done, evdi_send_done };
	const char *names[] = { "evdi_input", "evdi_output" };
	int err;

	err = evdi->vdev->config->find_vqs(evdi->vdev, 2, vqs, callbacks, names);
	if (err < 0)
		return err;

	evdi->rvq = vqs[0];
	evdi->svq = vqs[1];

	return 0;
}

int _init_device(void)
{
	int i, ret;

	if (alloc_chrdev_region(&evdi_dev_number, 0, NUM_OF_EVDI, DEVICE_NAME) < 0) {
		LOGERR("fail to alloc_chrdev_region\n");
		return -1;
	}

	evdi_class = class_create(THIS_MODULE, DEVICE_NAME);

	if (evdi_class == NULL ) {
		unregister_chrdev_region(evdi_dev_number, NUM_OF_EVDI);
		return -1;
	}

	for (i = 0; i < NUM_OF_EVDI; i++) {
		pevdi_info[i] = kmalloc(sizeof(struct virtevdi_info), GFP_KERNEL);

		if (!pevdi_info[i]) {
			LOGERR("Bad malloc\n");
			return -ENOMEM;
		}

		sprintf(pevdi_info[i]->name, "%s%d", DEVICE_NAME, i);

		pevdi_info[i]->index = i;
		pevdi_info[i]->guest_connected = false;

		cdev_init(&pevdi_info[i]->cdev, &evdi_fops);
		pevdi_info[i]->cdev.owner = THIS_MODULE;
		ret = cdev_add(&pevdi_info[i]->cdev, (evdi_dev_number + i), 1);

		/* init wait queue */
		init_waitqueue_head(&pevdi_info[i]->waitqueue);
		spin_lock_init(&pevdi_info[i]->inbuf_lock);
		spin_lock_init(&pevdi_info[i]->outvq_lock);

		if (ret == -1) {
			LOGERR("Bad cdev\n");
			return ret;
		}

		device_create(evdi_class, NULL, (evdi_dev_number + i), NULL, "%s%d",
				DEVICE_NAME, i);
	}

	return 0;
}


static int evdi_probe(struct virtio_device* dev) {
	int ret;

	vevdi = kmalloc(sizeof(struct virtio_evdi), GFP_KERNEL);

	INIT_LIST_HEAD(&vevdi->read_list);

	vevdi->vdev = dev;
	dev->priv = vevdi;

	ret = _init_device();
	if (ret)
	{
		LOGERR("failed to _init_device\n");
		return ret;
	}
	ret = init_vqs(vevdi);
	if (ret) {
		dev->config->del_vqs(dev);
		kfree(vevdi);
		dev->priv = NULL;

		LOGERR("failed to init_vqs\n");
		return ret;
	}

	 /* enable callback */
	virtqueue_enable_cb(vevdi->rvq);
	virtqueue_enable_cb(vevdi->svq);


	memset(&vevdi->read_msginfo, 0x00, sizeof(vevdi->read_msginfo));
	sg_set_buf(vevdi->sg_read, &vevdi->read_msginfo, sizeof(struct msg_info));

	memset(&vevdi->send_msginfo, 0x00, sizeof(vevdi->send_msginfo));
	sg_set_buf(vevdi->sg_send, &vevdi->send_msginfo, sizeof(struct msg_info));


	sg_init_one(vevdi->sg_read, &vevdi->read_msginfo, sizeof(vevdi->read_msginfo));
	sg_init_one(vevdi->sg_send, &vevdi->send_msginfo, sizeof(vevdi->send_msginfo));



	LOGDEBUG("EVDI Probe completed");
	return 0;
}

static void evdi_remove(struct virtio_device* dev)
{
	struct virtio_evdi* _evdi = dev->priv;
	if (!_evdi)
	{
		LOGERR("evdi is NULL\n");
		return;
	}

	dev->config->reset(dev);
	dev->config->del_vqs(dev);

	kfree(_evdi);

	LOGDEBUG("driver is removed.\n");
}

MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_evdi_driver = {
		.driver = {
				.name = KBUILD_MODNAME,
				.owner = THIS_MODULE ,
		},
		.id_table = id_table,
		.probe = evdi_probe,
		.remove = evdi_remove,
};

static int __init evdi_init(void)
{
	LOGDEBUG("EVDI driver initialized.\n");

	return register_virtio_driver(&virtio_evdi_driver);
}

static void __exit evdi_exit(void)
{
	int i;

	unregister_chrdev_region(evdi_dev_number, NUM_OF_EVDI);

	for (i = 0; i < NUM_OF_EVDI; i++) {
		device_destroy(evdi_class, MKDEV(MAJOR(evdi_dev_number), i));
		cdev_del(&pevdi_info[i]->cdev);
		kfree(pevdi_info[i]);
	}

	/*device_destroy(evdi_class, evdi_dev_number);*/

	class_destroy(evdi_class);

	unregister_virtio_driver(&virtio_evdi_driver);

	LOGDEBUG("EVDI driver is destroyed.\n");
}

module_init(evdi_init);
module_exit(evdi_exit);

MODULE_LICENSE("GPL2");
MODULE_AUTHOR("DaiYoung Kim <daiyoung777.kim@samsung.com>");
MODULE_DESCRIPTION("Emulator Virtio EmulatorVirtualDeviceInterface Driver");

