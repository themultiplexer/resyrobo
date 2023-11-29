#!/bin/sh

if [ $# -eq 1 ]
  then
    echo "Compiling and Installing to " $1
    make
    echo "Removing existing module"
    ssh root@$1 'rmmod ultrasonic'
    echo "Copying module"
    scp ultrasonic.ko root@$1:
    echo "Installing module"
    ssh root@$1 'insmod /root/ultrasonic.ko'
  else
    echo "./build.sh <IP_OF_RASPBERRY>"
fi


