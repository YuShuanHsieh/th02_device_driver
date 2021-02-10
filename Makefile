KERNELDIR := ${HOME}/linux

PWD :=$(shell pwd)

ARCH=arm                   
CROSS_COMPILE=arm-linux-gnueabihf-
CC=$(CROSS_COMPILE)gcc
LD=$(CROSS_COMPILE)ld

obj-m := th02_driver.o

modules:
	$(MAKE) -C $(KERNELDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) modules  

clean:
	rm *.o *.ko *.mod.c *.order *.symvers