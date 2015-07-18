#!/bin/sh
# Build x86 emulator kernel image

ARCH=i386 make tizen_emul_defconfig
./scripts/config --set-str CONFIG_INITRAMFS_SOURCE ramfs/initramfs.i386
ARCH=i386 CROSS_COMPILE='' make -j8
