/*
 * Maru Virtio Sensor Device Driver
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 *  Jinhyung Choi <jinhyung2.choi@samsung.com>
 *  Daiyoung Kim <daiyoung777.kim@samsung.com>
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/cdev.h>

#include "maru_virtio_sensor.h"

static struct virtio_device_id id_table[] = { { VIRTIO_ID_SENSOR,
		VIRTIO_DEV_ANY_ID }, { 0 }, };

static char sensor_data[__MAX_BUF_SENSOR];

struct virtio_sensor *vs;

static DECLARE_WAIT_QUEUE_HEAD(wq);

int sensor_atoi(const char *value)
{
    int val = 0;

    for (;; value++) {
        switch (*value) {
            case '0' ... '9':
                val = 10*val+(*value-'0');
                break;
            default:
                return val;
        }
    }
}

int register_sensor_device(struct device *dev, struct virtio_sensor *vs,
		struct device_attribute *attributes[], const char* name)
{
	int i = 0, err = 0;

	if (!vs->sensor_class) {
		LOG(KERN_ERR, "sensor class is not created before make device");
		return -1;
	}

	LOG(KERN_INFO, "device creation: %s.", name);

	dev = device_create(vs->sensor_class, NULL, 0, NULL, "%s", name);
	if (dev < 0) {
		LOG(KERN_ERR, "register_device_create failed!");
		return -1;
	}

	if (attributes == NULL) {
		LOG(KERN_ERR, "attributes is NULL.");
		return -1;
	}

	for (i = 0; attributes[i] != NULL; i++) {
		if ((err = device_create_file(dev, attributes[i])) < 0) {
			LOG(KERN_ERR, "failed to create device file with attribute[%d - %d]", i, err);
			return -1;
		}
	}

	LOG(KERN_INFO, "register_sensor_device ends: %s.", name);

	return 0;
}

#ifdef SUPPORT_LEGACY_SENSOR

int l_register_sensor_device(struct device *dev, struct virtio_sensor *vs,
		struct device_attribute *attributes[], const char* name)
{
	int i = 0, err = 0;

	if (!vs->l_sensor_class) {
		LOG(KERN_ERR, "l sensor class is not created before make device");
		return -1;
	}

	dev = device_create(vs->l_sensor_class, NULL, 0, NULL, "%s", name);
	if (dev < 0) {
		LOG(KERN_ERR, "legacy register_device_create failed!");
		return -1;
	}

	if (attributes == NULL) {
		LOG(KERN_ERR, "l sensor attributes is NULL.");
		return -1;
	}

	for (i = 0; attributes[i] != NULL; i++) {
		if ((err = device_create_file(dev, attributes[i])) < 0) {
			LOG(KERN_ERR, "failed to create legacy device file with attribute[%d - %d]", i, err);
			return -1;
		}
	}

	return 0;
}

#endif

static void sensor_vq_done(struct virtqueue *rvq) {
	unsigned int len;
	struct msg_info* msg;

	msg = (struct msg_info*) virtqueue_get_buf(vs->vq, &len);
	if (msg == NULL) {
		LOG(KERN_ERR, "failed to virtqueue_get_buf");
		return;
	}

	if (msg->req != request_answer) {
		LOG(KERN_DEBUG, "receive queue- not an answer message: %d", msg->req);
		return;
	}
	if (msg->buf == NULL) {
		LOG(KERN_ERR, "receive queue- message from host is NULL.");
		return;
	}

	LOG(KERN_DEBUG, "msg buf: %s, req: %d, type: %d", msg->buf, msg->req, msg->type);

	mutex_lock(&vs->lock);
	memset(sensor_data, 0, __MAX_BUF_SENSOR);
	strcpy(sensor_data, msg->buf);
	vs->flags = 1;
	mutex_unlock(&vs->lock);

	wake_up_interruptible(&wq);
}

void set_sensor_data(int type, const char* buf)
{
	int err = 0;

	if (buf == NULL) {
		LOG(KERN_ERR, "set_sensor buf is NULL.");
		return;
	}

	if (vs == NULL) {
		LOG(KERN_ERR, "Invalid sensor handle");
		return;
	}

	mutex_lock(&vs->lock);
	memset(&vs->msginfo, 0, sizeof(vs->msginfo));

	vs->msginfo.req = request_set;
	vs->msginfo.type = type;
	strcpy(vs->msginfo.buf, buf);
	mutex_unlock(&vs->lock);

	LOG(KERN_DEBUG, "set_sensor_data type: %d, req: %d, buf: %s",
			vs->msginfo.type, vs->msginfo.req, vs->msginfo.buf);

	err = virtqueue_add_outbuf(vs->vq, vs->sg_vq, 1, &vs->msginfo, GFP_ATOMIC);
	if (err < 0) {
		LOG(KERN_ERR, "failed to add buffer to virtqueue (err = %d)", err);
		return;
	}

	virtqueue_kick(vs->vq);
}

int get_sensor_data(int type, char* data)
{
	struct scatterlist *sgs[2];
	int err = 0;

	if (vs == NULL || data == NULL) {
		LOG(KERN_ERR, "Invalid sensor handle or data is NULL.");
		return -1;
	}

	mutex_lock(&vs->lock);
	memset(&vs->msginfo, 0, sizeof(vs->msginfo));

	vs->msginfo.req = request_get;
	vs->msginfo.type = type;
	mutex_unlock(&vs->lock);

	LOG(KERN_DEBUG, "get_sensor_data type: %d, req: %d",
			vs->msginfo.type, vs->msginfo.req);

	sgs[0] = &vs->sg_vq[0];
	sgs[1] = &vs->sg_vq[1];
	err = virtqueue_add_sgs(vs->vq, sgs, 1, 1, &vs->msginfo, GFP_ATOMIC);
	if (err < 0) {
		LOG(KERN_ERR, "failed to add buffer to virtqueue (err = %d)", err);
		return err;
	}

	virtqueue_kick(vs->vq);

	wait_event_interruptible(wq, vs->flags != 0);

	mutex_lock(&vs->lock);
	vs->flags = 0;
	memcpy(data, sensor_data, strlen(sensor_data));
	mutex_unlock(&vs->lock);

	return 0;
}

int get_data_for_show(int type, char* buf)
{
	char sensor_data[__MAX_BUF_SENSOR];
	int ret;

	memset(sensor_data, 0, __MAX_BUF_SENSOR);
	ret = get_sensor_data(type, sensor_data);
	if (ret)
		return sprintf(buf, "%d", -1);

	return sprintf(buf, "%s", sensor_data);
}

static int device_init(struct virtio_sensor *vs)
{
	int ret = 0;

	if (vs->sensor_capability & sensor_cap_accel) {
		ret = maru_accel_init(vs);
		if (ret)
			return ret;
	}

	if (vs->sensor_capability & sensor_cap_geo) {
		ret = maru_geo_init(vs);
		if (ret)
			return ret;
	}

	if (vs->sensor_capability & sensor_cap_gyro) {
		ret = maru_gyro_init(vs);
		if (ret)
			return ret;
	}

	if (vs->sensor_capability & sensor_cap_light) {
		ret = maru_light_init(vs);
		if (ret)
			return ret;
	}

	if (vs->sensor_capability & sensor_cap_proxi) {
		ret = maru_proxi_init(vs);
		if (ret)
			return ret;
	}

	return ret;
}

static void device_exit(struct virtio_sensor *vs)
{
	if (vs->sensor_capability & sensor_cap_accel) {
		maru_accel_exit(vs);
	}

	if (vs->sensor_capability & sensor_cap_geo) {
		maru_geo_exit(vs);
	}

	if (vs->sensor_capability & sensor_cap_gyro) {
		maru_gyro_exit(vs);
	}

	if (vs->sensor_capability & sensor_cap_light) {
		maru_light_exit(vs);
	}

	if (vs->sensor_capability & sensor_cap_proxi) {
		maru_proxi_exit(vs);
	}
}

static void cleanup(struct virtio_device* dev) {
	dev->config->del_vqs(dev);

	if (vs == NULL)
		return;

	if (vs->sensor_class) {
		device_destroy(vs->sensor_class, (dev_t)NULL);
		class_destroy(vs->sensor_class);
	}

#ifdef SUPPORT_LEGACY_SENSOR
	if (vs->l_sensor_class) {
		device_destroy(vs->l_sensor_class, (dev_t)NULL);
		class_destroy(vs->l_sensor_class);
	}
#endif

	kfree(vs);
	vs = NULL;
}

static int sensor_probe(struct virtio_device* dev)
{
	int ret = 0;
	int index = 0;
	char sensor_data[__MAX_BUF_SENSOR];

	LOG(KERN_INFO, "Sensor probe starts");

	vs = kmalloc(sizeof(struct virtio_sensor), GFP_KERNEL);
	if (!vs) {
		LOG(KERN_ERR, "failed to allocate sensor structure.");
		return -ENOMEM;
	}

	vs->vdev = dev;
	dev->priv = vs;

	vs->sensor_class = class_create(THIS_MODULE, SENSOR_CLASS_NAME);
	if (IS_ERR(vs->sensor_class)) {
		LOG(KERN_ERR, "sensor class creation is failed.");
		return PTR_ERR(vs->sensor_class);
	}

#ifdef SUPPORT_LEGACY_SENSOR
	vs->l_sensor_class = class_create(THIS_MODULE, L_SENSOR_CLASS_NAME);
	if (IS_ERR(vs->sensor_class)) {
		LOG(KERN_ERR, "sensor class creation is failed.");
		return PTR_ERR(vs->sensor_class);
	}
#endif

	vs->vq = virtio_find_single_vq(dev, sensor_vq_done, "sensor");
	if (IS_ERR(vs->vq)) {
		cleanup(dev);
		LOG(KERN_ERR, "failed to init virt queue");
		return ret;
	}

	virtqueue_enable_cb(vs->vq);

	sg_init_table(vs->sg_vq, 2);
	for (; index < 2; index++) {
		sg_set_buf(&vs->sg_vq[index], &vs->msginfo, sizeof(vs->msginfo));
	}

	mutex_init(&vs->lock);

	memset(sensor_data, 0, __MAX_BUF_SENSOR);
	ret = get_sensor_data(sensor_type_list, sensor_data);
	if (ret) {
		LOG(KERN_ERR, "sensor capability data is null.");
		cleanup(dev);
		return ret;
	}

	vs->sensor_capability = sensor_atoi(sensor_data);
	LOG(KERN_INFO, "sensor capability is %02x", vs->sensor_capability);

	ret = device_init(vs);
	if (ret) {
		LOG(KERN_ERR, "failed initialing devices");
		cleanup(dev);
		return ret;
	}

	LOG(KERN_INFO, "Sensor probe completes");

	return ret;
}

static void sensor_remove(struct virtio_device* dev)
{
	struct virtio_sensor* vs = dev->priv;
	if (!vs)
	{
		LOG(KERN_ERR, "vs is NULL");
		return;
	}

	dev->config->reset(dev);

	device_exit(vs);

	cleanup(dev);

	LOG(KERN_INFO, "Sensor driver is removed.");
}

MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_sensor_driver = {
		.driver = {
				.name = KBUILD_MODNAME,
				.owner = THIS_MODULE ,
		},
		.id_table = id_table,
		.probe = sensor_probe,
		.remove = sensor_remove,
};


static int __init sensor_init(void)
{
	LOG(KERN_INFO, "Sensor driver initialized.");

	return register_virtio_driver(&virtio_sensor_driver);
}

static void __exit sensor_exit(void)
{
	unregister_virtio_driver(&virtio_sensor_driver);

	LOG(KERN_INFO, "Sensor driver is destroyed.");
}

module_init(sensor_init);
module_exit(sensor_exit);

MODULE_LICENSE("GPL2");
MODULE_AUTHOR("Jinhyung Choi <jinhyung2.choi@samsung.com>");
MODULE_DESCRIPTION("Emulator Virtio Sensor Driver");

