#
# Makefile for ccic devices
#

ifeq ($(CONFIG_CCIC_S2MU107), y)
	obj-$(CONFIG_CCIC_NOTIFIER)	+= s2mu107_notifier.o s2mu107_sysfs.o s2mu107_misc.o
else
	obj-$(CONFIG_CCIC_NOTIFIER)	+= ccic_notifier.o ccic_sysfs.o ccic_core.o ccic_misc.o
endif
obj-$(CONFIG_CCIC_ALTERNATE_MODE)	+= ccic_alternate.o
obj-$(CONFIG_CCIC_S2MM003)		+= s2mm003.o
obj-$(CONFIG_CCIC_S2MM005)		+= s2mm005_fw.o s2mm005_cc.o s2mm005_pd.o s2mm005.o
obj-$(CONFIG_CCIC_S2MU107)		+= s2mu107-usbpd.o usbpd_cc.o usbpd_manager.o usbpd_policy.o usbpd.o pdic_notifier.o
obj-$(CONFIG_CCIC_MAX77705)		+= max77705_cc.o max77705_pd.o max77705_usbc.o max77705_alternate.o
obj-$(CONFIG_CCIC_MAX77705_DEBUG)	+= max77705_debug.o
