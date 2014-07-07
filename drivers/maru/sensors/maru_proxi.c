/*
 * Maru Virtio Proximity Sensor Device Driver
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

struct maru_proxi_data {
	struct input_dev *input_data;
	struct delayed_work work;
	struct mutex data_mutex;

	struct virtio_sensor* vs;

	atomic_t enable;
	atomic_t poll_delay;
};

static struct device *proxi_sensor_device;

#ifdef SUPPORT_LEGACY_SENSOR
static struct device *l_proxi_sensor_device;
#endif

static void maru_proxi_input_work_func(struct work_struct *work) {

	int poll_time = 200000000;
	int enable = 0;
	int ret = 0;
	int proxi = 0;
	char sensor_data[__MAX_BUF_SENSOR];
	struct maru_proxi_data *data = container_of((struct delayed_work *)work,
			struct maru_proxi_data, work);

	LOG(1, "maru_proxi_input_work_func starts");

	memset(sensor_data, 0, __MAX_BUF_SENSOR);
	poll_time = atomic_read(&data->poll_delay);

	mutex_lock(&data->data_mutex);
	enable = atomic_read(&data->enable);
	mutex_unlock(&data->data_mutex);

	if (enable) {
		mutex_lock(&data->data_mutex);
		ret = get_sensor_data(sensor_type_proxi, sensor_data);
		mutex_unlock(&data->data_mutex);
		if (!ret) {
			sscanf(sensor_data, "%d", &proxi);
			if (proxi)
				proxi = 1;
			LOG(1, "proxi_set %d", proxi);

			input_report_rel(data->input_data, ABS_DISTANCE, proxi);
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

	LOG(1, "maru_proxi_input_work_func ends");

}

static ssize_t maru_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s", MARU_PROXI_DEVICE_NAME);
}

static ssize_t maru_vendor_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s", MARU_SENSOR_DEVICE_VENDOR);
}

static ssize_t maru_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return get_data_for_show(sensor_type_proxi_enable, buf);
}

static ssize_t maru_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct input_dev *input_data = to_input_dev(dev);
	struct maru_proxi_data *data = input_get_drvdata(input_data);
	int value = simple_strtoul(buf, NULL, 10);

	if (value != 0 && value != 1)
		return count;

	set_sensor_data(sensor_type_proxi_enable, buf);

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
	return get_data_for_show(sensor_type_proxi_delay, buf);
}

static ssize_t maru_poll_delay_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct input_dev *input_data = to_input_dev(dev);
	struct maru_proxi_data *data = input_get_drvdata(input_data);
	int value = simple_strtoul(buf, NULL, 10);

	set_sensor_data(sensor_type_proxi_delay, buf);
	atomic_set(&data->poll_delay, value);

	return strnlen(buf, __MAX_BUF_SENSOR);
}

static struct device_attribute dev_attr_sensor_name =
		__ATTR(name, S_IRUGO, maru_name_show, NULL);

static struct device_attribute dev_attr_sensor_vendor =
		__ATTR(vendor, S_IRUGO, maru_vendor_show, NULL);

static struct device_attribute *proxi_sensor_attrs [] = {
	&dev_attr_sensor_name,
	&dev_attr_sensor_vendor,
	NULL,
};

#ifdef SUPPORT_LEGACY_SENSOR
#define PROXI_NAME_STR		"proxi_sim"

static ssize_t proxi_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s", PROXI_NAME_STR);
}

static ssize_t enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return get_data_for_show(sensor_type_proxi_enable, buf);
}

static ssize_t enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	set_sensor_data(sensor_type_proxi_enable, buf);
	return strnlen(buf, __MAX_BUF_SENSOR);
}

static ssize_t vo_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return get_data_for_show(sensor_type_proxi, buf);
}

static ssize_t vo_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	set_sensor_data(sensor_type_proxi, buf);
	return strnlen(buf, __MAX_BUF_SENSOR);
}

static struct device_attribute dev_attr_l_sensor_name =
		__ATTR(name, S_IRUGO, proxi_name_show, NULL);

static DEVICE_ATTR(enable, 0644, enable_show, enable_store);
static DEVICE_ATTR(vo, 0644, vo_show, vo_store);

static struct device_attribute *l_proxi_sensor_attrs [] = {
	&dev_attr_l_sensor_name,
	&dev_attr_enable,
	&dev_attr_vo,
	NULL,
};
#endif

static struct device_attribute attr_proxi [] =
{
	MARU_ATTR_RW(enable),
	MARU_ATTR_RW(poll_delay),
};

static struct attribute *maru_proxi_attribute[] = {
	&attr_proxi[0].attr,
	&attr_proxi[1].attr,
	NULL
};

static struct attribute_group maru_proxi_attribute_group = {
	.attrs = maru_proxi_attribute
};

static void proxi_clear(struct maru_proxi_data *data) {
	if (data == NULL)
		return;

	if (data->input_data) {
		sysfs_remove_group(&data->input_data->dev.kobj,
			&maru_proxi_attribute_group);
		input_free_device(data->input_data);
	}

	kfree(data);
	data = NULL;
}

static int set_initial_value(struct maru_proxi_data *data)
{
	int delay = 0;
	int ret = 0;
	int enable = 0;
	char sensor_data [__MAX_BUF_SENSOR];

	memset(sensor_data, 0, __MAX_BUF_SENSOR);

	ret = get_sensor_data(sensor_type_proxi_delay, sensor_data);
	if (ret) {
		ERR("failed to get initial delay time");
		return ret;
	}

	delay = sensor_atoi(sensor_data);

	ret = get_sensor_data(sensor_type_proxi_enable, sensor_data);
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

static int create_input_device(struct maru_proxi_data *data)
{
	int ret = 0;
	struct input_dev *input_data = NULL;

	input_data = input_allocate_device();
	if (input_data == NULL) {
		ERR("failed initialing input handler");
		proxi_clear(data);
		return -ENOMEM;
	}

	input_data->name = SENSOR_PROXI_INPUT_NAME;
	input_data->id.bustype = BUS_I2C;

	set_bit(EV_ABS, input_data->evbit);
	input_set_capability(input_data, EV_ABS, ABS_DISTANCE);

	data->input_data = input_data;

	ret = input_register_device(input_data);
	if (ret) {
		ERR("failed to register input data");
		proxi_clear(data);
		return ret;
	}

	input_set_drvdata(input_data, data);

	ret = sysfs_create_group(&input_data->dev.kobj,
			&maru_proxi_attribute_group);
	if (ret) {
		proxi_clear(data);
		ERR("failed initialing devices");
		return ret;
	}

	return ret;
}

int maru_proxi_init(struct virtio_sensor *vs) {
	int ret = 0;
	struct maru_proxi_data *data = NULL;

	INFO("maru_proxi device init starts.");

	data = kmalloc(sizeof(struct maru_proxi_data), GFP_KERNEL);
	if (data == NULL) {
		ERR("failed to create proxi data.");
		return -ENOMEM;
	}

	vs->proxi_handle = data;
	data->vs = vs;

	mutex_init(&data->data_mutex);

	INIT_DELAYED_WORK(&data->work, maru_proxi_input_work_func);

	// create name & vendor
	ret = register_sensor_device(proxi_sensor_device, vs,
			proxi_sensor_attrs, DRIVER_PROXI_NAME);
	if (ret) {
		ERR("failed to register proxi device");
		proxi_clear(data);
		return -1;
	}

#ifdef SUPPORT_LEGACY_SENSOR
		ret = l_register_sensor_device(l_proxi_sensor_device, vs,
			l_proxi_sensor_attrs, DRIVER_PROXI_NAME);
	if (ret) {
		ERR("failed to register legacy proxi device");
		proxi_clear(data);
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

	INFO("maru_proxi device init ends.");

	return ret;
}

int maru_proxi_exit(struct virtio_sensor *vs) {
	struct maru_proxi_data *data = NULL;

	data = (struct maru_proxi_data *)vs->proxi_handle;
	proxi_clear(data);
	INFO("maru_proxi device exit ends.");
	return 0;
}
