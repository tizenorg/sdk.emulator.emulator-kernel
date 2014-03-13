/*
 * MARU Virtual Backlight Driver
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd. All rights reserved.
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
#include <linux/lcd.h>

#include <linux/uaccess.h>

#define MARUBL_DRIVER_NAME			"maru_backlight"

#define MIN_BRIGHTNESS	0
#define MAX_BRIGHTNESS	100

static struct pci_device_id marubl_pci_table[] = {
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
	struct lcd_device		*lcd_dev;
	unsigned int			prev_brightness;
	unsigned int			brightness;
	resource_size_t			reg_start, reg_size;
	/* memory mapped registers */
	unsigned char __iomem		*marubl_mmreg;
	int				power_off;
	int				hbm_on;
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
	if (marubl_device->hbm_on && !off && intensity != MAX_BRIGHTNESS) {
		marubl_device->hbm_on = 0;
		printk(KERN_INFO "HBM is turned off because brightness reduced.\n");
	}

	writel(intensity, marubl_device->marubl_mmreg);
	writel(off, marubl_device->marubl_mmreg + 0x04);
	marubl_device->brightness = intensity;
	marubl_device->power_off = off ? 1 : 0;

	return 0;
}

static const struct backlight_ops marubl_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.get_brightness	= marubl_get_intensity,
	.update_status	= marubl_send_intensity,
};

int maru_lcd_get_power(struct lcd_device *ld)
{
	int ret = 0;

	if (marubl_device->power_off) {
		ret = FB_BLANK_POWERDOWN;
	} else {
		ret = FB_BLANK_UNBLANK;
	}
	return ret;
}

static struct lcd_ops maru_lcd_ops = {
	.get_power = maru_lcd_get_power,
};

static ssize_t hbm_show_status(struct device *dev,
								struct device_attribute *attr,
								char *buf)
{
	int rc;

	rc = sprintf(buf, "%s\n", marubl_device->hbm_on ? "on" : "off");
	printk(KERN_INFO "[%s] get: %d\n", __func__, marubl_device->hbm_on);

	return rc;
}

static ssize_t hbm_store_status(struct device *dev,
								struct device_attribute *attr,
								const char *buf, size_t count)
{
	int ret = -1;

	if (strcmp(buf, "on") == 0) {
		ret = 1;
	} else if (strcmp(buf, "off") == 0) {
		ret = 0;
	} else {
		return -EINVAL;
	}

	/* If the same as the previous state, ignore it */
	if (ret == marubl_device->hbm_on)
		return count;

	if (ret) {
		/* Save previous level, set to MAX level */
		mutex_lock(&marubl_device->bl_dev->ops_lock);
		marubl_device->prev_brightness =
				marubl_device->bl_dev->props.brightness;
		marubl_device->bl_dev->props.brightness = MAX_BRIGHTNESS;
		marubl_send_intensity(marubl_device->bl_dev);
		mutex_unlock(&marubl_device->bl_dev->ops_lock);
	} else {
		/* Restore previous level */
		mutex_lock(&marubl_device->bl_dev->ops_lock);
		marubl_device->bl_dev->props.brightness =
						marubl_device->prev_brightness;
		marubl_send_intensity(marubl_device->bl_dev);
		mutex_unlock(&marubl_device->bl_dev->ops_lock);
	}
	marubl_device->hbm_on = ret;
	printk(KERN_INFO "[%s] hbm = %d\n", __func__, ret);

	return count;
}

static struct device_attribute hbm_device_attr =
						__ATTR(hbm, 0644, hbm_show_status, hbm_store_status);

/* pci probe function
*/
static int marubl_probe(struct pci_dev *pci_dev,
				  const struct pci_device_id *ent)
{
	int ret;
	struct backlight_device *bd;
	struct lcd_device *ld;
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
	 * register High Brightness Mode
	 */
	ret = device_create_file(&pci_dev->dev, &hbm_device_attr);
	if (ret < 0) {
		iounmap(marubl_device->marubl_mmreg);
		release_mem_region(marubl_device->reg_start,
				   marubl_device->reg_size);
		pci_disable_device(pci_dev);
		kfree(marubl_device);
		marubl_device = NULL;
		return ret;
	}

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

	ld = lcd_device_register("emulator", &pci_dev->dev, NULL, &maru_lcd_ops);
	if (IS_ERR(ld)) {
		ret = PTR_ERR(ld);
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
	marubl_device->lcd_dev = ld;

	printk(KERN_INFO "marubl: MARU Virtual Backlight driver is loaded.\n");
	return 0;
}

static void marubl_exit(struct pci_dev *pcidev)
{
	/*
	 * Unregister backlight device
	 */
	struct backlight_device *bd = marubl_device->bl_dev;
	struct lcd_device *ld = marubl_device->lcd_dev;

	bd->props.power = 0;
	bd->props.brightness = 0;
	backlight_update_status(bd);

	lcd_device_unregister(ld);
	backlight_device_unregister(bd);
	device_remove_file(&pcidev->dev, &hbm_device_attr);

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
	.remove		= marubl_exit,
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
