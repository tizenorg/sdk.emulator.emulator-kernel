/*
 * Maru Virtio Sensor Device Driver
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd. All rights reserved.
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
	sensor_type_light,
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
	struct virtqueue* rvq;
	struct virtqueue* svq;

	struct msg_info read_msginfo;
	struct msg_info send_msginfo;

	struct scatterlist sg_read[2];
	struct scatterlist sg_send[2];
};

static struct virtio_device_id id_table[] = { { VIRTIO_ID_SENSOR,
		VIRTIO_DEV_ANY_ID }, { 0 }, };

struct virtio_sensor *vs;

static struct class* sensor_class;

#define ___ATTR_RONLY(_name,_show) { \
	.attr	= { .name = __stringify(_name), .mode = 0444 },	\
	.show	= _show,					\
}

#define ___ATTR_RW(_name) { \
	.attr = {.name = __stringify(_name), .mode = 0644 },	\
	.show	= _name##_show,					\
	.store	= _name##_store,					\
}

/*
 * Accelerometer
 */
#define ACCEL_NAME_STR		"accel_sim"
#define ACCEL_FILE_NUM		2

static char accel_xyz [__MAX_BUF_SENSOR] = {'0',',','9','8','0','6','6','5',',','0'};

static ssize_t accel_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, ACCEL_NAME_STR);
}

static ssize_t xyz_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s", accel_xyz);
}

static ssize_t xyz_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	strcpy(accel_xyz, buf);
	return strnlen(buf, PAGE_SIZE);
}

static struct device_attribute da_accel [] =
{
	___ATTR_RONLY(name, accel_name_show),
	___ATTR_RW(xyz),
};

/*
 * GeoMagnetic
 */
#define GEO_NAME_STR		"geo_sim"
#define GEO_FILE_NUM		3

static char geo_raw [__MAX_BUF_SENSOR] = {'0',' ','-','9','0',' ','0',' ','3'};
static char geo_tesla [__MAX_BUF_SENSOR] = {'1',' ','0',' ','-','1','0'};

static ssize_t geo_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, GEO_NAME_STR);
}

static ssize_t raw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s", geo_raw);
}

static ssize_t raw_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	strcpy(geo_raw, buf);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t tesla_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s", geo_tesla);
}

static ssize_t tesla_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	strcpy(geo_tesla, buf);
	return strnlen(buf, PAGE_SIZE);
}

static struct device_attribute da_geo [] =
{
	___ATTR_RONLY(name, geo_name_show),
	___ATTR_RW(raw),
	___ATTR_RW(tesla),
};


/*
 * Gyroscope
 */

#define GYRO_NAME_STR		"gyro_sim"
#define GYRO_FILE_NUM		4

static int gyro_x_raw = 0;
static int gyro_y_raw = 0;
static int gyro_z_raw = 0;

static ssize_t gyro_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, GYRO_NAME_STR);
}

static ssize_t gyro_x_raw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d", gyro_x_raw);
}

static ssize_t gyro_x_raw_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &gyro_x_raw);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t gyro_y_raw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d", gyro_y_raw);
}

static ssize_t gyro_y_raw_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &gyro_y_raw);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t gyro_z_raw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d", gyro_z_raw);
}

static ssize_t gyro_z_raw_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &gyro_z_raw);
	return strnlen(buf, PAGE_SIZE);
}

static struct device_attribute da_gyro [] =
{
	___ATTR_RONLY(name, gyro_name_show),
	___ATTR_RW(gyro_x_raw),
	___ATTR_RW(gyro_y_raw),
	___ATTR_RW(gyro_z_raw),
};

/*
 * Light
 */

#define LIGHT_NAME_STR		"light_sim"
#define LIGHT_FILE_NUM		3

static int light_adc = 65535;
static int light_level = 10;

static ssize_t light_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, LIGHT_NAME_STR);
}

static ssize_t adc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d", light_adc);
}

static ssize_t adc_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &light_adc);
	return strnlen(buf, PAGE_SIZE);
}

static ssize_t level_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d", light_level);
}

static ssize_t level_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &light_level);
	return strnlen(buf, PAGE_SIZE);
}

static struct device_attribute da_light [] =
{
	___ATTR_RONLY(name, light_name_show),
	___ATTR_RW(adc),
	___ATTR_RW(level),
};


/*
 * Proxi
 */

#define PROXI_NAME_STR		"proxi_sim"
#define PROXI_FILE_NUM		3

static int proxi_enable = 1;
static int proxi_vo = 8;

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
	return snprintf(buf, PAGE_SIZE, "%d", proxi_vo);
}

static ssize_t vo_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &proxi_vo);
	return strnlen(buf, PAGE_SIZE);
}

static struct device_attribute da_proxi [] =
{
	___ATTR_RONLY(name, proxi_name_show),
	___ATTR_RW(enable),
	___ATTR_RW(vo),
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

static int _make_buf_and_kick(void)
{
	int ret;
	memset(&vs->read_msginfo, 0x00, sizeof(vs->read_msginfo));
	ret = virtqueue_add_inbuf(vs->rvq, vs->sg_read, 1, &vs->read_msginfo, GFP_ATOMIC );
	if (ret < 0) {
		LOG(KERN_ERR, "failed to add buffer to virtqueue.(%d)\n", ret);
		return ret;
	}

	virtqueue_kick(vs->rvq);

	return 0;
}

static void get_sensor_value(int type)
{
	int err = 0;

	if (vs == NULL) {
		LOG(KERN_ERR, "Invalid sensor handle");
		return;
	}

	memset(&vs->send_msginfo, 0, sizeof(vs->send_msginfo));

	vs->send_msginfo.req = request_answer;

	switch (type) {
		case sensor_type_accel:
			vs->send_msginfo.type = sensor_type_accel;
			strcpy(vs->send_msginfo.buf, accel_xyz);
			break;
		case sensor_type_mag:
			vs->send_msginfo.type = sensor_type_mag;
			strcpy(vs->send_msginfo.buf, geo_tesla);
			break;
		case sensor_type_gyro:
			vs->send_msginfo.type = sensor_type_gyro;
			sprintf(vs->send_msginfo.buf, "%d, %d, %d", gyro_x_raw, gyro_y_raw, gyro_z_raw);
			break;
		case sensor_type_light:
			vs->send_msginfo.type = sensor_type_light;
			sprintf(vs->send_msginfo.buf, "%d", light_adc);
			break;
		case sensor_type_proxi:
			vs->send_msginfo.type = sensor_type_proxi;
			sprintf(vs->send_msginfo.buf, "%d", proxi_vo);
			break;
		default:
			return;
	}

	LOG(KERN_INFO, "vs->send_msginfo type: %d, req: %d, buf: %s",
			vs->send_msginfo.type, vs->send_msginfo.req, vs->send_msginfo.buf);

	err = virtqueue_add_outbuf(vs->svq, vs->sg_send, 1, &vs->send_msginfo, GFP_ATOMIC);
	if (err < 0) {
		LOG(KERN_ERR, "failed to add buffer to virtqueue (err = %d)", err);
		return;
	}

	virtqueue_kick(vs->svq);
}

static void set_sensor_value(char* buf, int type)
{

	LOG(KERN_INFO, "set_sensor_value- type: %d, buf: %s", type, buf);

	switch (type) {
		case sensor_type_accel:
			strcpy(accel_xyz, buf);
			break;
		case sensor_type_gyro:
			sscanf(buf, "%d %d %d", &gyro_x_raw, &gyro_y_raw, &gyro_z_raw);
			break;
		case sensor_type_light:
			sscanf(buf, "%d", &light_adc);
			light_level = (light_adc / 6554) % 10 + 1;
			break;
		case sensor_type_proxi:
			sscanf(buf, "%d", &proxi_vo);
			break;
		case sensor_type_mag:
			strcpy(geo_tesla, buf);
			break;
		case sensor_type_tilt:
			strcpy(geo_raw, buf);
			break;
		default:
			return;
	}

}

static void message_handler(char* buf, int req, int type)
{
	if (req == request_get) {
		get_sensor_value(type);
	} else if (req == request_set) {
		set_sensor_value(buf, type);
	} else {
		LOG(KERN_INFO, "wrong message request");
	}
}

static void sensor_recv_done(struct virtqueue *rvq) {
	unsigned int len;
	struct msg_info* msg;

	msg = (struct msg_info*) virtqueue_get_buf(vs->rvq, &len);
	if (msg == NULL ) {
		LOG(KERN_ERR, "failed to virtqueue_get_buf");
		return;
	}

	LOG(KERN_INFO, "msg buf: %s, req: %d, type: %d", msg->buf, msg->req, msg->type);

	message_handler(msg->buf, msg->req, msg->type);

	_make_buf_and_kick();
}

static void sensor_send_done(struct virtqueue *svq) {
	unsigned int len = 0;

	virtqueue_get_buf(svq, &len);
}

static int init_vqs(struct virtio_sensor *vsensor) {
	struct virtqueue *vqs[2];
	vq_callback_t *vq_callbacks[] = { sensor_recv_done, sensor_send_done };
	const char *vq_names[] = { "sensor_input", "sensor_output" };
	int err;

	err = vs->vdev->config->find_vqs(vs->vdev, 2, vqs, vq_callbacks, vq_names);
	if (err < 0)
		return err;

	vs->rvq = vqs[0];
	vs->svq = vqs[1];

	virtqueue_enable_cb(vs->rvq);
	virtqueue_enable_cb(vs->svq);

	return 0;
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

	ret = init_vqs(vs);
	if (ret) {
		cleanup(dev);
		LOG(KERN_ERR, "failed to init vqs");
		return ret;
	}

	memset(&vs->read_msginfo, 0x00, sizeof(vs->read_msginfo));
	sg_set_buf(vs->sg_read, &vs->read_msginfo, sizeof(struct msg_info));

	memset(&vs->send_msginfo, 0x00, sizeof(vs->send_msginfo));
	sg_set_buf(vs->sg_send, &vs->send_msginfo, sizeof(struct msg_info));

	sg_init_one(vs->sg_read, &vs->read_msginfo, sizeof(vs->read_msginfo));
	sg_init_one(vs->sg_send, &vs->send_msginfo, sizeof(vs->send_msginfo));

	ret = _make_buf_and_kick();
	if (ret) {
		cleanup(dev);
		LOG(KERN_ERR, "failed to send buf");
		return ret;
	}

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

