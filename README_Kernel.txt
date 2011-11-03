########################
##### How to Build #####
########################

(1) Get Toolchain from Codesourcery site. (http://www.codesourcery.com)

    ex) Download : https://sourcery.mentor.com/sgpp/lite/arm/portal/subscription?@template=lite
    
    recommand - Feature : ARM
                Target OS : "EABI"
                package : "IA32 GNU/Linux TAR"



(2) Edit Makefile for compile.

    edit "CROSS_COMPILE" to right toolchain path which you downloaded.

    ex) ARCH  ?= arm
        CROSS_COMPILE ?= ../toolchains/arm-2010.01/bin/arm-none-eabi-   //You have to modify this path!



(3) Compile as follow commands.

    $ cd kernel
    $ make clean
    $ make mrproper
    $ make msm8660_celox_usa_tmo_defconfig
    $ make



(4) Get the zImage on follow path.

    kernel/arch/arm/boot/zImage