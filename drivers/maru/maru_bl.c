/*
 * MARU Virtual Backlight Driver
 *
 * Copyright (c) 2011 - 2013 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 *  Jinhyung Jo <jinhyung.jo@samsung.com>
 *  YeongKyoon Lee <yeongkyoon.lee@samsung.com>
 *  Dohyung Hong
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
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

#include <linux/uaccess.h>

#define MARUBL_DRIVER_NAME			"maru_backlight"

#define MIN_BRIGHTNESS	0
#define MAX_BRIGHTNESS	100

static struct pci_device_id marubl_pci_table[] __devinitdata = {
	{
		.vendor		= PCI_VENDOR_ID_TIZEN,
		.device		= PCI_DEVICE_ID_VIRTUAL_BRIGHTNESS,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, marubl_pci_table);

/* MARU virtual brightness(backlight) device structure */
struct marubl {
	struct backlight_device		*bl_dev;
	unsigned int			brightness;
	resource_size_t			reg_start, reg_size;
	/* memory mapped registers */
	unsigned char __iomem		*marubl_mmreg;
};

/* ========================================================================== */
static struct marubl *marubl_device;
/* ========================================================================== */

static int min_brightness = MIN_BRIGHTNESS;
static int max_brightness = MAX_BRIGHTNESS;

static int marubl_get_intensity(struct backlight_device *bd)
{
	return marubl_device->brightness;
}

static int marubl_send_intensity(struct backlight_device *bd)
{
	int intensity = bd->props.brightness;
	unsigned int off = 0;

	if (bd->props.power != FB_BLANK_UNBLANK) {
		intensity = 0;
		off = 1;
	}
	if (bd->props.state & BL_CORE_FBBLANK) {
		intensity = 0;
		off = 1;
	}
	if (bd->props.state & BL_CORE_SUSPENDED) {
		intensity = 0;
		off = 1;
	}

	writel(intensity, marubl_device->marubl_mmreg);
	writel(off, marubl_device->marubl_mmreg + 0x04);
	marubl_device->brightness = intensity;

	return 0;
}

static const struct backlight_ops marubl_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.get_brightness	= marubl_get_intensity,
	.update_status	= marubl_send_intensity,
};

/* pci probe function
*/
static int __devinit marubl_probe(struct pci_dev *pci_dev,
				  const struct pci_device_id *ent)
{
	int ret;
	struct backlight_device *bd;
	struct backlight_properties props;

	marubl_device = kmalloc(sizeof(struct marubl), GFP_KERNEL);
	if (marubl_device == NULL) {
		printk(KERN_ERR "marubl: kmalloc() is failed.\n");
		return -ENOMEM;
	}

	memset(marubl_device, 0, sizeof(struct marubl));

	ret = pci_enable_device(pci_dev);
	if (ret < 0) {
		printk(KERN_ERR "marubl: pci_enable_device is failed.\n");
		kfree(marubl_device);
		marubl_device = NULL;
		return ret;
	}

	ret = -EIO;

	/* 1 : IORESOURCE_MEM */
	marubl_device->reg_start = pci_resource_start(pci_dev, 1);
	marubl_device->reg_size  = pci_resource_len(pci_dev, 1);
	if (!request_mem_region(marubl_device->reg_start,
				marubl_device->reg_size,
				MARUBL_DRIVER_NAME)) {
		pci_disable_device(pci_dev);
		kfree(marubl_device);
		marubl_device = NULL;
		return ret;
	}

	/* memory areas mapped kernel space */
	marubl_device->marubl_mmreg = ioremap(marubl_device->reg_start,
					      marubl_device->reg_size);
	if (!marubl_device->marubl_mmreg) {
		release_mem_region(marubl_device->reg_start,
				   marubl_device->reg_size);
		pci_disable_device(pci_dev);
		kfree(marubl_device);
		marubl_device = NULL;
		return ret;
	}

	pci_write_config_byte(pci_dev, PCI_LATENCY_TIMER, 64);
	pci_set_master(pci_dev);

	/*
	 * register backlight device
	 */
	memset(&props, 0, sizeof(struct backlight_properties));
	props.min_brightness = min_brightness;
	props.max_brightness = max_brightness;
	props.type = BACKLIGHT_PLATFORM;
	bd = backlight_device_register("emulator",
				       &pci_dev->dev,
				       NULL,
				       &marubl_ops,
				       &props);
	if (IS_ERR(bd)) {
		ret = PTR_ERR(bd);
		iounmap(marubl_device->marubl_mmreg);
		release_mem_region(marubl_device->reg_start,
				   marubl_device->reg_size);
		pci_disable_device(pci_dev);
		kfree(marubl_device);
		marubl_device = NULL;
		return ret;
	}

	bd->props.brightness = (unsigned int)readl(marubl_device->marubl_mmreg);
	bd->props.power = FB_BLANK_UNBLANK;
	backlight_update_status(bd);

	marubl_device->bl_dev = bd;

	printk(KERN_INFO "marubl: MARU Virtual Backlight driver is loaded.\n");
	return 0;
}

static void __devexit marubl_exit(struct pci_dev *pcidev)
{
	/*
	 * Unregister backlight device
	 */
	struct backlight_device *bd = marubl_device->bl_dev;

	bd->props.power = 0;
	bd->props.brightness = 0;
	backlight_update_status(bd);

	backlight_device_unregister(bd);

	/*
	 * Unregister pci device & delete device
	 */
	iounmap(marubl_device->marubl_mmreg);
	release_mem_region(marubl_device->reg_start, marubl_device->reg_size);
	pci_disable_device(pcidev);
	kfree(marubl_device);
	marubl_device = NULL;
}

/*
 * register pci driver
 */
static struct pci_driver marubl_pci_driver = {
	.name		= MARUBL_DRIVER_NAME,
	.id_table	= marubl_pci_table,
	.probe		= marubl_probe,
	.remove		= __devexit_p(marubl_exit),
#ifdef CONFIG_PM
	/* .suspend  = marubl_suspend, */
	/* .resume   = marubl_resume, */
#endif
};

static int __init marubl_module_init(void)
{
	return pci_register_driver(&marubl_pci_driver);
}

static void __exit marubl_module_exit(void)
{
	pci_unregister_driver(&marubl_pci_driver);
}

/*
 * if this is compiled into the kernel, we need to ensure that the
 * class is registered before users of the class try to register lcd's
 */
module_init(marubl_module_init);
module_exit(marubl_module_exit);

MODULE_LICENSE("GPL2");
MODULE_AUTHOR("Jinhyung Jo <jinhyung.jo@samsung.com>");
MODULE_DESCRIPTION("MARU Virtual Backlight Driver for x86");
