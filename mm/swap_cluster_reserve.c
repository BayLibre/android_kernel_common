// SPDX-License-Identifier: GPL-2.0
/*
 * Swap Cluster Reservation
 *
 * This file contains the knobs to:
 *   - set the percent of swap clusters reserved for large folios
 *   - set the order of folios for which clusters are reserved.
 *
 * Copyright (c) 2024, Google LLC.
 * Author: Kalesh Singh <kaleshsingh@goole.com>
 */

#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/kstrtox.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/sysfs.h>

/*
 * Boot parameter interface.
 */

#define MAX_SWAP_CLUSTER_RESERVES 5
unsigned int vm_swap_cluster_reserve_percents[MAX_SWAP_CLUSTER_RESERVES] = {0};
unsigned int vm_swap_cluster_reserve_orders[MAX_SWAP_CLUSTER_RESERVES] = {0};
unsigned int vm_swap_cluster_nr_reserves = 0;

static int nr_tuples(char *str)
{
	int count = 0;
	char* start = str;
	while ((start = strchr(start, '(')) != NULL) {
		count++;
		start++;
	}
	return count;
}

static int parse_tuples(char *str, int count)
{
	size_t size = sizeof(unsigned int) * MAX_SWAP_CLUSTER_RESERVES;
	/* First use temporary storage for the initial parsing */
	unsigned int percents[MAX_SWAP_CLUSTER_RESERVES];
	unsigned int orders[MAX_SWAP_CLUSTER_RESERVES];
	char* start = str;
	int ret = 0;

	for (int i = 0; i < count; i++) {
		int matches = sscanf(start, "(%u,%u)", &orders[i], &percents[i]);

		if (matches != 2) {
			ret = -EINVAL;
			pr_err("DEBUG: SWAP CLUSTER: Unexpected matches: expect 2, got %d", matches);
			goto out;
		}

		pr_err("DEBUG: SWAP CLUSTER: Found (%u, %u)", orders[i], percents[i]);

		start = strchr(start, ')') + 2;
	}

	memcpy(vm_swap_cluster_reserve_percents, percents, size);
	memcpy(vm_swap_cluster_reserve_orders, orders, size);
	vm_swap_cluster_nr_reserves = count;
out:
	return ret;
}

static int __init early_swap_cluster_reserves(char *buf)
{
	int ret = 0;

	int count = nr_tuples(buf);

	if (count < 1 || count > MAX_SWAP_CLUSTER_RESERVES)
		return -EINVAL;

	if (parse_tuples(buf, count))
		return -EINVAL;

	for (int i = 0; i < vm_swap_cluster_nr_reserves; i++) {
		pr_err("DEBUG: SWAP CLUSTER: Reserve %d: (%u,%u)", i, vm_swap_cluster_reserve_orders[i],
			vm_swap_cluster_reserve_percents[i]);
	}

	return ret;
}
early_param("swap_cluster_reserves", early_swap_cluster_reserves);
