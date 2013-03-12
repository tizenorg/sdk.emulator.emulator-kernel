#include "yagl_driver.h"
#include "yagl_marshal.h"
#include "yagl_version.h"
#include "yagl.h"
#include "debug.h"
#include "print.h"
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/pci.h>

#define YAGL_REG_BUFFPTR 0
#define YAGL_REG_TRIGGER 4
#define YAGL_REGS_SIZE   8

#define YAGL_MAX_USERS (PAGE_SIZE / YAGL_REGS_SIZE)

#define YAGL_USER_PTR(regs, index) ((regs) + ((index) * YAGL_REGS_SIZE))

#define PCI_VENDOR_ID_YAGL 0x19B1
#define PCI_DEVICE_ID_YAGL 0x1010

static struct pci_device_id yagl_pci_table[] __devinitdata =
{
    {
        .vendor     = PCI_VENDOR_ID_YAGL,
        .device     = PCI_DEVICE_ID_YAGL,
        .subvendor  = PCI_ANY_ID,
        .subdevice  = PCI_ANY_ID,
    },
    { 0 }
};
MODULE_DEVICE_TABLE(pci, yagl_pci_table);

struct yagl_device
{
    /* PCI device we're managing */
    struct pci_dev *pci_dev;

    /* Misc device for accessing YaGL memory from user space */
    struct miscdevice miscdev;

    /* Physical address of YaGL registers. */
    unsigned long regs_pa;

    /* Memory area which is used for target <-> host communications */
    void __iomem *regs;

    /* 1 when user is active, 0 when slot can be used */
    int user_map[YAGL_MAX_USERS];

    /* Mutex used to serialize device operations */
    struct mutex mutex;
};

struct yagl_file
{
    /* Owning device */
    struct yagl_device *device;

    /* Index in 'user_map', filled on 'open' */
    int index;

    /* Buffer used for marshalling, allocated in mmap */
    u8 *buff;

    /* Buffer size */
    unsigned long buff_size;
};

static void yagl_user_activate(void __iomem *regs,
                               int index,
                               unsigned long buff_pa)
{
    writel(buff_pa, YAGL_USER_PTR(regs, index) + YAGL_REG_BUFFPTR);
}

static void yagl_user_deactivate(void __iomem *regs, int index)
{
    writel(0, YAGL_USER_PTR(regs, index) + YAGL_REG_BUFFPTR);
}

static int yagl_misc_open(struct inode *inode, struct file *file)
{
    int ret = 0;
    struct yagl_device *device = container_of( file->private_data,
                                               struct yagl_device,
                                               miscdev );

    struct yagl_file *yfile;
    int i;

    mutex_lock(&device->mutex);

    if (file->f_mode & FMODE_EXEC) {
        ret = -EPERM;
        goto fail;
    }

    yfile = kzalloc(sizeof(*yfile), GFP_KERNEL);

    if (!yfile) {
        dprintk("%s: unable to allocate memory\n", device->miscdev.name);
        ret = -ENOMEM;
        goto fail;
    }

    yfile->device = device;
    yfile->index = -1;

    for (i = 0; i < YAGL_MAX_USERS; ++i) {
        if (!device->user_map[i]) {
            yfile->index = i;
            device->user_map[i] = 1;
            break;
        }
    }

    if (yfile->index == -1) {
        dprintk("%s: no free slots\n", device->miscdev.name);
        ret = -ENOMEM;
        goto fail;
    }

    file->private_data = yfile;

    mutex_unlock(&device->mutex);

    dprintk("%s: %d opened\n", device->miscdev.name, yfile->index);

    return nonseekable_open(inode, file);

fail:
    mutex_unlock(&device->mutex);

    return ret;
}

static int yagl_misc_release(struct inode *inode, struct file *file)
{
    struct yagl_file *yfile = file->private_data;
    int index = yfile->index;

    mutex_lock(&yfile->device->mutex);

    yfile->device->user_map[yfile->index] = 0;
    yfile->index = -1;

    if (yfile->buff) {
        yagl_user_deactivate(yfile->device->regs, index);

        free_pages((unsigned long)yfile->buff, get_order(yfile->buff_size));
        yfile->buff = NULL;

        dprintk("%s: YaGL exited\n", yfile->device->miscdev.name);
    }

    mutex_unlock(&yfile->device->mutex);

    dprintk("%s: %d closed\n", yfile->device->miscdev.name, index);

    kfree(file->private_data);
    file->private_data = NULL;

    return 0;
}

static int yagl_misc_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct yagl_file *yfile = file->private_data;
    int num_pages = (vma->vm_end - vma->vm_start) / PAGE_SIZE;
    int ret = 0;
    u8 *buff = NULL;
    pid_t process_id;
    pid_t thread_id;

    mutex_lock(&yfile->device->mutex);

    if (vma->vm_pgoff == 0) {
        /*
         * First page is 'regs'.
         */

        if (num_pages != 1) {
            dprintk("%s: mmap must be called for 1 page only\n",
                    yfile->device->miscdev.name);
            ret = -EINVAL;
            goto out;
        }

        vma->vm_flags |= VM_IO | VM_RESERVED;
        vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

        ret = remap_pfn_range(vma,
                              vma->vm_start,
                              (yfile->device->regs_pa >> PAGE_SHIFT),
                              num_pages,
                              vma->vm_page_prot);

        if (ret != 0) {
            dprintk( "%s: unable to remap regs memory: %d\n",
                     yfile->device->miscdev.name,
                     ret );
            goto out;
        }
    } else if (vma->vm_pgoff == 1) {
        /*
         * Marshalling buffer.
         */

        int i;
        unsigned long addr;

        if (yfile->buff) {
            dprintk("%s: marshalling buffer already mapped\n",
                    yfile->device->miscdev.name);
            ret = -EIO;
            goto out;
        }

        buff = (u8*)__get_free_pages(GFP_USER|__GFP_COMP,
                                     get_order(vma->vm_end - vma->vm_start));

        if (!buff) {
            dprintk("%s: unable to alloc memory\n",
                    yfile->device->miscdev.name);
            ret = -ENOMEM;
            goto out;
        }

        vma->vm_flags |= VM_RESERVED | VM_DONTEXPAND;

        addr = vma->vm_start;

        for (i = 0; i < num_pages; i++) {
            ret = vm_insert_page(vma,
                                 addr,
                                 virt_to_page((unsigned long)buff + (i * PAGE_SIZE)));
            if (ret != 0) {
                dprintk("%s: unable to remap marshalling memory: %d\n",
                        yfile->device->miscdev.name,
                        ret );
                goto out;
            }

            addr += PAGE_SIZE;
        }

        yfile->buff = buff;
        yfile->buff_size = vma->vm_end - vma->vm_start;

        process_id = task_tgid_vnr(current);
        thread_id = task_pid_vnr(current);

        yagl_marshal_put_uint32(&buff, YAGL_VERSION);
        yagl_marshal_put_pid(&buff, process_id);
        yagl_marshal_put_tid(&buff, thread_id);

        yagl_user_activate(yfile->device->regs,
                           yfile->index,
                           virt_to_phys(yfile->buff));

        buff = yfile->buff;

        if (yagl_marshal_get_uint32(&buff) != 1)
        {
            buff = yfile->buff;
            yfile->buff = NULL;
            ret = -EIO;
            print_error("%s: unable to init YaGL: probably version mismatch\n",
                        yfile->device->miscdev.name);
            goto out;
        }

        buff = yfile->buff;

        yagl_marshal_put_uint32(&buff, yfile->index);

        buff = NULL;

        dprintk("%s: YaGL entered\n",
                yfile->device->miscdev.name);
    } else {
        dprintk("%s: mmap must be called with page offset 0 or 1\n",
                yfile->device->miscdev.name);
        ret = -EINVAL;
        goto out;
    }

    ret = 0;

out:
    if (buff) {
        free_pages((unsigned long)buff, get_order(vma->vm_end - vma->vm_start));
    }
    mutex_unlock(&yfile->device->mutex);

    return ret;
}

static long yagl_misc_ioctl(struct file* file, unsigned int cmd, unsigned long arg)
{
    int ret = 0;
    unsigned int value = 0;

    if (_IOC_TYPE(cmd) != YAGL_IOC_MAGIC) {
        return -ENOTTY;
    }

    if (_IOC_DIR(cmd) & _IOC_READ) {
        ret = !access_ok(VERIFY_WRITE, (void __user*)arg, _IOC_SIZE(cmd));
    }

    if (_IOC_DIR(cmd) & _IOC_WRITE) {
        ret = ret || !access_ok(VERIFY_READ, (void __user*)arg, _IOC_SIZE(cmd));
    }

    if (ret != 0) {
        return -EFAULT;
    }

    ret = 0;

    switch (cmd) {
    case YAGL_IOC_GET_VERSION:
        value = YAGL_VERSION;
        ret = put_user(value, (unsigned int __user*)arg);
        break;
    default:
        ret = -ENOTTY;
        break;
    }

    return ret;
}

static struct file_operations yagl_misc_fops =
{
    .owner          = THIS_MODULE,
    .open           = yagl_misc_open,
    .mmap           = yagl_misc_mmap,
    .release        = yagl_misc_release,
    .unlocked_ioctl = yagl_misc_ioctl,
};

static int __devinit yagl_driver_probe(struct pci_dev *pci_dev,
                                       const struct pci_device_id *pci_id)
{
    int ret = 0;
    struct yagl_device *device = NULL;
    u32 mem_size = 0;

    dprintk("probing PCI device \"%s\"\n", dev_name(&pci_dev->dev));

    device = kzalloc(sizeof(*device), GFP_KERNEL);

    if (!device) {
        ret = -ENOMEM;
        goto fail;
    }

    ret = pci_enable_device(pci_dev);

    if (ret != 0) {
        dprintk("%s: unable to enable PCI device\n", dev_name(&pci_dev->dev));

        goto fail;
    }

    device->pci_dev = pci_dev;

    pci_set_master(pci_dev);

    if (!pci_resource_start(pci_dev, 0)) {
        dprintk("%s: bad PCI resource\n", dev_name(&pci_dev->dev));
        ret = -ENXIO;
        goto fail;
    }

    mem_size = pci_resource_len(pci_dev, 0);

    if (mem_size != PAGE_SIZE) {
        dprintk("%s: mem size must be PAGE_SIZE\n", dev_name(&pci_dev->dev));
        ret = -ENXIO;
        goto fail;
    }

    if (!request_mem_region(pci_resource_start(pci_dev, 0),
                            PAGE_SIZE,
                            dev_name(&pci_dev->dev))) {
        dprintk("%s: mem size must be PAGE_SIZE\n", dev_name(&pci_dev->dev));
        ret = -EBUSY;
        goto fail;
    }

    device->regs_pa = pci_resource_start(pci_dev, 0);

    device->regs = ioremap(device->regs_pa, mem_size);

    if (!device->regs) {
        ret = -ENXIO;
        goto fail;
    }

    mutex_init(&device->mutex);

    device->miscdev.minor = MISC_DYNAMIC_MINOR;
    device->miscdev.name = YAGL_NAME;
    device->miscdev.fops = &yagl_misc_fops;

    ret = misc_register(&device->miscdev);

    if (ret != 0) {
        dprintk("%s: unable to register misc device\n", dev_name(&pci_dev->dev));

        goto fail;
    }

    pci_set_drvdata(pci_dev, device);

    print_info("%s: device added\n", dev_name(&pci_dev->dev));

    return 0;

fail:
    if (device) {
        if (device->regs) {
            iounmap(device->regs);
        }
        if (device->regs_pa) {
            release_mem_region(device->regs_pa, mem_size);
        }
        if (device->pci_dev) {
            pci_disable_device(device->pci_dev);
        }
        kfree(device);
    }

    return ret;
}

static void __devinit yagl_driver_remove(struct pci_dev *pci_dev)
{
    struct yagl_device* device;

    dprintk("removing driver from \"%s\"\n", dev_name(&pci_dev->dev));

    device = pci_get_drvdata(pci_dev);

    if (device != NULL) {
        misc_deregister(&device->miscdev);

        pci_set_drvdata(pci_dev, NULL);

        iounmap(device->regs);
        release_mem_region(device->regs_pa, PAGE_SIZE);
        pci_disable_device(device->pci_dev);
        kfree(device);
    }
}

static struct pci_driver yagl_driver =
{
    .name       = YAGL_NAME,
    .id_table   = yagl_pci_table,
    .probe      = yagl_driver_probe,
    .remove     = __devexit_p(yagl_driver_remove),
};

int yagl_driver_register(void)
{
    return pci_register_driver(&yagl_driver);
}

void yagl_driver_unregister(void)
{
    pci_unregister_driver(&yagl_driver);
}
