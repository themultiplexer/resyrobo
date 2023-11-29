ifneq ($(KERNELRELEASE),)
obj-m	:= motor.o

else
KDIR	:= '~/linux/'
KDIR	:= ${HOME}/linux
PWD	:= $(shell pwd)

default:
	$(MAKE)	-C $(KDIR)	-I${KDIR}/include	M=$(PWD) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
	dtc -@ -I dts -O dtb -o configure_pwm.dtb configure_pwm.dts
endif

clean:
	rm -rf *.ko *.o *.mod.c
	rm -rf modules.order Module.symvers
	rm -rf .tmp_versions .*.cmd *.dwo .*.dwo
	rm -f motor
