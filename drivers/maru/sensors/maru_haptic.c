/*
 * Maru Virtio Haptic Sensor Device Driver
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

struct maru_haptic_data {
	struct input_dev *input_data;

	struct virtio_sensor* vs;
};

static void haptic_clear(struct maru_haptic_data *data) {
	if (data == NULL)
		return;

	if (data->input_data) {
		input_ff_destroy(data->input_data);
		input_free_device(data->input_data);
	}

	kfree(data);
	data = NULL;
}

static int maru_upload_effect(struct input_dev *dev, struct ff_effect *effect, struct ff_effect *old)
{
	INFO("called maru_upload_effect. No work to do.");
	return 0;
}

static int maru_erase_effect(struct input_dev *dev, int effect_id)
{
	INFO("called maru_erase_effect. No work to do.");
	return 0;
}

static void maru_set_gain(struct input_dev *dev, u16 gain)
{
}

static void maru_set_autocenter(struct input_dev *dev, u16 magnitude)
{
}

static int maru_playback(struct input_dev *dev, int effect_id, int value)
{
	INFO("called maru_playback. No work to do.");
	return 0;
}

static int create_input_device(struct maru_haptic_data *data)
{
	int ret = 0;
	struct ff_device *ff;
	struct input_dev *input_data = NULL;

	input_data = input_allocate_device();
	if (input_data == NULL) {
		ERR("failed initialing input handler");
		haptic_clear(data);
		return -ENOMEM;
	}

	input_data->name = SENSOR_HAPTIC_INPUT_NAME;
	input_data->id.bustype = BUS_I2C;

	set_bit(EV_FF, input_data->evbit);
	input_set_capability(input_data, EV_FF, FF_PERIODIC);

	data->input_data = input_data;

	input_set_drvdata(input_data, data);

	ret = input_ff_create(input_data, 16);
	if (ret)
		return ret;

	set_bit(FF_SQUARE, input_data->ffbit);
	ff = input_data->ff;
	ff->upload = maru_upload_effect;
	ff->erase = maru_erase_effect;
	ff->set_gain = maru_set_gain;
	ff->set_autocenter = maru_set_autocenter;
	ff->playback = maru_playback;

	ret = input_register_device(input_data);
	if (ret) {
		ERR("failed to register input data");
		haptic_clear(data);
		return ret;
	}


	return ret;
}

int maru_haptic_init(struct virtio_sensor *vs) {
	int ret = 0;
	struct maru_haptic_data *data = NULL;

	INFO("maru_haptic device init starts.");

	data = kmalloc(sizeof(struct maru_haptic_data), GFP_KERNEL);
	if (data == NULL) {
		ERR("failed to create haptic data.");
		return -ENOMEM;
	}

	data->vs = vs;
	vs->haptic_handle = data;

	// create input
	ret = create_input_device(data);
	if (ret) {
		ERR("failed to create input device");
		return ret;
	}

	INFO("maru_haptic device init ends.");

	return ret;
}

int maru_haptic_exit(struct virtio_sensor *vs) {
	struct maru_haptic_data *data = NULL;

	data = (struct maru_haptic_data *)vs->haptic_handle;
	haptic_clear(data);
	INFO("maru_haptic device exit ends.");

	return 0;
}
