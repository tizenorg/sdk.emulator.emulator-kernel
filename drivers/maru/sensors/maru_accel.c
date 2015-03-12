/*
 * Maru Virtio Accelerometer Sensor Device Driver
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

#include <linux/slab.h>

#include "maru_virtio_sensor.h"

struct maru_accel_data {
	struct input_dev *input_data;
	struct delayed_work work;

	struct virtio_sensor* vs;

	atomic_t enable;
	atomic_t poll_delay;
};

static struct device *accel_sensor_device;

#ifdef SUPPORT_LEGACY_SENSOR
static struct device *l_accel_sensor_device;
#endif

#define GRAVITY_CHANGE_UNIT		15322
static short sensor_convert_data(int number)
{
	int temp;
	temp = number / 64;
	temp = temp * (SHRT_MAX / 2);
	temp = temp / GRAVITY_CHANGE_UNIT;
	return (short)temp;
}

static void maru_accel_input_work_func(struct work_struct *work) {

	int poll_time = 200000000;
	int enable = 0;
	int ret = 0;
	int accel_x, accel_y, accel_z;
	short raw_x, raw_y, raw_z;
	char sensor_data[__MAX_BUF_SENSOR];
	struct maru_accel_data *data = container_of((struct delayed_work *)work,
			struct maru_accel_data, work);

	LOG(1, "maru_accel_input_work_func starts");

	enable = atomic_read(&data->enable);
	poll_time = atomic_read(&data->poll_delay);

	memset(sensor_data, 0, __MAX_BUF_SENSOR);

	if (enable) {
		mutex_lock(&data->vs->vqlock);
		ret = get_sensor_data(sensor_type_accel, sensor_data);
		mutex_unlock(&data->vs->vqlock);
		if (!ret) {
			sscanf(sensor_data, "%d,%d,%d", &accel_x, &accel_y, &accel_z);
			LOG(1, "accel_set act %d, %d, %d", accel_x, accel_y, accel_z);
			raw_x = sensor_convert_data(accel_x);
			raw_y = sensor_convert_data(accel_y);
			raw_z = sensor_convert_data(accel_z);
			LOG(1, "accel_set raw %d, %d, %d", raw_x, raw_y, raw_z);

			if (raw_x == 0) {
				raw_x = 1;
			}

			if (raw_y == 0) {
				raw_y = 1;
			}

			if (raw_z == 0) {
				raw_z = 1;
			}

			input_report_rel(data->input_data, REL_X, raw_x);
			input_report_rel(data->input_data, REL_Y, raw_y);
			input_report_rel(data->input_data, REL_Z, raw_z);
			input_sync(data->input_data);
		}
	}

	enable = atomic_read(&data->enable);

	LOG(1, "enable: %d, poll_time: %d", enable, poll_time);
	if (enable) {
		if (poll_time > 0) {
			schedule_delayed_work(&data->work, nsecs_to_jiffies(poll_time));
		} else {
			schedule_delayed_work(&data->work, 0);
		}
	}

	LOG(1, "maru_accel_input_work_func ends");

}

static ssize_t maru_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s", MARU_ACCEL_DEVICE_NAME);
}

static ssize_t maru_vendor_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s", MARU_SENSOR_DEVICE_VENDOR);
}

static ssize_t maru_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char sensor_data[__MAX_BUF_SENSOR];
	int ret;
	struct input_dev *input_data = to_input_dev(dev);
	struct maru_accel_data *data = input_get_drvdata(input_data);

	memset(sensor_data, 0, __MAX_BUF_SENSOR);
	mutex_lock(&data->vs->vqlock);
	ret = get_sensor_data(sensor_type_accel_enable, sensor_data);
	mutex_unlock(&data->vs->vqlock);
	if (ret)
		return sprintf(buf, "%d", -1);

	return sprintf(buf, "%s", sensor_data);
}

static ssize_t maru_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct input_dev *input_data = to_input_dev(dev);
	struct maru_accel_data *data = input_get_drvdata(input_data);
	int value = simple_strtoul(buf, NULL, 10);

	if (value != 0 && value != 1)
		return count;

	mutex_lock(&data->vs->vqlock);
	set_sensor_data(sensor_type_accel_enable, buf);
	mutex_unlock(&data->vs->vqlock);

	if (value) {
		if (atomic_read(&data->enable) != 1) {
			atomic_set(&data->enable, 1);
			schedule_delayed_work(&data->work, 0);

		}
	} else {
		if (atomic_read(&data->enable) != 0) {
			atomic_set(&data->enable, 0);
			cancel_delayed_work(&data->work);
		}
	}

	return strnlen(buf, __MAX_BUF_SENSOR);
}

static ssize_t maru_poll_delay_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char sensor_data[__MAX_BUF_SENSOR];
	int ret;
	struct input_dev *input_data = to_input_dev(dev);
	struct maru_accel_data *data = input_get_drvdata(input_data);

	memset(sensor_data, 0, __MAX_BUF_SENSOR);
	mutex_lock(&data->vs->vqlock);
	ret = get_sensor_data(sensor_type_accel_delay, sensor_data);
	mutex_unlock(&data->vs->vqlock);
	if (ret)
		return sprintf(buf, "%d", -1);

	return sprintf(buf, "%s", sensor_data);
}

static ssize_t maru_poll_delay_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct input_dev *input_data = to_input_dev(dev);
	struct maru_accel_data *data = input_get_drvdata(input_data);
	int value = simple_strtoul(buf, NULL, 10);

	if (value < __MIN_DELAY_SENSOR)
		return count;

	mutex_lock(&data->vs->vqlock);
	set_sensor_data(sensor_type_accel_delay, buf);
	mutex_unlock(&data->vs->vqlock);
	atomic_set(&data->poll_delay, value);

	return strnlen(buf, __MAX_BUF_SENSOR);
}

static struct device_attribute dev_attr_sensor_name =
		__ATTR(name, S_IRUGO, maru_name_show, NULL);

static struct device_attribute dev_attr_sensor_vendor =
		__ATTR(vendor, S_IRUGO, maru_vendor_show, NULL);

static struct device_attribute *accel_sensor_attrs [] = {
	&dev_attr_sensor_name,
	&dev_attr_sensor_vendor,
	NULL,
};

#ifdef SUPPORT_LEGACY_SENSOR
#define ACCEL_NAME_STR		"accel_sim"

static ssize_t accel_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s", ACCEL_NAME_STR);
}

static ssize_t xyz_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char sensor_data[__MAX_BUF_SENSOR];
	int ret;
	struct input_dev *input_data = to_input_dev(dev);
	struct maru_accel_data *data = input_get_drvdata(input_data);

	memset(sensor_data, 0, __MAX_BUF_SENSOR);
	mutex_lock(&data->vs->vqlock);
	ret = get_sensor_data(sensor_type_accel, sensor_data);
	mutex_unlock(&data->vs->vqlock);
	if (ret)
		return sprintf(buf, "%d", -1);

	return sprintf(buf, "%s", sensor_data);
}

static ssize_t xyz_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct input_dev *input_data = to_input_dev(dev);
	struct maru_accel_data *data = input_get_drvdata(input_data);
	mutex_lock(&data->vs->vqlock);
	set_sensor_data(sensor_type_accel, buf);
	mutex_unlock(&data->vs->vqlock);
	return strnlen(buf, __MAX_BUF_SENSOR);
}

static struct device_attribute dev_attr_l_sensor_name =
		__ATTR(name, S_IRUGO, accel_name_show, NULL);

static DEVICE_ATTR(xyz, 0644, xyz_show, xyz_store);

static struct device_attribute *l_accel_sensor_attrs [] = {
	&dev_attr_l_sensor_name,
	&dev_attr_xyz,
	NULL,
};
#endif

static struct device_attribute attr_accel [] =
{
	MARU_ATTR_RW(enable),
	MARU_ATTR_RW(poll_delay),
};

static struct attribute *maru_accel_attribute[] = {
	&attr_accel[0].attr,
	&attr_accel[1].attr,
	NULL
};

static struct attribute_group maru_accel_attribute_group = {
	.attrs = maru_accel_attribute
};

static void accel_clear(struct maru_accel_data *data) {
	if (data == NULL)
		return;

	if (data->input_data) {
		sysfs_remove_group(&data->input_data->dev.kobj,
			&maru_accel_attribute_group);
		input_free_device(data->input_data);
	}

	kfree(data);
	data = NULL;
}

static int set_initial_value(struct maru_accel_data *data)
{
	int delay = 0;
	int ret = 0;
	char sensor_data [__MAX_BUF_SENSOR];

	memset(sensor_data, 0, sizeof(sensor_data));

	ret = get_sensor_data(sensor_type_accel_delay, sensor_data);
	if (ret) {
		ERR("failed to get initial delay time");
		return ret;
	}

	delay = sensor_atoi(sensor_data);
	if (delay < 0) {
		ERR("weird value is set initial delay");
		return ret;
	}

	atomic_set(&data->poll_delay, delay);

	memset(sensor_data, 0, sizeof(sensor_data));
	sensor_data[0] = '0';

	mutex_lock(&data->vs->vqlock);
	set_sensor_data(sensor_type_accel_enable, sensor_data);
	mutex_unlock(&data->vs->vqlock);
	atomic_set(&data->enable, 0);

	return ret;
}

static int create_input_device(struct maru_accel_data *data)
{
	int ret = 0;
	struct input_dev *input_data = NULL;

	input_data = input_allocate_device();
	if (input_data == NULL) {
		ERR("failed initialing input handler");
		accel_clear(data);
		return -ENOMEM;
	}

	input_data->name = SENSOR_ACCEL_INPUT_NAME;
	input_data->id.bustype = BUS_I2C;

	set_bit(EV_REL, input_data->evbit);
	set_bit(EV_SYN, input_data->evbit);
	input_set_capability(input_data, EV_REL, REL_X);
	input_set_capability(input_data, EV_REL, REL_Y);
	input_set_capability(input_data, EV_REL, REL_Z);

	data->input_data = input_data;

	ret = input_register_device(input_data);
	if (ret) {
		ERR("failed to register input data");
		accel_clear(data);
		return ret;
	}

	input_set_drvdata(input_data, data);

	ret = sysfs_create_group(&input_data->dev.kobj,
			&maru_accel_attribute_group);
	if (ret) {
		accel_clear(data);
		ERR("failed initialing devices");
		return ret;
	}

	return ret;
}

int maru_accel_init(struct virtio_sensor *vs) {
	int ret = 0;
	struct maru_accel_data *data = NULL;

	INFO("maru_accel device init starts.");

	data = kmalloc(sizeof(struct maru_accel_data), GFP_KERNEL);
	if (data == NULL) {
		ERR("failed to create accel data.");
		return -ENOMEM;
	}

	vs->accel_handle = data;
	data->vs = vs;

	INIT_DELAYED_WORK(&data->work, maru_accel_input_work_func);

	// create name & vendor
	ret = register_sensor_device(accel_sensor_device, vs,
			accel_sensor_attrs, DRIVER_ACCEL_NAME);
	if (ret) {
		ERR("failed to register accel device");
		accel_clear(data);
		return -1;
	}

#ifdef SUPPORT_LEGACY_SENSOR
		ret = l_register_sensor_device(l_accel_sensor_device, vs,
			l_accel_sensor_attrs, DRIVER_ACCEL_NAME);
	if (ret) {
		ERR("failed to register legacy accel device");
		accel_clear(data);
		return -1;
	}
#endif

	// create input
	ret = create_input_device(data);
	if (ret) {
		ERR("failed to create input device");
		return ret;
	}

	// set initial delay & enable
	ret = set_initial_value(data);
	if (ret) {
		ERR("failed to set initial value");
		return ret;
	}

	INFO("maru_accel device init ends.");

	return ret;
}

int maru_accel_exit(struct virtio_sensor *vs) {
	struct maru_accel_data *data = NULL;

	data = (struct maru_accel_data *)vs->accel_handle;
	accel_clear(data);
	INFO("maru_accel device exit ends.");
	return 0;
}
