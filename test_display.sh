#!/bin/bash

# Script simple de prueba para el display LPM027M128C en Raspberry Pi

# Comprobar si se ejecuta como root
if [ "$(id -u)" != "0" ]; then
   echo "Este script debe ejecutarse como root" 1>&2
   exit 1
fi

# Comprobar si el framebuffer está disponible
if [ ! -c /dev/fb1 ]; then
    echo "Error: Framebuffer /dev/fb1 no encontrado."
    echo "Asegúrate de que el driver está cargado correctamente."
    exit 1
fi

# Mostrar un patrón de prueba simple
echo "Mostrando patrón de prueba en el display..."
dd if=/dev/urandom of=/dev/fb1 bs=1k count=375

echo "Si puedes ver un patrón aleatorio en el display, la prueba ha sido exitosa."
echo "Presiona Enter para continuar..."
read

# Limpiar la pantalla
echo "Limpiando pantalla..."
dd if=/dev/zero of=/dev/fb1 bs=1k count=375

echo "Prueba completada."
