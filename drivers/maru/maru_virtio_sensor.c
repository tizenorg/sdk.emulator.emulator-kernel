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
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/cdev.h>


#define LOG(log_level, fmt, ...) \
	printk(log_level "%s: " fmt "\n", SENSOR_CLASS_NAME, ##__VA_ARGS__)

#define SENSOR_CLASS_NAME	"sensor"

#define DRIVER_ACCEL_NAME	"accel"
#define DRIVER_GEO_NAME		"geo"
#define DRIVER_GYRO_NAME	"gyro"
#define DRIVER_LIGHT_NAME	"light"
#define DRIVER_PROXI_NAME	"proxi"

#define __MAX_BUF_SIZE		1024
#define __MAX_BUF_SENSOR	32

#define DEVICE_COUNT		5

enum sensor_types {
	sensor_type_accel = 0,
	sensor_type_geo,
	sensor_type_gyro,
	sensor_type_gyro_x,
	sensor_type_gyro_y,
	sensor_type_gyro_z,
	sensor_type_light,
	sensor_type_light_adc,
	sensor_type_light_level,
	sensor_type_proxi,
	sensor_type_mag,
	sensor_type_tilt,
	sensor_type_max
};

enum request_cmd {
	request_get = 0,
	request_set,
	request_answer
};

struct msg_info {
	char buf[__MAX_BUF_SIZE];

	uint16_t type;
	uint16_t req;
};

struct virtio_sensor {
	struct virtio_device* vdev;
	struct virtqueue* vq;

	struct msg_info msginfo;

	struct scatterlist sg_vq[2];

	int flags;
	struct mutex lock;
};

static struct virtio_device_id id_table[] = { { VIRTIO_ID_SENSOR,
		VIRTIO_DEV_ANY_ID }, { 0 }, };

struct virtio_sensor *vs;

static struct class* sensor_class;

static DECLARE_WAIT_QUEUE_HEAD(wq);

#define __ATTR_RONLY(_name,_show) { \
	.attr	= { .name = __stringify(_name), .mode = 0444 },	\
	.show	= _show,					\
}

#define __ATTR_RW(_name) { \
	.attr = {.name = __stringify(_name), .mode = 0644 },	\
	.show	= _name##_show,					\
	.store	= _name##_store,					\
}

static char sensor_data [PAGE_SIZE];

static void sensor_vq_done(struct virtqueue *rvq) {
	unsigned int len;
	struct msg_info* msg;

	msg = (struct msg_info*) virtqueue_get_buf(vs->vq, &len);
	if (msg == NULL) {
		LOG(KERN_ERR, "failed to virtqueue_get_buf");
		return;
	}

	if (msg->req != request_answer || msg->buf == NULL) {
		LOG(KERN_ERR, "message from host is cracked.");
		return;
	}

	LOG(KERN_DEBUG, "msg buf: %s, req: %d, type: %d", msg->buf, msg->req, msg->type);

	mutex_lock(&vs->lock);
	strcpy(sensor_data, msg->buf);
	vs->flags = 1;
	mutex_unlock(&vs->lock);

	wake_up_interruptible(&wq);
}

static void set_sensor_data(int type, const char* buf)
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
	memset(sensor_data, 0, PAGE_SIZE);
	memset(&vs->msginfo, 0, sizeof(vs->msginfo));

	strcpy(sensor_data, buf);

	vs->msginfo.req = request_set;
	vs->msginfo.type = type;
	strcpy(vs->msginfo.buf, buf);
	mutex_unlock(&vs->lock);

	LOG(KERN_DEBUG, "set_sensor_data type: %d, req: %d, buf: %s",
			vs->msginfo.type, vs->msginfo.req, vs->msginfo.buf);

	err = virtqueue_add_buf(vs->vq, vs->sg_vq, 1, 0, &vs->msginfo, GFP_ATOMIC);
	if (err < 0) {
		LOG(KERN_ERR, "failed to add buffer to virtqueue (err = %d)", err);
		return;
	}

	virtqueue_kick(vs->vq);
}

static void get_sensor_data(int type)
{
	int err = 0;

	if (vs == NULL) {
		LOG(KERN_ERR, "Invalid sensor handle");
		return;
	}

	mutex_lock(&vs->lock);
	memset(sensor_data, 0, PAGE_SIZE);
	memset(&vs->msginfo, 0, sizeof(vs->msginfo));

	vs->msginfo.req = request_get;
	vs->msginfo.type = type;

	mutex_unlock(&vs->lock);

	LOG(KERN_DEBUG, "get_sensor_data type: %d, req: %d",
			vs->msginfo.type, vs->msginfo.req);

	err = virtqueue_add_buf(vs->vq, vs->sg_vq, 1, 1, &vs->msginfo, GFP_ATOMIC);
	if (err < 0) {
		LOG(KERN_ERR, "failed to add buffer to virtqueue (err = %d)", err);
		return;
	}

	virtqueue_kick(vs->vq);

	wait_event_interruptible(wq, vs->flags != 0);

	mutex_lock(&vs->lock);
	vs->flags = 0;
	mutex_unlock(&vs->lock);
}

/*
 * Accelerometer
 */
#define ACCEL_NAME_STR		"accel_sim"
#define ACCEL_FILE_NUM		2

static ssize_t accel_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, ACCEL_NAME_STR);
}

static ssize_t xyz_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	get_sensor_data(sensor_type_accel);
	return snprintf(buf, PAGE_SIZE, "%s", sensor_data);
}

static ssize_t xyz_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	set_sensor_data(sensor_type_accel, buf);
	return strnlen(buf, PAGE_SIZE);
}

static struct device_attribute da_accel [] =
{
	__ATTR_RONLY(name, accel_name_show),
	__ATTR_RW(xyz),
};

/*
 * GeoMagnetic
 */
#define GEO_NAME_STR		"geo_sim"
#define GEO_FILE_NUM		3

static ssize_t geo_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, GEO_NAME_STR);
}

static ssize_t raw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	get_sensor_data(sensor_type_tilt);
	return snprintf(buf, PAGE_SIZE, "%s", sensor_data);
}

static ssize_t raw_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	set_sensor_data(sensor_type_tilt, buf);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t tesla_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	get_sensor_data(sensor_type_mag);
	return snprintf(buf, PAGE_SIZE, "%s", sensor_data);
}

static ssize_t tesla_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	set_sensor_data(sensor_type_mag, buf);
	return strnlen(buf, PAGE_SIZE);
}

static struct device_attribute da_geo [] =
{
	__ATTR_RONLY(name, geo_name_show),
	__ATTR_RW(raw),
	__ATTR_RW(tesla),
};


/*
 * Gyroscope
 */

#define GYRO_NAME_STR		"gyro_sim"
#define GYRO_FILE_NUM		4

static ssize_t gyro_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, GYRO_NAME_STR);
}

static ssize_t gyro_x_raw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	get_sensor_data(sensor_type_gyro_x);
	return snprintf(buf, PAGE_SIZE, "%s", sensor_data);
}

static ssize_t gyro_x_raw_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	set_sensor_data(sensor_type_gyro_x, buf);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t gyro_y_raw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	get_sensor_data(sensor_type_gyro_y);
	return snprintf(buf, PAGE_SIZE, "%s", sensor_data);
}

static ssize_t gyro_y_raw_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	set_sensor_data(sensor_type_gyro_y, buf);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t gyro_z_raw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	get_sensor_data(sensor_type_gyro_z);
	return snprintf(buf, PAGE_SIZE, "%s", sensor_data);
}

static ssize_t gyro_z_raw_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	set_sensor_data(sensor_type_gyro_z, buf);
	return strnlen(buf, PAGE_SIZE);
}

static struct device_attribute da_gyro [] =
{
	__ATTR_RONLY(name, gyro_name_show),
	__ATTR_RW(gyro_x_raw),
	__ATTR_RW(gyro_y_raw),
	__ATTR_RW(gyro_z_raw),
};

/*
 * Light
 */

#define LIGHT_NAME_STR		"light_sim"
#define LIGHT_FILE_NUM		3

static ssize_t light_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, LIGHT_NAME_STR);
}

static ssize_t adc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	get_sensor_data(sensor_type_light_adc);
	return snprintf(buf, PAGE_SIZE, "%s", sensor_data);
}

static ssize_t adc_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	set_sensor_data(sensor_type_light_adc, buf);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t level_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	get_sensor_data(sensor_type_light_level);
	return snprintf(buf, PAGE_SIZE, "%s", sensor_data);
}

static ssize_t level_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	set_sensor_data(sensor_type_light_level, buf);
	return strnlen(buf, PAGE_SIZE);
}

static struct device_attribute da_light [] =
{
	__ATTR_RONLY(name, light_name_show),
	__ATTR_RW(adc),
	__ATTR_RW(level),
};


/*
 * Proxi
 */

#define PROXI_NAME_STR		"proxi_sim"
#define PROXI_FILE_NUM		3

static int proxi_enable = 1;

static ssize_t proxi_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, PROXI_NAME_STR);
}

static ssize_t enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d", proxi_enable);
}

static ssize_t enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &proxi_enable);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t vo_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	get_sensor_data(sensor_type_proxi);
	return snprintf(buf, PAGE_SIZE, "%s", sensor_data);
}

static ssize_t vo_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	set_sensor_data(sensor_type_proxi, buf);
	return strnlen(buf, PAGE_SIZE);
}

static struct device_attribute da_proxi [] =
{
	__ATTR_RONLY(name, proxi_name_show),
	__ATTR_RW(enable),
	__ATTR_RW(vo),
};

/*
 * Initialize Devices
 */

static struct device* device_list[DEVICE_COUNT];

const char *device_name_list[] = { DRIVER_ACCEL_NAME, DRIVER_GEO_NAME, DRIVER_GYRO_NAME, DRIVER_LIGHT_NAME, DRIVER_PROXI_NAME };

const int device_file_num[] = {ACCEL_FILE_NUM, GEO_FILE_NUM, GYRO_FILE_NUM, LIGHT_FILE_NUM, PROXI_FILE_NUM};

static struct device_attribute* da_list[] = {da_accel, da_geo, da_gyro, da_light, da_proxi };

static void class_cleanup (void)
{
	int i = 0, j = 0;

	for (i = 0; i < DEVICE_COUNT; i++) {

		if (device_list[i] == NULL)
			continue;

		for (j = 0; j < device_file_num[i]; j++) {
			device_remove_file(device_list[i], &da_list[i][j]);
		}

		device_destroy(sensor_class, (dev_t)NULL);
		device_list[i] = NULL;
	}

	class_destroy(sensor_class);
	sensor_class = NULL;
}

static int init_device(void)
{
	int i = 0;
	int j = 0;
	int ret = 0;

	for (i = 0; i < DEVICE_COUNT; i++) {

		device_list[i] = device_create(sensor_class, NULL, (dev_t)NULL, NULL, device_name_list[i]);
		if (device_list[i] < 0) {
			LOG(KERN_ERR, "%dth sensor device creation is failed.", i);
			goto device_err;
		}

		for (j = 0; j < device_file_num[i]; j++) {
			ret = device_create_file(device_list[i], &da_list[i][j]);
			if (ret) {
				LOG(KERN_ERR, "%dth file creation is failed from %dth sensor device.", i, j);
				goto device_err;
			}
		}
	}

	return ret;
device_err:
	class_cleanup();
	return -1;
}

static void cleanup(struct virtio_device* dev) {
	dev->config->del_vqs(dev);

	if (vs) {
		kfree(vs);
		vs = NULL;
	}

	class_cleanup();
}

static int sensor_probe(struct virtio_device* dev)
{
	int err = 0;
	int ret = 0;
	int index = 0;

	LOG(KERN_INFO, "Sensor probe starts");

	vs = kmalloc(sizeof(struct virtio_sensor), GFP_KERNEL);

	vs->vdev = dev;
	dev->priv = vs;

	sensor_class = class_create(THIS_MODULE, SENSOR_CLASS_NAME);
	if (sensor_class == NULL) {
		LOG(KERN_ERR, "sensor class creation is failed.");
		return -1;
	}

	ret = init_device();
	if (ret) {
		cleanup(dev);
		return ret;
	}

	vs->vq = virtio_find_single_vq(dev, sensor_vq_done, "sensor");
	if (IS_ERR(vs->vq)) {
		cleanup(dev);
		LOG(KERN_ERR, "failed to init virt queue");
		return ret;
	}

	virtqueue_enable_cb(vs->vq);

	memset(&vs->msginfo, 0x00, sizeof(vs->msginfo));

	sg_init_table(vs->sg_vq, 2);
	for (; index < 2; index++) {
		sg_set_buf(&vs->sg_vq[index], &vs->msginfo, sizeof(vs->msginfo));
	}

	mutex_init(&vs->lock);

	LOG(KERN_INFO, "Sensor probe completes");

	return err;
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

