#!/bin/sh

if [ $# -eq 1 ]
  then
    echo "Compiling and Installing to " $1
    make
    echo "Removing existing module"
    ssh root@$1 'rmmod emergency'
    echo "Copying module"
    scp emergency.ko root@$1:
    echo "Installing module"
    ssh root@$1 'insmod /root/emergency.ko'
  else
    echo "./build.sh <IP_OF_RASPBERRY>"
fi


