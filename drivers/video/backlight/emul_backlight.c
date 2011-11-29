/*
 * Backlight Lowlevel Control Abstraction
 *
 * Copyright (C) 2003,2004 Hewlett-Packard Company
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/ctype.h>
#include <linux/err.h>

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
	struct pci_dev *pci_dev;
	unsigned int brightness_level;

	resource_size_t mem_start, reg_start;
	resource_size_t mem_size, reg_size;

	unsigned char __iomem *svb_mmreg;	/* svb: memory mapped registers */
};

/* ============================================================================== */
static struct svb *svb_device;
/* ============================================================================== */

static struct class *emul_backlight_class;
static struct device *emul_backlight_dev;

static int brightness;
static int max_brightness = 24;

static ssize_t bl_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	/* handover to qemu layer */
	brightness = (unsigned int)readl(svb_device->svb_mmreg);
	printk(KERN_INFO "%s: brightness = %d\n", __FUNCTION__, brightness);

	return sprintf(buf, "%d\n", brightness);
}

static ssize_t bl_brightness_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int rc;
	unsigned long brightness_level;

	rc = strict_strtoul(buf, 0, &brightness_level);
	if (rc)
		return rc;

	rc = -ENXIO;

	if (brightness_level > max_brightness) {
		rc = -EINVAL;
	} else {
		printk(KERN_INFO "backlight: set brightness to %lu\n", brightness_level);
		brightness = brightness_level;

		/* handover to qemu layer */
		writel(brightness, svb_device->svb_mmreg);

		rc = count;
	}
	return rc;
}

static ssize_t bl_max_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", max_brightness);
}

static ssize_t bl_max_brightness_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return 0;
}

static DEVICE_ATTR(brightness, 0664, bl_brightness_show, bl_brightness_store);
static DEVICE_ATTR(max_brightness, 0664, bl_max_brightness_show, NULL);

static struct device_attribute *emul_bl_device_attrib[] = {
		&dev_attr_brightness,
		&dev_attr_max_brightness,
};

/* pci probe function
*/
static int __devinit svb_probe(struct pci_dev *pci_dev,
			const struct pci_device_id *ent)
{
	int ret = -EBUSY;

	svb_device = kmalloc(sizeof(struct svb), GFP_KERNEL);
	if (svb_device == NULL) {
		printk(KERN_ERR "svb: kmalloc() is failed.\n");
		return -1;
	}

	memset(svb_device, 0, sizeof(struct svb));

	if (svb_device->pci_dev != NULL) {
		printk(KERN_ERR "[svb] Only one device is allowed.\n");
		goto outnotdev;
	}

	svb_device->pci_dev = pci_dev;

	ret = -EIO;
	if ((ret = pci_enable_device(svb_device->pci_dev)) < 0) {
		printk(KERN_ERR "svb: pci_enable_device is failed.\n");
		goto outnotdev;
	}

	/* 0 : IORESOURCE_IO */
	svb_device->mem_start = pci_resource_start(svb_device->pci_dev, 0);
	svb_device->mem_size = pci_resource_len(svb_device->pci_dev, 0);

	if (!request_mem_region(pci_resource_start(svb_device->pci_dev, 0),
				pci_resource_len(svb_device->pci_dev, 0),
				SVB_DRIVER_NAME)) {
	}

	/* 1 : IORESOURCE_MEM */
	svb_device->reg_start = pci_resource_start(svb_device->pci_dev, 1);
	svb_device->reg_size = pci_resource_len(svb_device->pci_dev, 1);

	if (!request_mem_region(pci_resource_start(svb_device->pci_dev, 1),
				pci_resource_len(svb_device->pci_dev, 1),
				SVB_DRIVER_NAME)) {
	}

	/* memory areas mapped kernel space */
	svb_device->svb_mmreg = ioremap(svb_device->reg_start, svb_device->reg_size);
	if (!svb_device->svb_mmreg) {
	}

	//pci_write_config_byte(svb_device->pci_dev, PCI_CACHE_LINE_SIZE, 8);
	pci_write_config_byte(svb_device->pci_dev, PCI_LATENCY_TIMER, 64);
	pci_set_master(svb_device->pci_dev);

	printk(KERN_INFO "svb: Samsung Virtual Backlight driver.\n");
	return 0;

outnotdev:
	kfree(svb_device);
	return ret;
}

static void __devexit svb_exit(struct pci_dev *pcidev)
{
	iounmap(svb_device->svb_mmreg);
	pci_disable_device(pcidev);
	kfree(svb_device);
}

/* register pci driver
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

static int backlight_svb_init(void)
{
	return pci_register_driver(&svb_pci_driver);
}

static int __init emul_backlight_class_init(void)
{
	int i, ret;

	emul_backlight_class = class_create(THIS_MODULE, "backlight");
	if (IS_ERR(emul_backlight_class)) {
		printk(KERN_WARNING "Unable to create backlight class; errno = %ld\n",
				PTR_ERR(emul_backlight_class));
		return PTR_ERR(emul_backlight_class);
	}

	emul_backlight_dev = device_create(emul_backlight_class, NULL, NULL, NULL, "emulator");

	for (i=0; i < ARRAY_SIZE(emul_bl_device_attrib); i++) {
		ret = device_create_file(emul_backlight_dev, emul_bl_device_attrib[i]);
		if (ret != 0) {
			printk(KERN_ERR "emul_bl: Failed to create attr %d: %d\n", i, ret);
			return ret;
		}
	}

	/* related to virtual pci device */
	backlight_svb_init();

	return 0;
}

static void __exit emul_backlight_class_exit(void)
{
	class_destroy(emul_backlight_class);
}

/*
 * if this is compiled into the kernel, we need to ensure that the
 * class is registered before users of the class try to register lcd's
 */
//postcore_initcall(emul_backlight_class_init);
module_init(emul_backlight_class_init);
module_exit(emul_backlight_class_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("s-core");
MODULE_DESCRIPTION("Emulator virtual backlight driver for x86");
