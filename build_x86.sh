#!/bin/sh
# Build x86 emulator kernel image

ARCH=i386 make i386_tizen_emul_defconfig
ARCH=i386 CROSS_COMPILE='' make -j8
