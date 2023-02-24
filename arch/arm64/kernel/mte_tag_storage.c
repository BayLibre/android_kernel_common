// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support for dynamic tag storage.
 *
 * Copyright (C) 2023 ARM Ltd.
 */

#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/of_device.h>
#include <linux/of_fdt.h>
#include <linux/range.h>
#include <linux/string.h>
#include <linux/xarray.h>

__ro_after_init DEFINE_STATIC_KEY_FALSE(mte_tag_storage_enabled_key);

struct tag_region {
	struct range mem_range;	/* Memory associated with the tag storage, in PFNs. */
	struct range tag_range;	/* Tag storage memory, in PFNs. */
	u32 block_size;		/* Tag block size, in pages. */
};

#define MAX_TAG_REGIONS	32

static struct tag_region tag_regions[MAX_TAG_REGIONS];
static int num_tag_regions;

static int __init tag_storage_of_flat_get_range(unsigned long node, const __be32 *reg,
						int reg_len, struct range *range)
{
	int addr_cells = dt_root_addr_cells;
	int size_cells = dt_root_size_cells;
	u64 size;

	if (reg_len / 4 > addr_cells + size_cells)
		return -EINVAL;

	range->start = PHYS_PFN(of_read_number(reg, addr_cells));
	size = PHYS_PFN(of_read_number(reg + addr_cells, size_cells));
	if (size == 0) {
		pr_err("Invalid node");
		return -EINVAL;
	}
	range->end = range->start + size - 1;

	return 0;
}

static int __init tag_storage_of_flat_get_tag_range(unsigned long node,
						    struct range *tag_range)
{
	const __be32 *reg;
	int reg_len;

	reg = of_get_flat_dt_prop(node, "reg", &reg_len);
	if (reg == NULL) {
		pr_err("Invalid metadata node");
		return -EINVAL;
	}

	return tag_storage_of_flat_get_range(node, reg, reg_len, tag_range);
}

static int __init tag_storage_of_flat_get_memory_range(unsigned long node, struct range *mem)
{
	const __be32 *reg;
	int reg_len;

	reg = of_get_flat_dt_prop(node, "linux,usable-memory", &reg_len);
	if (reg == NULL)
		reg = of_get_flat_dt_prop(node, "reg", &reg_len);

	if (reg == NULL) {
		pr_err("Invalid memory node");
		return -EINVAL;
	}

	return tag_storage_of_flat_get_range(node, reg, reg_len, mem);
}

struct find_memory_node_arg {
	unsigned long node;
	u32 phandle;
};

static int __init fdt_find_memory_node(unsigned long node, const char *uname,
				       int depth, void *data)
{
	const char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	struct find_memory_node_arg *arg = data;

	if (depth != 1 || !type || strcmp(type, "memory") != 0)
		return 0;

	if (of_get_flat_dt_phandle(node) == arg->phandle) {
		arg->node = node;
		return 1;
	}

	return 0;
}

static int __init tag_storage_get_memory_node(unsigned long tag_node, unsigned long *mem_node)
{
	struct find_memory_node_arg arg = { 0 };
	const __be32 *memory_prop;
	u32 mem_phandle;
	int ret, reg_len;

	memory_prop = of_get_flat_dt_prop(tag_node, "memory", &reg_len);
	if (!memory_prop) {
		pr_err("Missing 'memory' property in the tag storage node");
		return -EINVAL;
	}

	mem_phandle = be32_to_cpup(memory_prop);
	arg.phandle = mem_phandle;

	ret = of_scan_flat_dt(fdt_find_memory_node, &arg);
	if (ret != 1) {
		pr_err("Associated memory node not found");
		return -EINVAL;
	}

	*mem_node = arg.node;

	return 0;
}

static int __init tag_storage_of_flat_read_u32(unsigned long node, const char *propname,
					       u32 *retval)
{
	const __be32 *reg;

	reg = of_get_flat_dt_prop(node, propname, NULL);
	if (!reg)
		return -EINVAL;

	*retval = be32_to_cpup(reg);
	return 0;
}

static int __init fdt_init_tag_storage(unsigned long node, const char *uname,
				       int depth, void *data)
{
	struct tag_region *region;
	unsigned long mem_node;
	struct range *mem_range;
	struct range *tag_range;
	u32 block_size, nid;
	int ret;

	if (depth != 1 || !strstr(uname, "metadata"))
		return 0;

	if (!of_flat_dt_is_compatible(node, "arm,mte-tag-storage"))
		return 0;

	if (num_tag_regions == MAX_TAG_REGIONS) {
		pr_err("Maximum number of tag storage regions exceeded");
		return -EINVAL;
	}

	region = &tag_regions[num_tag_regions];
	mem_range = &region->mem_range;
	tag_range = &region->tag_range;

	ret = tag_storage_of_flat_get_tag_range(node, tag_range);
	if (ret) {
		pr_err("Invalid tag storage node");
		return ret;
	}

	ret = tag_storage_get_memory_node(node, &mem_node);
	if (ret)
		return ret;

	ret = tag_storage_of_flat_get_memory_range(mem_node, mem_range);
	if (ret) {
		pr_err("Invalid address for associated data memory node");
		return ret;
	}

	/* The tag region must exactly match the corresponding memory. */
	if (range_len(tag_range) * 32 != range_len(mem_range)) {
		pr_err("Tag region doesn't cover exactly the corresponding memory region");
		return -EINVAL;
	}

	ret = tag_storage_of_flat_read_u32(node, "block-size", &block_size);
	if (ret || block_size == 0) {
		pr_err("Invalid or missing 'block-size' property");
		return -EINVAL;
	}

	block_size = PFN_UP(block_size);
	if (range_len(tag_range) % block_size != 0) {
		pr_err("Tag storage region size is not a multiple of allocation block size");
		return -EINVAL;
	}
	// TODO: support block sizes larger than PAGE_SIZE.
	if (block_size != 1) {
		pr_err("Unsupported block size %u", block_size);
		return -EINVAL;
	}
	region->block_size = block_size;

	ret = tag_storage_of_flat_read_u32(mem_node, "numa-node-id", &nid);
	if (ret)
		nid = numa_node_id();

	ret = memblock_add_node(PFN_PHYS(tag_range->start), PFN_PHYS(range_len(tag_range)),
				nid, MEMBLOCK_NONE);
	if (ret) {
		pr_err("Error adding tag memblock (%d)", ret);
		return ret;
	}
	memblock_reserve(PFN_PHYS(tag_range->start), PFN_PHYS(range_len(tag_range)));

	pr_info("Found MTE tag storage region 0x%llx@0x%llx, block size 0x%llx",
		PFN_PHYS(range_len(tag_range)), PFN_PHYS(tag_range->start),
		PFN_PHYS(region->block_size));

	num_tag_regions++;

	return 0;
}

void __init mte_tag_storage_init(void)
{
	struct range *tag_range;
	int i, ret;

	ret = of_scan_flat_dt(fdt_init_tag_storage, NULL);
	if (ret) {
		pr_err("MTE tag storage management disabled");
		goto out_err;
	}

	if (num_tag_regions == 0)
		pr_info("No MTE tag storage regions detected");

	return;

out_err:
	for (i = 0; i < num_tag_regions; i++) {
		tag_range = &tag_regions[i].tag_range;
		memblock_remove(PFN_PHYS(tag_range->start), PFN_PHYS(range_len(tag_range)));
	}
	num_tag_regions = 0;
}

bool alloc_can_use_tag_storage(gfp_t gfp_mask)
{
	return !(gfp_mask & __GFP_ZEROTAGS);
}
