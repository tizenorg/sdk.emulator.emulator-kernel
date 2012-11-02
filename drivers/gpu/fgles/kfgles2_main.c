/* Copyright (c) 2009-2010 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) any later version of the License.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>

#include "kfgles2_hcalls.h"

/* TODO: figure out default value dynamically. */
static unsigned int kfgles2_abi = CONFIG_TIZEN_FGLES_FLOAT_ABI;
module_param(kfgles2_abi, uint, 0644);
MODULE_PARM_DESC(kfgles2_abi, "procedure call interface");

static unsigned int kfgles2_last_client = 0;

#ifdef CONFIG_TIZEN_FGLES_DEBUG
static bool debug = true;
#else
static bool debug = false;
#endif

module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "enable debug prints");

#define KFGLES2_PRINT(format, args...) \
	do { if (debug) printk(KERN_DEBUG "kfgles2: DEBUG: " format, ##args); } while (0)

#if CONFIG_TIZEN_FGLES_FLOAT_ABI != 1 && CONFIG_TIZEN_FGLES_FLOAT_ABI != 2
#error fake GLES module float ABI wrong value!
#endif

typedef enum 
{
	EGL  = 0,
	ES11 = 1,
	ES20 = 2,
	VG1  = 3,
} API_ENUM;

#define MAX_APIS 10
//EGL always needs to exist and be the first
static API_ENUM KFGLES2_SUPPORTED_API[] = {EGL,ES11,ES20,VG1};

#define KFGLES2_DEVICE "kfgles2"
static int kfgles2_minor = CONFIG_TIZEN_FGLES_MINOR;
module_param(kfgles2_minor, uint, 0644);
MODULE_PARM_DESC(kfgles2_minor, "Minor number to be used when registering miscdev");

static unsigned long kfgles2_hwbase = 0;

#define KFGLES2_HWSIZE       0x00100000
#define KFGLES2_NUM_API      (sizeof(KFGLES2_SUPPORTED_API) / sizeof(API_ENUM))
#define KFGLES2_BLOCKSIZE    (KFGLES2_HWSIZE / KFGLES2_NUM_API) //num APIs + EGL

static unsigned long KFGLES2_BASE_ADDR[MAX_APIS];
/*
   Value set through ioctl system call
*/
static unsigned int kfgles2_api = EGL;

/* Client specific data holder. */
struct kfgles2_client {
	uint32_t nr;
	void* offset;
	unsigned long buffer;
	int count;
};

void __iomem *kfgles2_base_egl;
static DEFINE_MUTEX(kfgles2_mutex);

static void kfgles2_vopen(struct vm_area_struct *vma)
{
	struct kfgles2_client *client;
	mutex_lock(&kfgles2_mutex);
	client=vma->vm_private_data;
	client->count++;
	KFGLES2_PRINT("client %d: one user more! %d.\n", client->nr, client->count);
	mutex_unlock(&kfgles2_mutex);
}

/* Release a mapped register area, and disconnect the client. */
static void kfgles2_vclose(struct vm_area_struct *vma)
{
	struct kfgles2_client *client;
	mutex_lock(&kfgles2_mutex);
	client=vma->vm_private_data;

	KFGLES2_PRINT("munmap called!\n");

	if (client) {
		if (client->count > 1) {
			KFGLES2_PRINT("client %d: one user less! %d.\n", client->nr, client->count);
			client->count--;
		} else {

			KFGLES2_PRINT("Exiting client ID %d.\n", client->nr);
			kfgles2_host_exit_egl(client->nr);
		
		
			KFGLES2_PRINT("Freeing...\n");
			kfree(client);
		}
	}
	
	vma->vm_private_data = 0;
	mutex_unlock(&kfgles2_mutex);
}

/* Operations for kernel to deal with a mapped register area. */
static struct vm_operations_struct kfgles2_vops =
{
	.close = kfgles2_vclose,
	.open = kfgles2_vopen,
};

/* Nothing to do when opening the file. */
static int kfgles2_open(struct inode *inode, struct file *filep)
{
	return 0;
}

/* Nothing to do when closing the file. */
static int kfgles2_release(struct inode *inode, struct file *filep)
{
	return 0;
}
/* Used to set the API, es11/20 */
static long kfgles2_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	kfgles2_api = cmd;
	KFGLES2_PRINT("ioctl called: cmd=%d, kfgles2_api=%d\n",cmd,kfgles2_api);
	return 0;		
}

/* Map a register area for connecting client. */
static int kfgles2_mmap(struct file *filep, struct vm_area_struct *vma)
{
	struct kfgles2_client *client;
	int ret;
	unsigned long pfn;

	mutex_lock(&kfgles2_mutex);
	KFGLES2_PRINT("mmap called!\n");

	KFGLES2_PRINT("remap_pfn_range: vm_start=0x%lx, vm_end=0x%lx, vm_pgoff=0x%lx\n",vma->vm_start, vma->vm_end, vma->vm_pgoff);
	client = 0;
	if (!vma->vm_pgoff) {
		KFGLES2_PRINT("EGL root requested!\n");
		kfgles2_last_client = 0;
		kfgles2_api = 0;
	} else {
		kfgles2_api = (unsigned int)(vma->vm_pgoff -1);
		switch (kfgles2_api) {
			case EGL:
				KFGLES2_PRINT("Requesting EGL client block..\n");
				if (!(client = kmalloc(sizeof(*client), GFP_KERNEL))) {
					return -ENOMEM;
				}
				client->nr = kfgles2_host_init_egl(kfgles2_abi);
				client->count = 1;
				kfgles2_last_client = client->nr;
				KFGLES2_PRINT("-> Got %d!\n", client->nr);
				break;
			case ES11:
				KFGLES2_PRINT("Requesting ES11 client block..\n");
				break;
			case ES20:
				KFGLES2_PRINT("Requesting ES20 client block..\n");
				break;
			default:
				KFGLES2_PRINT("Error: Requesting UNKNOWN client block!..\n");
				ret = -1;
				goto failure;
				break;
		}			
	}
	
	
	pfn =  ((KFGLES2_BASE_ADDR[kfgles2_api]) >> PAGE_SHIFT) + kfgles2_last_client + 1;
	vma->vm_flags |= VM_IO | VM_RESERVED;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if ((ret = remap_pfn_range(vma,
		vma->vm_start,
		pfn,
		vma->vm_end - vma->vm_start,
		vma->vm_page_prot)) < 0)
	{
		KFGLES2_PRINT("remap failed!\n");
		goto failure;
	}

	KFGLES2_PRINT("remap_pfn_range: vm_start=0x%lx, vm_end=0x%lx, pfn=0x%lx, vm_pgoff=0x%lx, ret=0x%x\n",vma->vm_start, vma->vm_end, pfn, vma->vm_pgoff, ret);
	if (client) {
		vma->vm_private_data = client;
		vma->vm_ops = &kfgles2_vops;
    }
	if (kfgles2_last_client)
		KFGLES2_PRINT("remap successfull for client %d block %d!\n", kfgles2_last_client, kfgles2_api);
	else
		KFGLES2_PRINT("remap successfull for root!\n");
	
	mutex_unlock(&kfgles2_mutex);
	return 0;
failure:
	mutex_unlock(&kfgles2_mutex);
	kfree(client);
	return ret;
}

/* Operations for kernel to deal with the device file. */
static const struct file_operations kfgles2_fops = {
	.owner          = THIS_MODULE,
	.open           = kfgles2_open,
	.release        = kfgles2_release,
	.mmap           = kfgles2_mmap,
	.unlocked_ioctl = kfgles2_ioctl,
};

static struct miscdevice kfgles2_miscdev = {
	.name = KFGLES2_DEVICE,
	.fops = &kfgles2_fops
};

/* Module initialization. */
static int __devinit kfgles2_probe(struct platform_device *pdev)
{
	int err = 0, i;
	struct resource *mem;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mem) {
		kfgles2_hwbase = mem->start;
		if (!devm_request_mem_region(&pdev->dev, mem->start, KFGLES2_HWSIZE,
				pdev->name)) {
			return -EBUSY;
		}
	} else {
		if (!kfgles2_hwbase) {
			return -EINVAL;
		}
		if (!devm_request_mem_region(&pdev->dev, kfgles2_hwbase, KFGLES2_HWSIZE,
				pdev->name)) {
			return -EBUSY;
		}
	}

	for ( i = 0; i < KFGLES2_NUM_API; i++)
	{
		KFGLES2_BASE_ADDR[i] = kfgles2_hwbase + KFGLES2_BLOCKSIZE * i;
		KFGLES2_PRINT("Base %d = %lx.\n", i, KFGLES2_BASE_ADDR[i]);
	}

	printk(KERN_INFO "loading kfgles2 module (%s).\n",
		(kfgles2_abi == 1) ? "arm_softfp"
		: (kfgles2_abi == 2) ? "arm_hardfp" : "unknown abi");
	kfgles2_miscdev.minor = kfgles2_minor;

	if (!(kfgles2_base_egl = ioremap(KFGLES2_BASE_ADDR[EGL], KFGLES2_HWSIZE)))
	{
		KFGLES2_PRINT("ERROR: failed to map EGL hardware block.\n");
		return -ENOMEM;
	}
	
	if (misc_register(&kfgles2_miscdev) < 0)
		goto out_map;

	mutex_init(&kfgles2_mutex);

	goto out;

out_map:
	iounmap(kfgles2_base_egl);
out:
	return err;
}

/* Module cleanup. */ 
static int __devexit kfgles2_remove(struct platform_device *pdev)
{
	int ret = 0;
	printk(KERN_INFO "kfgles2 module removed.\n");

	if ((ret = misc_deregister(&kfgles2_miscdev))) {
		return ret;
	}
	if(kfgles2_base_egl) {
		iounmap(kfgles2_base_egl);
		kfgles2_base_egl = 0;
	}
	return ret;
}

/* Platform driver */

static struct of_device_id kfgles2_match[] = {
	{ .compatible = "kfgles2", },
	{},
};
MODULE_DEVICE_TABLE(of, kfgles2_match);

static struct platform_driver kfgles2_driver = {
	.probe		= kfgles2_probe,
	.remove		= __devexit_p(kfgles2_remove),
	.driver		= {
		.name	= "kfgles2",
		.owner	= THIS_MODULE,
		.of_match_table	= kfgles2_match,
	},
};

static int __init kfgles2_init(void)
{
	return platform_driver_register(&kfgles2_driver);
}

static void __exit kfgles2_exit(void)
{
	platform_driver_unregister(&kfgles2_driver);
}

module_init(kfgles2_init);
module_exit(kfgles2_exit);

MODULE_AUTHOR("Jean Fairlie <jean.fairlie at nomovok.com>");
MODULE_AUTHOR("Joonas Lahtinen <joonas.lahtinen at nomovok.com>");
MODULE_AUTHOR("Pablo Virolainen <pablo.virolainen at nomovok.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("QEMU OpenGL ES accelerator module");
