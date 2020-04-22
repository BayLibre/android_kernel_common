#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

# Script to update include/generated/autoksyms.h and dependency files
#
# Copyright:	(C) 2016  Linaro Limited
# Created by:	Nicolas Pitre, January 2016
#

# Update the include/generated/autoksyms.h file.
#
# For each symbol being added or removed, the corresponding dependency
# file's timestamp is updated to force a rebuild of the affected source
# file. All arguments passed to this script are assumed to be a command
# to be exec'd to trigger a rebuild of those files.

set -e

cur_ksyms_file="include/generated/autoksyms.h"
new_ksyms_file="include/generated/autoksyms.h.tmpnew"

info() {
	if [ "$quiet" != "silent_" ]; then
		printf "  %-7s %s\n" "$1" "$2"
	fi
}

info "CHK" "$cur_ksyms_file"

# Use "make V=1" to debug this script.
case "$KBUILD_VERBOSE" in
*1*)
	set -x
	;;
esac

# We need access to CONFIG_ symbols
. include/config/auto.conf

# Generate a new symbol list file
$CONFIG_SHELL $srctree/scripts/gen_autoksyms.sh "$new_ksyms_file"

if [ -n "$CONFIG_UNUSED_KSYMS_WHITELIST_ONLY" ] && [ -f "vmlinux" ] ; then
	ksym_wls=/dev/null
	for UNUSED_KSYMS_WHITELIST_FILE in $CONFIG_UNUSED_KSYMS_WHITELIST; do
		eval ksym_wl="$UNUSED_KSYMS_WHITELIST_FILE"
		[ "${ksym_wl}" != "${ksym_wl#/}" ] ||
		ksym_wl="$abs_srctree/$ksym_wl"
		if [ ! -f "$ksym_wl" ]; then
			echo "ERROR: '$ksym_wl' whitelist file not found" >&2
			exit 1
		fi
		ksym_wls="$ksym_wls $ksym_wl"
	done

	info "WARNING" "CONFIG_UNUSED_KSYMS_WHITELIST_ONLY is enabled. "\
"Non-whitelisted symbols will be undefined!"

	syms_from_whitelist=syms_from_whitelist.txt.tmp
	syms_from_vmlinux=syms_from_vmlinux.txt.tmp

	cat $ksym_wl |
	sort -u > "$syms_from_whitelist"

	$NM --defined-only vmlinux |
	grep "__ksymtab_" |
	sed 's/^.*__ksymtab_//' |
	sort -u > "$syms_from_vmlinux"

	# Forcefully unexport the symbols that are not declared in the whitelist
	syms_to_unexport=$(comm -13 "$syms_from_whitelist" "$syms_from_vmlinux")

	echo "$syms_to_unexport" |
	xargs -I% sed -i "/^#define __KSYM_% 1/d" "$new_ksyms_file"

	rm -f "$syms_from_whitelist" "$syms_from_vmlinux"
fi

# Extract changes between old and new list and touch corresponding
# dependency files.
changed=$(
count=0
sort "$cur_ksyms_file" "$new_ksyms_file" | uniq -u |
sed -n 's/^#define __KSYM_\(.*\) 1/\1/p' | tr "A-Z_" "a-z/" |
while read sympath; do
	if [ -z "$sympath" ]; then continue; fi
	depfile="include/ksym/${sympath}.h"
	mkdir -p "$(dirname "$depfile")"
	touch "$depfile"
	# Filesystems with coarse time precision may create timestamps
	# equal to the one from a file that was very recently built and that
	# needs to be rebuild. Let's guard against that by making sure our
	# dep files are always newer than the first file we created here.
	while [ ! "$depfile" -nt "$new_ksyms_file" ]; do
		touch "$depfile"
	done
	echo $((count += 1))
done | tail -1 )
changed=${changed:-0}

if [ $changed -gt 0 ]; then
	# Replace the old list with tne new one
	old=$(grep -c "^#define __KSYM_" "$cur_ksyms_file" || true)
	new=$(grep -c "^#define __KSYM_" "$new_ksyms_file" || true)
	info "KSYMS" "symbols: before=$old, after=$new, changed=$changed"
	info "UPD" "$cur_ksyms_file"
	mv -f "$new_ksyms_file" "$cur_ksyms_file"
	# Then trigger a rebuild of affected source files
	exec $@
else
	rm -f "$new_ksyms_file"
fi
