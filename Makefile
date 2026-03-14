zen-bclk-oc-objs+= mod.o
obj-m := zen-bclk-oc.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean