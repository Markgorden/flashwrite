#!/bin/sh

. $(dirname $0)/../../common/build/build.inc 

set -e

export FULL_VERSION="$VERSION revision $(svn info | grep ^Revision: | cut -d \  -f 2)"

modules="kernel"

assemble()
{
    return
    pushd ..
    rm -rf output
    zipName="$(echo "${PRODUCT#Omega }-$FULL_VERSION" | tr ' ' '-')"
    BUILD_DIR=arm_build

    # Docs
    mkdir -p "output/$zipName/Docs"
    cp docs/$BUILD_DIR/*pdf output/$zipName/Docs
    cp docs/$BUILD_DIR/Readme* output/$zipName/

	# Language packs
	mkdir -p output/$zipName/language-packs/
	cp station/$BUILD_DIR/language-packs/*tgz output/$zipName/language-packs/

    # hub
    mkdir -p output/$zipName/SmartHub

    # kernel and rootfs
    cp $PRODUCT_DIR/omega-firmware/$BUILD_DIR/web-firmware.bin \
       $PRODUCT_DIR/omega-firmware/$BUILD_DIR/usb-firmware.bin \
       $PRODUCT_DIR/usb-firmware/USB_flash_drive_how_to.txt \
       $PRODUCT_DIR/kernel/$BUILD_DIR/uImage \
       $PRODUCT_DIR/kernel/$BUILD_DIR/uImage.ltls \
       output/$zipName/SmartHub

    # Blueprint stuff
    mkdir -p output/$zipName/Blueprint
    cp $PRODUCT_DIR/station/blueprint/* output/$zipName/Blueprint/
    cp $PRODUCT_DIR/station/ini_schema.xml output/$zipName/Blueprint/PS200.terminal.config

    # create zip file
    cd output && zip -r "$zipName.zip" "$zipName"
    popd
}

if [ "$1" = "clean" ]; then
    cleanModules
else
    buildModules rdc
    assemble
fi
