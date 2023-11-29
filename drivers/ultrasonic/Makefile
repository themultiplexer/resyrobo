ifneq ($(KERNELRELEASE),)
obj-m	:= ultrasonic.o

else
KDIR	:= '~/linux/'
KDIR	:= ${HOME}/linux
PWD	:= $(shell pwd)

default:
	$(MAKE)	-C $(KDIR)	M=$(PWD) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
endif

clean:
	rm -rf *.ko *.o *.mod.c
	rm -rf modules.order Module.symvers
	rm -rf .tmp_versions .*.cmd *.dwo .*.dwo
	rm -f motor
