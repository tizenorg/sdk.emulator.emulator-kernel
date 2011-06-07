/*
 * Virtual Video driver - This code emulates a real video device with v4l2 api
 *
 * Copyright (C) 2011 S-core
 * Copyright (c) 2006 by:
 *      Mauro Carvalho Chehab <mchehab--a.t--infradead.org>
 *      Ted Walther <ted--a.t--enumera.com>
 *      John Sokol <sokol--a.t--videotechnology.com>
 *      http://v4l.videotechnology.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the BSD Licence, GNU General Public License
 * as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */
//#include <linux/module.h>
//#include <linux/delay.h>
//#include <linux/errno.h>
//#include <linux/fs.h>
//#include <linux/kernel.h>
//#include <linux/slab.h>
//#include <linux/mm.h>
//#include <linux/ioport.h>
//#include <linux/init.h>
//#include <linux/sched.h>
//#include <linux/pci.h>
//#include <linux/random.h>
#include <linux/version.h>
//#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
//#include <linux/kthread.h>
//#include <linux/highmem.h>
//#include <linux/freezer.h>
#include <media/videobuf-vmalloc.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
//#include "font.h"

#define SVVD_MODULE_NAME "svvd"

#define SVVD_MAJOR_VERSION 0
#define SVVD_MINOR_VERSION 6
#define SVVD_RELEASE 0
#define SVVD_VERSION \
	KERNEL_VERSION(SVVD_MAJOR_VERSION, SVVD_MINOR_VERSION, SVVD_RELEASE)

MODULE_DESCRIPTION("S-core Virtual Video Overlay Board");
MODULE_AUTHOR("Yuyeon Oh <yuyeon.oh@samsung.com>");
MODULE_LICENSE("Dual BSD/GPL");

static unsigned video_nr = -1;
module_param(video_nr, uint, 0644);
MODULE_PARM_DESC(video_nr, "videoX start number, -1 is autodetect");

static unsigned n_devs = 1;
module_param(n_devs, uint, 0644);
MODULE_PARM_DESC(n_devs, "number of video devices to create");

static unsigned debug = 1;      // debug is temporary 1
module_param(debug, uint, 0644);
MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
module_param(vid_limit, uint, 0644);
MODULE_PARM_DESC(vid_limit, "memory limit in megabytes");


#define dprintk(dev, level, fmt, arg...) \
	v4l2_dbg(level, debug, &dev->v4l2_dev, fmt, ## arg)

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/

struct svvd_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct svvd_fmt formats[] = {
	{
		.name     = "RGB888 - 24bit",
		.fourcc   = V4L2_PIX_FMT_RGB24,
		.depth    = 24,
	},
};

struct sg_to_addr {
	int pos;
	struct scatterlist *sg;
};

/* buffer for one video frame */
struct svvd_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct svvd_fmt        *fmt;
};

struct svvd_dmaqueue {
	struct list_head       active;

	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(svvd_devlist);

struct svvd_dev {
	struct list_head           svvd_devlist;
	struct v4l2_device 	   v4l2_dev;

	spinlock_t                 slock;
	struct mutex		   mutex;

	int                        users;

	/* various device info */
	struct video_device        *vfd;

	struct svvd_dmaqueue       vidq;

	/* Several counters */
	int                        h, m, s, ms;
	unsigned long              jiffies;
	char                       timestr[13];

	int			   mv_count;	/* Controls bars movement */

	/* Input Number */
	int			   input;

	// TODO: shared with qemu ?
	struct v4l2_rect           crop_overlay;
};

struct svvd_fh {
	struct svvd_dev            *dev;

	/* video capture */
	struct svvd_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
	unsigned char              bars[8][3];
	int			   input; 	/* Input Number on bars */
};

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

/* Bars and Colors should match positions */

enum colors {
	WHITE,
	AMBAR,
	CYAN,
	GREEN,
	MAGENTA,
	RED,
	BLUE,
	BLACK,
};

	/* R   G   B */
#define COLOR_WHITE	{204, 204, 204}
#define COLOR_AMBAR	{208, 208,   0}
#define COLOR_CIAN	{  0, 206, 206}
#define	COLOR_GREEN	{  0, 239,   0}
#define COLOR_MAGENTA	{239,   0, 239}
#define COLOR_RED	{205,   0,   0}
#define COLOR_BLUE	{  0,   0, 255}
#define COLOR_BLACK	{  0,   0,   0}

struct bar_std {
	u8 bar[8][3];
};

/* Maximum number of bars are 10 - otherwise, the input print code
   should be modified */
static struct bar_std bars[] = {
	{	/* Standard ITU-R color bar sequence */
		{
			COLOR_WHITE,
			COLOR_AMBAR,
			COLOR_CIAN,
			COLOR_GREEN,
			COLOR_MAGENTA,
			COLOR_RED,
			COLOR_BLUE,
			COLOR_BLACK,
		}
	}, {
		{
			COLOR_WHITE,
			COLOR_AMBAR,
			COLOR_BLACK,
			COLOR_WHITE,
			COLOR_AMBAR,
			COLOR_BLACK,
			COLOR_WHITE,
			COLOR_AMBAR,
		}
	}, {
		{
			COLOR_WHITE,
			COLOR_CIAN,
			COLOR_BLACK,
			COLOR_WHITE,
			COLOR_CIAN,
			COLOR_BLACK,
			COLOR_WHITE,
			COLOR_CIAN,
		}
	}, {
		{
			COLOR_WHITE,
			COLOR_GREEN,
			COLOR_BLACK,
			COLOR_WHITE,
			COLOR_GREEN,
			COLOR_BLACK,
			COLOR_WHITE,
			COLOR_GREEN,
		}
	},
};

#define NUM_INPUTS ARRAY_SIZE(bars)

#define TO_Y(r, g, b) \
	(((16829 * r + 33039 * g + 6416 * b  + 32768) >> 16) + 16)
/* RGB to  V(Cr) Color transform */
#define TO_V(r, g, b) \
	(((28784 * r - 24103 * g - 4681 * b  + 32768) >> 16) + 128)
/* RGB to  U(Cb) Color transform */
#define TO_U(r, g, b) \
	(((-9714 * r - 19070 * g + 28784 * b + 32768) >> 16) + 128)

/* precalculate color bar values to speed up rendering */
static void precalculate_bars(struct svvd_fh *fh)
{
	struct svvd_dev *dev = fh->dev;
	unsigned char r, g, b;
	int k, is_yuv;

	fh->input = dev->input;

	for (k = 0; k < 8; k++) {
		r = bars[fh->input].bar[k][0];
		g = bars[fh->input].bar[k][1];
		b = bars[fh->input].bar[k][2];
		is_yuv = 0;

		switch (fh->fmt->fourcc) {
		case V4L2_PIX_FMT_YUYV:
		case V4L2_PIX_FMT_UYVY:
			is_yuv = 1;
			break;
		case V4L2_PIX_FMT_RGB565:
		case V4L2_PIX_FMT_RGB565X:
			r >>= 3;
			g >>= 2;
			b >>= 3;
			break;
		case V4L2_PIX_FMT_RGB555:
		case V4L2_PIX_FMT_RGB555X:
			r >>= 3;
			g >>= 3;
			b >>= 3;
			break;
		}

		if (is_yuv) {
			fh->bars[k][0] = TO_Y(r, g, b);	/* Luma */
			fh->bars[k][1] = TO_U(r, g, b);	/* Cb */
			fh->bars[k][2] = TO_V(r, g, b);	/* Cr */
		} else {
			fh->bars[k][0] = r;
			fh->bars[k][1] = g;
			fh->bars[k][2] = b;
		}
	}

}

/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/
static int
buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
	struct svvd_fh  *fh = vq->priv_data;
	struct svvd_dev *dev  = fh->dev;

	*size = fh->width * fh->height * fh->fmt->depth / 8;	// RGB888

	if (0 == *count)
		*count = 32;

	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;

	dprintk(dev, 1, "%s, count=%d, size=%d\n", __func__,
		*count, *size);

	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct svvd_buffer *buf)
{
	struct svvd_fh  *fh = vq->priv_data;
	struct svvd_dev *dev  = fh->dev;

	dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);

	if (in_interrupt())
		BUG();

	videobuf_vmalloc_free(&buf->vb);
	dprintk(dev, 1, "free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

#define norm_maxw() 1024
#define norm_maxh() 1024
static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct svvd_fh     *fh  = vq->priv_data;
	struct svvd_dev    *dev = fh->dev;
	struct svvd_buffer *buf = container_of(vb, struct svvd_buffer, vb);
	int rc;

	dprintk(dev, 1, "%s, field=%d\n", __func__, field);

	BUG_ON(NULL == fh->fmt);

	if (fh->width  < 48 || fh->width  > norm_maxw() ||
	    fh->height < 32 || fh->height > norm_maxh())
		return -EINVAL;

	buf->vb.size = fh->width * fh->height * fh->fmt->depth / 8;	// RGB888 (24bit)
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	/* These properties only change when queue is idle, see s_fmt */
	buf->fmt       = fh->fmt;
	buf->vb.width  = fh->width;
	buf->vb.height = fh->height;
	buf->vb.field  = field;

	precalculate_bars(fh);

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
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
	struct svvd_buffer    *buf  = container_of(vb, struct svvd_buffer, vb);
	struct svvd_fh        *fh   = vq->priv_data;
	struct svvd_dev       *dev  = fh->dev;
	struct svvd_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);

	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct svvd_buffer   *buf  = container_of(vb, struct svvd_buffer, vb);
	struct svvd_fh       *fh   = vq->priv_data;
	struct svvd_dev      *dev  = (struct svvd_dev *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops svvd_video_qops = {
	.buf_setup      = buffer_setup,
	.buf_prepare    = buffer_prepare,
	.buf_queue      = buffer_queue,
	.buf_release    = buffer_release,
};

/* ------------------------------------------------------------------
	IOCTL vidioc handling
   ------------------------------------------------------------------*/
static int svvd_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct svvd_fh  *fh  = priv;
	struct svvd_dev *dev = fh->dev;

	strcpy(cap->driver, "svvd");
	strcpy(cap->card, "svvd");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = SVVD_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_OVERLAY;
	return 0;
}

static int svvd_g_fmt_vid_overlay(struct file *file, void *priv,
					struct v4l2_format *f)
{
	// TODO: 
	return 0;
}

static int svvd_s_fmt_vid_overlay(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct svvd_fh *fh = priv;
	struct svvd_dev *dev = fh->dev;

	if (f->type != V4L2_BUF_TYPE_VIDEO_OVERLAY)
	{
		printk("Crop type is (%d)\n, We support video overlay", f->type);
		return -EINVAL;
	}
	if (f->fmt.win.w.height < 0)
		return -EINVAL;
	if (f->fmt.win.w.width < 0)
		return -EINVAL;

	mutex_lock(&dev->mutex);
	dev->crop_overlay.top = f->fmt.win.w.top;
	dev->crop_overlay.left = f->fmt.win.w.left;
	dev->crop_overlay.height = f->fmt.win.w.height;
	dev->crop_overlay.width = f->fmt.win.w.width;
	mutex_unlock(&dev->mutex);

	return 0;
}

static int svvd_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct svvd_fh  *fh = priv;

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int svvd_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct svvd_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int svvd_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct svvd_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int svvd_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct svvd_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int svvdgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct svvd_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int svvd_s_std(struct file *file, void *priv, v4l2_std_id *i)
{
	return 0;
}

static int svvd_cropcap(struct file *file, void *priv,
					struct v4l2_cropcap *cap)
{
	/* nothing to do 
	cap->bounds  = dev->crop_bounds;
	cap->defrect = dev->crop_defrect;
	*/
	cap->pixelaspect.numerator   = 1; 
	cap->pixelaspect.denominator = 1; 

	return 0;
}

static int svvd_s_crop(struct file *file, void *priv,
					struct v4l2_crop *crop)
{
	struct svvd_fh *fh = priv;
	struct svvd_dev *dev = fh->dev;

	if (crop->type != V4L2_BUF_TYPE_VIDEO_OVERLAY)
	{
		printk("Crop type is (%d)\n, We support video overlay", crop->type);
		return -EINVAL;
	}
	if (crop->c.height < 0)
		return -EINVAL;
	if (crop->c.width < 0)
		return -EINVAL;

	if (crop->c.width != fh->width)
		return -EINVAL;
	if (crop->c.height != fh->height)
		return -EINVAL;		

	mutex_lock(&dev->mutex);
	dev->crop_overlay.top = crop->c.top;
	dev->crop_overlay.left = crop->c.left;
	dev->crop_overlay.height = crop->c.height;
	dev->crop_overlay.width = crop->c.width;
	mutex_unlock(&dev->mutex);

	return 0;
}

static int svvd_overlay (struct file *file, void *fh,
					unsigned int i)
{
	// TODO: qemu job
	return 0;
}

/* ------------------------------------------------------------------
	File operations for the device
   ------------------------------------------------------------------*/

static int svvd_open(struct file *file)
{
	struct svvd_dev *dev = video_drvdata(file);
	struct svvd_fh *fh = NULL;
	int retval = 0;

	mutex_lock(&dev->mutex);
	dev->users++;

	if (dev->users > 1) {
		dev->users--;
		mutex_unlock(&dev->mutex);
		return -EBUSY;
	}

	dprintk(dev, 1, "open /dev/video%d type=%s users=%d\n", dev->vfd->num,
		v4l2_type_names[V4L2_BUF_TYPE_VIDEO_OVERLAY], dev->users);

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (NULL == fh) {
		dev->users--;
		retval = -ENOMEM;
	}
	mutex_unlock(&dev->mutex);

	if (retval)
		return retval;

	file->private_data = fh;
	fh->dev      = dev;

	fh->type     = V4L2_BUF_TYPE_VIDEO_OVERLAY;
	fh->fmt      = &formats[0];
	// TODO: variable size support? getting from fb?
	fh->width    = 480;
	fh->height   = 800;

	/* Resets frame counters */
	dev->h = 0;
	dev->m = 0;
	dev->s = 0;
	dev->ms = 0;
	dev->mv_count = 0;
	dev->jiffies = jiffies;
	sprintf(dev->timestr, "%02d:%02d:%02d:%03d",
			dev->h, dev->m, dev->s, dev->ms);

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &svvd_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct svvd_buffer), fh);

	return 0;
}

static int svvd_close(struct file *file)
{
	struct svvd_fh         *fh = file->private_data;
	struct svvd_dev *dev       = fh->dev;

	int minor = video_devdata(file)->minor;

	videobuf_stop(&fh->vb_vidq);
	videobuf_mmap_free(&fh->vb_vidq);

	kfree(fh);

	mutex_lock(&dev->mutex);
	dev->users--;
	mutex_unlock(&dev->mutex);

	dprintk(dev, 1, "close called (minor=%d, users=%d)\n",
		minor, dev->users);

	return 0;
}

static int svvd_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct svvd_fh  *fh = file->private_data;
	struct svvd_dev *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations svvd_fops = {
	.owner		= THIS_MODULE,
	.open           = svvd_open,
	.release        = svvd_close,
//	.read           = svvd_read,
//	.poll		= svvd_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = svvd_mmap,
};

static const struct v4l2_ioctl_ops svvd_ioctl_ops = {
	.vidioc_querycap      = svvd_querycap,
	.vidioc_g_fmt_vid_overlay = svvd_g_fmt_vid_overlay,
	.vidioc_s_fmt_vid_overlay = svvd_s_fmt_vid_overlay,
	.vidioc_reqbufs       = svvd_reqbufs,
	.vidioc_querybuf      = svvd_querybuf,
//	.vidioc_qbuf          = svvd_qbuf,
//	.vidioc_dqbuf         = svvd_dqbuf,
//	.vidioc_s_std         = svvd_s_std,
	.vidioc_cropcap       = svvd_cropcap,
	.vidioc_s_crop        = svvd_s_crop,
	.vidioc_overlay       = svvd_overlay,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf          = svvdgmbuf,
#endif
};

static struct video_device svvd_template = {
	.name		= "svvd",
	.fops		= &svvd_fops,
	.ioctl_ops 	= &svvd_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release,

	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};

/* -----------------------------------------------------------------
	Initialization and module stuff
   ------------------------------------------------------------------*/

static int svvd_release(void)
{
	struct svvd_dev *dev;
	struct list_head *list;

	while (!list_empty(&svvd_devlist)) {
		list = svvd_devlist.next;
		list_del(list);
		dev = list_entry(list, struct svvd_dev, svvd_devlist);

		v4l2_info(&dev->v4l2_dev, "unregistering /dev/video%d\n",
			dev->vfd->num);
		video_unregister_device(dev->vfd);
		v4l2_device_unregister(&dev->v4l2_dev);
		kfree(dev);
	}

	return 0;
}

static int __init svvd_create_instance(int inst)
{
	struct svvd_dev *dev;
	struct video_device *vfd;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	snprintf(dev->v4l2_dev.name, sizeof(dev->v4l2_dev.name),
			"%s-%03d", SVVD_MODULE_NAME, inst);
	ret = v4l2_device_register(NULL, &dev->v4l2_dev);
	if (ret)
		goto free_dev;

	/* initialize locks */
	spin_lock_init(&dev->slock);
	mutex_init(&dev->mutex);

	ret = -ENOMEM;
	vfd = video_device_alloc();
	if (!vfd)
		goto unreg_dev;

	*vfd = svvd_template;
	vfd->debug = debug;

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, video_nr);
	if (ret < 0)
		goto rel_vdev;

	video_set_drvdata(vfd, dev);

	/* Now that everything is fine, let's add it to device list */
	list_add_tail(&dev->svvd_devlist, &svvd_devlist);

	snprintf(vfd->name, sizeof(vfd->name), "%s (%i)",
			svvd_template.name, vfd->num);

	if (video_nr >= 0)
		video_nr++;

	dev->vfd = vfd;
	v4l2_info(&dev->v4l2_dev, "V4L2 device registered as /dev/video%d\n",
			vfd->num);
	return 0;

rel_vdev:
	video_device_release(vfd);
unreg_dev:
	v4l2_device_unregister(&dev->v4l2_dev);
free_dev:
	kfree(dev);
	return ret;
}

/* This routine allocates from 1 to n_devs virtual drivers.

   The real maximum number of virtual drivers will depend on how many drivers
   will succeed. This is limited to the maximum number of devices that
   videodev supports, which is equal to VIDEO_NUM_DEVICES.
 */
static int __init svvd_init(void)
{
	int ret = 0, i;

	if (n_devs <= 0)
		n_devs = 1;

	for (i = 0; i < n_devs; i++) {
		ret = svvd_create_instance(i);
		if (ret) {
			/* If some instantiations succeeded, keep driver */
			if (i)
				ret = 0;
			break;
		}
	}

	if (ret < 0) {
		printk(KERN_INFO "Error %d while loading svvd driver\n", ret);
		return ret;
	}

	printk(KERN_INFO "S-core Virtual Video "
			"Overlay Board ver %u.%u.%u successfully loaded.\n",
			(SVVD_VERSION >> 16) & 0xFF, (SVVD_VERSION >> 8) & 0xFF,
			SVVD_VERSION & 0xFF);

	/* n_devs will reflect the actual number of allocated devices */
	n_devs = i;

	return ret;
}

static void __exit svvd_exit(void)
{
	svvd_release();
}

module_init(svvd_init);
module_exit(svvd_exit);
