#!/bin/bash

# Script de instalación simple para el driver LPM027M128C en Raspberry Pi

# Comprobar si se ejecuta como root
if [ "$(id -u)" != "0" ]; then
   echo "Este script debe ejecutarse como root" 1>&2
   exit 1
fi

# Instalar dependencias
echo "Instalando dependencias..."
apt-get update
apt-get install -y raspberrypi-kernel-headers build-essential device-tree-compiler

# Compilar el driver
echo "Compilando el driver..."
make clean
make
make dtbo

# Instalar el driver
echo "Instalando el driver..."
make install
mkdir -p /boot/overlays
cp lpm027m128c-overlay-rpi.dtbo /boot/overlays/

# Configurar el overlay en config.txt si no está ya configurado
if ! grep -q "dtoverlay=lpm027m128c-overlay-rpi" /boot/config.txt; then
    echo "dtoverlay=lpm027m128c-overlay-rpi" >> /boot/config.txt
    echo "Overlay añadido a /boot/config.txt"
fi

# Habilitar SPI si no está habilitado
if ! grep -q "^dtparam=spi=on" /boot/config.txt; then
    echo "dtparam=spi=on" >> /boot/config.txt
    echo "SPI habilitado en /boot/config.txt"
fi

echo "Instalación completada. Por favor, reinicia la Raspberry Pi."
echo "Después del reinicio, el framebuffer debería estar disponible como /dev/fb1"
