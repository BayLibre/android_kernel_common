// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Copyright (c) 2022 Google LLC

#include "test_fuse_bpf.h"

SEC("test_trace")
/* return FUSE_BPF_BACKING to use backing fs, 0 to pass to usermode */
int trace_test(struct fuse_bpf_args *fa)
{
	return 0;
}
