#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2020 Google, Inc

../harness.sh -s    1024 -- ./simple-read-all -r   4096 
../harness.sh -s 1024000 -- ./simple-read-all -r   4096 
../harness.sh -s 1024000 -- ./simple-read-all -r    500 
../harness.sh -s 1024000 -- ./simple-read-all -r  40960
../harness.sh -s 1024000 -- ./simple-read-all -r 409600
../harness.sh -s    1024 -- ./short-read-all  -r   4096 
../harness.sh -s 1024000 -- ./short-read-all  -r   4096 
../harness.sh -s 1024000 -- ./short-read-all  -r    500
../harness.sh -s 1024000 -- ./short-read-all  -r  40960
../harness.sh -s 1024000 -- ./short-read-all  -r 409600
