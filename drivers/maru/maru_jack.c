/*
 * Virtual device node
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 * JinHyung Choi <jinhyung2.choi@samsung.com>
 * SooYoung Ha <yoosah.ha@samsung.com>
 * Sungmin Ha <sungmin82.ha@samsung.com>
 * YeongKyoon Lee <yeongkyoon.lee@samsung.com
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
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/wait.h>
#include <linux/sched.h>

#define __MAX_BUF_SIZE		1024

struct msg_info {
	char buf[__MAX_BUF_SIZE];

	uint16_t type;
	uint16_t req;
};

struct virtio_jack {
	struct virtio_device* vdev;
	struct virtqueue* vq;

	struct msg_info msginfo;

	struct scatterlist sg_vq[2];

	int flags;
	struct mutex lock;
};

enum jack_types {
	jack_type_charger = 0,
	jack_type_earjack,
	jack_type_earkey,
	jack_type_hdmi,
	jack_type_usb,
	jack_type_max
};

enum request_cmd {
	request_get = 0,
	request_set,
	request_answer
};

struct jack_data {
	int no;
	char buffer[50];
};

struct virtio_jack *v_jack;

static char jack_data [PAGE_SIZE];

static DECLARE_WAIT_QUEUE_HEAD(wq);

static struct virtio_device_id id_table[] = { { VIRTIO_ID_JACK,
		VIRTIO_DEV_ANY_ID }, { 0 }, };

#define DEVICE_NAME	"jack"
#define JACK_DEBUG

#ifdef JACK_DEBUG
#define DLOG(level, fmt, ...) \
	printk(level "maru_%s: " fmt, DEVICE_NAME, ##__VA_ARGS__)
#else
// do nothing
#define DLOG(level, fmt, ...)
#endif

static void jack_vq_done(struct virtqueue *vq) {
	unsigned int len;
	struct msg_info* msg;

	msg = (struct msg_info*) virtqueue_get_buf(v_jack->vq, &len);
	if (msg == NULL) {
		DLOG(KERN_ERR, "failed to virtqueue_get_buf");
		return;
	}

	if (msg->req != request_answer || msg->buf == NULL) {
		return;
	}

	DLOG(KERN_DEBUG, "msg buf: %s, req: %d, type: %d", msg->buf, msg->req, msg->type);

	mutex_lock(&v_jack->lock);
	strcpy(jack_data, msg->buf);
	v_jack->flags ++;
	DLOG(KERN_DEBUG, "flags : %d", v_jack->flags);
	mutex_unlock(&v_jack->lock);

	wake_up_interruptible(&wq);
}

static void set_jack_data(int type, const char* buf)
{
	int err = 0;

	if (buf == NULL) {
		DLOG(KERN_ERR, "set_jack buf is NULL.");
		return;
	}

	if (v_jack == NULL) {
		DLOG(KERN_ERR, "Invalid jack handle");
		return;
	}

	mutex_lock(&v_jack->lock);
	memset(jack_data, 0, PAGE_SIZE);
	memset(&v_jack->msginfo, 0, sizeof(v_jack->msginfo));

	strcpy(jack_data, buf);

	v_jack->msginfo.req = request_set;
	v_jack->msginfo.type = type;
	strcpy(v_jack->msginfo.buf, buf);
	mutex_unlock(&v_jack->lock);

	DLOG(KERN_DEBUG, "set_jack_data type: %d, req: %d, buf: %s",
			v_jack->msginfo.type, v_jack->msginfo.req, v_jack->msginfo.buf);

	err = virtqueue_add_buf(v_jack->vq, v_jack->sg_vq, 1, 0, &v_jack->msginfo, GFP_ATOMIC);
	if (err < 0) {
		DLOG(KERN_ERR, "failed to add buffer to virtqueue (err = %d)", err);
		return;
	}

	virtqueue_kick(v_jack->vq);
}

static void get_jack_data(int type)
{
	int err = 0;

	if (v_jack == NULL) {
		DLOG(KERN_ERR, "Invalid jack handle");
		return;
	}

	mutex_lock(&v_jack->lock);
	memset(jack_data, 0, PAGE_SIZE);
	memset(&v_jack->msginfo, 0, sizeof(v_jack->msginfo));

	v_jack->msginfo.req = request_get;
	v_jack->msginfo.type = type;

	mutex_unlock(&v_jack->lock);

	DLOG(KERN_DEBUG, "get_jack_data type: %d, req: %d",
			v_jack->msginfo.type, v_jack->msginfo.req);

	err = virtqueue_add_buf(v_jack->vq, v_jack->sg_vq, 1, 1, &v_jack->msginfo, GFP_ATOMIC);
	if (err < 0) {
		DLOG(KERN_ERR, "failed to add buffer to virtqueue (err = %d)", err);
		return;
	}

	virtqueue_kick(v_jack->vq);

	wait_event_interruptible(wq, v_jack->flags != 0);

	mutex_lock(&v_jack->lock);
	v_jack->flags --;
	DLOG(KERN_DEBUG, "flags : %d", v_jack->flags);
	mutex_unlock(&v_jack->lock);
}

static ssize_t show_charger_online(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	get_jack_data(jack_type_charger);
	return snprintf(buf, PAGE_SIZE, "%s", jack_data);
}

static ssize_t store_charger_online(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	set_jack_data(jack_type_charger, buf);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_earjack_online(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	get_jack_data(jack_type_earjack);
	return snprintf(buf, PAGE_SIZE, "%s", jack_data);
}

static ssize_t store_earjack_online(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	set_jack_data(jack_type_earjack, buf);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_earkey_online(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	get_jack_data(jack_type_earkey);
	return snprintf(buf, PAGE_SIZE, "%s", jack_data);
}

static ssize_t store_earkey_online(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	set_jack_data(jack_type_earkey, buf);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_hdmi_online(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	get_jack_data(jack_type_hdmi);
	return snprintf(buf, PAGE_SIZE, "%s", jack_data);
}

static ssize_t store_hdmi_online(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	set_jack_data(jack_type_hdmi, buf);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_usb_online(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	get_jack_data(jack_type_usb);
	return snprintf(buf, PAGE_SIZE, "%s", jack_data);
}

static ssize_t store_usb_online(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	set_jack_data(jack_type_usb, buf);
	return strnlen(buf, PAGE_SIZE);
}

static DEVICE_ATTR(charger_online, S_IRUGO | S_IWUSR, show_charger_online, store_charger_online);
static DEVICE_ATTR(earjack_online, S_IRUGO | S_IWUSR, show_earjack_online, store_earjack_online);
static DEVICE_ATTR(earkey_online, S_IRUGO | S_IWUSR, show_earkey_online, store_earkey_online);
static DEVICE_ATTR(hdmi_online, S_IRUGO | S_IWUSR, show_hdmi_online, store_hdmi_online);
static DEVICE_ATTR(usb_online, S_IRUGO | S_IWUSR, show_usb_online, store_usb_online);

static int maru_jack_sysfs_create_file(struct device *dev)
{
	int result = 0;

	DLOG(KERN_INFO, "sysfs_create_file\n");

	result = device_create_file(dev, &dev_attr_charger_online);
	if (result){
		DLOG(KERN_ERR, "failed to create charger_online file\n");
		return result;
	}

	result = device_create_file(dev, &dev_attr_earjack_online);
	if (result){
		DLOG(KERN_ERR, "failed to create earjack_online file\n");
		return result;
	}

	result = device_create_file(dev, &dev_attr_earkey_online);
	if (result){
		DLOG(KERN_ERR, "failed to create earkey_online file\n");
		return result;
	}

	result = device_create_file(dev, &dev_attr_hdmi_online);
	if (result){
		DLOG(KERN_ERR, "failed to create hdmi_online file\n");
		return result;
	}

	result = device_create_file(dev, &dev_attr_usb_online);
	if (result){
		DLOG(KERN_ERR, "failed to create usb_online file\n");
		return result;
	}

	return 0;
}


static void maru_jack_sysfs_remove_file(struct device *dev)
{
	DLOG(KERN_INFO, "sysfs_remove_file\n");

	device_remove_file(dev, &dev_attr_charger_online);
	device_remove_file(dev, &dev_attr_earjack_online);
	device_remove_file(dev, &dev_attr_earkey_online);
	device_remove_file(dev, &dev_attr_hdmi_online);
	device_remove_file(dev, &dev_attr_usb_online);
}

static void maru_jack_sysfs_dev_release(struct device *dev)
{
	DLOG(KERN_INFO, "sysfs_dev_release\n");
}

static struct platform_device the_pdev = {
	.name = DEVICE_NAME,
	.id = -1,
	.dev = {
		.release = maru_jack_sysfs_dev_release,
	}
};

static int jack_probe(struct virtio_device* dev){
	int err = 0, index = 0;
	struct jack_data *data;

	DLOG(KERN_INFO, "jack_probe\n");

	v_jack = kmalloc(sizeof(struct virtio_jack), GFP_KERNEL);

	v_jack->vdev = dev;
	dev->priv = v_jack;
	v_jack->flags = 0;

	err = platform_device_register(&the_pdev);
	if (err) {
		DLOG(KERN_ERR, "platform_device_register failure\n");
		return err;
	}

	data = kzalloc(sizeof(struct jack_data), GFP_KERNEL);
	if (!data) {
		DLOG(KERN_ERR, "kzalloc failure\n");
		platform_device_unregister(&the_pdev);
		return -ENOMEM;
	}

	dev_set_drvdata(&the_pdev.dev, (void*)data);

	err = maru_jack_sysfs_create_file(&the_pdev.dev);
	if (err) {
		DLOG(KERN_ERR, "sysfs_create_file failure\n");
		kfree(data);
		platform_device_unregister(&the_pdev);
		return err;
	}

	v_jack->vq = virtio_find_single_vq(dev, jack_vq_done, "jack");
	if (IS_ERR(v_jack->vq)) {
		DLOG(KERN_ERR, "virtio queue is not found.\n");
		kfree(data);
		platform_device_unregister(&the_pdev);
		return err;
	}

	virtqueue_enable_cb(v_jack->vq);

	memset(&v_jack->msginfo, 0x00, sizeof(v_jack->msginfo));

	sg_init_table(v_jack->sg_vq, 2);
	for (; index < 2; index++) {
		sg_set_buf(&v_jack->sg_vq[index], &v_jack->msginfo, sizeof(v_jack->msginfo));
	}

	mutex_init(&v_jack->lock);

	return 0;
}

static void jack_remove(struct virtio_device* dev){
	void *data = dev_get_drvdata(&the_pdev.dev);

	DLOG(KERN_INFO, "sysfs_exit\n");

	if (data) {
		kfree(data);
	}
	maru_jack_sysfs_remove_file(&the_pdev.dev);
	platform_device_unregister(&the_pdev);

	if (v_jack) {
		kfree(v_jack);
		v_jack = NULL;
	}
}

MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_jack_driver = {
		.driver = {
				.name = KBUILD_MODNAME,
				.owner = THIS_MODULE ,
		},
		.id_table = id_table,
		.probe = jack_probe,
		.remove = jack_remove,
};

static int __init maru_jack_init(void)
{
	DLOG(KERN_INFO, "maru_%s: init\n", DEVICE_NAME);
	return register_virtio_driver(&virtio_jack_driver);
}

static void __exit maru_jack_exit(void)
{
	DLOG(KERN_INFO, "maru_%s: exit\n", DEVICE_NAME);
	unregister_virtio_driver(&virtio_jack_driver);
}

module_init(maru_jack_init);
module_exit(maru_jack_exit);

MODULE_LICENSE("GPL2");
MODULE_AUTHOR("Jinhyung Choi <jinhyung2.choi@samsung.com>");
MODULE_DESCRIPTION("Emulator Virtio Power Driver");
