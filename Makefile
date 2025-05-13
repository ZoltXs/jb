# Makefile simple para el driver LPM027M128C para Raspberry Pi

obj-m := lpm027m128c_fb.o

# Ruta al kernel de Raspberry Pi OS
KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build

all:
	make -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	make -C $(KERNEL_DIR) M=$(PWD) clean

install:
	make -C $(KERNEL_DIR) M=$(PWD) modules_install
	depmod -a

dtbo:
	dtc -@ -I dts -O dtb -o lpm027m128c-overlay-rpi.dtbo lpm027m128c-overlay-rpi.dts

install-dtbo:
	cp lpm027m128c-overlay-rpi.dtbo /boot/overlays/
