/*
 *  linux/arch/arm/mach-exynos/tizen-vpci.c
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach/pci.h>

#include "tizen-vpci.h"

#define DEVICE_ID_OFFSET    0x00
#define CSR_OFFSET          0x04
#define CLASS_ID_OFFSET     0x08

#define TIZEN_VPCI_DEVICE_ID    0x030010ee
#define TIZEN_VPCI_CLASS_ID     0x0b400000

static unsigned long pci_slot_ignore = 0;

static int __init tizen_vpci_slot_ignore(char *str)
{
    int retval;
    int slot;

    while ((retval = get_option(&str,&slot))) {
        if ((slot < 0) || (slot > 31)) {
            printk("Illegal slot value: %d\n",slot);
        } else {
            pci_slot_ignore |= (1 << slot);
        }
    }
    return 1;
}

__setup("pci_slot_ignore=", tizen_vpci_slot_ignore);

static void __iomem *__pci_addr(struct pci_bus *bus,
                unsigned int devfn, int offset)
{
    unsigned int busnr = bus->number;

    /*
     * Trap out illegal values
     */
    if (offset > 255)
        BUG();
    if (busnr > 255)
        BUG();
    if (devfn > 255)
        BUG();

    return TIZEN_VPCI_CFG_VIRT_BASE + ((busnr << 16) |
        (PCI_SLOT(devfn) << 11) | (PCI_FUNC(devfn) << 8) | offset);
}

static int tizen_vpci_read_config(struct pci_bus *bus, unsigned int devfn, int where,
                 int size, u32 *val)
{
    void __iomem *addr = __pci_addr(bus, devfn, where & ~3);
    u32 v;
    int slot = PCI_SLOT(devfn);

    if (pci_slot_ignore & (1 << slot)) {
        /* Ignore this slot */
        switch (size) {
        case 1:
            v = 0xff;
            break;
        case 2:
            v = 0xffff;
            break;
        default:
            v = 0xffffffff;
        }
    } else {
        switch (size) {
        case 1:
            v = __raw_readl(addr);
            if (where & 2) v >>= 16;
            if (where & 1) v >>= 8;
            v &= 0xff;
            break;

        case 2:
            v = __raw_readl(addr);
            if (where & 2) v >>= 16;
            v &= 0xffff;
            break;

        default:
            v = __raw_readl(addr);
            break;
        }
    }

    *val = v;
    return PCIBIOS_SUCCESSFUL;
}

static int tizen_vpci_write_config(struct pci_bus *bus, unsigned int devfn, int where,
                  int size, u32 val)
{
    void __iomem *addr = __pci_addr(bus, devfn, where);
    int slot = PCI_SLOT(devfn);

    if (pci_slot_ignore & (1 << slot)) {
        return PCIBIOS_SUCCESSFUL;
    }

    switch (size) {
    case 1:
        __raw_writeb((u8)val, addr);
        break;

    case 2:
        __raw_writew((u16)val, addr);
        break;

    case 4:
        __raw_writel(val, addr);
        break;
    }

    return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops tizen_vpci_ops = {
    .read   = tizen_vpci_read_config,
    .write  = tizen_vpci_write_config,
};

static struct resource io_mem = {
    .name   = "PCI I/O space",
    .start  = TIZEN_VPCI_IO_OFFSET,
    .end    = TIZEN_VPCI_IO_BASE_SIZE-1,
    .flags  = IORESOURCE_IO,
};

static struct resource non_mem = {
    .name   = "PCI non-prefetchable",
    .start  = TIZEN_VPCI_MEM_BASE1,
    .end    = TIZEN_VPCI_MEM_BASE1+TIZEN_VPCI_MEM_BASE1_SIZE-1,
    .flags  = IORESOURCE_MEM,
};

static struct resource pre_mem = {
    .name   = "PCI prefetchable",
    .start  = TIZEN_VPCI_MEM_BASE2,
    .end    = TIZEN_VPCI_MEM_BASE2+TIZEN_VPCI_MEM_BASE2_SIZE-1,
    .flags  = IORESOURCE_MEM | IORESOURCE_PREFETCH,
};

static int __init tizen_vpci_setup_resources(struct pci_sys_data *sys)
{
    int ret = 0;

    ret = request_resource(&ioport_resource, &io_mem);
    if (ret) {
        printk(KERN_ERR "PCI: unable to allocate I/O "
               "memory region (%d)\n", ret);
        goto out;
    }
    ret = request_resource(&iomem_resource, &non_mem);
    if (ret) {
        printk(KERN_ERR "PCI: unable to allocate non-prefetchable "
               "memory region (%d)\n", ret);
        goto release_io_mem;
    }
    ret = request_resource(&iomem_resource, &pre_mem);
    if (ret) {
        printk(KERN_ERR "PCI: unable to allocate prefetchable "
               "memory region (%d)\n", ret);
        goto release_non_mem;
    }

    /*
     * the IO resource for this bus
     * the mem resource for this bus
     * the prefetch mem resource for this bus
     */
    pci_add_resource_offset(&sys->resources, &io_mem, sys->io_offset);
    pci_add_resource_offset(&sys->resources, &non_mem, sys->mem_offset);
    pci_add_resource_offset(&sys->resources, &pre_mem, sys->mem_offset);

    goto out;

release_non_mem:
    release_resource(&non_mem);
release_io_mem:
    release_resource(&io_mem);
out:
    return ret;
}

int __init tizen_vpci_setup(int nr, struct pci_sys_data *sys)
{
    int ret = 0;
    int i;
    int myslot = -1;
    unsigned long val;
    void __iomem *local_pci_cfg_base;

    if (nr == 0) {
        sys->mem_offset = 0;
        sys->io_offset = 0;
        ret = tizen_vpci_setup_resources(sys);
        if (ret < 0) {
            printk("tizen_vpci_setup: resources... oops?\n");
            goto out;
        }
    } else {
        printk("tizen_vpci_setup: resources... nr == 0??\n");
        goto out;
    }

    /*
     *  We need to discover the PCI core first to configure itself
     *  before the main PCI probing is performed
     */
    for (i=0; i<32; i++)
        if ((__raw_readl(TIZEN_VPCI_CFG_VIRT_BASE+(i<<11)+DEVICE_ID_OFFSET)
                == TIZEN_VPCI_DEVICE_ID) &&
            (__raw_readl(TIZEN_VPCI_CFG_VIRT_BASE+(i<<11)+CLASS_ID_OFFSET)
                == TIZEN_VPCI_CLASS_ID)) {
            myslot = i;
            break;
        }

    if (myslot == -1) {
        printk("Cannot find PCI core!\n");
        ret = -EIO;
        goto out;
    }

    printk("PCI core found (slot %d)\n",myslot);

    local_pci_cfg_base = TIZEN_VPCI_CFG_VIRT_BASE + (myslot << 11);

    val = __raw_readl(local_pci_cfg_base + CSR_OFFSET);
    val |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_INVALIDATE;
    __raw_writel(val, local_pci_cfg_base + CSR_OFFSET);

    /*
     * Configure the PCI inbound memory windows to be 1:1 mapped to SDRAM
     */
    __raw_writel(PHYS_OFFSET, local_pci_cfg_base + PCI_BASE_ADDRESS_0);
    __raw_writel(PHYS_OFFSET, local_pci_cfg_base + PCI_BASE_ADDRESS_1);
    __raw_writel(PHYS_OFFSET, local_pci_cfg_base + PCI_BASE_ADDRESS_2);

    /*
     * Do not to map PCI device into memory space
     */
    pci_slot_ignore |= (1 << myslot);
    ret = 1;

out:
    return ret;
}

struct pci_bus * __init tizen_vpci_scan_bus(int nr, struct pci_sys_data *sys)
{
    return pci_scan_root_bus(NULL, sys->busnr, &tizen_vpci_ops, sys,
                 &sys->resources);
}

void __init tizen_vpci_preinit(void)
{
    pcibios_min_io = TIZEN_VPCI_IO_OFFSET;
    pcibios_min_mem = TIZEN_VPCI_MEM_BASE1;
}

static int __init tizen_vpci_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
    int irq;
    int devslot = PCI_SLOT(dev->devfn);

    switch (pin) {
    case 1:
        irq = TIZEN_VPCI_IRQ0;
        break;
    case 2:
        irq = TIZEN_VPCI_IRQ1;
        break;
    case 3:
        irq = TIZEN_VPCI_IRQ2;
        break;
    case 4:
        irq = TIZEN_VPCI_IRQ3;
        break;
    default:
        BUG();
    }

    printk("PCI map irq: slot %d, pin %d, devslot %d, irq: %d\n", slot, pin, devslot, irq);

    return irq;
}

static struct hw_pci tizen_vpci __initdata = {
    .swizzle        = NULL,
    .map_irq        = tizen_vpci_map_irq,
    .nr_controllers = 1,
    .setup          = tizen_vpci_setup,
    .scan           = tizen_vpci_scan_bus,
    .preinit        = tizen_vpci_preinit,
};

static int __init tizen_vpci_init(void)
{
    pci_common_init(&tizen_vpci);
    return 0;
}

subsys_initcall(tizen_vpci_init);
