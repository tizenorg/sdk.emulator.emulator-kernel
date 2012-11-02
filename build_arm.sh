#!/bin/sh
# Build x86 emulator kernel image

ARCH=arm make arm_tizen_emul_defconfig
ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- make -j8
