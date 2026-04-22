MODULE_NAME     := zen-bclk-oc
VERSION         := 1.0
DKMS_ROOT_PATH  := /usr/src/$(MODULE_NAME)-$(VERSION)

zen-bclk-oc-objs+= mod.o
obj-m := $(MODULE_NAME).o

all: 
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

dkms-install:
	-dkms remove $(MODULE_NAME)/$(VERSION) --all -q
	rm -rf $(DKMS_ROOT_PATH)
	mkdir $(DKMS_ROOT_PATH)
	cp dkms.conf Makefile mod.c $(DKMS_ROOT_PATH)

	sed -e "s/@CFLGS@/${MCFLAGS}/" \
	    -e "s/@VERSION@/$(VERSION)/" \
	    -i $(DKMS_ROOT_PATH)/dkms.conf

	dkms add $(MODULE_NAME)/$(VERSION)
	dkms build $(MODULE_NAME)/$(VERSION)
	dkms install $(MODULE_NAME)/$(VERSION)
	echo $(MODULE_NAME) > /etc/modules-load.d/$(MODULE_NAME).conf

dkms-uninstall:
	dkms remove $(MODULE_NAME)/$(VERSION) --all
	rm -rf $(DKMS_ROOT_PATH)
	rm /etc/modules-load.d/$(MODULE_NAME).conf
