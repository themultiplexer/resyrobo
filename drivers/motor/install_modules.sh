#!/bin/sh

# Register SIGINT Signal to kill process via SSH
trap "echo CTRL-C was pressed && ssh root@$1 'killall -9 robocar' > /dev/null" 2

if [ $# -eq 1 ]
  then
    echo "Compiling and Installing to " $1
    make
    echo "Removing existing module"
    ssh root@$1 'rmmod motor'
    echo "Copying module"
    scp configure_pwm.dtb root@$1:/boot/overlays/
    scp motor.ko root@$1:
    echo "Installing module"
    ssh root@$1 'insmod /root/motor.ko'
  else
    echo "./build.sh <IP_OF_RASPBERRY>"
fi


