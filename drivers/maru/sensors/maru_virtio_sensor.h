/*
 * Maru Virtio Sensor Device Driver
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 *  Jinhyung Choi <jinhyung2.choi@samsung.com>
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
 *
 */

#ifndef _MARU_VIRTIO_SENSOR_H
#define _MARU_VIRTIO_SENSOR_H

#include <linux/kernel.h>
#include <linux/virtio.h>
#include <linux/input.h>

#define SUPPORT_LEGACY_SENSOR	1

enum request_cmd {
	request_get = 0,
	request_set,
	request_answer
};

enum sensor_types {
    sensor_type_list = 0,
    sensor_type_accel,
    sensor_type_accel_enable,
    sensor_type_accel_delay,
	sensor_type_geo,
    sensor_type_geo_enable,
    sensor_type_geo_delay,
	sensor_type_gyro,
    sensor_type_gyro_enable,
    sensor_type_gyro_delay,
	sensor_type_gyro_x,
	sensor_type_gyro_y,
	sensor_type_gyro_z,
	sensor_type_light,
	sensor_type_light_enable,
	sensor_type_light_delay,
	sensor_type_light_adc,
	sensor_type_light_level,
	sensor_type_proxi,
	sensor_type_proxi_enable,
	sensor_type_proxi_delay,
	sensor_type_rotation_vector,
	sensor_type_rotation_vector_enable,
	sensor_type_rotation_vector_delay,
	sensor_type_mag,
	sensor_type_tilt,
	sensor_type_max
};

enum sensor_capabilities {
	sensor_cap_accel 			= 0x01,
	sensor_cap_geo				= 0x02,
	sensor_cap_gyro				= 0x04,
	sensor_cap_light			= 0x08,
	sensor_cap_proxi			= 0x10,
	sensor_cap_rotation_vector	= 0x20,
	sensor_cap_haptic			= 0x40
};

#define __MAX_BUF_SIZE			1024
#define __MAX_BUF_SENSOR		128

struct msg_info {
	char buf[__MAX_BUF_SIZE];

	uint16_t type;
	uint16_t req;
};

#ifdef SUPPORT_LEGACY_SENSOR
#  define L_SENSOR_CLASS_NAME		"sensor"
#endif

struct virtio_sensor {
	struct virtio_device* vdev;
	struct virtqueue* vq;

	struct msg_info msginfo;
	struct scatterlist sg_vq[2];

	int flags;
	struct mutex lock;

	struct class* sensor_class;

#ifdef SUPPORT_LEGACY_SENSOR
	struct class* l_sensor_class;
#endif

	int sensor_capability;

	void* accel_handle;
	void* geo_handle;
	void* gyro_handle;
	void* light_handle;
	void* proxi_handle;
	void* rotation_vector_handle;
	void* haptic_handle;
};

#define MARU_DEVICE_ATTR(_name)	\
	struct device_attribute dev_attr_##_name = MARU_ATTR_RONLY(_name)

#define MARU_ATTR_RONLY(_name) { \
	.attr	= { .name = __stringify(_name), .mode = 0444 },	\
	.show	= maru_##_name##_show,					\
}

#define MARU_ATTR_RW(_name) { \
	.attr = {.name = __stringify(_name), .mode = 0644 },	\
	.show	= maru_##_name##_show,					\
	.store	= maru_##_name##_store,					\
}

int sensor_atoi(const char *value);

int get_data_for_show(int type, char* buf);
int register_sensor_device(struct device *dev, struct virtio_sensor *vs,
		struct device_attribute *attributes[], const char* name);

#ifdef SUPPORT_LEGACY_SENSOR
int l_register_sensor_device(struct device *dev, struct virtio_sensor *vs,
		struct device_attribute *attributes[], const char* name);
#endif

void set_sensor_data(int type, const char* buf);
int get_sensor_data(int type, char* data);

#define SENSOR_CLASS_NAME			"sensors"
#define MARU_SENSOR_DEVICE_VENDOR	"Tizen_SDK"

#define DRIVER_ACCEL_NAME			"accel"
#define SENSOR_ACCEL_INPUT_NAME		"accelerometer_sensor"
#define MARU_ACCEL_DEVICE_NAME		"maru_sensor_accel_1"

#define DRIVER_GEO_NAME				"geo"
#define SENSOR_GEO_INPUT_NAME		"geomagnetic_sensor"
#define MARU_GEO_DEVICE_NAME		"maru_sensor_geo_1"

#define DRIVER_GYRO_NAME			"gyro"
#define SENSOR_GYRO_INPUT_NAME		"gyro_sensor"
#define MARU_GYRO_DEVICE_NAME		"maru_sensor_gyro_1"

#define DRIVER_LIGHT_NAME			"light"
#define SENSOR_LIGHT_INPUT_NAME		"light_sensor"
#define MARU_LIGHT_DEVICE_NAME		"maru_sensor_light_1"

#define DRIVER_PROXI_NAME			"proxi"
#define SENSOR_PROXI_INPUT_NAME		"proximity_sensor"
#define MARU_PROXI_DEVICE_NAME		"maru_sensor_proxi_1"

#define DRIVER_ROTATION_NAME		"rotation"
#define SENSOR_ROTATION_INPUT_NAME	"rot_sensor"
#define MARU_ROTATION_DEVICE_NAME	"maru_sensor_rotation_vector_1"

#define SENSOR_HAPTIC_INPUT_NAME	"haptic_sensor"

// It locates /sys/module/maru_virtio_sensor/parameters/sensor_driver_debug
extern int sensor_driver_debug;

#define ERR(fmt, ...)	\
	printk(KERN_ERR "%s: " fmt "\n", SENSOR_CLASS_NAME, ##__VA_ARGS__)

#define INFO(fmt, ...)	\
	printk(KERN_INFO "%s: " fmt "\n", SENSOR_CLASS_NAME, ##__VA_ARGS__)

#define LOG(log_level, fmt, ...) \
	do {	\
		if (sensor_driver_debug >= (log_level)) {	\
			printk(KERN_INFO "%s: " fmt "\n", SENSOR_CLASS_NAME, ##__VA_ARGS__);	\
		}	\
	} while (0)

/*
 * Accelerometer device
 */
int maru_accel_init(struct virtio_sensor *vs);
int maru_accel_exit(struct virtio_sensor *vs);

/*
 * Geomagnetic device
 */
int maru_geo_init(struct virtio_sensor *vs);
int maru_geo_exit(struct virtio_sensor *vs);

/*
 * Gyroscope device
 */
int maru_gyro_init(struct virtio_sensor *vs);
int maru_gyro_exit(struct virtio_sensor *vs);

/*
 * Light device
 */
int maru_light_init(struct virtio_sensor *vs);
int maru_light_exit(struct virtio_sensor *vs);

/*
 * Proximity device
 */
int maru_proxi_init(struct virtio_sensor *vs);
int maru_proxi_exit(struct virtio_sensor *vs);

/*
 * Rotation Vector device
 */
int maru_rotation_vector_init(struct virtio_sensor *vs);
int maru_rotation_vector_exit(struct virtio_sensor *vs);

/*
 * Haptic device
 */
int maru_haptic_init(struct virtio_sensor *vs);
int maru_haptic_exit(struct virtio_sensor *vs);

#endif
