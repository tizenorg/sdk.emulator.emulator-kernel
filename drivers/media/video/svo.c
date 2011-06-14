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
	struct video_device *video_dev;		/* video device parameters */
	unsigned char __iomem *svo_mem;		/* svo: memory */
	unsigned char __iomem *svo_mmregs;	/* svo: memory mapped registers */
};

static const struct v4l2_file_operations svo_fops = {
	.owner		= THIS_MODULE,
//	.open		= meye_open,
//	.release	= meye_release,
//	.mmap		= meye_mmap,
	.ioctl		= video_ioctl2,
//	.poll		= meye_poll,
};

/* driver structure - only one possible */
static struct svo svo;

static struct video_device svo_template = {
	.name		= "svo",
	.fops		= &svo_fops,
//	.ioctl_ops 	= &meye_ioctl_ops,
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
	unsigned long svo_mem_adr, svo_reg_adr;

	if (svo.pci_dev != NULL) {
		printk(KERN_ERR "svo: only one device allowed!\n");
		goto outnotdev;
	}

	ret = -ENOMEM;
	svo.pci_dev = pci_dev;
	svo.video_dev = video_device_alloc();
	if (!svo.video_dev) {
		printk(KERN_ERR "svo: video_device_alloc() failed!\n");
		goto outnotdev;
	}

	memcpy(svo.video_dev, &svo_template, sizeof(svo_template));
	svo.video_dev->parent = &svo.pci_dev->dev;

	ret = -EIO;

	if ((ret = pci_enable_device(svo.pci_dev))) {
		printk(KERN_ERR "svo: pci_enable_device failed\n");
		goto outnotdev;
	}

	svo_mem_adr = pci_resource_start(svo.pci_dev,0);
	if (!svo_mem_adr) {
		printk(KERN_ERR "svo: svo has no device base address\n");
		goto outregions;
	}
	if (!request_mem_region(pci_resource_start(svo.pci_dev, 0),
				pci_resource_len(svo.pci_dev, 0),
				"svo")) {
		printk(KERN_ERR "svo: request_mem_region failed\n");
		goto outregions;
	}
	svo.svo_mem = ioremap(svo_mem_adr, 0x200000);	// 2MB
	if (!svo.svo_mem) {
		printk(KERN_ERR "svo: ioremap failed\n");
		goto outremap;
	}
	
	svo_reg_adr = pci_resource_start(svo.pci_dev,1);
	if (!svo_reg_adr) {
		printk(KERN_ERR "svo: svo has no device base address\n");
		goto outregions;
	}
	if (!request_mem_region(pci_resource_start(svo.pci_dev, 1),
				pci_resource_len(svo.pci_dev, 1),
				"svo")) {
		printk(KERN_ERR "svo: request_mem_region failed\n");
		goto outregions;
	}
	svo.svo_mmregs = ioremap(svo_mem_adr, 0x100);	// 256 bytes
	if (!svo.svo_mmregs) {
		printk(KERN_ERR "svo: ioremap failed\n");
		goto outremap;
	}

	pci_write_config_byte(svo.pci_dev, PCI_CACHE_LINE_SIZE, 8);
	pci_write_config_byte(svo.pci_dev, PCI_LATENCY_TIMER, 64);

	pci_set_master(svo.pci_dev);

	if (video_register_device(svo.video_dev, VFL_TYPE_GRABBER,
				  video_nr) < 0) {
		printk(KERN_ERR "svo: video_register_device failed\n");
		goto outreqirq;
	}

	printk(KERN_INFO "svo: Samsung Virtual Overlay Driver v0.1\n");

	return 0;

outreqirq:
	iounmap(svo.svo_mem);
	iounmap(svo.svo_mmregs);
outremap:
	release_mem_region(pci_resource_start(svo.pci_dev, 0),
			   pci_resource_len(svo.pci_dev, 0));
	release_mem_region(pci_resource_start(svo.pci_dev, 1),
			   pci_resource_len(svo.pci_dev, 1));
outregions:
	pci_disable_device(svo.pci_dev);
	video_device_release(svo.video_dev);
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
	printk(KERN_INFO "svo: samsung virtual overlay driver version 0.1 loaded\n");

	return pci_register_driver(&svo_pci_driver);
}

static void __exit svo_fini(void)
{
	pci_unregister_driver(&svo_pci_driver);
}

module_init(svo_init);
module_exit(svo_fini); 
