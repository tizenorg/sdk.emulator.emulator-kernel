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


#define DEVICE_NAME				"power_supply"
#define FILE_PERMISSION			(S_IRUGO | S_IWUSR)

//#define DEBUG_MARU_POWER_SUPPLY

#ifdef DEBUG_MARU_POWER_SUPPLY
#define DLOG(level, fmt, ...) \
	printk(level "maru_%s: " fmt, DEVICE_NAME, ##__VA_ARGS__)
#else
// do nothing
#define DLOG(level, fmt, ...)
#endif

#define __MAX_BUF_SIZE		1024

static struct virtio_device_id id_table[] = { { VIRTIO_ID_POWER,
		VIRTIO_DEV_ANY_ID }, { 0 }, };

struct msg_info {
	char buf[__MAX_BUF_SIZE];

	uint16_t type;
	uint16_t req;
};

struct virtio_power {
	struct virtio_device* vdev;
	struct virtqueue* vq;

	struct msg_info msginfo;

	struct scatterlist sg_vq[2];

	int flags;
	struct mutex lock;
};

enum power_types {
	power_type_capacity = 0,
	power_type_charge_full,
	power_type_charge_now,
	power_type_max
};

enum request_cmd {
	request_get = 0,
	request_set,
	request_answer
};

struct virtio_power *v_power;

static struct class* power_class;
static struct device* power_device;

static char power_data [PAGE_SIZE];

static DECLARE_WAIT_QUEUE_HEAD(wq);

static void power_vq_done(struct virtqueue *vq) {
	unsigned int len;
	struct msg_info* msg;

	msg = (struct msg_info*) virtqueue_get_buf(v_power->vq, &len);
	if (msg == NULL) {
		DLOG(KERN_ERR, "failed to virtqueue_get_buf");
		return;
	}

	if (msg->req != request_answer || msg->buf == NULL) {
		return;
	}

	DLOG(KERN_DEBUG, "msg buf: %s, req: %d, type: %d", msg->buf, msg->req, msg->type);

	mutex_lock(&v_power->lock);
	strcpy(power_data, msg->buf);
	v_power->flags ++;
	DLOG(KERN_DEBUG, "flags : %d", v_power->flags);
	mutex_unlock(&v_power->lock);

	wake_up_interruptible(&wq);
}

static void set_power_data(int type, const char* buf)
{
	int err = 0;

	if (buf == NULL) {
		DLOG(KERN_ERR, "set_power buf is NULL.");
		return;
	}

	if (v_power == NULL) {
		DLOG(KERN_ERR, "Invalid power handle");
		return;
	}

	mutex_lock(&v_power->lock);
	memset(power_data, 0, PAGE_SIZE);
	memset(&v_power->msginfo, 0, sizeof(v_power->msginfo));

	strcpy(power_data, buf);

	v_power->msginfo.req = request_set;
	v_power->msginfo.type = type;
	strcpy(v_power->msginfo.buf, buf);
	mutex_unlock(&v_power->lock);

	DLOG(KERN_DEBUG, "set_power_data type: %d, req: %d, buf: %s",
			v_power->msginfo.type, v_power->msginfo.req, v_power->msginfo.buf);

	err = virtqueue_add_buf(v_power->vq, v_power->sg_vq, 1, 0, &v_power->msginfo, GFP_ATOMIC);
	if (err < 0) {
		DLOG(KERN_ERR, "failed to add buffer to virtqueue (err = %d)", err);
		return;
	}

	virtqueue_kick(v_power->vq);
}

static void get_power_data(int type)
{
	int err = 0;

	if (v_power == NULL) {
		DLOG(KERN_ERR, "Invalid power handle");
		return;
	}

	mutex_lock(&v_power->lock);
	memset(power_data, 0, PAGE_SIZE);
	memset(&v_power->msginfo, 0, sizeof(v_power->msginfo));

	v_power->msginfo.req = request_get;
	v_power->msginfo.type = type;

	mutex_unlock(&v_power->lock);

	DLOG(KERN_DEBUG, "get_power_data type: %d, req: %d",
			v_power->msginfo.type, v_power->msginfo.req);

	err = virtqueue_add_buf(v_power->vq, v_power->sg_vq, 1, 1, &v_power->msginfo, GFP_ATOMIC);
	if (err < 0) {
		DLOG(KERN_ERR, "failed to add buffer to virtqueue (err = %d)", err);
		return;
	}

	virtqueue_kick(v_power->vq);

	wait_event_interruptible(wq, v_power->flags != 0);

	mutex_lock(&v_power->lock);
	v_power->flags --;
	DLOG(KERN_DEBUG, "flags : %d", v_power->flags);
	mutex_unlock(&v_power->lock);
}

static ssize_t show_capacity(struct device *dev, struct device_attribute *attr, char *buf)
{
	get_power_data(power_type_capacity);
	return snprintf(buf, PAGE_SIZE, "%s", power_data);
}

static ssize_t store_capacity(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	set_power_data(power_type_capacity, buf);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_charge_full(struct device *dev, struct device_attribute *attr, char *buf)
{
	get_power_data(power_type_charge_full);
	return snprintf(buf, PAGE_SIZE, "%s", power_data);
}

static ssize_t store_charge_full(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	set_power_data(power_type_charge_full, buf);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t show_charge_now(struct device *dev, struct device_attribute *attr, char *buf)
{
	get_power_data(power_type_charge_now);
	return snprintf(buf, PAGE_SIZE, "%s", power_data);
}

static ssize_t store_charge_now(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	set_power_data(power_type_charge_now, buf);
	return strnlen(buf, PAGE_SIZE);
}

static struct device_attribute ps_device_attributes[] = {
	__ATTR(capacity, FILE_PERMISSION, show_capacity, store_capacity),
	__ATTR(charge_full, FILE_PERMISSION, show_charge_full, store_charge_full),
	__ATTR(charge_now, FILE_PERMISSION, show_charge_now, store_charge_now),
};

static void class_cleanup (void)
{
	int i = 2;

	for (; i > 0; i--) {

		if (power_device == NULL)
			continue;

		device_remove_file(power_device, &ps_device_attributes[i]);

		device_unregister(power_device);

		device_destroy(power_class, (dev_t)NULL);
	}

	class_destroy(power_class);
	power_class = NULL;
}

static int init_device(void)
{
	int err = 0, i = 0;
	power_device = device_create(power_class, NULL, (dev_t)NULL, NULL, "battery");

	for (i = 0; i < 3; i++) {
		err = device_create_file(power_device, &ps_device_attributes[i]);
		if (err) {
			printk(KERN_ERR
				"maru_%s: failed to create power_supply files\n", DEVICE_NAME);
			goto device_err;
		}
	}

	return err;
device_err:
	class_cleanup();
	return -1;
}

static void cleanup(struct virtio_device* dev) {
	dev->config->del_vqs(dev);

	if (v_power) {
		kfree(v_power);
		v_power = NULL;
	}

	class_cleanup();
}

static int power_probe(struct virtio_device* dev)
{
	int err = 0;
	int ret = 0;
	int index = 0;

	DLOG(KERN_INFO, "Power probe starts");

	v_power = kmalloc(sizeof(struct virtio_power), GFP_KERNEL);

	v_power->vdev = dev;
	dev->priv = v_power;
	v_power->flags = 0;

	power_class = class_create(THIS_MODULE, DEVICE_NAME);
	if (power_class == NULL) {
		DLOG(KERN_ERR, "Power class creation is failed.");
		return -1;
	}

	ret = init_device();
	if (ret) {
		cleanup(dev);
		return ret;
	}

	v_power->vq = virtio_find_single_vq(dev, power_vq_done, "power");
	if (IS_ERR(v_power->vq)) {
		cleanup(dev);
		DLOG(KERN_ERR, "failed to init virt queue");
		return ret;
	}

	virtqueue_enable_cb(v_power->vq);

	memset(&v_power->msginfo, 0x00, sizeof(v_power->msginfo));

	sg_init_table(v_power->sg_vq, 2);
	for (; index < 2; index++) {
		sg_set_buf(&v_power->sg_vq[index], &v_power->msginfo, sizeof(v_power->msginfo));
	}

	mutex_init(&v_power->lock);

	DLOG(KERN_INFO, "Power probe completes");

	return err;
}

static void power_remove(struct virtio_device* dev)
{
	struct virtio_power* v_power = dev->priv;
	if (!v_power)
	{
		DLOG(KERN_ERR, "virtio_power is NULL");
		return;
	}

	dev->config->reset(dev);

	cleanup(dev);

	DLOG(KERN_INFO, "Power driver is removed.");
}

MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_power_driver = {
		.driver = {
				.name = KBUILD_MODNAME,
				.owner = THIS_MODULE ,
		},
		.id_table = id_table,
		.probe = power_probe,
		.remove = power_remove,
};

static int __init maru_power_supply_init(void)
{
	DLOG(KERN_INFO, "maru_%s: init\n", DEVICE_NAME);
	return register_virtio_driver(&virtio_power_driver);
}

static void __exit maru_power_supply_exit(void)
{
	DLOG(KERN_INFO, "maru_%s: exit\n", DEVICE_NAME);
	unregister_virtio_driver(&virtio_power_driver);
}

module_init(maru_power_supply_init);
module_exit(maru_power_supply_exit);

MODULE_LICENSE("GPL2");
MODULE_AUTHOR("Jinhyung Choi <jinhyung2.choi@samsung.com>");
MODULE_DESCRIPTION("Emulator Virtio Power Driver");
