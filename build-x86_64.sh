#!/bin/sh
# Build x86 emulator kernel image

ARCH=x86_64 make tizen_emul_defconfig
./scripts/config --set-str CONFIG_INITRAMFS_SOURCE ramfs/initramfs.x86_64
ARCH=x86_64 CROSS_COMPILE='' make -j8
