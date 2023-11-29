# Aufsetzten der Distribution

### 1. Installation von DietPi.

1. DietPi Image für Raspberry Pi laden ([DietPi HomePage](https://dietpi.com/))
2. Mit balenaEtcher auf SD-Karte installieren ([balenaEtcher HomePage](https://www.balena.io/etcher/))

### 2. Cross-Kompilieren des Linux-RT-Kernels

1. Tools holen
```
git clone https://github.com/raspberrypi/tools ~/tools
```
2. Tools in PATH hinterlegen
```
echo PATH=\$PATH:~/tools/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian-x64/bin
```
3. Kernel Sourcen holen (4.14.y Real Time)
```
git clone --depth=1 --branch rpi-4.14.y-rt https://github.com/raspberrypi/linux
```
4. In Sourcecodeverzeichnis wechseln und Konfiguration erstellen (für Pi 2, Pi 3, Pi 3+)
```
cd linux
KERNEL=kernel7
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- bcm2709_defconfig
```
5. Erweiterte Optionen konfigurieren
```
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- menuconfig
```
6. Mit verfügbaren Kernen zImage, Module und dtbs erstellen
```
make -j $(nproc) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- zImage modules dtbs
```

7. SD-Karte mit vorhandenem System (DietPi) mounten
```
mkdir mnt
mkdir mnt/fat32
mkdir mnt/ext4
sudo mount /dev/sdb1 mnt/fat32
sudo mount /dev/sdb2 mnt/ext4
```
8. Module auf SD-Karte installieren
```
sudo make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- INSTALL_MOD_PATH=mnt/ext4 modules_install
```
9. Install Kernel Image and DTBs
```
sudo cp mnt/fat32/$KERNEL.img mnt/fat32/$KERNEL-backup.img
sudo cp arch/arm/boot/zImage mnt/fat32/$KERNEL.img
sudo cp arch/arm/boot/dts/*.dtb mnt/fat32/
sudo cp arch/arm/boot/dts/overlays/*.dtb* mnt/fat32/overlays/
sudo cp arch/arm/boot/dts/overlays/README mnt/fat32/overlays/
```

10. Unmount Partitions
```
sudo umount mnt/fat32
sudo umount mnt/ext4
```

# Installation von benötigten Paketen

## - libc 2.28 (Rust braucht 2.28+)

1. Edit sources.list:
`nano /etc/apt/sources.list`

2. Add line:
`deb http://ftp.debian.org/debian sid main contrib non-free`

3. Receive keys:
```
sudo apt-key adv --recv-key --keyserver keyserver.ubuntu.com 7638D0442B90D010
sudo apt-key adv --recv-key --keyserver keyserver.ubuntu.com 04EE7237B7D453EC
```
4. Update package list:
`apt update`

5. Install libc6:
`apt install libc6`

6. Edit sources.list:
`nano /etc/apt/sources.list`

7. Remove / comment line:
`deb http://ftp.debian.org/debian sid main contrib non-free`

# Konfigurieren von SPI und PWM

1. DietPi Bootkonfiguration öffnen
`
vim /DietPi/config.txt
`

2. SPI aktivieren. off => on
```
#-------SPI-------------
dtparam=spi=on
```

3. PWM overlay mit pins angeben. Als neue Zeile hinzufügen.
```
dtoverlay=pwm-2chan,pin=18,func=2,pin2=13,func2=4
```
