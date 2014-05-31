/*
 * arch/arm/mach-exynos4/include/mach/io.h
 */

#ifndef __ASM_ARCH_IO_H
#define __ASM_ARCH_IO_H

#ifdef CONFIG_TIZEN_VPCI
#define IO_SPACE_LIMIT 0xffffffff

#define TIZEN_VPCI_IO_VIRT_BASE (void __iomem *)0xf4ff0000ul

#define __io(a) ((void __iomem *)(TIZEN_VPCI_IO_VIRT_BASE + (a)))
#else
#define __io(a) __typesafe_io(a)
#endif

#endif
