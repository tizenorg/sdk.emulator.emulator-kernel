/*
 * Samsung Virtual Camera driver
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd All Rights Reserved
 *
 * Author : Jinhyung Jo <jinhyung.jo@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the BSD Licence, GNU General Public License
 * as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/videodev2.h>
#include <media/videobuf-vmalloc.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

#define SVCD_MODULE_NAME "svcd"

#define SVCD_MAJOR_VERSION 0
#define SVCD_MINOR_VERSION 15
#define SVCD_RELEASE 1
#define SVCD_VERSION \
	KERNEL_VERSION(SVCD_MAJOR_VERSION, SVCD_MINOR_VERSION, SVCD_RELEASE)

MODULE_DESCRIPTION("Samsung Virtual Camera Driver");
MODULE_AUTHOR("Jinhyung Jo <jinhyung.jo@samsung.com>");
MODULE_LICENSE("GPL2");

#define DFL_WIDTH	320
#define DFL_HEIGHT	240
#define VRAM_LIMIT	4

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/
#define SVCAM_CMD_INIT           0x00
#define SVCAM_CMD_OPEN           0x04
#define SVCAM_CMD_CLOSE          0x08
#define SVCAM_CMD_ISSTREAM       0x0C
#define SVCAM_CMD_START_PREVIEW  0x10
#define SVCAM_CMD_STOP_PREVIEW   0x14
#define SVCAM_CMD_S_PARAM        0x18
#define SVCAM_CMD_G_PARAM        0x1C
#define SVCAM_CMD_ENUM_FMT       0x20
#define SVCAM_CMD_TRY_FMT        0x24
#define SVCAM_CMD_S_FMT          0x28
#define SVCAM_CMD_G_FMT          0x2C
#define SVCAM_CMD_QCTRL          0x30
#define SVCAM_CMD_S_CTRL         0x34
#define SVCAM_CMD_G_CTRL         0x38
#define SVCAM_CMD_ENUM_FSIZES    0x3C
#define SVCAM_CMD_ENUM_FINTV     0x40
#define SVCAM_CMD_S_DATA         0x44
#define SVCAM_CMD_G_DATA         0x48
#define SVCAM_CMD_CLRIRQ         0x4C
#define SVCAM_CMD_DTC            0x50

/* buffer for one video frame */
struct svcd_buffer {
	struct videobuf_buffer 	vb;
	unsigned int			pixelformat;
};

struct svcd_device {
	struct v4l2_device		v4l2_dev;

	spinlock_t				slock;
	struct mutex			mlock;

	struct video_device		*vfd;
	struct pci_dev			*pdev;

	void __iomem			*mmregs;
	void __iomem			*mmmems;
	resource_size_t			io_base;
	resource_size_t			io_size;
	resource_size_t			mem_base;
	resource_size_t			mem_size;

	enum v4l2_buf_type		type;
	unsigned int			width;
	unsigned int			height;
	unsigned int 			pixelformat;
	struct videobuf_queue	vb_vidq;

	struct list_head		active;
};

static int get_image_size(struct svcd_device *dev)
{
	int size;

	switch (dev->pixelformat) {
		case V4L2_PIX_FMT_YUV420:
		case V4L2_PIX_FMT_NV12:
			size = (dev->width * dev->height * 3) /2;
			break;
		case V4L2_PIX_FMT_YUYV:
		default:
			size = dev->width * dev->height * 2;
			break;
	}

	return size;
}

static void svcd_fillbuf(struct svcd_device *dev)
{
	struct svcd_buffer *buf = NULL;
	unsigned long flags = 0;

	spin_lock_irqsave(&dev->slock, flags);
	if (list_empty(&dev->active)) {
		spin_unlock_irqrestore(&dev->slock, flags);
		return;
	}
	
	buf = list_entry(dev->active.next, struct svcd_buffer, vb.queue);
	if (!waitqueue_active(&buf->vb.done)) {
		spin_unlock_irqrestore(&dev->slock, flags);
		return;
	}
	list_del(&buf->vb.queue);

	memcpy_fromio(videobuf_to_vmalloc(&buf->vb), dev->mmmems, get_image_size(dev));

	buf->vb.state = VIDEOBUF_DONE;
	do_gettimeofday(&buf->vb.ts);
	buf->vb.field_count++;
	wake_up_interruptible(&buf->vb.done);

	spin_unlock_irqrestore(&dev->slock, flags);
}

static irqreturn_t svcd_irq_handler(int irq, void *dev_id)
{
	struct svcd_device *dev = dev_id;

	if (!ioread32(dev->mmregs + SVCAM_CMD_ISSTREAM))
		return IRQ_NONE;

	iowrite32(0, dev->mmregs + SVCAM_CMD_CLRIRQ);
	svcd_fillbuf(dev);
	return IRQ_HANDLED;
}

/* ------------------------------------------------------------------
	IOCTL vidioc handling
   ------------------------------------------------------------------*/
static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct svcd_device *dev = priv;

	strcpy(cap->driver, SVCD_MODULE_NAME);
	strcpy(cap->card, SVCD_MODULE_NAME);
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = SVCD_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct svcd_device *dev = priv;
	uint32_t ret;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	iowrite32(0, dev->mmregs + SVCAM_CMD_DTC);
	iowrite32(f->index, dev->mmregs + SVCAM_CMD_S_DATA);

	iowrite32(0, dev->mmregs + SVCAM_CMD_ENUM_FMT);
	ret = ioread32(dev->mmregs + SVCAM_CMD_ENUM_FMT);
	if (ret > 0) 
		return -(ret);

	f->index		= ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->flags		= ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->pixelformat	= ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	ioread32_rep(dev->mmregs + SVCAM_CMD_G_DATA, f->description, 8);

	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct svcd_device *dev = priv;
	uint32_t ret;

	iowrite32(0, dev->mmregs + SVCAM_CMD_DTC);
	iowrite32(0, dev->mmregs + SVCAM_CMD_G_FMT);
	ret = ioread32(dev->mmregs + SVCAM_CMD_G_FMT);
	if (ret > 0) 
		return -(ret);
	
	f->fmt.pix.width        = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.height       = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.field        = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.pixelformat  = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.bytesperline = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.sizeimage 	= ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.colorspace = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.priv = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);

	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct svcd_device *dev = priv;
	uint32_t ret;

	iowrite32(0, dev->mmregs + SVCAM_CMD_DTC);
	iowrite32(f->fmt.pix.width, dev->mmregs + SVCAM_CMD_S_DATA);
	iowrite32(f->fmt.pix.height, dev->mmregs + SVCAM_CMD_S_DATA);
	iowrite32(f->fmt.pix.pixelformat, dev->mmregs + SVCAM_CMD_S_DATA);
	iowrite32(f->fmt.pix.field, dev->mmregs + SVCAM_CMD_S_DATA);

	iowrite32(0, dev->mmregs + SVCAM_CMD_TRY_FMT);
	ret = ioread32(dev->mmregs + SVCAM_CMD_TRY_FMT);

	if (ret > 0) 
		return -(ret);

	f->fmt.pix.width        = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.height       = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.field        = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.pixelformat  = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.bytesperline = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.sizeimage 	= ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.colorspace = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.priv = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct svcd_device *dev = priv;
	struct videobuf_queue *q = &dev->vb_vidq;
	uint32_t ret;

	mutex_lock(&q->vb_lock);
	if (videobuf_queue_is_busy(&dev->vb_vidq)) {
		printk(KERN_DEBUG "svcd : %s queue busy\n", __func__);
		mutex_unlock(&q->vb_lock);
		return -EBUSY;
	}
	iowrite32(0, dev->mmregs + SVCAM_CMD_DTC);
	iowrite32(f->fmt.pix.width, dev->mmregs + SVCAM_CMD_S_DATA);
	iowrite32(f->fmt.pix.height, dev->mmregs + SVCAM_CMD_S_DATA);
	iowrite32(f->fmt.pix.pixelformat, dev->mmregs + SVCAM_CMD_S_DATA);
	iowrite32(f->fmt.pix.field, dev->mmregs + SVCAM_CMD_S_DATA);

	iowrite32(0, dev->mmregs + SVCAM_CMD_S_FMT);
	ret = ioread32(dev->mmregs + SVCAM_CMD_S_FMT);
	mutex_unlock(&q->vb_lock);

	if (ret > 0) {
		printk(KERN_ERR "svcd[%s] : SVCAM_CMD_S_FMT failed\n", __func__);
		return -(ret);
	}

	f->fmt.pix.width        = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.height       = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.field        = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.pixelformat  = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.bytesperline = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.sizeimage 	= ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.colorspace = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	f->fmt.pix.priv = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);

	dev->pixelformat = f->fmt.pix.pixelformat;
	dev->width = f->fmt.pix.width;
	dev->height = f->fmt.pix.height;
	dev->vb_vidq.field = f->fmt.pix.field;
	dev->type = f->type;
	
	return 0;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct svcd_device *dev = priv;

	dev->type = p->type;

	return (videobuf_reqbufs(&dev->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct svcd_device *dev = priv;

	return (videobuf_querybuf(&dev->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct svcd_device *dev = priv;

	return (videobuf_qbuf(&dev->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct svcd_device *dev = priv;

	return (videobuf_dqbuf(&dev->vb_vidq, p, file->f_flags & O_NONBLOCK));
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	int ret = 0;
	struct svcd_device *dev = priv;

	if (dev->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != dev->type)
		return -EINVAL;

	iowrite32(1, dev->mmregs + SVCAM_CMD_START_PREVIEW);
	ret = (int)ioread32(dev->mmregs + SVCAM_CMD_START_PREVIEW);
	if (ret) {
		printk(KERN_ERR "svcd : device streamon failed!\n");
		return -(ret);
	}

	ret = videobuf_streamon(&dev->vb_vidq);
	if (ret < 0) {
		printk(KERN_ERR "svcd : vidioc_streamon failed!, ret = %d\n", ret);
	}
	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	int ret = 0;
	struct svcd_device *dev = priv;

	if (dev->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != dev->type)
		return -EINVAL;

	iowrite32(1, dev->mmregs + SVCAM_CMD_STOP_PREVIEW);
	ret = (int)ioread32(dev->mmregs + SVCAM_CMD_STOP_PREVIEW);
	if (ret > 0) {
		printk(KERN_ERR "svcd : device streamoff failed!\n");
		return -(ret);
	}

	ret = videobuf_streamoff(&dev->vb_vidq);
	if (ret < 0) {
		printk(KERN_ERR "svcd : vidioc_streamoff failed!\n");
	}

	return ret;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id *i)
{
	return 0;
}

static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *inp)
{
	if (inp->index != 0)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	sprintf(inp->name, "Samsung Virtual Camera %u", inp->index);

	return (0);
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;

	return (0);
}
static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	return (0);
}

	/* --- controls ---------------------------------------------- */
static int vidioc_queryctrl(struct file *file, void *priv,
			    struct v4l2_queryctrl *qc)
{
	struct svcd_device *dev = priv;
	uint32_t ret;

	iowrite32(0, dev->mmregs + SVCAM_CMD_DTC);
	iowrite32(qc->id, dev->mmregs + SVCAM_CMD_S_DATA);
	
	iowrite32(0, dev->mmregs + SVCAM_CMD_QCTRL);
	ret = ioread32(dev->mmregs + SVCAM_CMD_QCTRL);
	
	if (ret > 0)
		return -(ret);

	qc->id 				= ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	qc->minimum 		= ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	qc->maximum 		= ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	qc->step 			= ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	qc->default_value 	= ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	qc->flags 			= ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	ioread32_rep(dev->mmregs + SVCAM_CMD_G_DATA, qc->name, 8);
	
	return 0;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct svcd_device *dev = priv;
	uint32_t ret;

	iowrite32(0, dev->mmregs + SVCAM_CMD_DTC);
	iowrite32(ctrl->id, dev->mmregs + SVCAM_CMD_S_DATA);

	iowrite32(0, dev->mmregs + SVCAM_CMD_G_CTRL);
	ret = ioread32(dev->mmregs + SVCAM_CMD_G_CTRL);
	
	if (ret > 0)
		return -(ret);
	
	ctrl->value = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);

	return 0;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct svcd_device *dev = priv;
	uint32_t ret;
	
	iowrite32(0, dev->mmregs + SVCAM_CMD_DTC);
	iowrite32(ctrl->id, dev->mmregs + SVCAM_CMD_S_DATA);
	iowrite32(ctrl->value, dev->mmregs + SVCAM_CMD_S_DATA);
	
	iowrite32(0, dev->mmregs + SVCAM_CMD_S_CTRL);
	ret = ioread32(dev->mmregs + SVCAM_CMD_S_CTRL);

	if (ret > 0)
		return -(ret);

	return 0;
}

static int vidioc_s_parm(struct file *file, void *priv,
				struct v4l2_streamparm *parm)
{
	struct svcd_device *dev = priv;
	struct v4l2_captureparm *cp = &parm->parm.capture;
	uint32_t ret;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	iowrite32(0, dev->mmregs + SVCAM_CMD_DTC);
	iowrite32(cp->timeperframe.numerator, dev->mmregs + SVCAM_CMD_S_DATA);
	iowrite32(cp->timeperframe.denominator, dev->mmregs + SVCAM_CMD_S_DATA);

	iowrite32(0, dev->mmregs + SVCAM_CMD_S_PARAM);
	ret = ioread32(dev->mmregs + SVCAM_CMD_S_PARAM);
	
	if (ret > 0)
		return -(ret);

	return 0;
}

static int vidioc_g_parm(struct file *file, void *priv,
				struct v4l2_streamparm *parm)
{
	struct svcd_device *dev = priv;
	struct v4l2_captureparm *cp = &parm->parm.capture;
	uint32_t ret;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	
	iowrite32(0, dev->mmregs + SVCAM_CMD_DTC);
	iowrite32(0, dev->mmregs + SVCAM_CMD_G_PARAM);
	ret = ioread32(dev->mmregs + SVCAM_CMD_G_PARAM);
	if (ret > 0) 
		return -(ret);
	
	cp->capability = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	cp->timeperframe.numerator = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	cp->timeperframe.denominator = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);

	return 0;	
}

static int vidioc_enum_framesizes(struct file *file, void *priv,
				struct v4l2_frmsizeenum *fsize)
{
	struct svcd_device *dev = priv;
	uint32_t ret;

	iowrite32(0, dev->mmregs + SVCAM_CMD_DTC);
	iowrite32(fsize->index, dev->mmregs + SVCAM_CMD_S_DATA);
	iowrite32(fsize->pixel_format, dev->mmregs + SVCAM_CMD_S_DATA);

	iowrite32(0, dev->mmregs + SVCAM_CMD_ENUM_FSIZES);
	ret = ioread32(dev->mmregs + SVCAM_CMD_ENUM_FSIZES);
	if (ret > 0)
		return -(ret);

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	fsize->discrete.height = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);

	return 0;	
}

static int vidioc_enum_frameintervals(struct file *file, void *priv,
				struct v4l2_frmivalenum *fival)
{
	struct svcd_device *dev = priv;
	uint32_t ret;

	iowrite32(0, dev->mmregs + SVCAM_CMD_DTC);
	iowrite32(fival->index, dev->mmregs + SVCAM_CMD_S_DATA);
	iowrite32(fival->pixel_format, dev->mmregs + SVCAM_CMD_S_DATA);
	iowrite32(fival->width, dev->mmregs + SVCAM_CMD_S_DATA);
	iowrite32(fival->height, dev->mmregs + SVCAM_CMD_S_DATA);

	iowrite32(0, dev->mmregs + SVCAM_CMD_ENUM_FINTV);
	ret = ioread32(dev->mmregs + SVCAM_CMD_ENUM_FINTV);
	if (ret > 0)
		return -(ret);

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete.numerator = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);
	fival->discrete.denominator = ioread32(dev->mmregs + SVCAM_CMD_G_DATA);

	return 0;
}

/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/
static void free_buffer(struct videobuf_queue *vq, struct svcd_buffer *buf)
{
	printk(KERN_DEBUG "svcd : %s, state: %i\n", __func__, buf->vb.state);

	if (in_interrupt())
		BUG();

	videobuf_vmalloc_free(&buf->vb);
	printk(KERN_DEBUG "svcd : free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

static int
buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
	struct svcd_device *dev = vq->priv_data;

	*size = get_image_size(dev);

	if (0 == *count)
		*count = 2;

	while (*size * *count > VRAM_LIMIT * 1024 * 1024)
		(*count)--;

	printk(KERN_DEBUG "svcd : %s, count=%d, size=%d\n", __func__,
		*count, *size);

	return 0;
}

static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	int rc;
	struct svcd_device *dev = vq->priv_data;

	struct svcd_buffer *buf = container_of(vb, struct svcd_buffer, vb);

	printk(KERN_DEBUG "svcd : %s, field=%d\n", __func__, field);

	buf->vb.size = get_image_size(dev);
	
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	buf->pixelformat = dev->pixelformat;
	buf->vb.width  = dev->width;
	buf->vb.height = dev->height;
	buf->vb.field  = field;

	if (buf->vb.state == VIDEOBUF_NEEDS_INIT) {
		rc = videobuf_iolock(vq, &buf->vb, NULL);
		if (rc < 0)
			goto fail;
	}

	buf->vb.state = VIDEOBUF_PREPARED;

	return 0;

fail:
	free_buffer(vq, buf);
	return rc;
}

static void
buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct svcd_buffer		*buf  = container_of(vb, struct svcd_buffer, vb);
	struct svcd_device		*dev = vq->priv_data;

	printk(KERN_DEBUG "svcd : %s\n", __func__);

	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &dev->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct svcd_buffer	*buf  = container_of(vb, struct svcd_buffer, vb);

	printk(KERN_DEBUG "svcd : %s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops svcd_video_qops = {
	.buf_setup      = buffer_setup,
	.buf_prepare    = buffer_prepare,
	.buf_queue      = buffer_queue,
	.buf_release    = buffer_release,
};

/* ------------------------------------------------------------------
	File operations for the device
   ------------------------------------------------------------------*/

static int svcd_open(struct file *file)
{
	struct svcd_device *dev = video_drvdata(file);
	int ret;

	file->private_data 	= dev;
	dev->type     		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dev->pixelformat     = V4L2_PIX_FMT_YUYV;
	dev->width    		= DFL_WIDTH;
	dev->height   		= DFL_HEIGHT;

	ret = request_irq(dev->pdev->irq, svcd_irq_handler,
				IRQF_SHARED, SVCD_MODULE_NAME, dev);
	if (ret) {
		printk(KERN_ERR "svcd : request_irq failed!!! irq num : %d\n", dev->pdev->irq);
		return ret;
	}

	videobuf_queue_vmalloc_init(&dev->vb_vidq, &svcd_video_qops,
				&dev->pdev->dev, &dev->slock, dev->type, V4L2_FIELD_INTERLACED,
				sizeof(struct svcd_buffer), dev);
	
	iowrite32(0, dev->mmregs + SVCAM_CMD_OPEN);
	ret = (int)ioread32(dev->mmregs + SVCAM_CMD_OPEN);

	if (ret > 0) {
		printk(KERN_ERR "svcd : open failed\n");
		return -ret;
	}

	return 0;
}

static int svcd_close(struct file *file)
{
	struct svcd_device *dev = file->private_data;
	uint32_t ret;

	int minor = video_devdata(file)->minor;

	videobuf_stop(&dev->vb_vidq);
	videobuf_mmap_free(&dev->vb_vidq);
	
	free_irq(dev->pdev->irq, dev);

	iowrite32(0, dev->mmregs + SVCAM_CMD_CLOSE);
	ret = ioread32(dev->mmregs + SVCAM_CMD_CLOSE);
	if (ret > 0) {
		printk(KERN_ERR "svcd : close failed\n");
		return -(ret);
	}

	printk(KERN_DEBUG "svcd : close called (minor=%d)\n", minor);

	return 0;
}

static ssize_t
svcd_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct svcd_device *dev = file->private_data;

	if (dev->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&dev->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	printk(KERN_ERR "svcd : %s, not supported uf type\n", __func__);
	return 0;
}

static unsigned int
svcd_poll(struct file *file, struct poll_table_struct *wait)
{
	struct svcd_device *dev = file->private_data;
	struct videobuf_queue *q = &dev->vb_vidq;

	unsigned int ret;

	if (dev->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return POLLERR;

	ret = videobuf_poll_stream(file, q, wait);
	return ret;
}

static int svcd_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct svcd_device *dev = file->private_data;
	int ret;

	printk(KERN_DEBUG "svcd : mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&dev->vb_vidq, vma);

	printk(KERN_DEBUG "svcd : vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_ioctl_ops svcd_ioctl_ops = {
	.vidioc_querycap			= vidioc_querycap,
	.vidioc_enum_fmt_vid_cap	= vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= vidioc_s_fmt_vid_cap,
	.vidioc_reqbufs				= vidioc_reqbufs,
	.vidioc_querybuf			= vidioc_querybuf,
	.vidioc_qbuf				= vidioc_qbuf,
	.vidioc_dqbuf				= vidioc_dqbuf,
	.vidioc_s_std				= vidioc_s_std,
	.vidioc_enum_input			= vidioc_enum_input,
	.vidioc_g_input				= vidioc_g_input,
	.vidioc_s_input				= vidioc_s_input,
	.vidioc_queryctrl			= vidioc_queryctrl,
	.vidioc_g_ctrl				= vidioc_g_ctrl,
	.vidioc_s_ctrl				= vidioc_s_ctrl,
	.vidioc_streamon			= vidioc_streamon,
	.vidioc_streamoff			= vidioc_streamoff,
	.vidioc_g_parm				= vidioc_g_parm,
	.vidioc_s_parm				= vidioc_s_parm,
	.vidioc_enum_framesizes		= vidioc_enum_framesizes,
	.vidioc_enum_frameintervals	= vidioc_enum_frameintervals,
};

static const struct v4l2_file_operations svcd_fops = {
	.owner		= THIS_MODULE,
	.open		= svcd_open,
	.release	= svcd_close,
	.read		= svcd_read,
	.poll		= svcd_poll,
	.mmap		= svcd_mmap,
	.ioctl		= video_ioctl2,
};

static struct video_device svcd_video_dev = {
	.name			= SVCD_MODULE_NAME,
	.fops			= &svcd_fops,
	.ioctl_ops		= &svcd_ioctl_ops,
	.minor			= -1,
	.release		= video_device_release,
};

/* -----------------------------------------------------------------
	Initialization and module stuff
   ------------------------------------------------------------------*/

static struct pci_device_id svcd_pci_id_tbl[] = {
	{
		.vendor		= PCI_VENDOR_ID_SAMSUNG,
		.device		= 0x1018,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	}
};

MODULE_DEVICE_TABLE(pci, svcd_pci_id_tbl);

static int svcd_pci_initdev(struct pci_dev *pdev,	const struct pci_device_id *id)
{
	int ret;
	struct svcd_device *dev;

	dev = kzalloc(sizeof(struct svcd_device), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret)
		goto out_free;

	INIT_LIST_HEAD(&dev->active);

	mutex_init(&dev->mlock);
	spin_lock_init(&dev->slock);

	dev->pdev = pdev;
	
	ret = -ENOMEM;
	dev->vfd = video_device_alloc();
	if (!dev->vfd) {
		printk(KERN_ERR "svcd : video_device_alloc() failed!!\n");
		goto out_unreg;
	}
	
	memcpy(dev->vfd, &svcd_video_dev, sizeof(svcd_video_dev));

	dev->vfd->parent = &dev->pdev->dev;
	dev->vfd->v4l2_dev = &dev->v4l2_dev;
	
	ret = pci_enable_device(dev->pdev);
	if (ret)
		goto rel_vdev;
	pci_set_master(dev->pdev);

	ret = -EIO;
	dev->mem_base = pci_resource_start(dev->pdev, 0);
	dev->mem_size = pci_resource_len(dev->pdev, 0);
	
	if (!dev->mem_base) {
		printk(KERN_ERR "svcd : pci_resource_start failed!!\n");
		goto out_disable;
	}

	if (!request_mem_region(dev->mem_base, dev->mem_size, SVCD_MODULE_NAME)) {
		printk(KERN_ERR "svcd : request_mem_region(mem) failed!!\n");
		goto out_disable;
	} 

	dev->io_base = pci_resource_start(dev->pdev, 1);
	dev->io_size = pci_resource_len(dev->pdev, 1);
	
	if (!dev->io_base) {
		printk(KERN_ERR "svcd : pci_resource_start failed!!\n");
		goto out_rel_mem_region;
	}

	if (!request_mem_region(dev->io_base, dev->io_size, SVCD_MODULE_NAME)) {
		printk(KERN_ERR "svcd : request_mem_region(io) failed!!\n");
		goto out_rel_mem_region;
	}
	
	dev->mmmems = ioremap(dev->mem_base, dev->mem_size);
	if (!dev->mmmems) {
		printk(KERN_ERR "svcd : ioremap failed!!\n");
		goto out_rel_io_region;
	}

	dev->mmregs = ioremap(dev->io_base, dev->io_size);
	if (!dev->mmregs) {
		printk(KERN_ERR "svcd : ioremap failed!!\n");
		goto out_mem_iounmap;
	}

	ret = video_register_device(dev->vfd, VFL_TYPE_GRABBER, 0);
	if (ret < 0) {
		printk(KERN_ERR "svcd : video_register_device failed!!\n");
		goto out_iounmap;
	}
	video_set_drvdata(dev->vfd, dev);
	pci_set_drvdata(pdev, dev);

	snprintf(dev->vfd->name, sizeof(dev->vfd->name), "%s (%i)", 
				svcd_video_dev.name, dev->vfd->num);

	v4l2_info(&dev->v4l2_dev, "V4L2 device registerd as /dev/video%d\n", 
				dev->vfd->num);

	return 0;

out_iounmap:
	iounmap(dev->mmregs);
out_mem_iounmap:
	iounmap(dev->mmmems);
out_rel_io_region:
	release_mem_region(dev->io_base, dev->io_size);
out_rel_mem_region:
	release_mem_region(dev->mem_base, dev->mem_size);
out_disable:
	pci_disable_device(dev->pdev);
rel_vdev:
	video_device_release(dev->vfd);
out_unreg:
	v4l2_device_unregister(&dev->v4l2_dev);
out_free:
	kfree(dev);
	
	return ret;
}

static void svcd_pci_removedev(struct pci_dev *pdev)
{
	struct svcd_device *dev = pci_get_drvdata(pdev);

	if (dev == NULL) {
		printk(KERN_WARNING "svcd : pci_remove on unknown pdev %p.\n", pdev);
		return ;
	}

	video_unregister_device(dev->vfd);
	
	if (dev->mmmems) {
		iounmap(dev->mmmems);
		dev->mmmems = 0;
	}
	if (dev->mmregs) {
		iounmap(dev->mmregs);
		dev->mmregs = 0;
	}
	
	if (dev->io_base) {
		release_mem_region(dev->io_base, dev->io_size);
		dev->io_base = 0;
	}
	if (dev->mem_base) {
		release_mem_region(dev->mem_base, dev->mem_size);
		dev->mem_base = 0;
	}
	pci_disable_device(dev->pdev);
	v4l2_device_unregister(&dev->v4l2_dev);
	kfree(dev);
}

static struct pci_driver svcd_pci_driver = {
	.name		= SVCD_MODULE_NAME,
	.id_table	= svcd_pci_id_tbl,
	.probe		= svcd_pci_initdev,
	.remove		= svcd_pci_removedev,
};

static int __init svcd_init(void)
{
	int ret = 0;
	
	ret = pci_register_driver(&svcd_pci_driver);
	if (ret < 0) {
		printk(KERN_INFO "svcd : Error %d while loading svcd driver\n", ret);
		return ret;
	}

	printk(KERN_INFO "Samsung Virtual Camera Driver ver %u.%u.%u successfully loaded.\n",
			(SVCD_VERSION >> 16) & 0xFF, (SVCD_VERSION >> 8) & 0xFF,
			SVCD_VERSION & 0xFF);

	return ret;
}

static void __exit svcd_exit(void)
{
	pci_unregister_driver(&svcd_pci_driver);
}

module_init(svcd_init);
module_exit(svcd_exit);
