.PHONY: modules_install clean
obj-m := gpio_sysfs.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

modules_install:
	make -C $(KERNELDIR) M=$(PWD) modules_install
clean:
	make -C $(KERNELDIR) M=$(PWD) clean