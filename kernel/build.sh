#!/bin/bash

rdc()
{
    mkdir -p $BUILD_DIR
	cp config $BUILD_DIR/.config
#	cp $PREFIX/cramfs.bin arch/mips/cramfs/
	make O=$BUILD_DIR V=1
	showSize /tftpboot/bzImage
	size=`wc -c /tftpboot/bzImage | cut -d' ' -f1`
	if [ $size -gt $((0x7c0000)) ]; then
		echo "Firmware can not be bigger that $((0x7c0000)) (0x7c0000) bytes"
		return 1
	else
		echo "$((0x7c0000-$size)) bytes left for Firmware"
	fi
}

. $(dirname $0)/../../common/build/build.inc
$1
