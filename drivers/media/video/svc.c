/*
 * Samsung Virtual Camera Emulate Driver 
 * - This driver is virtual camera driver for QEMU and emulates a real camera device with v4l2 api.
 *   This code is based on vivi driver(vivi.c)
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
#include <linux/fb.h>

#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <asm/uaccess.h>

#ifdef CONFIG_VIDEO_V4L1_COMPAT
/* Include V4L1 specific functions. Should be removed soon */
#include <linux/videodev.h>
#endif
#include <linux/interrupt.h>
#include <media/videobuf-vmalloc.h>
#include <media/v4l2-common.h>
#include <linux/kthread.h>
#include <linux/highmem.h>
#include <linux/freezer.h>

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1000
#define BUFFER_TIMEOUT     msecs_to_jiffies(1000)  /* 0.5 seconds */

#include "font.h"

#define SVC_MAJOR_VERSION 0
#define SVC_MINOR_VERSION 4
#define SVC_RELEASE 0
#define SVC_VERSION \
	KERNEL_VERSION(SVC_MAJOR_VERSION, SVC_MINOR_VERSION, SVC_RELEASE)

#define DFL_WIDTH	320
#define DFL_HEIGHT	240

#define VIDEO_PATH_320x240	"/root/test_320x240.yuv"
#define VIDEO_PATH_640x480	"/root/test_640x480.yuv"
#define ENABLE_OVERLAY 1

/* Declare static vars that will be used as parameters */
static unsigned int vid_limit = 16;	/* Video memory limit, in Mb */
static int video_nr = -1;		/* /dev/videoN, -1 for autodetect */
static int n_devs = 1;			/* Number of virtual devices */

static struct file *video_filp;
mm_segment_t old_fs;
loff_t file_offset = 0;
loff_t file_size = 0;

/* supported controls */
static struct v4l2_queryctrl svc_qctrl[] = {
	{
		.id            = V4L2_CID_AUDIO_VOLUME,
		.name          = "Volume",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535/100,
		.default_value = 65535,
		.flags         = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	}, {
		.id            = V4L2_CID_BRIGHTNESS,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Brightness",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = 127,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_CONTRAST,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Contrast",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 0x1,
		.default_value = 0x10,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_SATURATION,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Saturation",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 0x1,
		.default_value = 127,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_HUE,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Hue",
		.minimum       = -128,
		.maximum       = 127,
		.step          = 0x1,
		.default_value = 0,
		.flags         = 0,
	}
};

static int qctl_regs[ARRAY_SIZE(svc_qctrl)];

#define dprintk(dev, level, fmt, arg...)				\
	do {								\
		if (dev->vfd->debug >= (level))				\
			printk(KERN_DEBUG "svb : " fmt , ## arg);	\
	} while (0)

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/

struct svc_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct svc_fmt format = {
	.name     = "4:2:0, planar, YUV420",
	.fourcc   = V4L2_PIX_FMT_YUV420,
	.depth    = 16,
};

struct sg_to_addr {
	int pos;
	struct scatterlist *sg;
};

/* buffer for one video frame */
struct svc_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct svc_fmt        *fmt;
	int                   videoid;
	int                   overlay_drawn;
};

struct svc_dmaqueue {
	struct list_head       active;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	struct task_struct         *svc_kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(svc_devlist);

struct svc_dev {
	struct list_head           svc_devlist;
	struct v4l2_rect           crop_current; 
	struct v4l2_rect           crop_overlay; 

	spinlock_t                 slock;
	struct mutex		   mutex;

	int                        users;

	/* various device info */
	struct video_device        *vfd;

	struct svc_dmaqueue       vidq;

	/* Several counters */
	int                        h, m, s, ms;
	unsigned long              jiffies;
	char                       timestr[13];

	int			   mv_count;
};

struct svc_fh {
	struct svc_dev            *dev;

	/* video capture */
	struct svc_fmt            *fmt;
	unsigned int               width, height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
};

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

static void svc_readfile(char *read_buf, int buf_size)
{
	loff_t file_offset_tmp;

	file_offset_tmp = file_offset;

	if (vfs_read(video_filp,
                     read_buf,
                     buf_size, &file_offset_tmp) != buf_size) {
            printk("[svc_readfile] read failed!!\n");
        }
        else {
          file_offset += buf_size;
        }
        if (file_offset >= file_size) {
          file_offset = 0;
        }
}

static void svo_send_framebuffer(struct svc_dev *dev, struct svc_buffer *buf, char *screen_buf, int buf_size)
{
	struct timeval ts;
	void *vbuf;

	vbuf = videobuf_to_vmalloc(&buf->vb);

	mix_overlay(vbuf);

	dev->mv_count++;

	/* Updates stream time */

	dev->ms += jiffies_to_msecs(jiffies-dev->jiffies);
	dev->jiffies = jiffies;
	if (dev->ms >= 1000) {
		dev->ms -= 1001;
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
	buf->overlay_drawn = 1;
}

/* FIXME : in kmalloc function, GFP_KERNEL's maximum size = 32*PAGE_SIZE = 32*4K(4096) = 131072Bytes */
void svo_thread_tick(struct svc_fh *fh)
{
	struct svc_buffer *buf;
	struct svc_dev *dev = fh->dev;
	struct svc_dmaqueue *dma_q = &dev->vidq;

#if 0
	static int svotime = 1;

	if (svotime++ % 30 == 0) {
		printk("svotime=%d\n", svotime);
	}
#endif

	unsigned long flags = 0;
	struct list_head *p_active;
	p_active = &dma_q->active;

	dprintk(dev, 1, "Thread tick\n");

	spin_lock_irqsave(&dev->slock, flags);

	dprintk(dev, 1, "p_active=%0x, nex=%0x..\n", p_active, p_active->next);

	if (list_empty(&dma_q->active)) {
		printk("No active queue to serve\n");
		dprintk(dev, 1, "No active queue to serve\n");
		goto unlock;
	}

active_next:

	buf = list_entry(dma_q->active.next,
			 struct svc_buffer, vb.queue);

	if (buf == NULL) {
		printk("Video Buf is null\n");
		goto unlock;
	}
	
#if 0

	while (buf->vb.state == VIDEOBUF_DONE) {
		if (!list_empty(p_active->next)) {
			buf = list_entry(p_active->next->next,
					struct svc_buffer, vb.queue);
			p_active = p_active->next;
			printk("while in:%d\n", buf->videoid);
		}
	}

	/*
	printk("p_active=%0x, nex=%0x..%0x(%0x)\n", p_active, p_active->next, &buf->vb.queue, &dma_q->active);
	printk("p_active=next next(%0x)\n", p_active->next->next);
	printk("p_active=next next next (%0x)\n", p_active->next->next->next);
	*/

#else

	if (buf->overlay_drawn == 1) {
		if (p_active->next->next != &dma_q->active) {
			list_del(&buf->vb.queue);
			wake_up(&buf->vb.done);
			buf->vb.state = VIDEOBUF_DONE;
			buf->overlay_drawn = 0;
			goto active_next;
		}
	}

	dprintk(dev, 1, "Active Draw\n");
#endif
	/*
	else {
		list_del(&buf->vb.queue);
		buf = list_entry(dma_q->active.next,
				struct svc_buffer, vb.queue);
	}

	// Nobody is waiting on this buffer, return >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
	if (!waitqueue_active(&buf->vb.done)) {
		goto unlock;
	}
	else {
	}
	*/

	//list_del(&buf->vb.queue);  // removed by overlay system

	do_gettimeofday(&buf->vb.ts);

	/* Fill buffer */

	svo_send_framebuffer(dev, buf, NULL, 0);

	wake_up(&buf->vb.done);

	//dprintk(dev, 2, "[%p/%d] wakeup\n", buf, buf->vb. i);
unlock:
	spin_unlock_irqrestore(&dev->slock, flags);
	
	return;
}

#define frames_to_ms(frames)					\
	((frames * WAKE_NUMERATOR * 800) / WAKE_DENOMINATOR)

static void svo_sleep(struct file *file)
{
	struct svc_fh *fh;
	struct svc_dev *dev;
	struct svc_dmaqueue *dma_q;
	int timeout;
	DECLARE_WAITQUEUE(wait, current);

	/*
	static int svotime = 1;

	if (svotime++ % 30 == 0) {
		printk("svotime=%d\n", svotime);
	}
	*/

	if ((file == NULL) || (file->private_data == NULL)) {
		timeout = msecs_to_jiffies(frames_to_ms(1));
		mix_overlay(NULL);
		schedule_timeout_interruptible(timeout);
		try_to_freeze();
		return;
	}

	fh = file->private_data;
	dev = fh->dev;
	dma_q = &dev->vidq;

	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	timeout = msecs_to_jiffies(frames_to_ms(1));

	svo_thread_tick(fh);

	schedule_timeout_interruptible(timeout);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int svo_thread(struct file *file)
{
	struct svc_fh *fh;
	struct svc_dev *dev;

	printk("svb : thread started\n");

	if (file != NULL) {
		fh = file->private_data;
		dev = fh->dev;
	}

	set_freezable();

	for (;;) {
		svo_sleep(file);

		if (kthread_should_stop())
			break;
	}

	printk("svb : thread exit\n");
	return 0;
}

static int svbo_start_thread(struct file *file)
{
	struct svc_fh *fh;
	struct svc_dev *dev;
	struct svc_dmaqueue *dma_q;
	static struct task_struct  *kthread = NULL;

	if (file == NULL) {
		if (kthread == NULL) {
			/* Test Only */
			printk("svb : SPLIT mode\n");
			kthread = kthread_run(svo_thread, file, "svb");
			return 0;
		}
		return -1;
	}

	if (kthread != NULL) {
		printk("svb : SPLIT mode off\n");
		kthread_stop(kthread);
		kthread = NULL;
	}

	printk("%s : kthread run (Samsung Video board for Overlay)\n", __func__);

	fh = file->private_data;
	dev = fh->dev;
	dma_q = &dev->vidq;
	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	if (dma_q->kthread) {
		printk("%s : kthread already run\n");
		return -1;
	}

	dma_q->kthread = kthread_run(svo_thread, file, "svb-overlay");

	if (IS_ERR(dma_q->kthread)) {
		printk(KERN_ERR "svb : kernel_thread() failed\n");
		return PTR_ERR(dma_q->kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

static void svc_fillbuff(struct svc_dev *dev, struct svc_buffer *buf, char *screen_buf, int buf_size)
{
	struct timeval ts;
	void *vbuf = videobuf_to_vmalloc(&buf->vb);

	memcpy(vbuf, screen_buf, buf_size);

	dev->mv_count++;

	/* Updates stream time */

	dev->ms += jiffies_to_msecs(jiffies-dev->jiffies);
	dev->jiffies = jiffies;
	if (dev->ms >= 1000) {
		dev->ms -= 1001;
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

void svc_thread_tick(struct svc_fh *fh)
{
	struct svc_buffer *buf;
	struct svc_dev *dev = fh->dev;
	struct svc_dmaqueue *dma_q = &dev->vidq;
	int buf_size = (fh->width * fh->height * 3) / 2;
	char *screen_buf = kmalloc(buf_size, GFP_KERNEL);

	unsigned long flags = 0;

	dprintk(dev, 1, "Thread tick\n");

	if (fh->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		svc_readfile(screen_buf, buf_size);

	spin_lock_irqsave(&dev->slock, flags);
	if (list_empty(&dma_q->active)) {
		dprintk(dev, 1, "No active queue to serve\n");
		goto unlock;
	}

	buf = list_entry(dma_q->active.next,
			 struct svc_buffer, vb.queue);

	if (buf == NULL) {
		goto unlock;
	}

	/* Nobody is waiting on this buffer, return */
	if (!waitqueue_active(&buf->vb.done)) {
		goto unlock;
	}

	list_del(&buf->vb.queue);

	do_gettimeofday(&buf->vb.ts);

	/* Fill buffer */
	svc_fillbuff(dev, buf, screen_buf, buf_size);

	dprintk(dev, 1, "filled buffer %p\n", buf);

	wake_up(&buf->vb.done);

	dprintk(dev, 2, "[%p/%d] wakeup\n", buf, buf->vb. i);
unlock:
	spin_unlock_irqrestore(&dev->slock, flags);
	kfree(screen_buf);
	return;
}


static void svc_sleep(struct file *file)
{
	struct svc_fh *fh;
	struct svc_dev *dev;
	struct svc_dmaqueue *dma_q;
	int timeout;
	DECLARE_WAITQUEUE(wait, current);

	fh = file->private_data;
	dev = fh->dev;
	dma_q = &dev->vidq;
	dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
		(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (kthread_should_stop())
		goto stop_task;

	/* Calculate time to wake up */
	timeout = msecs_to_jiffies(frames_to_ms(1));

	svc_thread_tick(fh);

	schedule_timeout_interruptible(timeout);

stop_task:
	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

static int svc_thread(struct file *file)
{
	struct svc_fh *fh;
	struct svc_dev *dev;

	printk("svb : thread started for camera\n");

	if (file != NULL) {
		fh = file->private_data;
		dev = fh->dev;
	}

	set_freezable();

	for (;;) {
		svc_sleep(file);

		if (kthread_should_stop())
			break;
	}

	printk("svb : thread exit for camera\n");
	return 0;
}

static int svbc_start_thread(struct file *file)
{
	struct svc_fh *fh;
	struct svc_dev *dev;
	struct svc_dmaqueue *dma_q;

	fh = file->private_data;
	dev = fh->dev;
	dma_q = &dev->vidq;
	dma_q->frame = 0;
	dma_q->ini_jiffies = jiffies;

	printk("%s : kthread run (Samsung Video board for Camera)\n", __func__);

	dma_q->svc_kthread = kthread_run(svc_thread, file, "svb-camera");
	printk("okdear=%0x\n", dma_q->svc_kthread);

	if (IS_ERR(dma_q->svc_kthread)) {
		printk(KERN_ERR "svb : kernel_thread() failed for camera\n");
		return PTR_ERR(dma_q->svc_kthread);
	}
	/* Wakes thread */
	wake_up_interruptible(&dma_q->wq);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}


static void svco_stop_thread(struct svc_dmaqueue  *dma_q)
{
	struct svc_dev *dev = container_of(dma_q, struct svc_dev, vidq);

	dprintk(dev, 1, "%s\n", __func__);
	/* shutdown control thread */
	if (dma_q->kthread) {
		printk("svb : thread stop for overlay\n");
		kthread_stop(dma_q->kthread);
		dma_q->kthread = NULL;
	}

	if (dma_q->svc_kthread) {
		printk("dma_q->svc_kthread end=%0x\n", dma_q->svc_kthread);
		printk("svb : thread stop for camera\n");
		kthread_stop(dma_q->svc_kthread);
		dma_q->svc_kthread = NULL;
	}
}

/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/
static int
buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
	struct svc_fh  *fh = vq->priv_data;
	struct svc_dev *dev  = fh->dev;

	if(format.fourcc == V4L2_PIX_FMT_YUV420)
		*size = (fh->width*fh->height*3)/2;
	else if(format.fourcc == V4L2_PIX_FMT_YUV422P)
		*size = fh->width*fh->height*2;

	if (0 == *count)
		*count = 32;

	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;

	dprintk(dev, 1, "%s, count=%d, size=%d\n", __func__, *count, *size);

	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct svc_buffer *buf)
{
	struct svc_fh  *fh = vq->priv_data;
	struct svc_dev *dev  = fh->dev;

	dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);

	if (in_interrupt())
		BUG();

	videobuf_vmalloc_free(&buf->vb);
	dprintk(dev, 1, "free_buffer: freed\n");
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

/* FIXME: maximum size will be required */
#define norm_maxw() 1024
#define norm_maxh() 800
static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct svc_fh     *fh  = vq->priv_data;
	struct svc_dev    *dev = fh->dev;
	struct svc_buffer *buf = container_of(vb, struct svc_buffer, vb);
	int rc;

	dprintk(dev, 1, "%s, field=%d\n", __func__, field);
	BUG_ON(NULL == fh->fmt);

	if (fh->width  < 48 || fh->width  > norm_maxw() ||
	    fh->height < 32 || fh->height > norm_maxh()) {
		printk("size problem: w:%d, h:%d\n", fh->width, fh->height);
		return -EINVAL;
	}

/* 
 * YUV420(I420)'s buffer size = y(width*height) size + u(width*height/4) size + v(width*height/4) size
 * YUV422P's buffer size = y(width*height) size + u(width*height/2) size + v(width*height/2) size
 * Only two format(fourcc) would be supported.
 */
	if(format.fourcc == V4L2_PIX_FMT_YUV420)
		buf->vb.size = (fh->width*fh->height*3)/2;
	else if(format.fourcc == V4L2_PIX_FMT_YUV422P)
		buf->vb.size = fh->width*fh->height*2;
	
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size) {
		printk("buffer prepare error\n");
		return -EINVAL;
	}

	/* These properties only change when queue is idle, see s_fmt */
	buf->fmt       = fh->fmt;
	buf->vb.width  = fh->width;
	buf->vb.height = fh->height;
	buf->vb.field  = field;

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
		rc = videobuf_iolock(vq, &buf->vb, NULL);
		if (rc < 0) {
			printk("lock fail\n");
			goto fail;
		}
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
	struct svc_buffer    *buf  = container_of(vb, struct svc_buffer, vb);
	struct svc_fh        *fh   = vq->priv_data;
	struct svc_dev       *dev  = fh->dev;
	struct svc_dmaqueue *vidq = &dev->vidq;
	static int videoid;
	videoid++;

	dprintk(dev, 1, "%s\n", __func__);

	buf->vb.state = VIDEOBUF_QUEUED;
	buf->videoid = videoid;
	//printk("add tail =%d\n", videoid);
	list_add_tail(&buf->vb.queue, &vidq->active);
	//printk("add tail =%d end\n", videoid);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct svc_buffer   *buf  = container_of(vb, struct svc_buffer, vb);
	struct svc_fh       *fh   = vq->priv_data;
	struct svc_dev      *dev  = (struct svc_dev *)fh->dev;

	dprintk(dev, 1, "%s\n", __func__);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops svc_video_qops = {
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
	strcpy(cap->driver, "svc");
	strcpy(cap->card, "svc");
	cap->version = SVC_VERSION;
	cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_VIDEO_OUTPUT  |
				V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	if (f->index > 0)
		return -EINVAL;

	strlcpy(f->description, format.name, sizeof(f->description));
	f->pixelformat = format.fourcc;
	return 0;
}

static int vidioc_g_fmt_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct svc_fh *fh = priv;

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

static int vidioc_try_fmt_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct svc_fh  *fh  = priv;
	struct svc_dev *dev = fh->dev;
	struct svc_fmt *fmt;
	enum v4l2_field field;
	unsigned int maxw, maxh;

	if (format.fourcc != f->fmt.pix.pixelformat) {
		dprintk(dev, 1, "Fourcc format (0x%08x) invalid. "
			"Driver accepts only 0x%08x\n",
			f->fmt.pix.pixelformat, format.fourcc);
		return -EINVAL;
	}
	fmt = &format;

	field = f->fmt.pix.field;

	if (field == V4L2_FIELD_ANY) {
		field = V4L2_FIELD_INTERLACED;
	} else if (V4L2_FIELD_INTERLACED != field) {
		printk("Field type invalid.\n");
		return -EINVAL;
	}

	maxw  = norm_maxw();
	maxh  = norm_maxh();

	f->fmt.pix.field = field;
	if (f->fmt.pix.height < 32)
		f->fmt.pix.height = 32;
	if (f->fmt.pix.height > maxh)
		f->fmt.pix.height = maxh;
	if (f->fmt.pix.width < 48)
		f->fmt.pix.width = 48;
	if (f->fmt.pix.width > maxw)
		f->fmt.pix.width = maxw;
	f->fmt.pix.width &= ~0x03;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	return 0;
}

/*FIXME: This seems to be generic enough to be at videodev2 */
static int vidioc_s_fmt_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct svc_fh  *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;
	int ret;

	mutex_lock(&q->vb_lock);

	if (videobuf_queue_is_busy(&fh->vb_vidq)) {
		dprintk(fh->dev, 1, "%s queue busy\n", __func__);
		ret = -EBUSY;
		goto out;
	}

	fh->fmt           = &format;
	fh->width         = f->fmt.pix.width;
	fh->height        = f->fmt.pix.height;
	fh->vb_vidq.field = f->fmt.pix.field;
	fh->type          = f->type;

	ret = 0;
out:
	mutex_unlock(&q->vb_lock);

	return (ret);
}

#ifdef ENABLE_OVERLAY

static int vidioc_enum_fmt_video_output(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	if (f->index > 0)
		return -EINVAL;

	strlcpy(f->description, format.name, sizeof(f->description));
	f->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	//f->pixelformat = format.fourcc;
	return 0;
}


static int vidioc_g_fmt_video_output(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct svc_fh *fh = priv;
	//struct svc_dev *dev = fh->dev;

	f->fmt.pix.width        = fh->width;
	f->fmt.pix.height       = fh->height;
	f->fmt.pix.field        = fh->vb_vidq.field;
	f->fmt.pix.pixelformat  = fh->fmt->fourcc;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fh->fmt->depth);
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;

	return (0);
}

static int vidioc_try_fmt_video_output(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct svc_fh  *fh  = priv;
	struct svc_dev *dev = fh->dev;
	struct svc_fmt *fmt;
	enum v4l2_field field;
	unsigned int maxw, maxh;

	switch (f->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YUV420:
		break;
	default:
		dprintk(dev, 1, "Fourcc format (0x%08x) invalid. "
			"Driver accepts only 0x%08x\n",
			f->fmt.pix.pixelformat, format.fourcc);
		return -EINVAL;
	}

	//fmt = &format;

	field = f->fmt.pix.field;

	if (field == V4L2_FIELD_ANY) {
		field = V4L2_FIELD_INTERLACED;
	} else if (V4L2_FIELD_INTERLACED != field) {
		printk("Field type invalid.\n");
		return -EINVAL;
	}

	maxw  = norm_maxw();
	maxh  = norm_maxh();

	f->fmt.pix.field = field;
	if (f->fmt.pix.height < 32)
		f->fmt.pix.height = 32;
	if (f->fmt.pix.height > maxh)
		f->fmt.pix.height = maxh;
	if (f->fmt.pix.width < 48)
		f->fmt.pix.width = 48;
	if (f->fmt.pix.width > maxw)
		f->fmt.pix.width = maxw;
	f->fmt.pix.width &= ~0x03;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fmt->depth);
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	return 0;
}

static int vidioc_s_fmt_video_output(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct svc_fh  *fh = priv;
	struct videobuf_queue *q = &fh->vb_vidq;
	int ret;

	mutex_lock(&q->vb_lock);

	if (videobuf_queue_is_busy(&fh->vb_vidq)) {
		printk("video queue busy\n");
		dprintk(fh->dev, 1, "%s queue busy\n", __func__);
		ret = -EBUSY;
		goto out;
	}

	fh->fmt->fourcc = f->fmt.pix.pixelformat;

	switch (f->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_RGB565:
		fh->fmt->depth = 4;
		break;
	case V4L2_PIX_FMT_RGB24:
		fh->fmt->depth = 3;
		break;
	case V4L2_PIX_FMT_RGB32:
		fh->fmt->depth = 4;
		break;
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YUV420:
	default:
		printk("Fourcc format (0x%08x) invalid. "
			"Driver accepts only 0x%08x\n",
			f->fmt.pix.pixelformat, format.fourcc);
		return -EINVAL;
	}

	set_overlay(f->fmt.pix.pixelformat);

	fh->width         = f->fmt.pix.width;
	fh->height        = f->fmt.pix.height;
	fh->vb_vidq.field = f->fmt.pix.field;
	fh->type          = f->type;

	q->type = f->type;

	ret = 0;
out:
	mutex_unlock(&q->vb_lock);

	return (ret);
}

static int vidioc_cropcap(struct file *file, void *priv,
					struct v4l2_cropcap *cap)
{
	/* nothing do 
	cap->bounds  = dev->crop_bounds;
	cap->defrect = dev->crop_defrect;
	*/
	cap->pixelaspect.numerator   = 1; 
	cap->pixelaspect.denominator = 1; 

	return 0;
}

static int vidioc_g_crop(struct file *file, void *priv,
					struct v4l2_crop *crop)
{
	struct svc_fh *fh = priv;
	struct svc_dev *dev = fh->dev;

	crop->c = dev->crop_current;

	return 0;
}

static int vidioc_s_crop(struct file *file, void *priv,
					struct v4l2_crop *crop)
{

	struct svc_fh *fh = priv;
	struct svc_dev *dev = fh->dev;

	if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
			crop->type != V4L2_BUF_TYPE_VIDEO_OVERLAY &&
			crop->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
	{
		printk("Crop type is (%d)\n, We support video capture or video overlay or video output", crop->type);
		return -EINVAL;
	}
	if (crop->c.height < 0)
		return -EINVAL;
	if (crop->c.width < 0)
		return -EINVAL;

	mutex_lock(&dev->mutex);
	dev->crop_current.top = crop->c.top;
	dev->crop_current.left = crop->c.left;
	dev->crop_current.height = crop->c.height;
	dev->crop_current.width = crop->c.width;

	//dev->crop_current = crop->c;
	mutex_unlock(&dev->mutex);


	return 0;

}

static int vidioc_g_fmt_overlay(struct file *file, void *priv,
					struct v4l2_crop *crop)
{
	struct svc_fh *fh = priv;
	struct svc_dev *dev = fh->dev;

	crop->c = dev->crop_current;
	return 0;
}

static int vidioc_s_fmt_overlay(struct file *file, void *priv,
					struct v4l2_crop *crop)
{
	struct svc_fh *fh = priv;
	struct svc_dev *dev = fh->dev;

	if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
			crop->type != V4L2_BUF_TYPE_VIDEO_OVERLAY &&
			crop->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
	{
		printk("Crop type is (%d)\n, We support video capture or video overlay or video output", crop->type);
		return -EINVAL;
	}
	if (crop->c.height < 0)
		return -EINVAL;
	if (crop->c.width < 0)
		return -EINVAL;

	mutex_lock(&dev->mutex);
	dev->crop_overlay.top = crop->c.top;
	dev->crop_overlay.left = crop->c.left;
	dev->crop_overlay.height = crop->c.height;
	dev->crop_overlay.width = crop->c.width;

	//dev->crop_current = crop->c;
	mutex_unlock(&dev->mutex);


	return 0;


}

#endif

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct svc_fh  *fh = priv;
	fh->type = p->type;

	if (p->type != V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		/* "VIDIOC_REQBUFS" ioctl called, file open in kernel mode and start thread. */
		old_fs = get_fs();
		set_fs(KERNEL_DS);

		if(fh->width == 320 && fh->height == 240)
			video_filp = filp_open(VIDEO_PATH_320x240 , O_RDONLY | O_LARGEFILE, 0);
		else if(fh->width == 640 && fh->height == 480)
			video_filp = filp_open(VIDEO_PATH_640x480 , O_RDONLY | O_LARGEFILE, 0);
		else 
			video_filp = filp_open(VIDEO_PATH_320x240 , O_RDONLY | O_LARGEFILE, 0);
		if (IS_ERR(video_filp)) {
			printk("[svc_open] file open error!!\n");
			return 0;
		}

		file_offset = 0;

		file_size = i_size_read(video_filp->f_path.dentry->d_inode->i_mapping->host);
		if (file_size < 0) {
			printk("%s: unable to find file size: %s\n", __FUNCTION__, "/root/test***x***.yuv");
			return 0;
		}

		svbc_start_thread(file);
	}
	else {
		svbo_start_thread(file);
	}

	return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct svc_fh  *fh = priv;

	return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct svc_fh *fh = priv;

	return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct svc_fh  *fh = priv;

#if 0
	/* overlay : 6list */
	struct svc_buffer *buf;
	unsigned long flags = 0;
	struct svc_dev *dev = fh->dev;

	spin_lock_irqsave(&dev->slock, flags);
	struct svc_dmaqueue *dma_q = &dev->vidq;

	/* remove by thread -> removed by dqbuf 
	 * : for keeping overlay buffer */
next:
	if (!list_empty(&dma_q->active)) {
		dprintk(dev, 1, "not empty (dq)\n");
		buf = list_entry(dma_q->active.next,
				struct svc_buffer, vb.queue);

		if (buf != NULL) {
			dprintk(dev, 1, "call list del (dq)\n");
			printk("[%d]buf->vb.state=%d\n", buf->videoid, buf->vb.state);
			
			if (buf->vb.state == VIDEOBUF_DONE) {
				/* drawn state */
				list_del(&buf->vb.queue);
				goto next;
			}
			else {
			}
			dprintk(dev, 1, "call list del complete (dq)\n");
		}
	}
	spin_unlock_irqrestore(&dev->slock, flags);
#endif

	return (videobuf_dqbuf(&fh->vb_vidq, p,
				file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct svc_fh  *fh = priv;

	return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct svc_fh  *fh = priv;

	/*
	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;
		*/

	return videobuf_streamon(&fh->vb_vidq);
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct svc_fh  *fh = priv;

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
	inp->std = V4L2_STD_525_60;
	strcpy(inp->name, "Camera");

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

	for (i = 0; i < ARRAY_SIZE(svc_qctrl); i++)
		if (qc->id && qc->id == svc_qctrl[i].id) {
			memcpy(qc, &(svc_qctrl[i]),
				sizeof(*qc));
			return (0);
		}

	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(svc_qctrl); i++)
		if (ctrl->id == svc_qctrl[i].id) {
			ctrl->value = qctl_regs[i];
			return (0);
		}

	return -EINVAL;
}
static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(svc_qctrl); i++)
		if (ctrl->id == svc_qctrl[i].id) {
			if (ctrl->value < svc_qctrl[i].minimum
			    || ctrl->value > svc_qctrl[i].maximum) {
					return (-ERANGE);
				}
			qctl_regs[i] = ctrl->value;
			return (0);
		}
	return -EINVAL;
}

/* ------------------------------------------------------------------
	File operations for the device
   ------------------------------------------------------------------*/

#define line_buf_size(norm) (norm_maxw(norm)*(format.depth+7)/8)

static int svc_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct svc_dev *dev;
	struct svc_fh *fh = NULL;
	int i;
	int retval = 0;

	struct svc_dmaqueue *vidq;

	if ( file->private_data) {
		fh = file->private_data;
		if (fh->dev) {
			dev = fh->dev;
			vidq = &dev->vidq;
			printk(KERN_DEBUG "svb : overlay thread close\n");
			svco_stop_thread(vidq);

			/* init video dma queues */
			INIT_LIST_HEAD(&dev->vidq.active);
			init_waitqueue_head(&dev->vidq.wq);

			videobuf_stop(&fh->vb_vidq);
			videobuf_mmap_free(&fh->vb_vidq);

			kfree(fh);
			file->private_data = NULL;

		}
	}

	printk(KERN_DEBUG "svb : open called (minor=%d)\n", minor);

	list_for_each_entry(dev, &svc_devlist, svc_devlist)
		if (dev->vfd->minor == minor)
			goto found;
	
	return -ENODEV;

found:
	
	mutex_lock(&dev->mutex);
	dev->users++;

	if (dev->users > 1) {
		dev->users--;
		retval = -EBUSY;
		goto unlock;
	}

	dprintk(dev, 1, "open minor=%d type=%s users=%d\n", minor,
		v4l2_type_names[V4L2_BUF_TYPE_VIDEO_CAPTURE], dev->users);

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (NULL == fh) {
		dev->users--;
		retval = -ENOMEM;
		goto unlock;
	}
unlock:
	mutex_unlock(&dev->mutex);
	if (retval)
		return retval;

	file->private_data = fh;
	fh->dev      = dev;

	fh->type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fh->fmt      = &format;
	fh->width    = DFL_WIDTH;
	fh->height   = DFL_HEIGHT;

	/* Put all controls at a sane state */
	for (i = 0; i < ARRAY_SIZE(svc_qctrl); i++)
		qctl_regs[i] = svc_qctrl[i].default_value;

	/* Resets frame counters */
	dev->h = 0;
	dev->m = 0;
	dev->s = 0;
	dev->ms = 0;
	dev->mv_count = 0;
	dev->jiffies = jiffies;
	sprintf(dev->timestr, "%02d:%02d:%02d:%03d",
			dev->h, dev->m, dev->s, dev->ms);

	videobuf_queue_vmalloc_init(&fh->vb_vidq, &svc_video_qops,
			NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
			sizeof(struct svc_buffer), fh);

	return 0;
}

static ssize_t
svc_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct svc_fh *fh = file->private_data;

	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
svc_poll(struct file *file, struct poll_table_struct *wait)
{
	struct svc_fh        *fh = file->private_data;
	struct svc_dev       *dev = fh->dev;
	struct videobuf_queue *q = &fh->vb_vidq;

	dprintk(dev, 1, "%s\n", __func__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	return videobuf_poll_stream(file, q, wait);
}

static int svc_close(struct inode *inode, struct file *file)
{
	struct svc_fh         *fh = file->private_data;
	struct svc_dev *dev       = fh->dev;
	struct svc_dmaqueue *vidq = &dev->vidq;

	int minor = iminor(inode);

	svco_stop_thread(vidq);

#ifdef ENABLE_TEST_FB0_COPY
	svbo_start_thread(NULL);
#endif

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->vidq.active);
	init_waitqueue_head(&dev->vidq.wq);

	videobuf_stop(&fh->vb_vidq);
	videobuf_mmap_free(&fh->vb_vidq);

	kfree(fh);
	file->private_data = NULL;

	mutex_lock(&dev->mutex);
	dev->users--;
	mutex_unlock(&dev->mutex);

	dprintk(dev, 1, "close called (minor=%d, users=%d)\n",
		minor, dev->users);

	if(!IS_ERR(video_filp) && video_filp!=NULL)
		filp_close(video_filp, 0);
	file_offset = 0;

	set_fs(old_fs);

	printk(KERN_DEBUG "svb : close called (minor=%d)\n", minor);
	return 0;
}

static int svc_release(void)
{
	struct svc_dev *dev;
	struct list_head *list;

	while (!list_empty(&svc_devlist)) {
		list = svc_devlist.next;
		list_del(list);
		dev = list_entry(list, struct svc_dev, svc_devlist);

		if (-1 != dev->vfd->minor)
			video_unregister_device(dev->vfd);
		else
			video_device_release(dev->vfd);

		kfree(dev);
	}

	return 0;
}

static int svc_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct svc_fh  *fh = file->private_data;
	struct svc_dev *dev = fh->dev;
	int ret;

	dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

	ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static const struct file_operations svc_fops = {
	.owner		= THIS_MODULE,
	.open           = svc_open,
	.release        = svc_close,
	.read           = svc_read,
	.poll		= svc_poll,
	.ioctl          = video_ioctl2, /* V4L2 ioctl handler */
	.compat_ioctl   = v4l_compat_ioctl32,
	.mmap           = svc_mmap,
	.llseek         = no_llseek,
};

static struct video_device svc_template = {
	.name		= "svc",
	.type		= VID_TYPE_CAPTURE,
	.fops           = &svc_fops,
	.minor		= -1,
	.release	= video_device_release,

	.vidioc_querycap      = vidioc_querycap,
	.vidioc_enum_fmt_cap  = vidioc_enum_fmt_cap,
	.vidioc_g_fmt_cap     = vidioc_g_fmt_cap,
	.vidioc_try_fmt_cap   = vidioc_try_fmt_cap,
	.vidioc_s_fmt_cap     = vidioc_s_fmt_cap,
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

	/* added by okdear */
#ifdef ENABLE_OVERLAY

	/* S FMT : video output  (original data)
	 * -> crop cap (cut)
	 * -> S FMT : video output overlay(resize) */
	.vidioc_enum_fmt_video_output = vidioc_enum_fmt_video_output,

	.vidioc_g_fmt_video_output = vidioc_g_fmt_video_output,
	.vidioc_try_fmt_video_output = vidioc_try_fmt_video_output,
	.vidioc_s_fmt_video_output     = vidioc_s_fmt_video_output,

	.vidioc_cropcap         = vidioc_cropcap,
	.vidioc_g_crop          = vidioc_g_crop,
	.vidioc_s_crop          = vidioc_s_crop,

	.vidioc_g_fmt_overlay = vidioc_g_fmt_overlay,
	.vidioc_s_fmt_overlay = vidioc_s_fmt_overlay,

#endif

#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf          = vidiocgmbuf,
#endif
	.tvnorms              = V4L2_STD_525_60,
	.current_norm         = V4L2_STD_NTSC_M,
};
/* -----------------------------------------------------------------
	Initialization and module stuff
   ------------------------------------------------------------------*/

static int __init svc_init(void)
{
	int ret = -ENOMEM, i;
	struct svc_dev *dev;
	struct video_device *vfd;

	for (i = 0; i < n_devs; i++) {
		dev = kzalloc(sizeof(*dev), GFP_KERNEL);
		if (NULL == dev)
			break;

		list_add_tail(&dev->svc_devlist, &svc_devlist);

		/* init video dma queues */
		INIT_LIST_HEAD(&dev->vidq.active);
		init_waitqueue_head(&dev->vidq.wq);

		/* initialize locks */
		spin_lock_init(&dev->slock);
		mutex_init(&dev->mutex);

		vfd = video_device_alloc();
		if (NULL == vfd)
			break;

		*vfd = svc_template;

		ret = video_register_device(vfd, VFL_TYPE_GRABBER, video_nr);
		if (ret < 0)
			break;

		snprintf(vfd->name, sizeof(vfd->name), "%s (%i)",
			 svc_template.name, vfd->minor);

		if (video_nr >= 0)
			video_nr++;

		dev->vfd = vfd;
	}

#ifdef ENABLE_SPLIT_FB0
	split_fb0();
#endif
#ifdef ENABLE_TEST_FB0_COPY
	svbo_start_thread(NULL);
#endif

	if (ret < 0) {
		svc_release();
		printk(KERN_INFO "Error %d while loading svc driver\n", ret);
	} else
		printk(KERN_INFO "Samsung Video Board for virtual Camera and Overlay"
				 "Capture Board successfully loaded.\n");
	return ret;
}

static void __exit svc_exit(void)
{
	svc_release();
}

module_init(svc_init);
module_exit(svc_exit);

MODULE_DESCRIPTION("Samsung Video Board for camera capture and overlay");
MODULE_LICENSE("Dual BSD/GPL");

module_param(video_nr, int, 0);
MODULE_PARM_DESC(video_nr, "video iminor start number");

module_param(n_devs, int, 0);
MODULE_PARM_DESC(n_devs, "number of video devices to create");

module_param_named(debug, svc_template.debug, int, 0444);
MODULE_PARM_DESC(debug, "activates debug info");

module_param(vid_limit, int, 0644);
MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");
