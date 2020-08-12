#!/bin/sh
# Copyright (c) 2020, Google LLC. All rights reserved.
# Author: Saravana Kannan <saravanak@google.com>

function help() {
	cat << EOF
Usage: $(basename $0) [-c|-d|-m|-f] <list of device paths in /sys/devices>

The script takes as input a list of one or more directories under
/sys/devices and then lists the probe dependency chain (suppliers and
parents) of these devices. It does a breadth first search of the dependency
chain, so the last entry in the output is close to the root of the
dependency chain.

By default it lists the full path to the devices under /sys/devices.

It also takes an optional modifier flag as the first parameter to change what
information is listed in the output. If the requested information is not
available, the device name is printed.

  -c	lists the compatible string of the dependencies
  -d	lists the driver name of the dependencies that have probed
  -m	lists the module name of the dependencies that have a module
  -f	list the firmware node path of the dependencies

EOF
}

function dev_to_detail() {
	local i=0
	for s in $@
	do
		echo $(printf "%05u" ${i})$'\t'$(detail ${s})
		i=$((i+1))
	done
}

function already_seen() {
	for o in ${OUT_LIST[@]}
	do
		if [ "$1" = "$o" ]
		then
			# if-statement treats 0 (no-error) as true
			return 0
		fi
	done

	# if-statement treats 1 (error) as false
	return 1
}

function add_suppliers() {
	CON=$1

	if already_seen $CON
	then
		return 1
	fi

	# If this is not a device with a driver, we don't care about its
	# suppliers.
	if [ ! -e $CON/driver ]
	then
		return 1
	fi

	SUPPLIER_LINKS=$(ls -1d $CON/supplier:* 2>/dev/null)
	for SL in $SUPPLIER_LINKS;
	do
		SYNC_STATE=$(cat $SL/sync_state_only)

		# sync_state_only links are proxy dependencies.
		# They can also have cycles. So, don't follow them.
		if [ "$SYNC_STATE" != '0' ]
		then
			continue
		fi

		SUPPLIER=$(realpath $SL/supplier)

		CONSUMERS+=($SUPPLIER)
	done
	return 0
}

case $1 in
	--help)
		help
		exit 0
		;;
	-c)
		function detail() {
			f=$1/of_node/compatible
			if [ -e $f ]
			then
				echo -n $(cat $f)
			else
				echo -n $1
			fi
		}
		shift
		;;
	-m)
		function detail() {
			f=$1/driver/module
			if [ -e $f ]
			then
				echo -n $(basename $(realpath $f))
			else
				echo -n $1
			fi
		}
		shift
		;;
	-d)
		function detail() {
			f=$1/driver
			if [ -e $f ]
			then
				echo -n $(basename $(realpath $f))
			else
				echo -n $1
			fi
		}
		shift
		;;
	-f)
		function detail() {
			f=$1/firmware_node
			if [ ! -e $f ]
			then
				f=$1/of_node
			fi

			if [ -e $f ]
			then
				echo -n $(realpath $f)
			else
				echo -n $1
			fi
		}
		shift
		;;
	*)
		function detail() { echo -n $1; }
		;;
esac

if [ $# -eq 0 ]
then
	help
	exit 1
fi

CONSUMERS=($@)
OUT_LIST=()

# Do a breadth first, non-recursive tracking of suppliers. The parent is also
# considered a "supplier" as a device can't probe without its parent.
i=0
while [ $i -lt ${#CONSUMERS[@]} ]
do
	CONSUMER=$(realpath ${CONSUMERS[$i]})
	i=$(($i+1))

	# The $CONSUMER could be a symlink path. So, we need to find the real
	# path and then go up one level to find the real parent.
	PARENT=$(realpath $CONSUMER/..)

	if [ "${PARENT}" != "/sys/devices" ]
	then
		CONSUMERS+=(${PARENT})
	fi

	# Add suppliers to CONSUMERS list and output the consumer details.
	#
	# We don't need to worry about a cycle in the dependency chain causing
	# infinite loops. That's because the kernel doesn't allow cycles in
	# device links unless it's a sync_state_only device link. And we ignore
	# sync_state_only device links inside add_suppliers.
	if add_suppliers ${CONSUMER}
	then
		OUT_LIST+=(${CONSUMER})
	fi
done

# Can NOT combine sort and uniq using sort -suk2 because stable sort in Android
# isn't really stable.
dev_to_detail ${OUT_LIST[@]} | sort -k2 -k1 | uniq -f 1 | sort | cut -f2-

exit 0
