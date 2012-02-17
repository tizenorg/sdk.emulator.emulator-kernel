/*
 * Samsung Virtual Backlight Driver
 *
 * Copyright (c) 2011 - 2012 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 * Dohyung Hong <don.hong@samsung.com>
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/backlight.h>

#include <asm/uaccess.h>

#define SVB_DRIVER_NAME "svb"
#define PCI_DEVICE_ID_VIRTIO_SVB	0x1014

static struct pci_device_id svb_pci_table[] __devinitdata =
{
	{
		.vendor		= PCI_VENDOR_ID_SAMSUNG,
		.device     = PCI_DEVICE_ID_VIRTIO_SVB,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	}
};
MODULE_DEVICE_TABLE(pci, svb_pci_table);

/* samsung virtual brightness(backlight) device structure */
struct svb {
	struct backlight_device *bl_dev;
	unsigned int brightness;
	resource_size_t reg_start, reg_size;
	unsigned char __iomem *svb_mmreg;	/* svb: memory mapped registers */
};

/* ============================================================================== */
static struct svb *svb_device;
/* ============================================================================== */

static int max_brightness = 24;

static int emulbl_get_intensity(struct backlight_device *bd)
{
	return svb_device->brightness;
	//	return svb_device->brightness = (unsigned int)readl(svb_device->svb_mmreg);
}

static int emulbl_send_intensity(struct backlight_device *bd)
{
	int intensity = bd->props.brightness;

	if (bd->props.power != FB_BLANK_UNBLANK)
		intensity = 0;
	if (bd->props.state & BL_CORE_FBBLANK)
		intensity = 0;
	if (bd->props.state & BL_CORE_SUSPENDED)
		intensity = 0;
//	if (bd->props.state & GENERICBL_BATTLOW)
//		intensity &= bl_machinfo->limit_mask;

	writel(intensity, svb_device->svb_mmreg);
	svb_device->brightness = intensity;

	return 0;
}

static struct backlight_ops emulbl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = emulbl_get_intensity,
	.update_status  = emulbl_send_intensity,
};

/* pci probe function
*/
static int __devinit svb_probe(struct pci_dev *pci_dev,
			const struct pci_device_id *ent)
{
	int ret;
	struct backlight_device *bd;

	svb_device = kmalloc(sizeof(struct svb), GFP_KERNEL);
	if (svb_device == NULL) {
		printk(KERN_ERR "svb: kmalloc() is failed.\n");
		return -1;
	}

	memset(svb_device, 0, sizeof(struct svb));

	if ((ret = pci_enable_device(pci_dev)) < 0) {
		printk(KERN_ERR "svb: pci_enable_device is failed.\n");
		goto outnotdev;
	}

	ret = -EIO;

	/* 1 : IORESOURCE_MEM */
	if (!request_mem_region(svb_device->reg_start = pci_resource_start(pci_dev, 1),
							svb_device->reg_size  = pci_resource_len(pci_dev, 1),
							SVB_DRIVER_NAME)) {
		goto outnotdev;
	}

	/* memory areas mapped kernel space */
	svb_device->svb_mmreg = ioremap(svb_device->reg_start, svb_device->reg_size);
	if (!svb_device->svb_mmreg) {
		goto outnotdev;
	}

	//pci_write_config_byte(pci_dev, PCI_CACHE_LINE_SIZE, 8);
	pci_write_config_byte(pci_dev, PCI_LATENCY_TIMER, 64);
	pci_set_master(pci_dev);

	/*
	 * register backlight device
	 */
	bd = backlight_device_register ("emulator",	&pci_dev->dev, NULL, &emulbl_ops);
	if (IS_ERR (bd)) {
		ret = PTR_ERR (bd);
		goto outnotdev;
	}

	bd->props.brightness = (unsigned int)readl(svb_device->svb_mmreg);;
	bd->props.max_brightness = max_brightness;
	bd->props.power = FB_BLANK_UNBLANK;
	backlight_update_status(bd);

	svb_device->bl_dev = bd;

	printk(KERN_INFO "svb: Samsung Virtual Backlight driver.\n");
	return 0;

outnotdev:
	if (svb_device->svb_mmreg)
		iounmap(svb_device->svb_mmreg);
	kfree(svb_device);
	return ret;
}

static void __devexit svb_exit(struct pci_dev *pcidev)
{
	/*
	 * Unregister backlight device
	 */
	struct backlight_device *bd = svb_device->bl_dev;

	bd->props.power = 0;
	bd->props.brightness = 0;
	backlight_update_status(bd);

	backlight_device_unregister(bd);

	/*
	 * Unregister pci device & delete device
	 */
	iounmap(svb_device->svb_mmreg);
	pci_disable_device(pcidev);
	kfree(svb_device);
}

/*
 * register pci driver
 */
static struct pci_driver svb_pci_driver = {
	.name 	  = SVB_DRIVER_NAME,
	.id_table = svb_pci_table,
	.probe	  = svb_probe,
	.remove   = __devexit_p(svb_exit),
#ifdef CONFIG_PM
	//.suspend  = svb_suspend,
	//.resume   = svb_resume,
#endif
};

static int __init emulbl_init(void)
{
	return pci_register_driver(&svb_pci_driver);
}

static void __exit emulbl_exit(void)
{
	pci_unregister_driver(&svb_pci_driver);
}

/*
 * if this is compiled into the kernel, we need to ensure that the
 * class is registered before users of the class try to register lcd's
 */
module_init(emulbl_init);
module_exit(emulbl_exit);

MODULE_LICENSE("GPL2");
MODULE_DESCRIPTION("Emulator Virtual Backlight Driver for x86");
