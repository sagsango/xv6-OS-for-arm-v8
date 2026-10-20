#!/bin/sh

# add the ARM cross toolchain to your path
# for aarch64
export PATH=/home/lakshman/Acads/695_osdi/aarch64-toolchain/gcc-linaro-4.9-2014.11-x86_64_aarch64-elf/bin:$PATH
#export PATH=/home/lakshman/Acads/695_osdi/aarch64-toolchain/gcc-linaro-4.9-2014.11-x86_64_aarch64-linux-gnu/bin:$PATH

# Build Kernel
clear

echo Building xv6 for ARM 64 bit

#make -j`getconf _NPROCESSORS_ONLN`
#make TOOLPREFIX=aarch64-linux-gnu- CC=aarch64-linux-gnu-gcc
#TOOLPREFIX=aarch64-linux-gnu-  make TOOLPREFIX=aarch64-linux-gnu- CC=aarch64-linux-gnu-gcc LD=aarch64-linux-gnu-ld
make TOOLPREFIX=aarch64-linux-gnu- OBJCOPY=aarch64-linux-gnu-objcopy OBJDUMP=aarch64-linux-gnu-objdump CC=aarch64-linux-gnu-gcc LD=aarch64-linux-gnu-ld

