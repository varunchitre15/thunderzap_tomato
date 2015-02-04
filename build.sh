 #
 # Copyright © 2014, Varun Chitre "varun.chitre15" <varun.chitre15@gmail.com>
 #
 # Custom build script
 #
 # This software is licensed under the terms of the GNU General Public
 # License version 2, as published by the Free Software Foundation, and
 # may be copied, distributed, and modified under those terms.
 #
 # This program is distributed in the hope that it will be useful,
 # but WITHOUT ANY WARRANTY; without even the implied warranty of
 # MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 # GNU General Public License for more details.
 #
 # Please maintain this if you use this script or any part of it
 #
KERNEL_DIR=$PWD
ZIMAGE=$KERNEL_DIR/arch/arm/boot/zImage
MKBOOTIMG=$KERNEL_DIR/tools/mkbootimg
MKBOOTFS=$KERNEL_DIR/tools/mkbootfs
DTBTOOL=$KERNEL_DIR/tools/dtbToolCM
ROOTFS=$KERNEL_DIR/root.fs
BOOTIMG=$KERNEL_DIR/boot.img
BUILD_START=$(date +"%s")
blue='\033[0;34m'
cyan='\033[0;36m'
yellow='\033[0;33m'
red='\033[0;31m'
nocol='\033[0m'
# Modify the following variable if you want to build
export CROSS_COMPILE="/root/linaro/4.7.3-2013.04.20130415/bin/arm-linux-gnueabihf-"
export ARCH=arm
export SUBARCH=arm
export KBUILD_BUILD_USER="varun.chitre15"
export KBUILD_BUILD_HOST="Monster-Machine"

compile_kernel ()
{
echo -e "$blue***********************************************"
echo "          Compiling ThunderZap kernel          "
echo -e "***********************************************$nocol"
make cyanogenmod_tomato_defconfig
make zImage -j32
make dtbs
if ! [ -a $ZIMAGE ];
then
echo -e "$red Kernel Compilation failed! Fix the errors! $nocol"
exit 1
fi
}

compile_bootimg ()
{
echo -e "$yellow*************************************************"
echo "             Creating boot image for $2"
echo -e "*************************************************$nocol"
$MKBOOTFS $1/ > $KERNEL_DIR/ramdisk.cpio
cat $KERNEL_DIR/ramdisk.cpio | gzip > $KERNEL_DIR/root.fs
$DTBTOOL -2 -o $KERNEL_DIR/arch/arm/boot/dt.img -s 2048 -p $KERNEL_DIR/scripts/dtc/ $KERNEL_DIR/arch/arm/boot/dts/
$MKBOOTIMG --kernel $ZIMAGE --ramdisk $KERNEL_DIR/root.fs --cmdline "console=ttyHSL0,115200,n8 boot_cpus=0,4,5,6,7 androidboot.console=ttyHSL0 androidboot.hardware=qcom user_debug=31 msm_rtb.filter=0x3F ehci-hcd.park=3 androidboot.bootdevice=7824900.sdhci sched_enable_hmp=1"  --base 0x80000000 --pagesize 2048 --ramdisk_offset 0x01000000 --kernel_offset 0x00008000 --tags_offset 0x00000100 --second_offset 0x00f00000 --dt arch/arm/boot/dt.img -o $KERNEL_DIR/boot.img
if ! [ -a $ROOTFS ];
then
echo -e "$red Ramdisk creation failed $nocol"
exit 1
fi
if ! [ -a $BOOTIMG ];
then
echo -e "$red Boot image creation failed $nocol"
exit 1
fi
finally_done
}

finally_done ()
{
echo -e "$cyan BOOT Image installed on: $BOOTIMG$nocol"
}

case $1 in
cm11)
compile_kernel
compile_bootimg ramdisk-cm11 CM11
;;
stock)
compile_kernel
compile_bootimg ramdisk Stock
;;
cm12)
compile_kernel
compile_bootimg ramdisk-cm12 CM12
;;
clean)
make ARCH=arm -j8 clean mrproper
rm -rf $KERNEL_DIR/ramdisk.cpio $KERNEL_DIR/root.fs $KERNEL_DIR/boot.img
;;
*)
echo -e "Add valid option\nValid options are:\n./build.sh (stock|cm11|cm12|clean)"
exit 1
;;
esac
BUILD_END=$(date +"%s")
DIFF=$(($BUILD_END - $BUILD_START))
echo -e "$yellow Build completed in $(($DIFF / 60)) minute(s) and $(($DIFF % 60)) seconds.$nocol"
