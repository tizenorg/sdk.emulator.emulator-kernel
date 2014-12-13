#
# Makefile for the SMACK LSM
#

ifndef CONFIG_MARU_EXTENSION_SOURCE
obj-$(CONFIG_SECURITY_SMACK) := smack.o

smack-y := smack_lsm.o smack_access.o smackfs.o
<<<<<<< HEAD
endif
=======
smack-$(CONFIG_NETFILTER) += smack_netfilter.o
>>>>>>> c141075... Smack: secmark support for netfilter
