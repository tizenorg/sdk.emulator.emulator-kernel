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

static struct pci_device_id svo_pci_tbl[] = {
	{
		.vendor       = PCI_VENDOR_ID_SAMSUNG,
		.device       = 0x1010,
		.subvendor    = PCI_ANY_ID,
		.subdevice    = PCI_ANY_ID,
	}
};

static int __devinit svo_initdev(struct pci_dev *pci_dev,
				    const struct pci_device_id *pci_id)
{
	return 0;
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
#if 0
	pci_unregister_driver(&cx8800_pci_driver);
#endif
}

module_init(svo_init);
module_exit(svo_fini); 
