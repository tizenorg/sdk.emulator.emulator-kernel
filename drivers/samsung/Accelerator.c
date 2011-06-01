#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/sched.h>

#include <asm/io_32.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#define DRIVER_NAME "HW_Accelerator"
#define PCI_DEVICE_ID_VIRTIO_OPENGL 0x1004

#define ACCEL_DEV_MAJOR 240
#define ACCEL_DEV_NAME "opengl"

#define MIN(x,y) ((x<y)?x:y)

void __iomem *base_addr;
unsigned long memory_size;

struct accel_param {
    unsigned int function_number;
    unsigned int pid;
    void *ret_string;
    void *args;
    void *args_size;
};

struct accel_dev {
    void __iomem *ioaddr;
    struct cdev cdev;
    int nreaders, nwriters;
};

static unsigned long v2p(unsigned long va/*, int pid*/)
{
        unsigned long pa = 0;
        struct task_struct *pcb_tmp = current;
        pgd_t *pgd_tmp = NULL;
        pud_t *pud_tmp = NULL;
        pmd_t *pmd_tmp = NULL;
        pte_t *pte_tmp = NULL;
#if 0
        printk(KERN_INFO"PAGE_OFFSET = 0x%lx\n",PAGE_OFFSET);
        printk(KERN_INFO"PGDIR_SHIFT = %d\n",PGDIR_SHIFT);
        printk(KERN_INFO"PUD_SHIFT = %d\n",PUD_SHIFT);
        printk(KERN_INFO"PMD_SHIFT = %d\n",PMD_SHIFT);
        printk(KERN_INFO"PAGE_SHIFT = %d\n",PAGE_SHIFT);

        printk(KERN_INFO"PTRS_PER_PGD = %d\n",PTRS_PER_PGD);
        printk(KERN_INFO"PTRS_PER_PUD = %d\n",PTRS_PER_PUD);
        printk(KERN_INFO"PTRS_PER_PMD = %d\n",PTRS_PER_PMD);
        printk(KERN_INFO"PTRS_PER_PTE = %d\n",PTRS_PER_PTE);

        printk(KERN_INFO"PAGE_MASK = 0x%lx\n",PAGE_MASK);

        if(!(pcb_tmp = find_task_by_pid(pid))) {
                printk(KERN_INFO"Can't find the task %d .\n",pid);
                return 0;
        }
	
        printk(KERN_INFO"pgd = 0x%p\n",pcb_tmp->mm->pgd);
#endif
        if(!find_vma(pcb_tmp->mm,va)){
                printk(KERN_INFO"virt_addr 0x%lx not available.\n",va);
                return 0;
        }

        pgd_tmp = pgd_offset(pcb_tmp->mm,va);
//        printk(KERN_INFO"pgd_tmp = 0x%p\n",pgd_tmp);
//        printk(KERN_INFO"pgd_val(*pgd_tmp) = 0x%lx\n",pgd_val(*pgd_tmp));
        if(pgd_none(*pgd_tmp)){
                printk(KERN_INFO"Not mapped in pgd.\n");        
                return 0;
        }
        pud_tmp = pud_offset(pgd_tmp,va);
//        printk(KERN_INFO"pud_tmp = 0x%p\n",pud_tmp);
//        printk(KERN_INFO"pud_val(*pud_tmp) = 0x%lx\n",pud_val(*pud_tmp));
        if(pud_none(*pud_tmp)){
                printk(KERN_INFO"Not mapped in pud.\n");
                return 0;
        }
        pmd_tmp = pmd_offset(pud_tmp,va);
//        printk(KERN_INFO"pmd_tmp = 0x%p\n",pmd_tmp);
//        printk(KERN_INFO"pmd_val(*pmd_tmp) = 0x%lx\n",pmd_val(*pmd_tmp));
        if(pmd_none(*pmd_tmp)){
                printk(KERN_INFO"Not mapped in pmd.\n");
                return 0;
        }

        pte_tmp = pte_offset_kernel(pmd_tmp,va);

//        printk(KERN_INFO"pte_tmp = 0x%p\n",pte_tmp);
//        printk(KERN_INFO"pte_val(*pte_tmp) = 0x%lx\n",pte_val(*pte_tmp));
        if(pte_none(*pte_tmp)){
                printk(KERN_INFO"Not mapped in pte.\n");
                return 0;
        }
        if(!pte_present(*pte_tmp)){
                printk(KERN_INFO"pte not in RAM.\n");
                return 0;
        }
        pa = (pte_val(*pte_tmp) & PAGE_MASK) |(va & ~PAGE_MASK);
//        printk(KERN_INFO"virt_addr 0x%lx in RAM is 0x%lx .\n",va,pa);
//        printk(KERN_INFO"contect in 0x%lx is 0x%lx\n",pa,
//                *(unsigned long *)((char *)pa + PAGE_OFFSET));
                                                        
        return pa;

}

static int check_phy_contiguous(unsigned long addr, unsigned long len)
{
    unsigned long phyaddr;
    unsigned long lastphyaddr;

    if((phyaddr = v2p(addr)) == 0UL)
	return 0;
    lastphyaddr = phyaddr;
    len = (len>PAGE_SIZE) ? len-PAGE_SIZE:0;
    addr += PAGE_SIZE;
    while(len)
    {
	if((phyaddr = v2p(addr)) == 0UL)
	    return 0;				/* Not mapped */
	if(lastphyaddr+PAGE_SIZE != phyaddr)
	    return 2;				/* discontiguous */
	lastphyaddr=phyaddr;
	addr += PAGE_SIZE;
	len = (len>PAGE_SIZE) ? len-PAGE_SIZE:0;
    }
    return 1;					/* contiguous */
}

static int memcpy_virt_to_phys(void *phys_addr, unsigned long virt_addr, int len)
{
    int i;
    unsigned long aligned_target_addr = (virt_addr & ~(PAGE_SIZE - 1));
    int to_end_page = (long)aligned_target_addr + PAGE_SIZE - (long)virt_addr;
    if((unsigned long)aligned_target_addr != virt_addr)
    {
	memcpy(phys_addr, (void *)virt_addr, MIN(len, to_end_page));
	if(len <= to_end_page)
	    return 1;
	len -= to_end_page;
	phys_addr += to_end_page;
	virt_addr = aligned_target_addr + PAGE_SIZE;
    }
    for(i=0;i<len;i+=PAGE_SIZE)
    {
        memcpy(phys_addr + i, (void *)virt_addr + i, (i + PAGE_SIZE <= len) ? PAGE_SIZE : len & (PAGE_SIZE -1));
    }

    return 0;
}

static struct accel_dev *accel_device;

static struct pci_device_id accel_pci_ids[] = {
    {PCI_VENDOR_ID_SAMSUNG, PCI_DEVICE_ID_VIRTIO_OPENGL, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
    {0,}
};

static int accel_open(struct inode *inode, struct file *file)
{
    if(!try_module_get(THIS_MODULE)) return -ENODEV;
    return 0;
}

static ssize_t accel_write(struct file *file, char __user *buf, size_t count, loff_t *fops)
{
    void __iomem *ioaddr;

    struct accel_param accel_param;
    struct accel_param *test;
    unsigned long *args, *args_size;
    char *args_temp = NULL;
    int loop;

    ioaddr = base_addr;

    copy_from_user((void *)&accel_param, (void *)buf, sizeof(struct accel_param));
    
    args = (unsigned long *)accel_param.args;
    args_size = (unsigned long *)accel_param.args_size;

    for(loop=0;loop<count;loop++)
    {
	if(*(args_size+loop) <= 0)
	    continue;

	if((check_phy_contiguous(*(args+loop), *(args_size+loop))) == 2)
	{
	    args_temp = kmalloc(*(args_size+loop), GFP_KERNEL);
	    if(args_temp == NULL)
	    {
		printk(KERN_ERR "Memory insufficient\n");
		return -1;
	    }
	    memcpy_virt_to_phys(args_temp, *(args+loop), *(args_size+loop));
	    *(args+loop) = virt_to_phys(args_temp);
	    kfree(args_temp);
	}
	else
	    *(args+loop) = v2p(*(args+loop));
    }
   
#if 0
    for(loop=0;loop<count;loop++)
    {
	printk(KERN_INFO "args[%d]=0x%x, args_size[%d]=%x\n", loop, *(args+loop), loop, *(args_size+loop));
	if(args_temp != NULL)
	    printk(KERN_INFO "tmp_args=0x%x\n", *(args_temp + loop));
    }
#endif

    writew(accel_param.function_number, ioaddr);
    writew(accel_param.pid, ioaddr);
    writel((uint32_t)v2p(accel_param.ret_string), ioaddr);
    writel((uint32_t)v2p(args), ioaddr);
    writel((uint32_t)v2p(accel_param.args_size), ioaddr);

    return 0;
}

static ssize_t accel_read (struct file *file, char __user *buf, size_t count, loff_t *fops)
{
/*
    struct accel_dev *dev = file->private_data;
    if(copy_to_user(buf, (void *)dev->function_number, count) < 0)
    {
	printk(KERN_INFO "Read fail\n");
	return -1;
    }
*/
    void __iomem *ioaddr;
    unsigned int read_byte;

    ioaddr = base_addr;

    read_byte = readb(ioaddr+0x10);
    return read_byte;
}

static int accel_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long argu)
{
    return 0;
}

static int accel_release(struct inode *inode, struct file *file)
{
    module_put(THIS_MODULE);
    return 0;
}

struct file_operations accel_fops = {
    .owner =	THIS_MODULE,
    .llseek =	no_llseek,
    .read =	accel_read,
    .write =	accel_write,
    .open =	accel_open,
    .release =	accel_release,
    .ioctl =	accel_ioctl,
};

static void __devexit accel_pci_remove(struct pci_dev *pdev)
{
    unregister_chrdev(ACCEL_DEV_MAJOR, ACCEL_DEV_NAME);
    pci_release_region(pdev, 0);
    pci_disable_device(pdev);
}

static int __devinit accel_pci_probe(struct pci_dev *pdev, const struct pci_device_id *pci_id)
{
    void __iomem *ioaddr;
    int ped, pr2, disable_dev_on_err = 0;
    int rc = 0;
    unsigned long pio_start, pio_end, pio_flags, pio_len;
    unsigned long mmio_start, mmio_end, mmio_flags, mmio_len;
    dev_t dev;

    ped = pci_enable_device(pdev);
    if(ped)
	goto err_out;

    pio_start = pci_resource_start(pdev, 0);
    pio_end = pci_resource_end(pdev, 0);
    pio_flags = pci_resource_flags(pdev, 0);
    pio_len = pci_resource_len(pdev, 0);

    mmio_start = pci_resource_start(pdev, 1);
    mmio_end = pci_resource_end(pdev, 1);
    mmio_flags = pci_resource_flags(pdev, 1);
    mmio_len = pci_resource_flags(pdev, 1);

    pr2 = pci_request_regions(pdev, ACCEL_DEV_NAME);
    if(pr2)
	goto err_out;
    disable_dev_on_err = 1;
    
    pci_set_master(pdev);

    ioaddr = (void *)pci_iomap(pdev, 1, 0);
    if(ioaddr == NULL)
    {
	dev_err(&pdev->dev, "Cannot remap MMIO, aborting\n");
	rc = -EIO;
	goto err_out;
    }

    base_addr = ioaddr;
    memory_size = mmio_len;

    dev = MKDEV(ACCEL_DEV_MAJOR, 0);
    rc = register_chrdev_region(dev, 1, ACCEL_DEV_NAME);

    if(rc < 0)
    {
	rc = -ENODEV;
	goto err_out;
    }
   
    accel_device = kmalloc(sizeof(struct accel_dev), GFP_KERNEL);
    if(accel_device == NULL)
    {
	unregister_chrdev_region(dev, 1);
	return 0;
    }

    memset(accel_device, 0, sizeof(struct accel_dev));

    cdev_init(&accel_device->cdev, &accel_fops);
    accel_device->cdev.owner = THIS_MODULE;
    accel_device->cdev.ops = &accel_fops;
    
    rc = cdev_add(&accel_device->cdev, dev, 1);
    if(rc)
    {
	rc = -ENODEV;
	goto err_out;
    }

    return 0;

err_out:
    if(disable_dev_on_err)
	pci_disable_device(pdev);
    return rc;
}

static struct pci_driver accel_driver = {
    .name = DRIVER_NAME,
    .id_table = accel_pci_ids,
    .probe = accel_pci_probe,
    .remove = accel_pci_remove,
};

MODULE_DEVICE_TABLE(pci, accel_pci_ids);

static int __init card_accel_init(void)
{
    printk(KERN_INFO "Accel init\n");
    return pci_register_driver(&accel_driver);
}

static void __exit card_accel_exit(void)
{
    pci_unregister_driver(&accel_driver);
}
    
module_init(card_accel_init)
module_exit(card_accel_exit)
