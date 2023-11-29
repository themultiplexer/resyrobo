#!/bin/sh

if [ $# -eq 1 ]
  then
    echo "Compiling and Installing to " $1
    make
    echo "Removing existing module"
    ssh root@$1 'rmmod lightbarrier'
    echo "Copying module"
    scp configure_pullups.dtb root@$1:/boot/overlays/
    scp lightbarrier.ko root@$1:
    echo "Installing module"
    ssh root@$1 'insmod /root/lightbarrier.ko'
  else
    echo "./build.sh <IP_OF_RASPBERRY>"
fi


