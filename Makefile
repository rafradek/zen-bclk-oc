zen-bclk-oc-objs+= mod.o
obj-m := zen-bclk-oc.o
ccflags-y += -flax-vector-conversions -g3 -Wno-format

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean