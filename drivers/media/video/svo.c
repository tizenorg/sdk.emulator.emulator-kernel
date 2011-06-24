/*
 * Samsung virtual overlay driver.
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd All Rights Reserved
 *
 * Authors:
 *  Yuyeon Oh   <yuyeon.oh@samsung.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>

#define SVO_DRIVER_MAJORVERSION	 0
#define SVO_DRIVER_MINORVERSION  1

enum {
	OVERLAY0_POWER    = 0x00,
	OVERLAY0_SIZE     = 0x04,	// width and height
	OVERLAY0_POSITION = 0x08,	// left top position
	NUM_REGISTER      = 0x0C,
	OVERLAY1_POWER    = 0x0C,
	OVERLAY1_SIZE     = 0x10,	// width and height
	OVERLAY1_POSITION = 0x14,	// left top position
};

static struct pci_device_id svo_pci_tbl[] = {
	{
		.vendor       = PCI_VENDOR_ID_SAMSUNG,
		.device       = 0x1010,
		.subvendor    = PCI_ANY_ID,
		.subdevice    = PCI_ANY_ID,
	}
};

struct svo {
	struct pci_dev *pci_dev;		/* pci device */

	struct video_device *video_dev0;	/* video device parameters */
	struct video_device *video_dev1;	/* video device parameters */

	resource_size_t mem_start;
	resource_size_t reg_start;

	resource_size_t mem_size;
	resource_size_t reg_size;

	unsigned char __iomem *svo_mmreg;	/* svo: memory mapped registers */

	unsigned long in_use0;			/* set to 1 if the device is in use */
	unsigned long in_use1;			/* set to 1 if the device is in use */

	struct v4l2_rect w0, w1;		/* overlaid rect */
};

/* driver structure - only one possible */
static struct svo svo;

/****************************************************************************/
/* virtual register access helper                                                 */
/****************************************************************************/

static void overlay_power(int num, int onoff) {
	writel(onoff, svo.svo_mmreg + OVERLAY0_POWER + NUM_REGISTER * num);
}

/****************************************************************************/
/* svo ioctls                                                  */
/****************************************************************************/

static int svo_querycap(struct file *file, void *fh,
				struct v4l2_capability *cap)
{
	strcpy(cap->driver, "svo");
	strcpy(cap->card, "svo");
	sprintf(cap->bus_info, "PCI:%s", pci_name(svo.pci_dev));

	cap->version = (SVO_DRIVER_MAJORVERSION << 8) +
		       SVO_DRIVER_MINORVERSION;

	cap->capabilities = V4L2_CAP_VIDEO_OVERLAY;

	return 0;
}

static int svo0_g_fmt_vid_overlay(struct file *file, void *priv,
						struct v4l2_format *f)
{
	if (f->type != V4L2_BUF_TYPE_VIDEO_OVERLAY)
		return -EINVAL;

	f->fmt.win.w.left = svo.w0.left;
	f->fmt.win.w.top = svo.w0.top;
	f->fmt.win.w.width = svo.w0.width;
	f->fmt.win.w.height = svo.w0.height;

	return 0;
}

static int svo1_g_fmt_vid_overlay(struct file *file, void *priv,
						struct v4l2_format *f)
{
	if (f->type != V4L2_BUF_TYPE_VIDEO_OVERLAY)
		return -EINVAL;

	f->fmt.win.w.left = svo.w1.left;
	f->fmt.win.w.top = svo.w1.top;
	f->fmt.win.w.width = svo.w1.width;
	f->fmt.win.w.height = svo.w1.height;

	return 0;
}

static int svo0_s_fmt_vid_overlay(struct file *file, void *priv,
						struct v4l2_format *f)
{
	if (f->type != V4L2_BUF_TYPE_VIDEO_OVERLAY)
		return -EINVAL;

	if (f->fmt.win.w.left < 0)
		return -EINVAL;
	if (f->fmt.win.w.top < 0)
		return -EINVAL;

	if (f->fmt.win.w.width < 0)
		return -EINVAL;
	if (f->fmt.win.w.height < 0)
		return -EINVAL;

	svo.w0.left = f->fmt.win.w.left;
	svo.w0.top = f->fmt.win.w.top;
	svo.w0.width = f->fmt.win.w.width;
	svo.w0.height = f->fmt.win.w.height;

	return 0;
}

static int svo1_s_fmt_vid_overlay(struct file *file, void *priv,
						struct v4l2_format *f)
{
	if (f->type != V4L2_BUF_TYPE_VIDEO_OVERLAY)
		return -EINVAL;

	if (f->fmt.win.w.left < 0)
		return -EINVAL;
	if (f->fmt.win.w.top < 0)
		return -EINVAL;

	if (f->fmt.win.w.width < 0)
		return -EINVAL;
	if (f->fmt.win.w.height < 0)
		return -EINVAL;

	svo.w1.left = f->fmt.win.w.left;
	svo.w1.top = f->fmt.win.w.top;
	svo.w1.width = f->fmt.win.w.width;
	svo.w1.height = f->fmt.win.w.height;

	return 0;
}

static int svo_reqbufs(struct file *file, void *priv,
				  struct v4l2_requestbuffers *p)
{
	if (p->type != V4L2_BUF_TYPE_VIDEO_OVERLAY)
		return -EINVAL;

	if (p->count > 1)
		return -EINVAL;

	if (p->memory != V4L2_MEMORY_MMAP)
		return -EINVAL;

	return 0;
}

static int svo0_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	if (p->type != V4L2_BUF_TYPE_VIDEO_OVERLAY)
		return -EINVAL;

	if (p->index)
		return -EINVAL;

	p->length = svo.mem_size / 2;
	p->m.offset = svo.mem_start;

	return 0;
}

static int svo1_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	if (p->type != V4L2_BUF_TYPE_VIDEO_OVERLAY)
		return -EINVAL;

	if (p->index)
		return -EINVAL;

	p->length = svo.mem_size / 2;
	p->m.offset = svo.mem_start + p->length;

	return 0;
}

static int svo0_overlay (struct file *file, void *fh, unsigned int i)
{
	overlay_power(0, i);

	return 0;
}

static int svo1_overlay (struct file *file, void *fh, unsigned int i)
{
	overlay_power(1, i);

	return 0;
}

/****************************************************************************/
/* File operations                                                  */
/****************************************************************************/

static int svo0_open(struct file *file)
{
	if (test_and_set_bit(0, &svo.in_use0))
		return -EBUSY;

	return 0;
}

static int svo1_open(struct file *file)
{
	if (test_and_set_bit(0, &svo.in_use1))
		return -EBUSY;

	return 0;
}

static void svo_vm_open(struct vm_area_struct *vma)
{
}

static void svo_vm_close(struct vm_area_struct *vma)
{
}

static const struct vm_operations_struct svo_vm_ops = {
	.open		= svo_vm_open,
	.close		= svo_vm_close,
};

static int svo_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;

	if (size > svo.mem_size)
		return -EINVAL;

	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, size, PAGE_SHARED))
		return -EAGAIN;
	
	vma->vm_ops = &svo_vm_ops;
	vma->vm_flags &= ~VM_IO;	/* not I/O memory */
	vma->vm_flags |= VM_RESERVED;	/* avoid to swap out this VMA */

	return 0;
}

static int svo0_release(struct file *file)
{
	clear_bit(0, &svo.in_use0);
	overlay_power(0, 0);

	return 0;
}

static int svo1_release(struct file *file)
{
	clear_bit(0, &svo.in_use1);
	overlay_power(1, 0);

	return 0;
}

static const struct v4l2_ioctl_ops svo0_ioctl_ops = {
	.vidioc_querycap	= svo_querycap,
	.vidioc_g_fmt_vid_overlay = svo0_g_fmt_vid_overlay,
	.vidioc_s_fmt_vid_overlay = svo0_s_fmt_vid_overlay,
	.vidioc_reqbufs		= svo_reqbufs,
	.vidioc_querybuf	= svo0_querybuf,
	.vidioc_overlay		= svo0_overlay,
};

static const struct v4l2_ioctl_ops svo1_ioctl_ops = {
	.vidioc_querycap	= svo_querycap,
	.vidioc_g_fmt_vid_overlay = svo1_g_fmt_vid_overlay,
	.vidioc_s_fmt_vid_overlay = svo1_s_fmt_vid_overlay,
	.vidioc_reqbufs		= svo_reqbufs,
	.vidioc_querybuf	= svo1_querybuf,
	.vidioc_overlay		= svo1_overlay,
};

static const struct v4l2_file_operations svo0_fops = {
	.owner		= THIS_MODULE,
	.open		= svo0_open,
	.release	= svo0_release,
	.mmap		= svo_mmap,
	.ioctl		= video_ioctl2,
};

static const struct v4l2_file_operations svo1_fops = {
	.owner		= THIS_MODULE,
	.open		= svo1_open,
	.release	= svo1_release,
	.mmap		= svo_mmap,
	.ioctl		= video_ioctl2,
};

static struct video_device svo0_template = {
	.name		= "svo0",
	.fops		= &svo0_fops,
	.ioctl_ops 	= &svo0_ioctl_ops,
	.release	= video_device_release,
	.minor		= -1,
};

static struct video_device svo1_template = {
	.name		= "svo1",
	.fops		= &svo1_fops,
	.ioctl_ops 	= &svo1_ioctl_ops,
	.release	= video_device_release,
	.minor		= -1,
};

/* /dev/videoX registration number */
static int video_nr = -1;
module_param(video_nr, int, 0444);
MODULE_PARM_DESC(video_nr, "video device to register (0=/dev/video0, etc)");

static int __devinit svo_initdev(struct pci_dev *pci_dev,
				    const struct pci_device_id *ent)
{
	int ret = -EBUSY;

	if (svo.pci_dev != NULL) {
		printk(KERN_ERR "svo: only one device allowed!\n");
		goto outnotdev;
	}

	ret = -ENOMEM;
	svo.pci_dev = pci_dev;
	svo.video_dev0 = video_device_alloc();
	if (!svo.video_dev0) {
		printk(KERN_ERR "svo0: video_device_alloc() failed!\n");
		goto outnotdev;
	}

	svo.video_dev1 = video_device_alloc();
	if (!svo.video_dev1) {
		printk(KERN_ERR "svo1: video_device_alloc() failed!\n");
		goto outnotdev;
	}

	memcpy(svo.video_dev0, &svo0_template, sizeof(svo0_template));
	svo.video_dev0->parent = &svo.pci_dev->dev;
	memcpy(svo.video_dev1, &svo1_template, sizeof(svo1_template));
	svo.video_dev1->parent = &svo.pci_dev->dev;

	ret = -EIO;

	if ((ret = pci_enable_device(svo.pci_dev))) {
		printk(KERN_ERR "svo: pci_enable_device failed\n");
		goto outnotdev;
	}

	svo.mem_start = pci_resource_start(svo.pci_dev,0);
	svo.mem_size = pci_resource_len(svo.pci_dev, 0);
	if (!svo.mem_start) {
		printk(KERN_ERR "svo: svo has no device base address\n");
		goto outregions;
	}
	if (!request_mem_region(pci_resource_start(svo.pci_dev, 0),
				pci_resource_len(svo.pci_dev, 0),
				"svo")) {
		printk(KERN_ERR "svo: request_mem_region failed\n");
		goto outregions;
	}
	
	svo.reg_start = pci_resource_start(svo.pci_dev,1);
	svo.reg_size = pci_resource_len(svo.pci_dev, 1);
	if (!svo.reg_start) {
		printk(KERN_ERR "svo: svo has no device base address\n");
		goto outregions;
	}
	if (!request_mem_region(pci_resource_start(svo.pci_dev, 1),
				pci_resource_len(svo.pci_dev, 1),
				"svo")) {
		printk(KERN_ERR "svo: request_mem_region failed\n");
		goto outregions;
	}

	svo.svo_mmreg = ioremap(svo.reg_start, svo.reg_size);
	if (!svo.svo_mmreg) {
		printk(KERN_ERR "svo: ioremap failed\n");
		goto outremap;
	}

	pci_write_config_byte(svo.pci_dev, PCI_CACHE_LINE_SIZE, 8);
	pci_write_config_byte(svo.pci_dev, PCI_LATENCY_TIMER, 64);

	pci_set_master(svo.pci_dev);

	if (video_register_device(svo.video_dev0, VFL_TYPE_GRABBER,
				  video_nr) < 0) {
		printk(KERN_ERR "svo: video_register_device failed\n");
		goto outreqirq;
	}
	if (video_register_device(svo.video_dev1, VFL_TYPE_GRABBER,
				  video_nr) < 0) {
		printk(KERN_ERR "svo: video_register_device failed\n");
		goto outreqirq;
	}

	printk(KERN_INFO "svo: Samsung Virtual Overlay Driver v%d.%d\n",
		SVO_DRIVER_MAJORVERSION, SVO_DRIVER_MINORVERSION);

	return 0;

outreqirq:
	iounmap(svo.svo_mmreg);
outremap:
	release_mem_region(pci_resource_start(svo.pci_dev, 0),
			   pci_resource_len(svo.pci_dev, 0));
	release_mem_region(pci_resource_start(svo.pci_dev, 1),
			   pci_resource_len(svo.pci_dev, 1));
outregions:
	pci_disable_device(svo.pci_dev);
	video_device_release(svo.video_dev0);
	video_device_release(svo.video_dev1);
outnotdev:
	return ret;
}

static struct pci_driver svo_pci_driver = {
	.name     = "svo",
	.id_table = svo_pci_tbl,
	.probe    = svo_initdev,
//	.remove   = __devexit_p(cx8800_finidev),
#ifdef CONFIG_PM
//	.suspend  = cx8800_suspend,
//	.resume   = cx8800_resume,
#endif
};

static int __init svo_init(void)
{
	printk(KERN_INFO "svo: samsung virtual overlay driver version %d.%d loaded\n",
		SVO_DRIVER_MAJORVERSION, SVO_DRIVER_MINORVERSION);

	return pci_register_driver(&svo_pci_driver);
}

static void __exit svo_fini(void)
{
	pci_unregister_driver(&svo_pci_driver);
}

module_init(svo_init);
module_exit(svo_fini); 
