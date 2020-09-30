#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2020 Google, Inc

# Just a fixed size for now, but it's passed to the tests and they're supposed
# to respect it.
SIZE=1024
BLOCK=kselftest-dm-user-block
CONTROL=kselftest-dm-user-control

while [ x"$1" != x"--" ]
do
    case "$1" in
    "-s")    SIZE="$2";                             shift 2;;
    *)       echo "$0: unknown argument $1" >&2;    exit  1;;
    esac
done
shift

dmsetup create $BLOCK << EOF
0 $SIZE user 0 $SIZE $CONTROL
EOF

dmsetup resume $BLOCK

"$@" -s $SIZE -b /dev/mapper/$BLOCK -c /dev/dm-user/$CONTROL

dmsetup remove $BLOCK
