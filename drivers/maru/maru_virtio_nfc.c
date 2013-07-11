/*
 * Maru Virtio NFC Device Driver
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 *  DaiYoung Kim <munkyu.im@samsung.com>
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
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/sysfs.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <asm/uaccess.h>
#include <linux/file.h>

#define DRIVER_NAME "NFC"

#define LOG(fmt, ...) \
	printk(KERN_ERR "%s: " fmt, DRIVER_NAME, ##__VA_ARGS__)

#define CLASS_NAME		"network"
#define DEVICE_NAME		"nfc"
#define NFC_DATA_FILE "/sys/devices/virtual/network/nfc/data"

/* device protocol */
#define __MAX_BUF_SIZE	4096

enum
{
	route_qemu = 0,
	route_control_server = 1,
	route_monitor = 2
};

enum request_cmd {
	request_get = 0,
	request_set,
	request_answer
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

static struct device* device;

static char data[PAGE_SIZE] = {0,};

static ssize_t show_data(struct device *dev, struct device_attribute *attr, char *buf)
{
	printk("NFC-[%s] \n", __FUNCTION__);
	return snprintf(buf, PAGE_SIZE, "%s", data);
}

static ssize_t store_data(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	printk("NFC-[%s] \n", __FUNCTION__);
    strncpy(data, buf, count);
	return strnlen(buf, PAGE_SIZE);
}

static struct device_attribute da = __ATTR(data, 0644, show_data, store_data);

/* device protocol */

#define SIZEOF_MSG_INFO	sizeof(struct msg_info)

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

    void *private;
};

struct virtio_nfc *vnfc;

static struct virtio_device_id id_table[] = { { VIRTIO_ID_NFC,
		VIRTIO_DEV_ANY_ID }, { 0 }, };

static struct class* nfc_class;


int make_buf_and_kick(void)
{
	int ret;
	memset(&vnfc->read_msginfo, 0x00, sizeof(vnfc->read_msginfo));
	ret = virtqueue_add_buf(vnfc->rvq, vnfc->sg_read, 0, 1, &vnfc->read_msginfo,
			GFP_ATOMIC );
	if (ret < 0) {
		LOG("failed to add buffer to virtqueue.(%d)\n", ret);
		return ret;
	}

	virtqueue_kick(vnfc->rvq);

	return 0;
}

static void write_file(char *filename, char *data, int len)
{
    struct file *file;
    loff_t pos = 0;
    int fd;

    mm_segment_t old_fs = get_fs();
    set_fs(KERNEL_DS);

    fd = sys_open(filename, O_WRONLY|O_CREAT, 0644);
    if (fd >= 0) {
        sys_write(fd, data, strlen(data));
        file = fget(fd);
        if (file) {
            vfs_write(file, data, len, &pos);
            fput(file);
            file_update_time(file);
        }
        sys_close(fd);
    }
    set_fs(old_fs);
}

static void nfc_recv_done(struct virtqueue *rvq) {

	unsigned int len;
	struct msg_info* msg;
    int err;
	msg = (struct msg_info*) virtqueue_get_buf(vnfc->rvq, &len);
	if (msg == NULL ) {
		LOG("failed to virtqueue_get_buf\n");
		return;
	}

    if(msg->route == request_set) {
        memset(data, 0x00, sizeof(data));
        write_file(NFC_DATA_FILE, msg->buf, len);
    }else if(msg->route == request_get) {
    	memset(&vnfc->send_msginfo, 0, sizeof(vnfc->send_msginfo));
    	vnfc->send_msginfo.route = request_answer;
		strcpy(vnfc->send_msginfo.buf, data);
        err = virtqueue_add_buf(vnfc->svq, vnfc->sg_send, 1, 0,	&vnfc->send_msginfo, GFP_ATOMIC);
    	if (err < 0) {
	    	LOG("failed to add buffer to virtqueue (err = %d)", err);
		    return;
    	}

	    virtqueue_kick(vnfc->svq);

    }
    
	LOG("msg buf: %sroute: %d, len: %d", msg->buf, msg->route, msg->use);

    make_buf_and_kick();
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

static int init_device(void)
{
	int ret;

	nfc_class = class_create(THIS_MODULE, CLASS_NAME);

	device = device_create(nfc_class, NULL, (dev_t)NULL, NULL, DEVICE_NAME);
    if (device < 0) {
			LOG("NFC device creation is failed.");
			return -1;
	}

	ret = device_create_file(device, &da);
	if (ret) {
		device_destroy(nfc_class, (dev_t)NULL);
	    class_destroy(nfc_class);
        return -1;
    }

	return 0;
}


static int nfc_probe(struct virtio_device* dev) {
	int ret;

	vnfc = kmalloc(sizeof(struct virtio_nfc), GFP_KERNEL);

	INIT_LIST_HEAD(&vnfc->read_list);

	vnfc->vdev = dev;
	dev->priv = vnfc;

	ret = init_device();
	if (ret)
	{
		LOG("failed to _init_device\n");
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
	sg_set_buf(vnfc->sg_read, &vnfc->read_msginfo, sizeof(struct msg_info));

	memset(&vnfc->send_msginfo, 0x00, sizeof(vnfc->send_msginfo));
	sg_set_buf(vnfc->sg_send, &vnfc->send_msginfo, sizeof(struct msg_info));


	sg_init_one(vnfc->sg_read, &vnfc->read_msginfo, sizeof(vnfc->read_msginfo));
	sg_init_one(vnfc->sg_send, &vnfc->send_msginfo, sizeof(vnfc->send_msginfo));

    ret = make_buf_and_kick();
	if (ret) {
		dev->config->del_vqs(dev);
		kfree(vnfc);
		dev->priv = NULL;
		LOG("failed to send buf");
		return ret;
	}

	LOG("NFC Probe completed");
	return 0;
}

static void __devexit nfc_remove(struct virtio_device* dev)
{
	struct virtio_nfc* _nfc = dev->priv;
	if (!_nfc)
	{
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
    device_destroy(nfc_class, (dev_t)NULL);

    /*device_destroy(nfc_class, nfc_dev_number);*/

    class_destroy(nfc_class);

    unregister_virtio_driver(&virtio_nfc_driver);

    LOG("NFC driver is destroyed.\n");
}

module_init(nfc_init);
module_exit(nfc_exit);

MODULE_LICENSE("GPL2");
MODULE_AUTHOR("DaiYoung Kim <daiyoung777.kim@samsung.com>");
MODULE_DESCRIPTION("Emulator Virtio EmulatorVirtualDeviceInterface Driver");

