/*
 * Maru Virtio Input Device Driver
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 *  SeokYeon Hwang <syeon.hwang@samsung.com>
 *  GiWoong Kim <giwoong.kim@samsung.com>
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

#ifndef _MARU_VIRTIO_H
#define _MARU_VIRTIO_H

#define MAX_BUF_COUNT 64

struct maru_virtio_device {
	struct virtio_device *vdev;
	struct virtqueue *vq;
	struct scatterlist sg[1];

	spinlock_t lock;
};

// should be called in the lock
static inline int add_new_buf(struct maru_virtio_device *mdev,
        void *buf, unsigned int buf_size)
{
    int err = 0;

    sg_init_one(mdev->sg, buf, buf_size);

    err = virtqueue_add_inbuf(mdev->vq, mdev->sg,
            1, buf, GFP_ATOMIC);
    if (err < 0) {
        return err;
	}

	return 0;
}

static inline int init_virtio_device(struct virtio_device *vdev,
        struct maru_virtio_device *mdev, vq_callback_t *c,
        void *buf, unsigned int buf_size)
{
	int index;
	int err = 0;
	unsigned long flags;

	spin_lock_init(&mdev->lock);

	mdev->vdev = vdev;

	mdev->vq = virtio_find_single_vq(mdev->vdev, c, "virtio-hwkey-vq");
	if (IS_ERR(mdev->vq)) {
		printk(KERN_ERR "failed to find virtqueue\n");
		return PTR_ERR(mdev->vq);
	}

	spin_lock_irqsave(&mdev->lock, flags);
	/* prepare the buffers */
	for (index = 0; index < MAX_BUF_COUNT; index++) {
		err = add_new_buf(mdev, buf + (index * buf_size), buf_size);
		if (err < 0) {
			spin_unlock_irqrestore(&mdev->lock, flags);
			return err;
		}
	}
	spin_unlock_irqrestore(&mdev->lock, flags);

	virtqueue_kick(mdev->vq);

    return 0;
}

static inline void deinit_virtio_device(struct virtio_device *vdev) {
	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);
}

#endif //_MARU_VIRTIO_H
