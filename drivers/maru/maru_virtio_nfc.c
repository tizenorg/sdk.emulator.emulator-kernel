/*
 * Maru Virtio NFC Device Driver
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 *  Munkyu Im <munkyu.im@samsung.com>
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

#define DRIVER_NAME "NFC"

#define LOG(fmt, ...) \
	printk(KERN_ERR "%s: " fmt, DRIVER_NAME, ##__VA_ARGS__)

#define NUM_OF_NFC  2
#define DEVICE_NAME     "nfc"

/* device protocol */
#define NFC_MAX_BUF_SIZE  4096

struct msg_info {
	unsigned char client_id;
	unsigned char client_type;
	uint32_t use;
	char buf[NFC_MAX_BUF_SIZE];
};

static int g_read_count = 0;

/* device protocol */

struct msg_buf {
	struct msg_info msg;
	struct list_head list;
};

#define SIZEOF_MSG_BUF  sizeof(struct msg_buf)

enum {
	NFC_READ = 0, NFC_WRITE = 1
};

struct virtnfc_info {

	wait_queue_head_t waitqueue;
	spinlock_t inbuf_lock;
	spinlock_t outvq_lock;

	struct cdev cdev;
	char name[10];

	int index;
	bool guest_connected;

} *pnfc_info[NUM_OF_NFC];

struct virtio_nfc {
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

struct virtio_nfc *vnfc;

static struct virtio_device_id id_table[] = { { VIRTIO_ID_NFC,
	VIRTIO_DEV_ANY_ID }, { 0 }, };

static dev_t nfc_dev_number;
static struct class* nfc_class;


static void* __xmalloc(size_t size)
{
	void* p = kmalloc(size, GFP_KERNEL);
	if (!p)
		return NULL;
	return p;
}

int make_buf_and_kick(void)
{
	int ret;
	memset(&vnfc->read_msginfo, 0x00, sizeof(vnfc->read_msginfo));
	ret = virtqueue_add_inbuf(vnfc->rvq, vnfc->sg_read,
			1, &vnfc->read_msginfo, GFP_ATOMIC);
	if (ret < 0) {
		LOG("failed to add buffer to virtqueue.(%d)\n", ret);
		return ret;
	}

	virtqueue_kick(vnfc->rvq);

	return 0;
}

static int add_inbuf(struct virtqueue *vq, struct msg_info *msg)
{
	struct scatterlist sg[1];
	int ret;

	sg_init_one(sg, msg, NFC_MAX_BUF_SIZE);

	ret = virtqueue_add_inbuf(vq, sg, 1, msg, GFP_ATOMIC);
	virtqueue_kick(vq);
	return ret;
}

static bool has_readdata(struct virtnfc_info *nfc)
{
	bool ret;
	unsigned long flags;

	spin_lock_irqsave(&nfc->inbuf_lock, flags);

	ret = true;

	if (list_empty(&vnfc->read_list))
		ret = false;

	spin_unlock_irqrestore(&nfc->inbuf_lock, flags);

	return ret;
}


static int nfc_open(struct inode* inode, struct file* filp)
{
	int i, ret;
	struct virtnfc_info* nfc_info;
	struct cdev *cdev = inode->i_cdev;

	nfc_info = NULL;
	LOG("nfc_open\n");

	for (i = 0; i < NUM_OF_NFC; i++) {
		LOG("nfc info index = %d, cdev dev = %d, inode dev = %d\n",
				i, pnfc_info[i]->cdev.dev, cdev->dev);

		if (pnfc_info[i]->cdev.dev == cdev->dev) {
			nfc_info = pnfc_info[i];
			break;
		}
	}

	filp->private_data = nfc_info;

	nfc_info->guest_connected = true;


	ret = make_buf_and_kick();
	if (ret < 0)
		return ret;

	LOG("nfc_opened\n");
	return 0;
}

static int nfc_close(struct inode* i, struct file* filp) {
	struct virtnfc_info *nfc_info;

	nfc_info = filp->private_data;
	nfc_info->guest_connected = false;

	LOG("nfc_closed\n");
	return 0;
}



static ssize_t nfc_read(struct file *filp, char __user *ubuf, size_t len,
		loff_t *f_pos)
{
	struct virtnfc_info *nfc;

	ssize_t ret;
	struct msg_buf* next;
	unsigned long flags;

	LOG("nfc_read\n");
	nfc = filp->private_data;
	if (!has_readdata(nfc)) {
		if (filp->f_flags & O_NONBLOCK) {
			LOG("list is empty, return EAGAIN\n");
			return -EAGAIN;
		}
		return -EFAULT;
	}

	next = list_first_entry(&vnfc->read_list, struct msg_buf, list);
	if (next == NULL) {
		LOG("invliad list entry\n");
		return -EFAULT;
	}

	ret = copy_to_user(ubuf, &next->msg, len);

	list_del(&next->list);
	kfree(next);

	spin_lock_irqsave(&pnfc_info[NFC_READ]->inbuf_lock, flags);


	if (add_inbuf(vnfc->rvq, &vnfc->read_msginfo) < 0){
		LOG("failed add_buf\n");
	}

	spin_unlock_irqrestore(&pnfc_info[NFC_READ]->inbuf_lock, flags);


	LOG("nfc_read count = %d!\n", ++g_read_count);

	if (ret < 0)
		return -EFAULT;

	*f_pos += len;

	return len;
}

static ssize_t nfc_write(struct file *f, const char __user *ubuf, size_t len,
		loff_t* f_pos)
{
	int err = 0;
	ssize_t ret = 0;

	LOG("start of nfc_write len= %d, msglen = %d\n", len, sizeof(vnfc->send_msginfo));

	if (vnfc == NULL) {
		LOG("invalid nfc handle\n");
		return 0;
	}

	memset(&vnfc->send_msginfo, 0, sizeof(vnfc->send_msginfo));
	ret = copy_from_user(&vnfc->send_msginfo, ubuf, sizeof(vnfc->send_msginfo));

	LOG("copy_from_user ret = %d, id = %02x, type = %02x, msg = %s use = %d\n",
			ret, vnfc->send_msginfo.client_id, vnfc->send_msginfo.client_type,
			vnfc->send_msginfo.buf, vnfc->send_msginfo.use);

	if (ret) {
		ret = -EFAULT;
		return ret;
	}

	sg_init_one(vnfc->sg_send, &vnfc->send_msginfo, sizeof(vnfc->send_msginfo));

	err = virtqueue_add_outbuf(vnfc->svq, vnfc->sg_send, 1,
			&vnfc->send_msginfo, GFP_ATOMIC);

	/*
	   err = virtqueue_add_buf(vnfc->svq, vnfc->sg_send, 1, 0,
	   &_msg, GFP_ATOMIC);*/

	if (err < 0) {
		LOG("failed to add buffer to virtqueue (err = %d)\n", err);
		return 0;
	}

	virtqueue_kick(vnfc->svq);

	LOG("send to host\n");

	return len;
}

static unsigned int nfc_poll(struct file *filp, poll_table *wait)
{
	struct virtnfc_info *nfc;
	unsigned int ret;

	nfc = filp->private_data;
	poll_wait(filp, &nfc->waitqueue, wait);

	if (!nfc->guest_connected) {
		/* nfc got unplugged */
		return POLLHUP;
	}

	ret = 0;

	if (has_readdata(nfc)) {
		LOG("POLLIN | POLLRDNORM\n");
		ret |= POLLIN | POLLRDNORM;
	}

	return ret;
}

static struct file_operations nfc_fops = {
	.owner = THIS_MODULE,
	.open = nfc_open,
	.release = nfc_close,
	.read = nfc_read,
	.write = nfc_write,
	.poll  = nfc_poll,
};



static void nfc_recv_done(struct virtqueue *rvq) {

	unsigned int len;
	unsigned long flags;
	unsigned char *msg;
	struct msg_buf* msgbuf;
	LOG("nfc_recv_done\n");
	/* TODO : check if guest has been connected. */

	msg = (unsigned char*) virtqueue_get_buf(vnfc->rvq, &len);
	if (msg == NULL ) {
		LOG("failed to virtqueue_get_buf\n");
		return;
	}

	INIT_LIST_HEAD(&vnfc->read_list);
	do {

		/* insert into queue */
		msgbuf = (struct msg_buf*) __xmalloc(SIZEOF_MSG_BUF);
		memset(msgbuf, 0x00, sizeof(*msgbuf));
		memcpy(&(msgbuf->msg), msg, len);

		//LOG("copied msg data = %s, %s\n", msgbuf->msg.buf, msg->buf);

		spin_lock_irqsave(&pnfc_info[NFC_READ]->inbuf_lock, flags);

		list_add_tail(&msgbuf->list, &vnfc->read_list);
		//LOG("== wake_up_interruptible = %d!\n", ++g_wake_up_interruptible_count);

		spin_unlock_irqrestore(&pnfc_info[NFC_READ]->inbuf_lock, flags);

		wake_up_interruptible(&pnfc_info[NFC_READ]->waitqueue);

		msg = (unsigned char*) virtqueue_get_buf(vnfc->rvq, &len);
		if (msg == NULL) {
			break;
		}

	} while (true);
	/*
	   if (add_inbuf(vnfc->rvq, &vnfc->readmsginfo) < 0)
	   {
	   LOG("failed add_buf\n");
	   }
	 */
}

static void nfc_send_done(struct virtqueue *svq) {
	unsigned int len = 0;

	virtqueue_get_buf(svq, &len);
}

/*
 *
 */

static int init_vqs(struct virtio_nfc *nfc) {
	struct virtqueue *vqs[2];
	vq_callback_t *callbacks[] = { nfc_recv_done, nfc_send_done };
	const char *names[] = { "nfc_input", "nfc_output" };
	int err;

	err = nfc->vdev->config->find_vqs(nfc->vdev, 2, vqs, callbacks, names);
	if (err < 0)
		return err;

	nfc->rvq = vqs[0];
	nfc->svq = vqs[1];

	return 0;
}

int init_device(void)
{
	int i, ret;

	if (alloc_chrdev_region(&nfc_dev_number, 0, NUM_OF_NFC, DEVICE_NAME) < 0) {
		LOG("fail to alloc_chrdev_region\n");
		return -1;
	}

	nfc_class = class_create(THIS_MODULE, DEVICE_NAME);

	if (nfc_class == NULL ) {
		unregister_chrdev_region(nfc_dev_number, NUM_OF_NFC);
		return -1;
	}

	for (i = 0; i < NUM_OF_NFC; i++) {
		pnfc_info[i] = kmalloc(sizeof(struct virtnfc_info), GFP_KERNEL);

		if (!pnfc_info[i]) {
			LOG("Bad malloc\n");
			return -ENOMEM;
		}

		sprintf(pnfc_info[i]->name, "%s%d", DEVICE_NAME, i);

		pnfc_info[i]->index = i;
		pnfc_info[i]->guest_connected = false;

		cdev_init(&pnfc_info[i]->cdev, &nfc_fops);
		pnfc_info[i]->cdev.owner = THIS_MODULE;
		ret = cdev_add(&pnfc_info[i]->cdev, (nfc_dev_number + i), 1);

		/* init wait queue */
		init_waitqueue_head(&pnfc_info[i]->waitqueue);
		spin_lock_init(&pnfc_info[i]->inbuf_lock);
		spin_lock_init(&pnfc_info[i]->outvq_lock);

		if (ret == -1) {
			LOG("Bad cdev\n");
			return ret;
		}

		device_create(nfc_class, NULL, (nfc_dev_number + i), NULL, "%s%d",
				DEVICE_NAME, i);
	}

	return 0;
}


static int nfc_probe(struct virtio_device* dev) {
	int ret;
	LOG("nfc_probe\n");
	vnfc = kmalloc(sizeof(struct virtio_nfc), GFP_KERNEL);

	INIT_LIST_HEAD(&vnfc->read_list);

	vnfc->vdev = dev;
	dev->priv = vnfc;

	ret = init_device();
	if (ret) {
		LOG("failed to init_device\n");
		return ret;
	}
	ret = init_vqs(vnfc);
	if (ret) {
		dev->config->del_vqs(dev);
		kfree(vnfc);
		dev->priv = NULL;

		LOG("failed to init_vqs\n");
		return ret;
	}

	/* enable callback */
	virtqueue_enable_cb(vnfc->rvq);
	virtqueue_enable_cb(vnfc->svq);


	memset(&vnfc->read_msginfo, 0x00, sizeof(vnfc->read_msginfo));
	sg_set_buf(vnfc->sg_read, &vnfc->read_msginfo, NFC_MAX_BUF_SIZE);

	memset(&vnfc->send_msginfo, 0x00, sizeof(vnfc->send_msginfo));
	sg_set_buf(vnfc->sg_send, &vnfc->send_msginfo, NFC_MAX_BUF_SIZE);


	sg_init_one(vnfc->sg_read, &vnfc->read_msginfo, sizeof(vnfc->read_msginfo));
	sg_init_one(vnfc->sg_send, &vnfc->send_msginfo, sizeof(vnfc->send_msginfo));



	LOG("NFC Probe completed");
	return 0;
}

static void nfc_remove(struct virtio_device* dev)
{
	struct virtio_nfc* _nfc = dev->priv;
	if (!_nfc) {
		LOG("nfc is NULL\n");
		return;
	}

	dev->config->reset(dev);
	dev->config->del_vqs(dev);

	kfree(_nfc);

	LOG("driver is removed.\n");
}

MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_nfc_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE ,
	},
	.id_table = id_table,
	.probe = nfc_probe,
	.remove = nfc_remove,
};

static int __init nfc_init(void)
{
	LOG("NFC driver initialized.\n");

	return register_virtio_driver(&virtio_nfc_driver);
}

static void __exit nfc_exit(void)
{
	int i;

	unregister_chrdev_region(nfc_dev_number, NUM_OF_NFC);

	for (i = 0; i < NUM_OF_NFC; i++) {
		device_destroy(nfc_class, MKDEV(MAJOR(nfc_dev_number), i));
		cdev_del(&pnfc_info[i]->cdev);
		kfree(pnfc_info[i]);
	}

	/*device_destroy(nfc_class, nfc_dev_number);*/

	class_destroy(nfc_class);

	unregister_virtio_driver(&virtio_nfc_driver);

	LOG("NFC driver is destroyed.\n");
}

module_init(nfc_init);
module_exit(nfc_exit);

MODULE_LICENSE("GPL2");
MODULE_AUTHOR("Munkyu Im <munkyu.im@samsung.com>");
MODULE_DESCRIPTION("Emulator Virtio NFC Driver");

