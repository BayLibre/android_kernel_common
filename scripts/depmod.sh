#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# A depmod wrapper

<<<<<<< HEAD   (d3c184 UPSTREAM: Revert "dma-contiguous: check for memory region ov)
if test $# -ne 2 -a $# -ne 3; then
	echo "Usage: $0 /sbin/depmod <kernelrelease> [System.map folder]" >&2
=======
if test $# -ne 1; then
	echo "Usage: $0 <kernelrelease>" >&2
>>>>>>> BRANCH (61401a Merge tag 'kbuild-v6.6' of git://git.kernel.org/pub/scm/linu)
	exit 1
fi
<<<<<<< HEAD   (d3c184 UPSTREAM: Revert "dma-contiguous: check for memory region ov)
DEPMOD=$1
KERNELRELEASE=$2
KBUILD_MIXED_TREE=$3
=======

KERNELRELEASE=$1

: ${DEPMOD:=depmod}
>>>>>>> BRANCH (61401a Merge tag 'kbuild-v6.6' of git://git.kernel.org/pub/scm/linu)

if ! test -r ${KBUILD_MIXED_TREE}System.map ; then
	echo "Warning: modules_install: missing 'System.map' file. Skipping depmod." >&2
	exit 0
fi

# legacy behavior: "depmod" in /sbin, no /sbin in PATH
PATH="$PATH:/sbin"
if [ -z $(command -v $DEPMOD) ]; then
	echo "Warning: 'make modules_install' requires $DEPMOD. Please install it." >&2
	echo "This is probably in the kmod package." >&2
	exit 0
fi

<<<<<<< HEAD   (d3c184 UPSTREAM: Revert "dma-contiguous: check for memory region ov)
# older versions of depmod require the version string to start with three
# numbers, so we cheat with a symlink here
depmod_hack_needed=true
tmp_dir=$(mktemp -d ${TMPDIR:-/tmp}/depmod.XXXXXX)
mkdir -p "$tmp_dir/lib/modules/$KERNELRELEASE"
if "$DEPMOD" -b "$tmp_dir" $KERNELRELEASE 2>/dev/null; then
	if test -e "$tmp_dir/lib/modules/$KERNELRELEASE/modules.dep" -o \
		-e "$tmp_dir/lib/modules/$KERNELRELEASE/modules.dep.bin"; then
		depmod_hack_needed=false
	fi
fi
rm -rf "$tmp_dir"
if $depmod_hack_needed; then
	symlink="$INSTALL_MOD_PATH/lib/modules/99.98.$KERNELRELEASE"
	ln -s "$KERNELRELEASE" "$symlink"
	KERNELRELEASE=99.98.$KERNELRELEASE
fi

set -- -ae -F ${KBUILD_MIXED_TREE}System.map
=======
set -- -ae -F System.map
>>>>>>> BRANCH (61401a Merge tag 'kbuild-v6.6' of git://git.kernel.org/pub/scm/linu)
if test -n "$INSTALL_MOD_PATH"; then
	set -- "$@" -b "$INSTALL_MOD_PATH"
fi
exec "$DEPMOD" "$@" "$KERNELRELEASE"
