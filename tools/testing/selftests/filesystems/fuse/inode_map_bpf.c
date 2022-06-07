// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Copyright (c) 2021 Google LLC

#include "test_bpf.h"

struct {
	__uint(type, BPF_MAP_TYPE_INODE_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, __u32);
	__type(value, __u32);
} inode_storage_map1 SEC(".maps");

struct compound_value {
	__u32 delete;
	__u32 store;
};

struct {
	__uint(type, BPF_MAP_TYPE_INODE_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, __u32);
	__type(value, struct compound_value);
} inode_storage_map2 SEC(".maps");

SEC("fuse/inode_map_test")
int inode_map_test(struct __bpf_fuse_args *fa)
{
	switch (fa->opcode) {
	case FUSE_LOOKUP | FUSE_PREFILTER: {
		return BPF_FUSE_POSTFILTER;
	}

	case FUSE_LOOKUP | FUSE_POSTFILTER: {
		const char *name = (void *)(long)fa->in_args[0].value;
		const char *end = (void *)(long)fa->in_args[0].end_offset;
		struct compound_value value = {
			!strcmp_check("file1", name, end),
			1000,
		};
		struct compound_value *pvalue;

		bpf_printk("Lookup postfilter %s inode %px", fa->in_args[0].value, fa->backing_inode);
		pvalue = bpf_inode_storage_get(&inode_storage_map2, fa->backing_inode, &value, BPF_LOCAL_STORAGE_GET_F_CREATE);
		bpf_printk("Lookup bpf_inode_storage_get returned %px", pvalue);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_CREATE | FUSE_PREFILTER: {
		uint32_t value = 100;
		uint32_t *pvalue;

		bpf_printk("Create %px", fa->backing_inode);
		pvalue = bpf_inode_storage_get(&inode_storage_map1, fa->backing_inode, &value, BPF_LOCAL_STORAGE_GET_F_CREATE);
		bpf_printk("Create bpf_inode_storage_get returned %px", pvalue);
		return BPF_FUSE_POSTFILTER;
	}

	case FUSE_CREATE | FUSE_POSTFILTER: {
		uint32_t value = 100;
		uint32_t *pvalue;

		bpf_printk("Create postfilter %s %px", fa->in_args[1].value, fa->backing_inode);
		pvalue = bpf_inode_storage_get(&inode_storage_map1, fa->backing_inode, &value, BPF_LOCAL_STORAGE_GET_F_CREATE);
		bpf_printk("Create bpf_inode_storage_get returned %px", pvalue);
		return 0;
	}

	case FUSE_OPEN | FUSE_PREFILTER: {
		uint32_t *pvalue;

		bpf_printk("Open %px", fa->backing_inode);
		pvalue = bpf_inode_storage_get(&inode_storage_map1, fa->backing_inode, NULL, 0);
		if (pvalue) {
			bpf_printk("Open: value %u", *pvalue);
			++*pvalue;
		}
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_RELEASE | FUSE_PREFILTER: {
		struct compound_value *pvalue;

		pvalue = bpf_inode_storage_get(&inode_storage_map2, fa->backing_inode, NULL, 0);
		bpf_printk("Close bpf_inode_storage_get from inode %px", fa->backing_inode);
		if (pvalue) {
			bpf_printk("Close bpf_inode_storage_get returned %d %d", pvalue->delete, pvalue->store);
			if (pvalue->delete) {
				int res = bpf_inode_storage_delete(&inode_storage_map2, fa->backing_inode);
				bpf_printk("deleting map entry with result %d", res);
			}
		}
		return BPF_FUSE_CONTINUE;
	}

	default:
		return BPF_FUSE_CONTINUE;
	}
}

char _license[] SEC("license") = "GPL";
