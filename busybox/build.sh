#!/bin/bash

rdc()
{
    makeBuildDir
	cp config .config
    make V=1 install 
}

. $(dirname $0)/../../common/build/build.inc
$1
