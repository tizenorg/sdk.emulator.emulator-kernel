/*
 *  linux/arch/arm/mach-exynos/tizen-vpci.h
 */

#ifndef __ASM_ARCH_MACH_EXYNOS_TIZEN_VPCI_H
#define __ASM_ARCH_MACH_EXYNOS_TIZEN_VPCI_H

#include <linux/init.h>

#include <mach/irqs.h>

#define TIZEN_VPCI_CFG_VIRT_BASE (void __iomem *)0xf5000000ul

/* PCI space */
#define TIZEN_VPCI_CFG_BASE         0xC2000000
#define TIZEN_VPCI_IO_BASE          0xC3000000
#define TIZEN_VPCI_IO_OFFSET        0x00000100 /* 256b */
#define TIZEN_VPCI_MEM_BASE1        0xD0000000
#define TIZEN_VPCI_MEM_BASE2        0xE0000000
/* Sizes of above maps */
#define TIZEN_VPCI_CFG_BASE_SIZE    0x01000000
#define TIZEN_VPCI_IO_BASE_SIZE     0x0000FFFF /* 64Kb */
#define TIZEN_VPCI_MEM_BASE1_SIZE   0x10000000 /* 256Mb */
#define TIZEN_VPCI_MEM_BASE2_SIZE   0x10000000 /* 256Mb */

/* PCI interrupts */
#define TIZEN_VPCI_IRQ0 IRQ_EINT(8)
#define TIZEN_VPCI_IRQ1 IRQ_EINT(9)
#define TIZEN_VPCI_IRQ2 IRQ_EINT(10)
#define TIZEN_VPCI_IRQ3 IRQ_EINT(11)

#endif
