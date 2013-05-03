#!/bin/bash

. $(dirname $0)/../../common/build/build.inc

makeDevNodes() {
	echo making device node ...
    sudo rm -rf dev
	mkdir -p dev 
	pushd dev
	sudo mknod -m 0600 console c 5 1 
	sudo mknod mem c 1 1
	sudo mknod null c 1 3
	sudo mknod ptmx c 5 2
	sudo mkdir pts
	for n in `seq 0 10`; do 
		sudo mknod pts/$n c 136 $n
		sudo mknod ttyUSB$n c 188 $n
	done
	sudo mknod random c 1 8
	sudo ln -sf /proc/self/fd/0 stdin
	sudo ln -sf /proc/self/fd/1 stdout
	sudo ln -sf /proc/self/fd/2 stderr
	sudo mknod ttyS0 c 4 64
	sudo mknod ttyACM0 c 166 0
	sudo mknod tty c 5 0
	sudo mknod zero c 1 5
    sudo mknod urandom c 1 9
    sudo mkdir misc
    sudo mknod misc/rtc c 254 0
	ln -sf /var/log
	popd
}

rdc() {
    set +e
	clean

	cd $DIR
    mkdir -p $BUILD_DIR
    
	set -e
	cp -a root $BUILD_DIR
	pushd $BUILD_DIR/root

	echo "$FULL_VERSION" > etc/version
	echo "$PRODUCT" > etc/product
	makeDevNodes
	popd
#	chmod og-rw rootfs/system/stunnel/mtx.*
#	find rootfs -name .svn | xargs rm -rf DUMMY 
#	find rootfs -name .svn | xargs rm -rf DUMMY
#        find rootfs -name *~BASE~ | xargs rm -rf DUMMY
#	find rootfs | xargs file | grep ELF | cut -d":" -f1 | xargs chmod +w
#	find rootfs | xargs file | grep ELF | cut -d":" -f1 | xargs mipsel-linux-uclibc-strip -s
#	find rootfs | xargs file | grep ELF | cut -d":" -f1 | xargs sstrip
#	mkfs.cramfs rootfs cramfs.bin
#	cp cramfs.bin $PREFIX
#	showSize cramfs.bin
}

$1
