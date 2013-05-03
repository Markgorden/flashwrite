#!/bin/sh

cd $(dirname $0)
pwgen -cns 8 1 > sshPasswd

echo passwd=$(cat sshPasswd)
