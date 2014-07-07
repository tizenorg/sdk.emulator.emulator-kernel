/*
 * Maru Virtio Rotation Vector Sensor Device Driver
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

#include <linux/kernel.h>
#include <linux/slab.h>

#include "maru_virtio_sensor.h"

struct maru_rotation_vector_data {
	struct input_dev *input_data;
	struct delayed_work work;
	struct mutex data_mutex;

	struct virtio_sensor* vs;

	atomic_t enable;
	atomic_t poll_delay;
};

static struct device *rotation_vector_sensor_device;

static void maru_rotation_vector_input_work_func(struct work_struct *work) {

	int poll_time = 200000000;
	int enable = 0;
	int ret = 0;
	int quad_a, quad_b, quad_c, quad_d, accuracy;
	char sensor_data[__MAX_BUF_SENSOR];
	struct maru_rotation_vector_data *data = container_of((struct delayed_work *)work,
			struct maru_rotation_vector_data, work);

	LOG(1, "maru_rotation_vector_input_work_func starts");

	memset(sensor_data, 0, __MAX_BUF_SENSOR);
	poll_time = atomic_read(&data->poll_delay);

	mutex_lock(&data->data_mutex);
	enable = atomic_read(&data->enable);
	mutex_unlock(&data->data_mutex);

	if (enable) {
		mutex_lock(&data->data_mutex);
		ret = get_sensor_data(sensor_type_rotation_vector, sensor_data);
		mutex_unlock(&data->data_mutex);
		if (!ret) {
			sscanf(sensor_data, "%d,%d,%d,%d,%d", &quad_a, &quad_b, &quad_c, &quad_d, &accuracy);
			LOG(1, "rotation_vector_set %d,%d,%d,%d,%d", quad_a, quad_b, quad_c, quad_d, accuracy);

			input_report_rel(data->input_data, REL_X, quad_a);
			input_report_rel(data->input_data, REL_Y, quad_b);
			input_report_rel(data->input_data, REL_Z, quad_c);
			input_report_rel(data->input_data, REL_RX, quad_d);
			input_report_rel(data->input_data, REL_RY, accuracy);
			input_sync(data->input_data);
		}
	}

	mutex_lock(&data->data_mutex);
	enable = atomic_read(&data->enable);
	mutex_unlock(&data->data_mutex);

	LOG(1, "enable: %d, poll_time: %d", enable, poll_time);
	if (enable) {
		if (poll_time > 0) {
			schedule_delayed_work(&data->work, nsecs_to_jiffies(poll_time));
		} else {
			schedule_delayed_work(&data->work, 0);
		}
	}

	LOG(1, "maru_rotation_vector_input_work_func ends");

}

static ssize_t maru_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s", MARU_ROTATION_DEVICE_NAME);
}

static ssize_t maru_vendor_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s", MARU_SENSOR_DEVICE_VENDOR);
}

static ssize_t maru_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return get_data_for_show(sensor_type_rotation_vector_enable, buf);
}

static ssize_t maru_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct input_dev *input_data = to_input_dev(dev);
	struct maru_rotation_vector_data *data = input_get_drvdata(input_data);
	int value = simple_strtoul(buf, NULL, 10);

	if (value != 0 && value != 1)
		return count;

	set_sensor_data(sensor_type_rotation_vector_enable, buf);

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
	return get_data_for_show(sensor_type_rotation_vector_delay, buf);
}

static ssize_t maru_poll_delay_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct input_dev *input_data = to_input_dev(dev);
	struct maru_rotation_vector_data *data = input_get_drvdata(input_data);
	int value = simple_strtoul(buf, NULL, 10);

	set_sensor_data(sensor_type_rotation_vector_delay, buf);
	atomic_set(&data->poll_delay, value);

	return strnlen(buf, __MAX_BUF_SENSOR);
}

static struct device_attribute dev_attr_sensor_name =
		__ATTR(name, S_IRUGO, maru_name_show, NULL);

static struct device_attribute dev_attr_sensor_vendor =
		__ATTR(vendor, S_IRUGO, maru_vendor_show, NULL);

static struct device_attribute *rotation_vector_sensor_attrs [] = {
	&dev_attr_sensor_name,
	&dev_attr_sensor_vendor,
	NULL,
};

static struct device_attribute attr_rotation_vector [] =
{
	MARU_ATTR_RW(enable),
	MARU_ATTR_RW(poll_delay),
};

static struct attribute *maru_rotation_vector_attribute[] = {
	&attr_rotation_vector[0].attr,
	&attr_rotation_vector[1].attr,
	NULL
};

static struct attribute_group maru_rotation_vector_attribute_group = {
	.attrs = maru_rotation_vector_attribute
};

static void rotation_vector_clear(struct maru_rotation_vector_data *data) {
	if (data == NULL)
		return;

	if (data->input_data) {
		sysfs_remove_group(&data->input_data->dev.kobj,
			&maru_rotation_vector_attribute_group);
		input_free_device(data->input_data);
	}

	kfree(data);
	data = NULL;
}

static int set_initial_value(struct maru_rotation_vector_data *data)
{
	int delay = 0;
	int ret = 0;
	char sensor_data [__MAX_BUF_SENSOR];

	memset(sensor_data, 0, __MAX_BUF_SENSOR);

	ret = get_sensor_data(sensor_type_rotation_vector_delay, sensor_data);
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
	set_sensor_data(sensor_type_rotation_vector_enable, sensor_data);
	atomic_set(&data->enable, 0);

	return ret;
}

static int create_input_device(struct maru_rotation_vector_data *data)
{
	int ret = 0;
	struct input_dev *input_data = NULL;

	input_data = input_allocate_device();
	if (input_data == NULL) {
		ERR("failed initialing input handler");
		rotation_vector_clear(data);
		return -ENOMEM;
	}

	input_data->name = SENSOR_ROTATION_INPUT_NAME;
	input_data->id.bustype = BUS_I2C;

	set_bit(EV_REL, input_data->evbit);
	set_bit(EV_SYN, input_data->evbit);
	input_set_capability(input_data, EV_REL, REL_X);
	input_set_capability(input_data, EV_REL, REL_Y);
	input_set_capability(input_data, EV_REL, REL_Z);
	input_set_capability(input_data, EV_REL, REL_RX);
	input_set_capability(input_data, EV_REL, REL_RY);

	data->input_data = input_data;

	ret = input_register_device(input_data);
	if (ret) {
		ERR("failed to register input data");
		rotation_vector_clear(data);
		return ret;
	}

	input_set_drvdata(input_data, data);

	ret = sysfs_create_group(&input_data->dev.kobj,
			&maru_rotation_vector_attribute_group);
	if (ret) {
		rotation_vector_clear(data);
		ERR("failed initialing devices");
		return ret;
	}

	return ret;
}

int maru_rotation_vector_init(struct virtio_sensor *vs) {
	int ret = 0;
	struct maru_rotation_vector_data *data = NULL;

	INFO("maru_rotation_vector device init starts.");

	data = kmalloc(sizeof(struct maru_rotation_vector_data), GFP_KERNEL);
	if (data == NULL) {
		ERR("failed to create rotation_vector data.");
		return -ENOMEM;
	}

	vs->rotation_vector_handle = data;
	data->vs = vs;

	mutex_init(&data->data_mutex);

	INIT_DELAYED_WORK(&data->work, maru_rotation_vector_input_work_func);

	// create name & vendor
	ret = register_sensor_device(rotation_vector_sensor_device, vs,
			rotation_vector_sensor_attrs, DRIVER_ROTATION_NAME);
	if (ret) {
		ERR("failed to register rotation_vector device");
		rotation_vector_clear(data);
		return -1;
	}

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

	INFO("maru_rotation_vector device init ends.");

	return ret;
}

int maru_rotation_vector_exit(struct virtio_sensor *vs) {
	struct maru_rotation_vector_data *data = NULL;

	data = (struct maru_rotation_vector_data *)vs->rotation_vector_handle;
	rotation_vector_clear(data);
	INFO("maru_rotation_vector device exit ends.");
	return 0;
}
