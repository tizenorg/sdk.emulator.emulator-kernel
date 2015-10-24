/*
 * Maru Virtio UltraViolet Sensor Device Driver
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

struct maru_uv_data {
	struct input_dev *input_data;
	struct delayed_work work;

	struct virtio_sensor* vs;

	atomic_t enable;
	atomic_t poll_delay;
};

static struct device *uv_sensor_device;

static void maru_uv_input_work_func(struct work_struct *work) {

	int poll_time = 200000000;
	int enable = 0;
	int ret = 0;
	int uv = 0;
	char sensor_data[__MAX_BUF_SENSOR];
	struct maru_uv_data *data = container_of((struct delayed_work *)work,
			struct maru_uv_data, work);

	LOG(1, "maru_uv_input_work_func starts");

	memset(sensor_data, 0, __MAX_BUF_SENSOR);
	poll_time = atomic_read(&data->poll_delay);

	enable = atomic_read(&data->enable);

	if (enable) {
		mutex_lock(&data->vs->vqlock);
		ret = get_sensor_data(sensor_type_uv, sensor_data);
		mutex_unlock(&data->vs->vqlock);
		if (!ret) {
			sscanf(sensor_data, "%d", &uv);
			LOG(1, "uv_set %d", uv);

			input_report_rel(data->input_data, REL_MISC, (uv + 1));
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

	LOG(1, "maru_uv_input_work_func ends");

}

static ssize_t maru_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s", MARU_UV_DEVICE_NAME);
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
	struct maru_uv_data *data = input_get_drvdata(input_data);

	memset(sensor_data, 0, __MAX_BUF_SENSOR);
	mutex_lock(&data->vs->vqlock);
	ret = get_sensor_data(sensor_type_uv_enable, sensor_data);
	mutex_unlock(&data->vs->vqlock);
	if (ret)
		return sprintf(buf, "%d", -1);

	return sprintf(buf, "%s", sensor_data);
}

static ssize_t maru_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct input_dev *input_data = to_input_dev(dev);
	struct maru_uv_data *data = input_get_drvdata(input_data);
	int value = simple_strtoul(buf, NULL, 10);

	if (value != 0 && value != 1)
		return count;

	mutex_lock(&data->vs->vqlock);
	set_sensor_data(sensor_type_uv_enable, buf);
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
	struct maru_uv_data *data = input_get_drvdata(input_data);

	memset(sensor_data, 0, __MAX_BUF_SENSOR);
	mutex_lock(&data->vs->vqlock);
	ret = get_sensor_data(sensor_type_uv_delay, sensor_data);
	mutex_unlock(&data->vs->vqlock);
	if (ret)
		return sprintf(buf, "%d", -1);

	return sprintf(buf, "%s", sensor_data);
}

static ssize_t maru_poll_delay_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct input_dev *input_data = to_input_dev(dev);
	struct maru_uv_data *data = input_get_drvdata(input_data);
	int value = simple_strtoul(buf, NULL, 10);

	if (value < __MIN_DELAY_SENSOR)
		return count;

	mutex_lock(&data->vs->vqlock);
	set_sensor_data(sensor_type_uv_delay, buf);
	mutex_unlock(&data->vs->vqlock);
	atomic_set(&data->poll_delay, value);

	return strnlen(buf, __MAX_BUF_SENSOR);
}

static struct device_attribute dev_attr_sensor_name =
		__ATTR(name, S_IRUGO, maru_name_show, NULL);

static struct device_attribute dev_attr_sensor_vendor =
		__ATTR(vendor, S_IRUGO, maru_vendor_show, NULL);

static struct device_attribute *uv_sensor_attrs [] = {
	&dev_attr_sensor_name,
	&dev_attr_sensor_vendor,
	NULL,
};

static struct device_attribute attr_uv [] =
{
	MARU_ATTR_RW(enable),
	MARU_ATTR_RW(poll_delay),
};

static struct attribute *maru_uv_attribute[] = {
	&attr_uv[0].attr,
	&attr_uv[1].attr,
	NULL
};

static struct attribute_group maru_uv_attribute_group = {
	.attrs = maru_uv_attribute
};

static void uv_clear(struct maru_uv_data *data) {
	if (data == NULL)
		return;

	if (data->input_data) {
		sysfs_remove_group(&data->input_data->dev.kobj,
			&maru_uv_attribute_group);
		input_free_device(data->input_data);
	}

	kfree(data);
	data = NULL;
}

static int set_initial_value(struct maru_uv_data *data)
{
	int delay = 0;
	int ret = 0;
	int enable = 0;
	char sensor_data [__MAX_BUF_SENSOR];

	memset(sensor_data, 0, __MAX_BUF_SENSOR);

	mutex_lock(&data->vs->vqlock);
	ret = get_sensor_data(sensor_type_uv_delay, sensor_data);
	mutex_unlock(&data->vs->vqlock);
	if (ret) {
		ERR("failed to get initial delay time");
		return ret;
	}

	delay = sensor_atoi(sensor_data);

	mutex_lock(&data->vs->vqlock);
	ret = get_sensor_data(sensor_type_uv_enable, sensor_data);
	mutex_unlock(&data->vs->vqlock);
	if (ret) {
		ERR("failed to get initial enable");
		return ret;
	}

	enable = sensor_atoi(sensor_data);

	if (delay < 0) {
		ERR("weird value is set initial delay");
		return ret;
	}

	atomic_set(&data->poll_delay, delay);

	if (enable) {
		atomic_set(&data->enable, 1);
		schedule_delayed_work(&data->work, 0);
	}

	return ret;
}

static int create_input_device(struct maru_uv_data *data)
{
	int ret = 0;
	struct input_dev *input_data = NULL;

	input_data = input_allocate_device();
	if (input_data == NULL) {
		ERR("failed initialing input handler");
		uv_clear(data);
		return -ENOMEM;
	}

	input_data->name = SENSOR_UV_INPUT_NAME;
	input_data->id.bustype = BUS_I2C;

	set_bit(EV_REL, input_data->evbit);
	input_set_capability(input_data, EV_REL, REL_MISC);

	data->input_data = input_data;

	ret = input_register_device(input_data);
	if (ret) {
		ERR("failed to register input data");
		uv_clear(data);
		return ret;
	}

	input_set_drvdata(input_data, data);

	ret = sysfs_create_group(&input_data->dev.kobj,
			&maru_uv_attribute_group);
	if (ret) {
		uv_clear(data);
		ERR("failed initialing devices");
		return ret;
	}

	return ret;
}

int maru_uv_init(struct virtio_sensor *vs) {
	int ret = 0;
	struct maru_uv_data *data = NULL;

	INFO("maru_uv device init starts.");

	data = kmalloc(sizeof(struct maru_uv_data), GFP_KERNEL);
	if (data == NULL) {
		ERR("failed to create uv data.");
		return -ENOMEM;
	}

	vs->uv_handle = data;
	data->vs = vs;

	INIT_DELAYED_WORK(&data->work, maru_uv_input_work_func);

	// create name & vendor
	ret = register_sensor_device(uv_sensor_device, vs,
			uv_sensor_attrs, DRIVER_UV_NAME);
	if (ret) {
		ERR("failed to register uv device");
		uv_clear(data);
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

	INFO("maru_uv device init ends.");

	return ret;
}

int maru_uv_exit(struct virtio_sensor *vs) {
	struct maru_uv_data *data = NULL;

	data = (struct maru_uv_data *)vs->uv_handle;
	uv_clear(data);
	INFO("maru_uv device exit ends.");
	return 0;
}
