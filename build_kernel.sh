#!/bin/bash

mkdir out

export ARCH=arm64

BUILD_CROSS_COMPILE=/home/mentalmuso/kernel/toolchain/4.9-2/bin/aarch64-linux-android-
KERNEL_LLVM_BIN=/home/mentalmuso/kernel/toolchain/clang2/bin/clang
CLANG_TRIPLE=aarch64-linux-gnu-
KERNEL_MAKE_ENV="DTC_EXT=$(pwd)/tools/dtc CONFIG_BUILD_ARM64_DT_OVERLAY=y"

make -C $(pwd) O=$(pwd)/out $KERNEL_MAKE_ENV CROSS_COMPILE=$BUILD_CROSS_COMPILE REAL_CC=$KERNEL_LLVM_BIN CLANG_TRIPLE=$CLANG_TRIPLE gts6lwifi_eur_open_defconfig
make -j16 -C $(pwd) O=$(pwd)/out $KERNEL_MAKE_ENV CROSS_COMPILE=$BUILD_CROSS_COMPILE REAL_CC=$KERNEL_LLVM_BIN CLANG_TRIPLE=$CLANG_TRIPLE

cp out/arch/arm64/boot/Image $(pwd)/arch/arm64/boot/Image
cp $(pwd)/out/arch/arm64/boot/Image.gz $(pwd)/WETA/Image.gz
cp $(pwd)/out/arch/arm64/boot/Image.gz-dtb $(pwd)/WETA/Image.gz-dtb
