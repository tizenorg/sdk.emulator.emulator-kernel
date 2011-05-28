#
# Makefile for the ipmi drivers.
#

dpram-config := 0

ifeq ($(CONFIG_MACH_MIRAGE),y)
	obj-$(CONFIG_DPRAM) += dpram.o
	dpram-config := 1
endif
ifeq ($(CONFIG_MACH_OMNIA),y)
	obj-$(CONFIG_DPRAM) += dpram.o
	dpram-config := 1
endif

obj-$(CONFIG_MULTIPDP) += multipdp.o
obj-$(CONFIG_MULTIPDP) += ipcmux.o
