/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2022 Google LLC.
 */

#ifndef _BPF_FUSE_H
#define _BPF_FUSE_H

#include <linux/fuse.h>

/* These flags are used internally to track information about the fuse buffers.
 * Fuse sets some of the flags in init. The helper functions sets others, depending on what
 * was requested by the bpf program.
 */
// Flags set by FUSE
#define BPF_FUSE_IMMUTABLE	(1 << 0) // Buffer may not be written to
#define BPF_FUSE_VARIABLE_SIZE	(1 << 1) // Buffer length may be changed (growth requires alloc)
#define BPF_FUSE_MUST_ALLOCATE	(1 << 2) // Buffer must be re allocated before allowing writes

// Flags set by helper function
#define BPF_FUSE_MODIFIED	(1 << 3) // The helper function allowed writes to the buffer
#define BPF_FUSE_ALLOCATED	(1 << 4) // The helper function allocated the buffer

struct bpf_fuse_args {
	struct bpf_fuse_context ctx;
	uint32_t in_flags[3];
	uint32_t out_flags[2];
};

extern void *bpf_fuse_get_writeable(struct bpf_fuse_context *fa, bool is_out, u32 index,
				    u64 size, bool copy);

#endif /* _BPF_FUSE_H */
