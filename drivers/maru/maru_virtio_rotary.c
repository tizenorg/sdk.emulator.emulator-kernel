/*
 * Maru Virtio Rotary Device Driver
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 *  Jinhyung Jo <jinhyung.jo@samsung.com>
 *  Sangho Park <sangho1206.park@samsung.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 * Contributors:
 * - S-Core Co., Ltd
 *
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/input.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>

MODULE_LICENSE("GPL2");
MODULE_AUTHOR("Jinhyung Jo <jinhyung.jo@samsung.com>");
MODULE_DESCRIPTION("Emulator Virtio Rotary Driver");

#define DRIVER_NAME "tizen_detent"
#define VR_LOG(log_level, fmt, ...) \
	printk(log_level "%s: " fmt, DRIVER_NAME, ##__VA_ARGS__)

#define ROTARY_EVENT_MAX		32
struct rotary_event {
	int32_t delta;
	int32_t type;
};

#define ROTARY_EVENT_BUF_SIZE	\
	(ROTARY_EVENT_MAX * sizeof(struct rotary_event))

struct virtio_rotary {
	struct virtio_device *vdev;
	struct virtqueue *vq;
	struct input_dev *idev;
	struct mutex mutex;

	struct rotary_event event[ROTARY_EVENT_MAX];
};

struct virtio_rotary *vrtr;

static int last_pos; /* 0 ~ 360 */
static int last_detent;

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_ROTARY, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

#define DETENT_UNIT (15)
#define REMAINDER(n, div) ({ \
typeof(n) _n = (n) % (div); \
	if (_n < 0) { \
		_n += (div); \
	} \
	_n; \
})

static int add_inbuf(struct virtqueue *vq, struct rotary_event *event)
{
	struct scatterlist sg[1];
	int ret;

	memset(event, 0x00, ROTARY_EVENT_BUF_SIZE);
	sg_init_one(sg, event, ROTARY_EVENT_BUF_SIZE);

	ret = virtqueue_add_inbuf(vq, sg, 1, event, GFP_ATOMIC);
	virtqueue_kick(vq);

	return ret;
}

static int get_rotary_pos(int value)
{
	return REMAINDER(value, 360);
}

static void vq_rotary_handler(struct virtqueue *vq)
{
	int err = 0;
	struct rotary_event *data;
	unsigned int len = 0;
	size_t j, num_event;

	data = (struct rotary_event *)virtqueue_get_buf(vq, &len);
	if (!data) {
		VR_LOG(KERN_ERR, "there is no available buffer\n");
		return;
	}

	num_event = (size_t)len / sizeof(struct rotary_event);
	VR_LOG(KERN_DEBUG, "len(%u), num_event(%zu)\n", len, num_event);

	for (j = 0; j < num_event; j++) {
		int i = 0;
		int pos = 0;
		int value = 0;
		struct rotary_event event;

		memcpy(&event, &data[j],
				sizeof(struct rotary_event));

		event.delta %= 360;
		if (event.delta == 0)
			continue;

		pos = get_rotary_pos(last_pos + event.delta);

		VR_LOG(KERN_DEBUG,
			"rotary event: idx(%zu), event.delta(%d), pos(%d)\n",
			j, event.delta, pos);

		for (i = 1; i <= abs(event.delta); i++) {
			value = (event.delta > 0) ? last_pos + i : last_pos - i;
			if ((value % DETENT_UNIT) == 0) {
				input_report_rel(vrtr->idev, REL_WHEEL, 1);
				input_sync(vrtr->idev);
				if (get_rotary_pos(value) != last_detent) {
					last_detent = get_rotary_pos(value);
					if (event.delta > 0) { /* CW */
						input_report_rel(vrtr->idev,
								REL_WHEEL, 2);
					} else { /* CCW */
						input_report_rel(vrtr->idev,
								REL_WHEEL, -2);
					}
				} else {
					input_report_rel(vrtr->idev,
								REL_WHEEL, -1);
				}
				input_sync(vrtr->idev);

				VR_LOG(KERN_INFO,
					"rotary event: delta(%d), detent(%d)\n",
					event.delta, last_detent);
			}
		}
		last_pos = pos;
	}

	err = add_inbuf(vrtr->vq, vrtr->event);
	if (err < 0) {
		VR_LOG(KERN_ERR, "failed to add buffer to virtqueue\n");
		return;
	}
	virtqueue_kick(vrtr->vq);
}

static int input_rotary_open(struct input_dev *dev)
{
	VR_LOG(KERN_DEBUG, "input_rotary_open\n");
	return 0;
}

static void input_rotary_close(struct input_dev *dev)
{
	VR_LOG(KERN_DEBUG, "input_rotary_close\n");
}

static int virtio_rotary_probe(struct virtio_device *vdev)
{
	int ret = 0;

	if (vrtr) {
		VR_LOG(KERN_ERR, "driver is already exist\n");
		return -EINVAL;
	}

	vdev->priv = vrtr = kzalloc(sizeof(struct virtio_rotary), GFP_KERNEL);
	if (!vrtr)
		return -ENOMEM;

	vrtr->vdev = vdev;
	mutex_init(&vrtr->mutex);

	vrtr->vq = virtio_find_single_vq(vrtr->vdev,
							vq_rotary_handler,
							"maru-rotary-vq");
	if (IS_ERR(vrtr->vq)) {
		ret = PTR_ERR(vrtr->vq);
		kfree(vrtr);
		vdev->priv = NULL;
		return ret;
	}

	/* register for input device */
	vrtr->idev = input_allocate_device();
	if (!vrtr->idev) {
		VR_LOG(KERN_ERR, "failed to allocate a input device\n");
		kfree(vrtr);
		vdev->priv = NULL;
		return -ENOMEM;
	}

	vrtr->idev->name = DRIVER_NAME;
	vrtr->idev->dev.parent = &vdev->dev;
	vrtr->idev->id.vendor = 0x0001;
	vrtr->idev->id.product = 0x0001;
	vrtr->idev->id.version = 0x0100;

	input_set_drvdata(vrtr->idev, vrtr);
	vrtr->idev->open = input_rotary_open;
	vrtr->idev->close = input_rotary_close;

	input_set_capability(vrtr->idev, EV_REL, REL_X);
	input_set_capability(vrtr->idev, EV_REL, REL_Y);
	input_set_capability(vrtr->idev, EV_REL, REL_WHEEL);
	input_set_capability(vrtr->idev, EV_KEY, BTN_LEFT);

	ret = input_register_device(vrtr->idev);
	if (ret) {
		VR_LOG(KERN_ERR, "failed to register a input device\n");
		input_free_device(vrtr->idev);
		kfree(vrtr);
		vdev->priv = NULL;
		return ret;
	}

	ret = add_inbuf(vrtr->vq, vrtr->event);
	if (ret < 0) {
		VR_LOG(KERN_ERR, "failed to add buffer to virtqueue\n");
		input_unregister_device(vrtr->idev);
		input_free_device(vrtr->idev);
		kfree(vrtr);
		vdev->priv = NULL;
		return ret;
	}

	VR_LOG(KERN_INFO, "driver probe done\n");

	return 0;
}

static void virtio_rotary_remove(struct virtio_device *vdev)
{
	if (!vrtr) {
		VR_LOG(KERN_ERR, "rotary instance is NULL\n");
		return;
	}

	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);

	input_unregister_device(vrtr->idev);
	input_free_device(vrtr->idev);

	kfree(vrtr);
	vrtr = NULL;
	vdev->priv = NULL;
	VR_LOG(KERN_INFO, "driver is removed\n");
}

MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_rotary_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
	},
	.id_table = id_table,
	.probe = virtio_rotary_probe,
	.remove = virtio_rotary_remove,
#if 0
#ifdef CONFIG_PM
	.freeze = virtio_rotary_freeze,
	.restore = virtio_rotary_restore,
#endif
#endif
};

static int __init virtio_rotary_init(void)
{
	VR_LOG(KERN_INFO, "driver is initialized\n");
	return register_virtio_driver(&virtio_rotary_driver);
}

static void __exit virtio_rotary_exit(void)
{
	VR_LOG(KERN_INFO, "driver is destroyed\n");
	unregister_virtio_driver(&virtio_rotary_driver);
}

module_init(virtio_rotary_init);
module_exit(virtio_rotary_exit);
