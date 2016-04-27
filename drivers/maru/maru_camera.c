/*
 * MARU Virtual Camera Driver
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 * Jinhyung Jo <jinhyung.jo@samsung.com>
 * Sangho Park <sangho1206.park@samsung.com>
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

/*
 * Some code based on vivi driver or videobuf_vmalloc.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <linux/interrupt.h>
#include <linux/videodev2.h>
#include <media/videobuf-core.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

#define MARUCAM_DEBUG_LEVEL	0

static unsigned debug;

#define MARUCAM_MODULE_NAME "marucam"

#define marucam_err(fmt, arg...) \
	printk(KERN_ERR "%s: error [%s:%d]: " fmt, MARUCAM_MODULE_NAME, \
					__func__, __LINE__, ##arg)

#define marucam_warn(fmt, arg...) \
	printk(KERN_WARNING "%s: " fmt, MARUCAM_MODULE_NAME, ##arg)

#define marucam_info(fmt, arg...) \
	printk(KERN_INFO "%s: " fmt, MARUCAM_MODULE_NAME, ##arg)

#define marucam_dbg(level, fmt, arg...) \
	do { \
		if (debug >= (level)) { \
			printk(KERN_DEBUG "%s: [%s:%d]: " fmt, \
					MARUCAM_MODULE_NAME, \
					__func__, __LINE__, ##arg); \
		} \
	} while (0)

#define MARUCAM_MAJOR_VERSION 1
#define MARUCAM_MINOR_VERSION 0
#define MARUCAM_RELEASE 2
#define MARUCAM_VERSION \
	KERNEL_VERSION(MARUCAM_MAJOR_VERSION, \
			MARUCAM_MINOR_VERSION, MARUCAM_RELEASE)

MODULE_DESCRIPTION("MARU Virtual Camera Driver");
MODULE_AUTHOR("Jinhyung Jo <jinhyung.jo@samsung.com>");
MODULE_LICENSE("GPL");

/*
 * Basic structures
 */
#define MARUCAM_INIT           0x00
#define MARUCAM_OPEN           0x04
#define MARUCAM_CLOSE          0x08
#define MARUCAM_ISR            0x0C
#define MARUCAM_STREAMON       0x10
#define MARUCAM_STREAMOFF      0x14
#define MARUCAM_S_PARM         0x18
#define MARUCAM_G_PARM         0x1C
#define MARUCAM_ENUM_FMT       0x20
#define MARUCAM_TRY_FMT        0x24
#define MARUCAM_S_FMT          0x28
#define MARUCAM_G_FMT          0x2C
#define MARUCAM_QUERYCTRL      0x30
#define MARUCAM_S_CTRL         0x34
#define MARUCAM_G_CTRL         0x38
#define MARUCAM_ENUM_FSIZES    0x3C
#define MARUCAM_ENUM_FINTV     0x40
#define MARUCAM_REQFRAME       0x44
#define MARUCAM_EXIT           0x48

enum marucam_opstate {
	S_IDLE = 0,
	S_RUNNING = 1
};

struct marucam_device {
	struct v4l2_device		v4l2_dev;

	unsigned char			dev_index;
	spinlock_t			slock;
	struct mutex			mlock;
	enum marucam_opstate		opstate;
	unsigned int			in_use;

	struct video_device		*vfd;
	struct pci_dev			*pdev;

	void __iomem			*mmregs;
	void __iomem			*args;
	resource_size_t			mem_base;
	resource_size_t			mem_size;
	resource_size_t			iomem_size;

	enum v4l2_buf_type		type;
	unsigned int			width;
	unsigned int			height;
	unsigned int			pixelformat;
	struct videobuf_queue		vb_vidq;

	struct list_head		active;
};

/*
 * Use only one instance.
 */
static struct marucam_device *marucam_instance[2];

/*
 * The code below has been modified from 'videobuf_vmalloc.c'.
 */

#define MAGIC_MARUCAM_MEM 0x18221223

#define MAGIC_CHECK(is, should)	\
	do { \
		if (unlikely((is) != (should))) { \
			marucam_err("invalid magic number:" \
				 " %x (expected %x)\n", is, should); \
			BUG(); \
		} \
	} while (0)

struct videobuf_marucam_memory {
	u32	magic;
	u32	mapped;
};

static void videobuf_vm_open(struct vm_area_struct *vma)
{
	struct videobuf_mapping *map = vma->vm_private_data;

	map->count++;
}

static void videobuf_vm_close(struct vm_area_struct *vma)
{
	int i;
	struct videobuf_mapping *map = vma->vm_private_data;
	struct videobuf_queue *q = map->q;

	map->count--;
	if (0 == map->count) {
		struct videobuf_marucam_memory *mem;

		mutex_lock(&q->vb_lock);

		if (q->streaming)
			videobuf_queue_cancel(q);

		for (i = 0; i < VIDEO_MAX_FRAME; i++) {
			if (NULL == q->bufs[i])
				continue;

			if (q->bufs[i]->map != map)
				continue;

			mem = q->bufs[i]->priv;
			if (mem) {
				MAGIC_CHECK(mem->magic, MAGIC_MARUCAM_MEM);
				mem->mapped = 0;
			}

			q->bufs[i]->map   = NULL;
			q->bufs[i]->baddr = 0;
		}

		kfree(map);

		mutex_unlock(&q->vb_lock);
	}

	return;
}

static const struct vm_operations_struct videobuf_vm_ops = {
	.open	= videobuf_vm_open,
	.close	= videobuf_vm_close,
};

static struct videobuf_buffer *__videobuf_alloc_vb(size_t size)
{
	struct videobuf_marucam_memory *mem;
	struct videobuf_buffer *vb;

	vb = kzalloc(size + sizeof(*mem), GFP_KERNEL);
	if (vb == NULL) {
		marucam_err("memory allocation failed for a video buffer\n");
		return vb;
	}

	mem = vb->priv = ((char *)vb) + size;
	mem->magic = MAGIC_MARUCAM_MEM;

	return vb;
}

static int __videobuf_iolock(struct videobuf_queue *q,
				struct videobuf_buffer *vb,
				struct v4l2_framebuffer *fbuf)
{
	struct videobuf_marucam_memory *mem = vb->priv;

	BUG_ON(!mem);

	MAGIC_CHECK(mem->magic, MAGIC_MARUCAM_MEM);

	switch (vb->memory) {
	case V4L2_MEMORY_MMAP:
		if (!mem->mapped) {
			marucam_err("memory is not mapped\n");
			return -EINVAL;
		}
		break;
	default:
		marucam_err("V4L2_MEMORY_MMAP only supported\n");
		return -EINVAL;
	}

	return 0;
}

static int __videobuf_mmap_mapper(struct videobuf_queue *q,
			struct videobuf_buffer *buf, struct vm_area_struct *vma)
{
	struct videobuf_marucam_memory *mem;
	struct videobuf_mapping *map;
	int retval, pages;

	map = kzalloc(sizeof(struct videobuf_mapping), GFP_KERNEL);
	if (NULL == map) {
		marucam_err("memory allocation failed for a video buffer mapping\n");
		return -ENOMEM;
	}

	buf->map = map;
	map->q = q;

	buf->baddr = vma->vm_start;

	mem = buf->priv;
	BUG_ON(!mem);
	mem->mapped = 1;
	MAGIC_CHECK(mem->magic, MAGIC_MARUCAM_MEM);

	pages = PAGE_ALIGN(vma->vm_end - vma->vm_start);

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	retval = remap_pfn_range(vma, vma->vm_start,
			(((struct marucam_device *)q->priv_data)->mem_base
			+ vma->vm_pgoff) >> PAGE_SHIFT,
			pages, vma->vm_page_prot);
	if (retval < 0) {
		marucam_err("remap failed: %d\n", retval);
		mem->mapped = 0;
		mem = NULL;
		kfree(map);
		return -ENOMEM;
	}

	vma->vm_ops		= &videobuf_vm_ops;
	vma->vm_flags		|= VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_private_data	= map;

	videobuf_vm_open(vma);

	return 0;
}

static struct videobuf_qtype_ops qops = {
	.magic		= MAGIC_QTYPE_OPS,
	.alloc_vb	= __videobuf_alloc_vb,
	.iolock		= __videobuf_iolock,
	.mmap_mapper	= __videobuf_mmap_mapper,
};

void videobuf_queue_marucam_init(struct videobuf_queue *q,
			 struct videobuf_queue_ops *ops,
			 void *dev,
			 spinlock_t *irqlock,
			 enum v4l2_buf_type type,
			 enum v4l2_field field,
			 unsigned int msize,
			 void *priv,
			 struct mutex *ext_lock)
{
	videobuf_queue_core_init(q, ops, dev, irqlock, type, field, msize,
				 priv, &qops, ext_lock);
}


/*
 * interrupt handling
 */

static int get_image_size(struct marucam_device *dev)
{
	int size;

	switch (dev->pixelformat) {
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_BGR24:
		size = dev->width * dev->height * 3;
		break;
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_NV12:
		size = (dev->width * dev->height * 3) / 2;
		break;
	case V4L2_PIX_FMT_YUYV:
	default:
		size = dev->width * dev->height * 2;
		break;
	}

	return size;
}

static void marucam_fillbuf(struct marucam_device *dev, uint32_t isr)
{
	struct videobuf_queue *q1 = &dev->vb_vidq;
	struct videobuf_buffer *buf = NULL;
	unsigned long flags = 0;

	spin_lock_irqsave(q1->irqlock, flags);
	if (dev->opstate != S_RUNNING) {
		marucam_err("state is not S_RUNNING\n");
		spin_unlock_irqrestore(q1->irqlock, flags);
		return;
	}
	if (list_empty(&dev->active)) {
		marucam_err("active list is empty\n");
		spin_unlock_irqrestore(q1->irqlock, flags);
		return;
	}

	buf = list_entry(dev->active.next, struct videobuf_buffer, queue);
	if (!waitqueue_active(&buf->done)) {
		marucam_err("wait queue list is empty\n");
		spin_unlock_irqrestore(q1->irqlock, flags);
		return;
	}

	list_del(&buf->queue);

	if (isr & 0x08) {
		marucam_err("invalid state\n");
		buf->state = 0xFF; /* invalid state */
	} else {
		marucam_dbg(2, "video buffer is filled\n");
		buf->state = VIDEOBUF_DONE;
	}
	do_gettimeofday(&buf->ts);
	buf->field_count++;
	wake_up_interruptible(&buf->done);
	spin_unlock_irqrestore(q1->irqlock, flags);
}

static irqreturn_t marucam_irq_handler(int irq, void *dev_id)
{
	struct marucam_device *dev = dev_id;
	uint32_t isr = 0;

	isr = ioread32(dev->mmregs + MARUCAM_ISR);
	if (!isr) {
		marucam_dbg(1, "mismatched irq\n");
		return IRQ_NONE;
	}

	marucam_fillbuf(dev, isr);
	return IRQ_HANDLED;
}

/*
 * IOCTL vidioc handling
 */
static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct marucam_device *dev = priv;

	strcpy(cap->driver, MARUCAM_MODULE_NAME);
	strcpy(cap->card, MARUCAM_MODULE_NAME);
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = MARUCAM_VERSION;
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	unsigned int ret;
	struct marucam_device *dev = priv;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		marucam_err("buf type is not V4L2_BUF_TYPE_VIDEO_CAPTURE\n");
		return -EINVAL;
	}

	mutex_lock(&dev->mlock);
	memset_io(dev->args, 0x00, dev->iomem_size);
	memcpy_toio(dev->args, (const void *)f, sizeof(struct v4l2_fmtdesc));
	iowrite32(0, dev->mmregs + MARUCAM_ENUM_FMT);
	ret = ioread32(dev->mmregs + MARUCAM_ENUM_FMT);
	if (ret) {
		if (ret != EINVAL) {
			marucam_err("enum_fmt failed: ret(%d), idx(%u)\n",
					ret, f->index);
		}
		mutex_unlock(&dev->mlock);
		return -ret;
	}
	memcpy_fromio((void *)f, dev->args, sizeof(struct v4l2_fmtdesc));
	mutex_unlock(&dev->mlock);

	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	unsigned int ret;
	struct marucam_device *dev = priv;
	struct v4l2_pix_format *pf = &f->fmt.pix;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		marucam_err("buf type is not V4L2_BUF_TYPE_VIDEO_CAPTURE\n");
		return -EINVAL;
	}

	mutex_lock(&dev->mlock);
	memset_io(dev->args, 0x00, dev->iomem_size);
	memcpy_toio(dev->args, (const void *)pf, sizeof(struct v4l2_format));
	iowrite32(0, dev->mmregs + MARUCAM_G_FMT);
	ret = ioread32(dev->mmregs + MARUCAM_G_FMT);
	if (ret) {
		marucam_err("g_fmt failed: ret(%d)\n", ret);
		mutex_unlock(&dev->mlock);
		return -ret;
	}
	memcpy_fromio((void *)pf, dev->args, sizeof(struct v4l2_format));

	dev->pixelformat	= pf->pixelformat;
	dev->width		= pf->width;
	dev->height		= pf->height;
	dev->vb_vidq.field	= pf->field;
	dev->type		= f->type;

	mutex_unlock(&dev->mlock);
	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	unsigned int ret;
	struct marucam_device *dev = priv;
	struct v4l2_pix_format *pf = &f->fmt.pix;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		marucam_err("buf type is not V4L2_BUF_TYPE_VIDEO_CAPTURE\n");
		return -EINVAL;
	}

	mutex_lock(&dev->mlock);
	memset_io(dev->args, 0x00, dev->iomem_size);
	memcpy_toio(dev->args, (const void *)pf, sizeof(struct v4l2_format));
	iowrite32(0, dev->mmregs + MARUCAM_TRY_FMT);
	ret = ioread32(dev->mmregs + MARUCAM_TRY_FMT);
	if (ret) {
		marucam_err("try_fmt failed: ret(%d), wxh(%ux%u), pf(0x%x)\n",
					ret, pf->width, pf->height,
					pf->pixelformat);
		mutex_unlock(&dev->mlock);
		return -ret;
	}
	memcpy_fromio((void *)pf, dev->args, sizeof(struct v4l2_format));
	mutex_unlock(&dev->mlock);
	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	unsigned int ret;
	struct marucam_device *dev = priv;
	struct videobuf_queue *q2 = &dev->vb_vidq;
	struct v4l2_pix_format *pf = &f->fmt.pix;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		marucam_err("buf type is not V4L2_BUF_TYPE_VIDEO_CAPTURE\n");
		return -EINVAL;
	}

	mutex_lock(&dev->mlock);
	if (dev->opstate != S_IDLE) {
		marucam_err("state is not S_IDLE\n");
		mutex_unlock(&dev->mlock);
		return -EBUSY;
	}
	mutex_lock(&q2->vb_lock);
	if (videobuf_queue_is_busy(&dev->vb_vidq)) {
		marucam_err("videobuf queue is busy\n");
		mutex_unlock(&q2->vb_lock);
		mutex_unlock(&dev->mlock);
		return -EBUSY;
	}
	mutex_unlock(&q2->vb_lock);

	memset_io(dev->args, 0x00, dev->iomem_size);
	memcpy_toio(dev->args, (const void *)pf, sizeof(struct v4l2_format));

	iowrite32(0, dev->mmregs + MARUCAM_S_FMT);
	ret = ioread32(dev->mmregs + MARUCAM_S_FMT);
	if (ret) {
		marucam_err("s_fmt failed: ret(%d), wxh(%ux%u), pf(0x%x)\n",
					ret, pf->width, pf->height,
					pf->pixelformat);
		mutex_unlock(&dev->mlock);
		return -ret;
	}
	memcpy_fromio((void *)pf, dev->args, sizeof(struct v4l2_format));

	dev->pixelformat	= pf->pixelformat;
	dev->width		= pf->width;
	dev->height		= pf->height;
	dev->vb_vidq.field	= pf->field;
	dev->type		= f->type;

	mutex_unlock(&dev->mlock);
	return 0;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	int ret;
	struct marucam_device *dev = priv;

	dev->type = p->type;

	ret = videobuf_reqbufs(&dev->vb_vidq, p);
	if (ret) {
		marucam_err("%s failed: ret(%d), count(%u), type(%u), memory(%u)\n",
					__func__, ret, p->count,
					p->type, p->memory);
	}

	return ret;
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	int ret;
	struct marucam_device *dev = priv;

	ret = videobuf_querybuf(&dev->vb_vidq, p);
	if (ret) {
		marucam_err("%s failed: ret(%d), idx(%u), type(%u), memory(%u)\n",
					__func__, ret, p->index,
					p->type, p->memory);
	}

	return ret;
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	int ret;
	struct marucam_device *dev = priv;

	ret = videobuf_qbuf(&dev->vb_vidq, p);
	if (ret) {
		marucam_err("%s failed: ret(%d), idx(%u), type(%u), memory(%u)\n",
					__func__, ret, p->index,
					p->type, p->memory);
	}

	return ret;
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	int ret;
	struct marucam_device *dev = priv;

	ret = videobuf_dqbuf(&dev->vb_vidq, p, file->f_flags & O_NONBLOCK);
	if (ret) {
		marucam_err("%s failed: ret(%d), idx(%u), type(%u), memory(%u)\n",
				__func__, ret, p->index,
				p->type, p->memory);
	}

	return ret;
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	int ret = 0;
	unsigned int dev_ret;
	struct marucam_device *dev = priv;

	if (dev->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		marucam_err("buf type is not V4L2_BUF_TYPE_VIDEO_CAPTURE\n");
		return -EINVAL;
	}
	if (i != dev->type) {
		marucam_err("mismatched buf type\n");
		return -EINVAL;
	}

	mutex_lock(&dev->mlock);
	if (dev->opstate != S_IDLE) {
		marucam_err("state is not S_IDLE\n");
		mutex_unlock(&dev->mlock);
		return -EBUSY;
	}

	memset_io(dev->args, 0x00, dev->iomem_size);
	iowrite32(0, dev->mmregs + MARUCAM_STREAMON);
	dev_ret = ioread32(dev->mmregs + MARUCAM_STREAMON);
	if (dev_ret) {
		marucam_err("stream_on failed: ret(%d)\n", dev_ret);
		mutex_unlock(&dev->mlock);
		return -dev_ret;
	}

	INIT_LIST_HEAD(&dev->active);
	ret = videobuf_streamon(&dev->vb_vidq);
	if (ret) {
		marucam_err("videobuf_streamon() failed: ret(%d)\n", ret);
		mutex_unlock(&dev->mlock);
		return ret;
	}

	dev->opstate = S_RUNNING;
	mutex_unlock(&dev->mlock);
	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	int ret = 0;
	unsigned int dev_ret;
	struct marucam_device *dev = priv;

	if (dev->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		marucam_err("buf type is not V4L2_BUF_TYPE_VIDEO_CAPTURE\n");
		return -EINVAL;
	}
	if (i != dev->type) {
		marucam_err("mismatched buf type\n");
		return -EINVAL;
	}

	mutex_lock(&dev->mlock);
	if (dev->opstate != S_RUNNING) {
		marucam_err("The device state is not S_RUNNING. Do nothing\n");
		mutex_unlock(&dev->mlock);
		return 0;
	}

	memset_io(dev->args, 0x00, dev->iomem_size);
	iowrite32(0, dev->mmregs + MARUCAM_STREAMOFF);
	dev_ret = ioread32(dev->mmregs + MARUCAM_STREAMOFF);
	if (dev_ret) {
		marucam_err("stream_off failed: ret(%d)\n", dev_ret);
		mutex_unlock(&dev->mlock);
		return -dev_ret;
	}

	dev->opstate = S_IDLE;
	ret = videobuf_streamoff(&dev->vb_vidq);
	if (ret)
		marucam_err("videobuf_streamoff() failed: ret(%d)\n", ret);

	INIT_LIST_HEAD(&dev->active);
	mutex_unlock(&dev->mlock);
	return ret;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id i)
{
	return 0;
}

static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *inp)
{
	if (inp->index != 0)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	sprintf(inp->name, "MARU Virtual Camera %u", inp->index);

	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;

	return 0;
}
static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	return 0;
}

/* controls
 *
 */
static int vidioc_queryctrl(struct file *file, void *priv,
			    struct v4l2_queryctrl *qc)
{
	unsigned int ret;
	struct marucam_device *dev = priv;

	mutex_lock(&dev->mlock);
	switch (qc->id) {
	/* we only support followed items. */
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_SHARPNESS:
		break;
	default:
		mutex_unlock(&dev->mlock);
		return -EINVAL;
	}

	memset_io(dev->args, 0x00, dev->iomem_size);
	memcpy_toio(dev->args, (const void *)qc,
			sizeof(struct v4l2_queryctrl));
	iowrite32(0, dev->mmregs + MARUCAM_QUERYCTRL);
	ret = ioread32(dev->mmregs + MARUCAM_QUERYCTRL);
	if (ret) {
		marucam_err("query_ctrl failed: ret(%d), id(%u)\n",
					ret, qc->id);
		mutex_unlock(&dev->mlock);
		return -ret;
	}
	memcpy_fromio((void *)qc, dev->args,
			sizeof(struct v4l2_queryctrl));

	mutex_unlock(&dev->mlock);
	return 0;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	unsigned int ret;
	struct marucam_device *dev = priv;

	mutex_lock(&dev->mlock);
	memset_io(dev->args, 0x00, dev->iomem_size);
	memcpy_toio(dev->args, (const void *)ctrl,
			sizeof(struct v4l2_control));
	iowrite32(0, dev->mmregs + MARUCAM_G_CTRL);
	ret = ioread32(dev->mmregs + MARUCAM_G_CTRL);
	if (ret) {
		marucam_err("g_ctrl failed: ret(%d), id(%u)\n",
					ret, ctrl->id);
		mutex_unlock(&dev->mlock);
		return -ret;
	}
	memcpy_fromio((void *)ctrl, dev->args,
			sizeof(struct v4l2_control));
	mutex_unlock(&dev->mlock);
	return 0;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	unsigned int ret;
	struct marucam_device *dev = priv;

	mutex_lock(&dev->mlock);
	memset_io(dev->args, 0x00, dev->iomem_size);
	memcpy_toio(dev->args, (const void *)ctrl,
			sizeof(struct v4l2_control));
	iowrite32(0, dev->mmregs + MARUCAM_S_CTRL);
	ret = ioread32(dev->mmregs + MARUCAM_S_CTRL);
	if (ret) {
		marucam_err("s_ctrl failed: ret(%d), id(%u), val(%d)\n",
					ret, ctrl->id, ctrl->value);
		mutex_unlock(&dev->mlock);
		return -ret;
	}
	memcpy_fromio((void *)ctrl, dev->args,
			sizeof(struct v4l2_control));
	mutex_unlock(&dev->mlock);
	return 0;
}

static int vidioc_s_parm(struct file *file, void *priv,
				struct v4l2_streamparm *parm)
{
	unsigned int ret;
	struct marucam_device *dev = priv;
	struct v4l2_captureparm *cp = &parm->parm.capture;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		marucam_err("buf type is not V4L2_BUF_TYPE_VIDEO_CAPTURE\n");
		return -EINVAL;
	}

	mutex_lock(&dev->mlock);
	memset_io(dev->args, 0x00, dev->iomem_size);
	memcpy_toio(dev->args, (const void *)cp,
			sizeof(struct v4l2_captureparm));
	iowrite32(0, dev->mmregs + MARUCAM_S_PARM);
	ret = ioread32(dev->mmregs + MARUCAM_S_PARM);
	if (ret) {
		marucam_err("s_parm failed: ret(%d)\n", ret);
		mutex_unlock(&dev->mlock);
		return -ret;
	}
	memcpy_fromio((void *)cp, dev->args,
			sizeof(struct v4l2_captureparm));
	mutex_unlock(&dev->mlock);
	return 0;
}

static int vidioc_g_parm(struct file *file, void *priv,
				struct v4l2_streamparm *parm)
{
	unsigned int ret;
	struct marucam_device *dev = priv;
	struct v4l2_captureparm *cp = &parm->parm.capture;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		marucam_err("buf type is not V4L2_BUF_TYPE_VIDEO_CAPTURE\n");
		return -EINVAL;
	}

	mutex_lock(&dev->mlock);
	memset_io(dev->args, 0x00, dev->iomem_size);
	memcpy_toio(dev->args, (const void *)cp,
			sizeof(struct v4l2_captureparm));
	iowrite32(0, dev->mmregs + MARUCAM_G_PARM);
	ret = ioread32(dev->mmregs + MARUCAM_G_PARM);
	if (ret) {
		marucam_err("g_parm failed: ret(%d)\n", ret);
		mutex_unlock(&dev->mlock);
		return -ret;
	}
	memcpy_fromio((void *)cp, dev->args,
			sizeof(struct v4l2_captureparm));
	mutex_unlock(&dev->mlock);
	return 0;
}

static int vidioc_enum_framesizes(struct file *file, void *priv,
				struct v4l2_frmsizeenum *fsize)
{
	unsigned int ret;
	struct marucam_device *dev = priv;

	mutex_lock(&dev->mlock);
	memset_io(dev->args, 0x00, dev->iomem_size);
	memcpy_toio(dev->args, (const void *)fsize,
			sizeof(struct v4l2_frmsizeenum));
	iowrite32(0, dev->mmregs + MARUCAM_ENUM_FSIZES);
	ret = ioread32(dev->mmregs + MARUCAM_ENUM_FSIZES);
	if (ret) {
		if (ret != EINVAL) {
			marucam_err("enum_framesizes failed: %d, index(%u), pix(%u)\n",
					ret, fsize->index, fsize->pixel_format);
		}
		mutex_unlock(&dev->mlock);
		return -ret;
	}
	memcpy_fromio((void *)fsize, dev->args,
			sizeof(struct v4l2_frmsizeenum));
	mutex_unlock(&dev->mlock);
	return 0;
}

static int vidioc_enum_frameintervals(struct file *file, void *priv,
				struct v4l2_frmivalenum *fival)
{
	unsigned int ret;
	struct marucam_device *dev = priv;

	mutex_lock(&dev->mlock);
	memset_io(dev->args, 0x00, dev->iomem_size);
	memcpy_toio(dev->args, (const void *)fival,
			sizeof(struct v4l2_frmivalenum));
	iowrite32(0, dev->mmregs + MARUCAM_ENUM_FINTV);
	ret = ioread32(dev->mmregs + MARUCAM_ENUM_FINTV);
	if (ret) {
		if (ret != EINVAL) {
			marucam_err("%s failed: ret(%d), idx(%u), pf(%u), %ux%u\n",
				__func__, ret, fival->index,
				fival->pixel_format, fival->width,
				fival->height);
		}
		mutex_unlock(&dev->mlock);
		return -ret;
	}
	memcpy_fromio((void *)fival, dev->args,
			sizeof(struct v4l2_frmivalenum));
	mutex_unlock(&dev->mlock);
	return 0;
}

/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/
static int buffer_setup(struct videobuf_queue *vq,
			unsigned int *count,
			unsigned int *size)
{
	struct marucam_device *dev = vq->priv_data;

	*size = get_image_size(dev);

	if (*count > 2)
		*count = 2;
	else if (*count == 0)
		*count = 2;

	marucam_dbg(1, "count=%d, size=%d\n", *count, *size);

	return 0;
}

static int buffer_prepare(struct videobuf_queue *vq,
			  struct videobuf_buffer *vb,
			  enum v4l2_field field)
{
	int rc;
	struct marucam_device *dev = vq->priv_data;

	marucam_dbg(1, "field=%d\n", field);

	vb->size = get_image_size(dev);

	if (0 != vb->baddr  &&  vb->bsize < vb->size) {
		marucam_err("invalid buffer size\n");
		return -EINVAL;
	}

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		rc = videobuf_iolock(vq, vb, NULL);
		if (rc < 0) {
			marucam_err("videobuf_iolock() failed: ret(%d)\n", rc);
			vb->state = VIDEOBUF_NEEDS_INIT;
			return rc;
		}
	}

	vb->width	= dev->width;
	vb->height	= dev->height;
	vb->field	= field;
	vb->state	= VIDEOBUF_PREPARED;

	return 0;
}

static void buffer_queue(struct videobuf_queue *vq,
			 struct videobuf_buffer *vb)
{
	struct marucam_device *dev = vq->priv_data;

	marucam_dbg(1, "\n");

	vb->state = VIDEOBUF_QUEUED;
	list_add_tail(&vb->queue, &dev->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	marucam_dbg(1, "buffer freed\n");
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static struct videobuf_queue_ops marucam_video_qops = {
	.buf_setup      = buffer_setup,
	.buf_prepare    = buffer_prepare,
	.buf_queue      = buffer_queue,
	.buf_release    = buffer_release,
};

/* ------------------------------------------------------------------
	File operations for the device
   ------------------------------------------------------------------*/

static int marucam_open(struct file *file)
{
	int ret;
	unsigned int dev_ret;
	struct marucam_device *dev = video_drvdata(file);

	file->private_data	= dev;

	mutex_lock(&dev->mlock);
	if (dev->in_use) {
		marucam_err("already opened\n");
		mutex_unlock(&dev->mlock);
		return -EBUSY;
	}

	dev->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dev->pixelformat	= 0;
	dev->width		= 0;
	dev->height		= 0;

	ret = request_irq(dev->pdev->irq, marucam_irq_handler,
				IRQF_SHARED, MARUCAM_MODULE_NAME, dev);
	if (ret) {
		marucam_err("request_irq() failed: ret(%d), irq(#%d)\n",
				ret, dev->pdev->irq);
		mutex_unlock(&dev->mlock);
		return ret;
	}

	videobuf_queue_marucam_init(&dev->vb_vidq, &marucam_video_qops,
				&dev->pdev->dev, &dev->slock, dev->type,
				V4L2_FIELD_NONE, sizeof(struct videobuf_buffer),
				dev, NULL);

	memset_io(dev->args, 0x00, dev->iomem_size);
	iowrite32(0, dev->mmregs + MARUCAM_OPEN);
	dev_ret = ioread32(dev->mmregs + MARUCAM_OPEN);
	if (dev_ret) {
		marucam_err("device open failed: ret(%d)\n", dev_ret);
		free_irq(dev->pdev->irq, dev);
		mutex_unlock(&dev->mlock);
		return -dev_ret;
	}

	dev->in_use = 1;
	mutex_unlock(&dev->mlock);
	return 0;

}

static int marucam_close(struct file *file)
{
	unsigned int ret;
	struct marucam_device *dev = file->private_data;

	mutex_lock(&dev->mlock);
	if (dev->opstate == S_RUNNING) {
		marucam_err("unexpectedly terminated\n");
		iowrite32(0, dev->mmregs + MARUCAM_STREAMOFF);
		ret = ioread32(dev->mmregs + MARUCAM_STREAMOFF);
		if (ret) {
			marucam_err("stream_off failed: ret(%d)\n", ret);
			mutex_unlock(&dev->mlock);
			return -ret;
		}

		dev->opstate = S_IDLE;
	}

	videobuf_stop(&dev->vb_vidq);
	videobuf_mmap_free(&dev->vb_vidq);
	INIT_LIST_HEAD(&dev->active);

	free_irq(dev->pdev->irq, dev);

	memset_io(dev->args, 0x00, dev->iomem_size);
	iowrite32(0, dev->mmregs + MARUCAM_CLOSE);
	ret = ioread32(dev->mmregs + MARUCAM_CLOSE);
	if (ret) {
		marucam_err("close failed: ret(%d)\n", ret);
		mutex_unlock(&dev->mlock);
		return -ret;
	}

	dev->in_use = 0;
	mutex_unlock(&dev->mlock);
	return 0;
}

static unsigned int marucam_poll(struct file *file,
				 struct poll_table_struct *wait)
{
	unsigned int rval = 0;
	struct marucam_device *poll_dev = file->private_data;
	struct videobuf_queue *q3 = &poll_dev->vb_vidq;
	struct videobuf_buffer *vbuf = NULL;

	if (q3->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return POLLERR;

	mutex_lock(&q3->vb_lock);
	if (q3->streaming) {
		if (!list_empty(&q3->stream)) {
			vbuf = list_entry(q3->stream.next,
						struct videobuf_buffer, stream);
		}
	}
	if (!vbuf) {
		marucam_err("video buffer list is empty\n");
		rval = POLLERR;
	}

	if (rval == 0) {
		poll_wait(file, &vbuf->done, wait);
		if (vbuf->state == VIDEOBUF_DONE ||
				vbuf->state == VIDEOBUF_ERROR ||
				vbuf->state == 0xFF) {
			rval = POLLIN | POLLRDNORM;
		} else {
			iowrite32(vbuf->i,
				  poll_dev->mmregs + MARUCAM_REQFRAME);
		}
	}
	mutex_unlock(&q3->vb_lock);
	return rval;
}

static int marucam_mmap(struct file *file, struct vm_area_struct *vma)
{
	int return_val;
	struct marucam_device *mmap_dev = file->private_data;

	marucam_dbg(1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	return_val = videobuf_mmap_mapper(&mmap_dev->vb_vidq, vma);

	marucam_dbg(1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		return_val);

	return return_val;
}

static const struct v4l2_ioctl_ops marucam_ioctl_ops = {
	.vidioc_querycap		= vidioc_querycap,
	.vidioc_enum_fmt_vid_cap	= vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= vidioc_s_fmt_vid_cap,
	.vidioc_reqbufs			= vidioc_reqbufs,
	.vidioc_querybuf		= vidioc_querybuf,
	.vidioc_qbuf			= vidioc_qbuf,
	.vidioc_dqbuf			= vidioc_dqbuf,
	.vidioc_s_std			= vidioc_s_std,
	.vidioc_enum_input		= vidioc_enum_input,
	.vidioc_g_input			= vidioc_g_input,
	.vidioc_s_input			= vidioc_s_input,
	.vidioc_queryctrl		= vidioc_queryctrl,
	.vidioc_g_ctrl			= vidioc_g_ctrl,
	.vidioc_s_ctrl			= vidioc_s_ctrl,
	.vidioc_streamon		= vidioc_streamon,
	.vidioc_streamoff		= vidioc_streamoff,
	.vidioc_g_parm			= vidioc_g_parm,
	.vidioc_s_parm			= vidioc_s_parm,
	.vidioc_enum_framesizes		= vidioc_enum_framesizes,
	.vidioc_enum_frameintervals	= vidioc_enum_frameintervals,
};

static const struct v4l2_file_operations marucam_fops = {
	.owner		= THIS_MODULE,
	.open		= marucam_open,
	.release	= marucam_close,
	.poll		= marucam_poll,
	.mmap		= marucam_mmap,
	.unlocked_ioctl		= video_ioctl2,
};

static struct video_device marucam_video_dev = {
	.name			= MARUCAM_MODULE_NAME,
	.fops			= &marucam_fops,
	.ioctl_ops		= &marucam_ioctl_ops,
	.minor			= -1,
	.release		= video_device_release,
};

/* -----------------------------------------------------------------
	Initialization and module stuff
   ------------------------------------------------------------------*/

static const struct pci_device_id marucam_pci_id_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_TIZEN, PCI_DEVICE_ID_VIRTUAL_CAMERA) },
	{}
};

MODULE_DEVICE_TABLE(pci, marucam_pci_id_tbl);

static int marucam_pci_initdev(struct pci_dev *pdev,
				const struct pci_device_id *id)
{
	int ret_val;
	struct marucam_device *dev;

	debug = MARUCAM_DEBUG_LEVEL;

	if (!pci_resource_len(pdev, 0)) {
		marucam_info("No available device\n");
		return -ENODEV;
	}

	if (marucam_instance[0] && marucam_instance[1]) {
		marucam_err("Two devices already exists\n");
		return -EBUSY;
	}

	dev = kzalloc(sizeof(struct marucam_device), GFP_KERNEL);
	if (!dev) {
		marucam_err("Memory allocation failed for a marucam device\n");
		return -ENOMEM;
	}

	ret_val = pci_enable_device(pdev);
	if (ret_val) {
		marucam_err("pci_enable_device failed\n");
		kfree(dev);
		return ret_val;
	}

	dev->mem_base = pci_resource_start(pdev, 0);
	dev->mem_size = pci_resource_len(pdev, 0);

	if (pci_request_region(pdev, 0, MARUCAM_MODULE_NAME)) {
		marucam_err("request region failed for 0 bar\n");
		pci_disable_device(pdev);
		kfree(dev);
		return -EBUSY;
	}

	if (pci_request_region(pdev, 1, MARUCAM_MODULE_NAME)) {
		marucam_err("request region failed for 1 bar\n");
		pci_release_region(pdev, 0);
		pci_disable_device(pdev);
		kfree(dev);
		return -EBUSY;
	}

	if (pci_request_region(pdev, 2, MARUCAM_MODULE_NAME)) {
		marucam_err("request region failed for 2 bar\n");
		pci_release_region(pdev, 0);
		pci_release_region(pdev, 1);
		pci_disable_device(pdev);
		kfree(dev);
		return -EBUSY;
	}

	dev->args = pci_ioremap_wc_bar(pdev, 1);
	if (!dev->args) {
		marucam_err("pci_ioremap_wc_bar failed for 1 bar\n");
		pci_release_region(pdev, 0);
		pci_release_region(pdev, 1);
		pci_release_region(pdev, 2);
		pci_disable_device(pdev);
		kfree(dev);
		return -EIO;
	}
	dev->iomem_size = pci_resource_len(pdev, 1);

	dev->mmregs = pci_ioremap_wc_bar(pdev, 2);
	if (!dev->mmregs) {
		marucam_err("pci_ioremap_wc_bar failed for 2 bar\n");
		iounmap(dev->args);
		pci_release_region(pdev, 0);
		pci_release_region(pdev, 1);
		pci_release_region(pdev, 2);
		pci_disable_device(pdev);
		kfree(dev);
		return -EIO;
	}

	dev->dev_index = ioread32(dev->mmregs + MARUCAM_INIT);
	marucam_info("device index is %d", dev->dev_index);
	pci_set_master(pdev);
	pci_set_drvdata(pdev, dev);

	ret_val = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret_val < 0) {
		marucam_err("v4l2_device_register() failed: %d\n", ret_val);
		iounmap(dev->args);
		iounmap(dev->mmregs);
		pci_release_region(pdev, 0);
		pci_release_region(pdev, 1);
		pci_release_region(pdev, 2);
		pci_disable_device(pdev);
		kfree(dev);
		return ret_val;
	}

	dev->vfd = video_device_alloc();
	if (dev->vfd == NULL) {
		v4l2_device_unregister(&dev->v4l2_dev);
		iounmap(dev->args);
		iounmap(dev->mmregs);
		pci_release_region(pdev, 0);
		pci_release_region(pdev, 1);
		pci_release_region(pdev, 2);
		pci_disable_device(pdev);
		kfree(dev);
		return -ENOMEM;
	}

	memcpy(dev->vfd, &marucam_video_dev, sizeof(marucam_video_dev));
	dev->vfd->dev_parent = &pdev->dev;
	dev->vfd->v4l2_dev = &dev->v4l2_dev;

	ret_val = video_register_device(dev->vfd,
						VFL_TYPE_GRABBER,
						dev->dev_index);
	if (ret_val < 0) {
		marucam_err("video_register_device() failed: %d\n", ret_val);
		video_device_release(dev->vfd);
		v4l2_device_unregister(&dev->v4l2_dev);
		iounmap(dev->args);
		iounmap(dev->mmregs);
		pci_release_region(pdev, 0);
		pci_release_region(pdev, 1);
		pci_release_region(pdev, 2);
		pci_disable_device(pdev);
		kfree(dev);
		return ret_val;
	}
	video_set_drvdata(dev->vfd, dev);

	INIT_LIST_HEAD(&dev->active);
	spin_lock_init(&dev->slock);
	mutex_init(&dev->mlock);
	dev->opstate = S_IDLE;
	dev->pdev = pdev;


	snprintf(dev->vfd->name, sizeof(dev->vfd->name), "%s (%i)",
				marucam_video_dev.name, dev->vfd->num);

	marucam_instance[dev->dev_index] = dev;
	marucam_info("Maru Camera(%u.%u.%u) device is registerd as /dev/video%d\n",
					(MARUCAM_VERSION >> 16) & 0xFF,
					(MARUCAM_VERSION >> 8) & 0xFF,
					MARUCAM_VERSION & 0xFF,
					dev->vfd->num);

	return 0;
}

static void marucam_pci_removedev(struct pci_dev *pdev)
{
	unsigned char dev_index;
	struct marucam_device *dev = pci_get_drvdata(pdev);

	if (dev == NULL) {
		marucam_warn("pci_remove on unknown pdev %p\n", pdev);
		return;
	}
	dev_index = dev->dev_index;

	video_unregister_device(dev->vfd);
	v4l2_device_unregister(&dev->v4l2_dev);
	iounmap(dev->args);
	iounmap(dev->mmregs);
	pci_release_region(dev->pdev, 0);
	pci_release_region(dev->pdev, 1);
	pci_release_region(dev->pdev, 2);
	pci_disable_device(dev->pdev);

	memset(dev, 0x00, sizeof(struct marucam_device));
	kfree(dev);
	dev = NULL;
	marucam_instance[dev_index] = NULL;
}

static struct pci_driver marucam_pci_driver = {
	.name		= MARUCAM_MODULE_NAME,
	.id_table	= marucam_pci_id_tbl,
	.probe		= marucam_pci_initdev,
	.remove		= marucam_pci_removedev,
};

static int __init marucam_init(void)
{
	return pci_register_driver(&marucam_pci_driver);
}

static void __exit marucam_exit(void)
{
	pci_unregister_driver(&marucam_pci_driver);
}

module_init(marucam_init);
module_exit(marucam_exit);
