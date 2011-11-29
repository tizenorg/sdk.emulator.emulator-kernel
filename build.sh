#!/bin/sh
# Build x86 emulator kernel image

make i386_emul_defconfig
make -j4
