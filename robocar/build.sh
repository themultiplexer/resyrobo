#!/bin/sh

# Register SIGINT Signal to kill process via SSH
trap "echo CTRL-C was pressed && ssh root@$1 'killall -SIGINT robocar' > /dev/null" INT
if [ $# -gt 0 ];
  then
    echo -e "\033[1mCross-Compiling and Installing to" $1 "\033[0m"
    echo "Killing running processes"
    ssh root@$1 'killall -9 robocar > /dev/null 2>&1'
    ssh root@$1 'killall -9 stress > /dev/null 2>&1'
    ssh root@$1 'killall -9 iperf > /dev/null 2>&1'
    ssh root@$1 'killall -9 dd > /dev/null 2>&1'
    if [[ "$@" == *"-m"* ]]
    then
      echo "Installing Modules"
      echo "  - Motor"
      cd ../drivers/motor/
      ./install_modules.sh $1 > /dev/null
      echo "  - Ultrasonic"
      cd ../ultrasonic/
      ./install_modules.sh $1 > /dev/null
      echo "  - Lightbarrier"
      cd ../lightbarrier/
      ./install_modules.sh $1 > /dev/null
      echo "  - Emergency"
      cd ../emergency/
      ./install_modules.sh $1 > /dev/null
      cd ../../robocar
    else
        echo -e "\e[33mSkipping Modules Install! Make sure they exist.\033[0m"
    fi
    
    cargo build --release --target=armv7-unknown-linux-gnueabihf
    echo "Copying executable"
    scp target/armv7-unknown-linux-gnueabihf/release/robocar root@$1:/root/
    if [[ "$@" == *"-l"* ]]
    then
      echo "Generating Load"
      ssh root@$1 "nohup iperf -s >/dev/null 2>&1 &"
      ssh root@$1 'nohup stress --cpu 8 >/dev/null 2>&1 &'
      (dd if=/dev/zero | ssh root@$1 dd of=/dev/null) >/dev/null 2>&1 &
      (dd if=/dev/zero | ssh root@$1 dd of=/dev/null) >/dev/null 2>&1 &
      (dd if=/dev/zero | ssh root@$1 dd of=/dev/null) >/dev/null 2>&1 &
      (dd if=/dev/zero | ssh root@$1 dd of=/dev/null) >/dev/null 2>&1 &
      #iperf -c $1 -b 200000K -d -t 100 >/dev/null 2>&1 &
      ssh root@$1 'nohup dd if=/dev/zero of=/root/shit1 >/dev/null 2>&1 &'
      ssh root@$1 'nohup dd if=/dev/zero of=/root/shit2 >/dev/null 2>&1 &'
      ssh root@$1 'nohup dd if=/dev/zero of=/root/shit3 >/dev/null 2>&1 &'
      ssh root@$1 'nohup dd if=/dev/zero of=/root/shit4 >/dev/null 2>&1 &'
    fi
    read -n 1 -s -r -p "Press enter to deploy"
    ssh root@$1 'nohup /root/robocar >/dev/null 2>&1 &'
    echo -e "\rSuccessfully deployed."
    read -n 1 -s -r -p "Press enter to kill"
    ssh root@$1 'nohup /root/robocar >/dev/null 2>&1 &'
    ssh root@$1 'killall -9 robocar > /dev/null 2>&1'
    ssh root@$1 'killall -9 stress > /dev/null 2>&1'
    ssh root@$1 'killall -9 iperf > /dev/null 2>&1'
    ssh root@$1 'killall -9 dd > /dev/null 2>&1'
    killall -9 dd
    echo -e "\rSuccessfully killed."
  else
    echo "./build.sh <IP_OF_RASPBERRY>"
fi


