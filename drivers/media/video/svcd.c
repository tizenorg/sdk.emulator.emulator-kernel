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
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/random.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/highmem.h>
#include <linux/freezer.h>
#include <media/videobuf-vmalloc.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include "font.h"

#define SVCD_MODULE_NAME "svcd"

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 40
#define WAKE_DENOMINATOR 1000
#define BUFFER_TIMEOUT     msecs_to_jiffies(1000)  /* 1 seconds */

#define SVCD_MAJOR_VERSION 0
#define SVCD_MINOR_VERSION 7
#define SVCD_RELEASE 0
#define SVCD_VERSION \
	KERNEL_VERSION(SVCD_MAJOR_VERSION, SVCD_MINOR_VERSION, SVCD_RELEASE)

MODULE_DESCRIPTION("S-Core Virtual Camera Driver");
MODULE_AUTHOR("Jinhyung Jo <jinhyung.jo@samsung.com>");
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
MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

#define DFL_WIDTH	320
#define DFL_HEIGHT	240

#define VIDEO_PATH_320x240	"/opt/home/root/test_320x240.yuv"
#define VIDEO_PATH_640x480	"/opt/home/root/test_640x480.yuv"

static struct file *video_filp;
mm_segment_t old_fs;
loff_t file_offset = 0;
loff_t file_size = 0;

/* supported controls */
static struct v4l2_queryctrl svcd_qctrl[] = {
	{
		.id            = V4L2_CID_AUDIO_VOLUME,
		.name          = "Volume",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535/100,
		.default_value = 65535,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	}, {
		.id            = V4L2_CID_BRIGHTNESS,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Brightness",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = 127,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
	}, {
		.id            = V4L2_CID_CONTRAST,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Contrast",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 0x1,
		.default_value = 0x10,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
	}, {
		.id            = V4L2_CID_SATURATION,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Saturation",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 0x1,
		.default_value = 127,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
	}, {
		.id            = V4L2_CID_HUE,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Hue",
		.minimum       = -128,
		.maximum       = 127,
		.step          = 0x1,
		.default_value = 0,
		.flags         = V4L2_CTRL_FLAG_SLIDER,
	}
};

#define dprintk(dev, level, fmt, arg...) \
	v4l2_dbg(level, debug, &dev->v4l2_dev, fmt, ## arg)

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/

struct svcd_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct svcd_fmt formats[] = {
	{
		.name     = "4:2:0, planar, YUV420",
		.fourcc   = V4L2_PIX_FMT_YUV420,
		.depth    = 16,
	},
};

static struct svcd_fmt *get_format(struct v4l2_format *f)
{
	struct svcd_fmt *fmt;
	unsigned int k;

	for (k = 0; k < ARRAY_SIZE(formats); k++) {
		fmt = &formats[k];
		if (fmt->fourcc == f->fmt.pix.pixelformat)
			break;
	}

	if (k == ARRAY_SIZE(formats))
		return NULL;

	return &formats[k];
}

struct sg_to_addr {
	int pos;
	struct scatterlist *sg;
};

/* buffer for one video frame */
struct svcd_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct svcd_fmt        *fmt;
};

struct svcd_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(svcd_devlist);

struct svcd_dev {
	struct list_head           svcd_devlist;
	struct v4l2_device 	   v4l2_dev;

	spinlock_t                 slock;
	struct mutex		   mutex;

	int                        users;

	/* various device info */
	struct video_device        *vfd;

	struct svcd_dmaqueue       vidq;

	/* Several counters */
	int                        h, m, s, ms;
	unsigned long              jiffies;
	char                       timestr[13];

	int			   mv_count;	/* Controls bars movement */

	/* Control 'registers' */
	int 			   qctl_regs[ARRAY_SIZE(svcd_qctrl)];
};

struct svcd_fh {
	struct svcd_dev            *dev;

	/* video capture */
	struct svcd_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
};

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

static void svcd_readfile(char *read_buf, int buf_size)
{
	loff_t file_offset_tmp;

	file_offset_tmp = file_offset;

	if (vfs_read(video_filp, read_buf, 
                     buf_size, &file_offset_tmp) != buf_size) {
        }
        else {
          file_offset += buf_size;
        }
        if (file_offset >= file_size) {
          file_offset = 0;
        }
}

static void svcd_fillbuff(struct svcd_fh *fh, struct svcd_buffer *buf)
{
	struct svcd_dev *dev = fh->dev;
	struct timeval ts;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);
	int buf_size = (fh->width * fh->height * 3) / 2;
	char *screen_buf = kmalloc(buf_size, GFP_KERNEL);
	
	svcd_readfile(screen_buf, buf_size);

	memcpy(vbuf, screen_buf, buf_size);
	
	kfree(screen_buf);

	dev->mv_count++;

	/* Updates stream time */

	dev->ms += jiffies_to_msecs(jiffies-dev->jiffies);
	dev->jiffies = jiffies;
	if (dev->ms >= 1000) {
		dev->ms -= 1000;
		dev->s++;
		if (dev->s >= 60) {
			dev->s -= 60;
			dev->m++;
			if (dev->m > 60) {
				dev->m -= 60;
				dev->h++;
				if (dev->h > 24)
					dev->h -= 24;
			}
		}
	}
	sprintf(dev->timestr, "%02d:%02d:%02d:%03d",
			dev->h, dev->m, dev->s, dev->ms);

	/* Advice that buffer was filled */
	buf->vb.field_count++;
	do_gettimeofday(&ts);
	buf->vb.ts = ts;
	buf->vb.state = VIDEOBUF_DONE;
}

static void svcd_thread_tick(struct svcd_fh *fh)
{
	struct svcd_buffer *buf;
	struct svcd_dev *dev = fh->dev;
	struct svcd_dmaqueue *dma_q = &dev->vidq;
	
	unsigned long flags = 0;

	dprintk(dev, 1, "Thread tick\n");

	spin_lock_irqsave(&dev->slock, flags);
	if (list_empty(&dma_q->active)) {
		dprintk(dev, 1, "No active queue to serve\n");
		goto unlock;
	}

	buf = list_entry(dma_q->active.next,
			 struct svcd_buffer, vb.queue);

	/* Nobody is waiting on this buffer, return */
	if (!waitqueue_active(&buf->vb.done))
		goto unlock;

	list_del(&buf->vb.queue);

	do_gettimeofday(&buf->vb.ts);

	/* Fill buffer */
	svcd_fillbuff(fh, buf);
	dprintk(dev, 1, "filled buffer %p\n", buf);

	wake_up(&buf->vb.done);
	dprintk(dev, 2, "[%p/%d] wakeup\n", buf, buf->vb. i);
unlock:
	spin_unlock_irqrestore(&dev->slock, flags);
	return;
}

#define frames_to_ms(frames)					\
	((frames * WAKE_NUMERATOR * 800) / WAKE_DENOMINATOR)

static void svcd_sleep(struct svcd_fh *fh)
{
	struct svcd_dev *dev = fh->dev;
	struct svcd_dmaqueue *dma_q = &dev->vidq;
	int timeout;
	DECLARE_WAITQUEUE(wait, current);

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	timeout = msecs_to_jiffies(frames_to_ms(1));

	svcd_thread_tick(fh);

	schedule_timeout_interruptible(timeout);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int svcd_thread(void *data)
{
	struct svcd_fh  *fh = data;
	struct svcd_dev *dev = fh->dev;

	dprintk(dev, 1, "thread started\n");

	set_freezable();

	for (;;) {
		svcd_sleep(fh);

		if (kthread_should_stop())
			break;
	}
	dprintk(dev, 1, "thread: exit\n");
	return 0;
}

static int svcd_start_thread(struct svcd_fh *fh)
{
	struct svcd_dev *dev = fh->dev;
	struct svcd_dmaqueue *dma_q = &dev->vidq;

	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	dprintk(dev, 1, "%s\n", __func__);

	dma_q->kthread = kthread_run(svcd_thread, fh, "svcd");

	if (IS_ERR(dma_q->kthread)) {
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void svcd_stop_thread(struct svcd_dmaqueue  *dma_q)
{
	struct svcd_dev *dev = container_of(dma_q, struct svcd_dev, vidq);

	dprintk(dev, 1, "%s\n", __func__);
	/* shutdown control thread */
	if (dma_q->kthread) {
		kthread_stop(dma_q->kthread);
		dma_q->kthread = NULL;
	}
}

/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/
static int
buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
	struct svcd_fh  *fh = vq->priv_data;
	struct svcd_dev *dev  = fh->dev;

	if (fh->fmt->fourcc == V4L2_PIX_FMT_YUV420) 
		*size = (fh->width * fh->height * 3) / 2;
	else 
		*size = fh->width * fh->height * 2;

	if (0 == *count)
		*count = 32;

	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;

	dprintk(dev, 1, "%s, count=%d, size=%d\n", __func__,
		*count, *size);

	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct svcd_buffer *buf)
{
	struct svcd_fh  *fh = vq->priv_data;
	struct svcd_dev *dev  = fh->dev;

	dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);

	if (in_interrupt())
		BUG();

	videobuf_vmalloc_free(&buf->vb);
	dprintk(dev, 1, "free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

/* FIXME : maximum size will be required */
#define norm_maxw() 1024
#define norm_maxh() 800
static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct svcd_fh     *fh  = vq->priv_data;
	struct svcd_dev    *dev = fh->dev;
	struct svcd_buffer *buf = container_of(vb, struct svcd_buffer, vb);
	int rc;

	dprintk(dev, 1, "%s, field=%d\n", __func__, field);

	BUG_ON(NULL == fh->fmt);

	if (fh->width  < 48 || fh->width  > norm_maxw() ||
	    fh->height < 32 || fh->height > norm_maxh())
		return -EINVAL;

/* 
 * YUV420(I420)'s buffer size = y(width*height) size + u(width*height/4) size + v(width*height/4) size
 * YUV422P's buffer size = y(width*height) size + u(width*height/2) size + v(width*height/2) size
 * Only two format(fourcc) would be supported.
 */
	if (fh->fmt->fourcc == V4L2_PIX_FMT_YUV420)
		buf->vb.size = (fh->width * fh->height * 3) / 2;
	else
		buf->vb.size = fh->width * fh->height * 2;
	
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	/* These properties only change when queue is idle, see s_fmt */
	buf->fmt       = fh->fmt;
	buf->vb.width  = fh->width;
	buf->vb.height = fh->height;
	buf->vb.field  = field;

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
	struct svcd_buffer    *buf  = container_of(vb, struct svcd_buffer, vb);
	struct svcd_fh        *fh   = vq->priv_data;
	struct svcd_dev       *dev  = fh->dev;
	struct svcd_dmaqueue *vidq = &dev->vidq;

	dprintk(dev, 1, "%s\n", __func__);

	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct svcd_buffer   *buf  = container_of(vb, struct svcd_buffer, vb);
	struct svcd_fh       *fh   = vq->priv_data;
	struct svcd_dev      *dev  = (struct svcd_dev *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops svcd_video_qops = {
	.buf_setup      = buffer_setup,
	.buf_prepare    = buffer_prepare,
	.buf_queue      = buffer_queue,
	.buf_release    = buffer_release,
};

/* ------------------------------------------------------------------
	IOCTL vidioc handling
   ------------------------------------------------------------------*/
static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct svcd_fh  *fh  = priv;
	struct svcd_dev *dev = fh->dev;

	strcpy(cap->driver, "svcd");
	strcpy(cap->card, "svcd");
	strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
	cap->version = SVCD_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct svcd_fmt *fmt;

	if (f->index >= ARRAY_SIZE(formats))
		return -EINVAL;

	fmt = &formats[f->index];

	strlcpy(f->description, fmt->name, sizeof(f->description));
	f->pixelformat = fmt->fourcc;
	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct svcd_fh *fh = priv;

	f->fmt.pix.width        = fh->width;
	f->fmt.pix.height       = fh->height;
	f->fmt.pix.field        = fh->vb_vidq.field;
	f->fmt.pix.pixelformat  = fh->fmt->fourcc;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fh->fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;

	return (0);
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct svcd_fh  *fh  = priv;
	struct svcd_dev *dev = fh->dev;
	struct svcd_fmt *fmt;
	enum v4l2_field field;
	unsigned int maxw, maxh;

	fmt = get_format(f);
	if (!fmt) {
		dprintk(dev, 1, "Fourcc format (0x%08x) invalid.\n",
			f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	field = f->fmt.pix.field;

	if (field == V4L2_FIELD_ANY) {
		field = V4L2_FIELD_INTERLACED;
	} else if (V4L2_FIELD_INTERLACED != field) {
		dprintk(dev, 1, "Field type invalid.\n");
		return -EINVAL;
	}

	maxw  = norm_maxw();
	maxh  = norm_maxh();

	f->fmt.pix.field = field;
	v4l_bound_align_image(&f->fmt.pix.width, 48, maxw, 2,
			      &f->fmt.pix.height, 32, maxh, 0, 0);
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

/*FIXME: This seems to be generic enough to be at videodev2 */
static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct svcd_fh *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;

	int ret = vidioc_try_fmt_vid_cap(file, fh, f);
	if (ret < 0)
		return ret;

	mutex_lock(&q->vb_lock);

	if (videobuf_queue_is_busy(&fh->vb_vidq)) {
		dprintk(fh->dev, 1, "%s queue busy\n", __func__);
		ret = -EBUSY;
		goto out;
	}

	fh->fmt           = get_format(f);
	fh->width         = f->fmt.pix.width;
	fh->height        = f->fmt.pix.height;
	fh->vb_vidq.field = f->fmt.pix.field;
	fh->type          = f->type;

	ret = 0;
out:
	mutex_unlock(&q->vb_lock);

	return ret;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct svcd_fh  *fh = priv;
	fh->type = p->type;

	/* "VIDIOC_REQBUF" ioctl called, file open in kernel mode and start thread. */
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	if (fh->width == 320 && fh->height == 240)
		video_filp = filp_open(VIDEO_PATH_320x240, O_RDONLY | O_LARGEFILE, 0);
	else if (fh->width == 640 && fh->height == 480)
		video_filp = filp_open(VIDEO_PATH_640x480, O_RDONLY | O_LARGEFILE, 0);
	else
		video_filp = filp_open(VIDEO_PATH_320x240, O_RDONLY | O_LARGEFILE, 0);
	if (IS_ERR(video_filp)) {
		dprintk(fh->dev, 1, "<camera video src> file open error!!");
		return 0;
	}

	file_offset = 0;

	file_size = i_size_read(video_filp->f_path.dentry->d_inode->i_mapping->host);
	if (file_size < 0) {
		dprintk(fh->dev, 1, "%s : unable to find file size: %s\n", __FUNCTION__, "/root/test_NNNxNNN.yuv");
		return 0;
	}
	
	svcd_start_thread(fh);

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct svcd_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct svcd_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct svcd_fh  *fh = priv;

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct svcd_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct svcd_fh  *fh = priv;

	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	return videobuf_streamon(&fh->vb_vidq);
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct svcd_fh  *fh = priv;

	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

	return videobuf_streamoff(&fh->vb_vidq);
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id *i)
{
	return 0;
}

/* only one input in this sample driver */
static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *inp)
{
	if (inp->index != 0)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	sprintf(inp->name, "Camera %u", inp->index);

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
	int i;

	for (i = 0; i < ARRAY_SIZE(svcd_qctrl); i++)
		if (qc->id && qc->id == svcd_qctrl[i].id) {
			memcpy(qc, &(svcd_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct svcd_fh *fh = priv;
	struct svcd_dev *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(svcd_qctrl); i++)
		if (ctrl->id == svcd_qctrl[i].id) {
			ctrl->value = dev->qctl_regs[i];
			return 0;
		}

	return -EINVAL;
}
static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct svcd_fh *fh = priv;
	struct svcd_dev *dev = fh->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(svcd_qctrl); i++)
		if (ctrl->id == svcd_qctrl[i].id) {
			if (ctrl->value < svcd_qctrl[i].minimum ||
			    ctrl->value > svcd_qctrl[i].maximum) {
				return -ERANGE;
			}
			dev->qctl_regs[i] = ctrl->value;
			return 0;
		}
	return -EINVAL;
}

static int vidioc_g_parm(struct file *file, void *fh,
				struct v4l2_streamparm *parm)
{
	struct v4l2_captureparm *cp = &parm->parm.capture;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	memset(cp, 0, sizeof(struct v4l2_captureparm));
	cp->capability = V4L2_CAP_TIMEPERFRAME;
	cp->timeperframe.numerator = 1;
	cp->timeperframe.denominator = 30;

	return 0;
}

static int vidioc_enum_framesizes(struct file *file, void *fh,
				struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index > 0)
		return -EINVAL;

	/* Only support V4L2_PIX_FMT_YUV420 & 320x240 size */
	switch (fsize->pixel_format) {
		case V4L2_PIX_FMT_YUV420:
			fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
			fsize->discrete.width = DFL_WIDTH;
			fsize->discrete.height = DFL_HEIGHT;
			return 0;
		default:
			return -EINVAL;
	}
}

static int vidioc_enum_frameintervals(struct file *file, void *fh,
				struct v4l2_frmivalenum *fival)
{
	if (fival->index > 1)
		return -EINVAL;

	/* Only support V4L2_PIX_FMT_YUV420 */
	if (fival->pixel_format != V4L2_PIX_FMT_YUV420)
		return -EINVAL;

	/* Only support 30 FPS */
	if ((fival->width == DFL_WIDTH) && (fival->height == DFL_HEIGHT)) {
		fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
		fival->discrete.numerator = 1;
		fival->discrete.denominator = 30;
		return 0;
	}
	return -EINVAL;
}

/* ------------------------------------------------------------------
	File operations for the device
   ------------------------------------------------------------------*/

static int svcd_open(struct file *file)
{
	struct svcd_dev *dev = video_drvdata(file);
	struct svcd_fh *fh = NULL;
	int retval = 0;

	mutex_lock(&dev->mutex);
	dev->users++;

	if (dev->users > 1) {
		dev->users--;
		mutex_unlock(&dev->mutex);
		return -EBUSY;
	}

	dprintk(dev, 1, "open /dev/video%d type=%s users=%d\n", dev->vfd->num,
		v4l2_type_names[V4L2_BUF_TYPE_VIDEO_CAPTURE], dev->users);

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

	fh->type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fh->fmt      = &formats[0];
	fh->width    = DFL_WIDTH;
	fh->height   = DFL_HEIGHT;

	/* Resets frame counters */
	dev->h = 0;
	dev->m = 0;
	dev->s = 0;
	dev->ms = 0;
	dev->mv_count = 0;
	dev->jiffies = jiffies;
	sprintf(dev->timestr, "%02d:%02d:%02d:%03d",
			dev->h, dev->m, dev->s, dev->ms);

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &svcd_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct svcd_buffer), fh);

	return 0;
}

static ssize_t
svcd_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct svcd_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
svcd_poll(struct file *file, struct poll_table_struct *wait)
{
	struct svcd_fh        *fh = file->private_data;
	struct svcd_dev       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int svcd_close(struct file *file)
{
	struct svcd_fh         *fh = file->private_data;
	struct svcd_dev *dev       = fh->dev;
	struct svcd_dmaqueue *vidq = &dev->vidq;

	int minor = video_devdata(file)->minor;

	svcd_stop_thread(vidq);
	videobuf_stop(&fh->vb_vidq);
	videobuf_mmap_free(&fh->vb_vidq);

	kfree(fh);

	mutex_lock(&dev->mutex);
	dev->users--;
	mutex_unlock(&dev->mutex);

	dprintk(dev, 1, "close called (minor=%d, users=%d)\n",
		minor, dev->users);

	if (!IS_ERR(video_filp) && video_filp != NULL)
		filp_close(video_filp, 0);
	file_offset = 0;

	set_fs(old_fs);

	return 0;
}

static int svcd_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct svcd_fh  *fh = file->private_data;
	struct svcd_dev *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct v4l2_file_operations svcd_fops = {
	.owner		= THIS_MODULE,
	.open           = svcd_open,
	.release        = svcd_close,
	.read           = svcd_read,
	.poll		= svcd_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = svcd_mmap,
};

static const struct v4l2_ioctl_ops svcd_ioctl_ops = {
	.vidioc_querycap      = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
	.vidioc_reqbufs       = vidioc_reqbufs,
	.vidioc_querybuf      = vidioc_querybuf,
	.vidioc_qbuf          = vidioc_qbuf,
	.vidioc_dqbuf         = vidioc_dqbuf,
	.vidioc_s_std         = vidioc_s_std,
	.vidioc_enum_input    = vidioc_enum_input,
	.vidioc_g_input       = vidioc_g_input,
	.vidioc_s_input       = vidioc_s_input,
	.vidioc_queryctrl     = vidioc_queryctrl,
	.vidioc_g_ctrl        = vidioc_g_ctrl,
	.vidioc_s_ctrl        = vidioc_s_ctrl,
	.vidioc_streamon      = vidioc_streamon,
	.vidioc_streamoff     = vidioc_streamoff,
	.vidioc_g_parm			= vidioc_g_parm,
	.vidioc_enum_framesizes	= vidioc_enum_framesizes,
	.vidioc_enum_frameintervals	= vidioc_enum_frameintervals,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf          = vidiocgmbuf,
#endif
};

static struct video_device svcd_template = {
	.name		= "svcd",
	.fops		= &svcd_fops,
	.ioctl_ops 	= &svcd_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release,
};

/* -----------------------------------------------------------------
	Initialization and module stuff
   ------------------------------------------------------------------*/

static int svcd_release(void)
{
	struct svcd_dev *dev;
	struct list_head *list;

	while (!list_empty(&svcd_devlist)) {
		list = svcd_devlist.next;
		list_del(list);
		dev = list_entry(list, struct svcd_dev, svcd_devlist);

		v4l2_info(&dev->v4l2_dev, "unregistering /dev/video%d\n",
			dev->vfd->num);
		video_unregister_device(dev->vfd);
		v4l2_device_unregister(&dev->v4l2_dev);
		kfree(dev);
	}

	return 0;
}

static int __init svcd_create_instance(int inst)
{
	struct svcd_dev *dev;
	struct video_device *vfd;
	int ret, i;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	snprintf(dev->v4l2_dev.name, sizeof(dev->v4l2_dev.name),
			"%s-%03d", SVCD_MODULE_NAME, inst);
	ret = v4l2_device_register(NULL, &dev->v4l2_dev);
	if (ret)
		goto free_dev;

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->vidq.active);
	init_waitqueue_head(&dev->vidq.wq);

	/* initialize locks */
	spin_lock_init(&dev->slock);
	mutex_init(&dev->mutex);

	ret = -ENOMEM;
	vfd = video_device_alloc();
	if (!vfd)
		goto unreg_dev;

	*vfd = svcd_template;
	vfd->debug = debug;

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, video_nr);
	if (ret < 0)
		goto rel_vdev;

	video_set_drvdata(vfd, dev);

	/* Set all controls to their default value. */
	for (i = 0; i < ARRAY_SIZE(svcd_qctrl); i++)
		dev->qctl_regs[i] = svcd_qctrl[i].default_value;

	/* Now that everything is fine, let's add it to device list */
	list_add_tail(&dev->svcd_devlist, &svcd_devlist);

	snprintf(vfd->name, sizeof(vfd->name), "%s (%i)",
			svcd_template.name, vfd->num);

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
static int __init svcd_init(void)
{
	int ret = 0, i;

	if (n_devs <= 0)
		n_devs = 1;

	for (i = 0; i < n_devs; i++) {
		ret = svcd_create_instance(i);
		if (ret) {
			/* If some instantiations succeeded, keep driver */
			if (i)
				ret = 0;
			break;
		}
	}

	if (ret < 0) {
		printk(KERN_INFO "Error %d while loading svcd driver\n", ret);
		return ret;
	}

	printk(KERN_INFO "S-Core Virtual Camera Board ver %u.%u.%u successfully loaded.\n",
			(SVCD_VERSION >> 16) & 0xFF, (SVCD_VERSION >> 8) & 0xFF,
			SVCD_VERSION & 0xFF);

	/* n_devs will reflect the actual number of allocated devices */
	n_devs = i;

	return ret;
}

static void __exit svcd_exit(void)
{
	svcd_release();
}

module_init(svcd_init);
module_exit(svcd_exit);
