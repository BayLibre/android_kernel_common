// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021 Google LLC

#include <linux/filter.h>
#include <linux/bpf_fuse.h>

static const struct bpf_func_proto *
fuse_prog_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_trace_printk:
			return bpf_get_trace_printk_proto();

	case BPF_FUNC_get_current_uid_gid:
			return &bpf_get_current_uid_gid_proto;

	case BPF_FUNC_get_current_pid_tgid:
			return &bpf_get_current_pid_tgid_proto;

	case BPF_FUNC_map_lookup_elem:
		return &bpf_map_lookup_elem_proto;

	case BPF_FUNC_map_update_elem:
		return &bpf_map_update_elem_proto;

	case BPF_FUNC_fuse_get_writeable_in:
		return &bpf_fuse_get_writeable_in_proto;

	case BPF_FUNC_fuse_get_writeable_out:
		return &bpf_fuse_get_writeable_out_proto;

	default:
		pr_debug("Invalid fuse bpf func %d\n", func_id);
		return NULL;
	}
}

static bool fuse_prog_is_valid_access(int off, int size,
				enum bpf_access_type type,
				const struct bpf_prog *prog,
				struct bpf_insn_access_aux *info)
{
	int i;

	if (off < 0 || off > offsetofend(struct bpf_fuse_context, out_args))
		return false;

	/* TODO Probably still need some adjustments here */
	for (i = 0; i < FUSE_MAX_IN_ARGS; i++) {
		if (off == offsetof(struct bpf_fuse_context, in_args[i].value)) {
			info->reg_type = PTR_TO_PACKET;
			info->data_id = i;
			if (type != BPF_READ)
				return false;
			return true;
		} else if (off == offsetof(struct bpf_fuse_context, in_args[i].end_offset)) {
			info->reg_type = PTR_TO_PACKET_END;
			info->data_id = i;
			if (type != BPF_READ)
				return false;
			return true;
		}
	}
	for (i = 0; i < FUSE_MAX_OUT_ARGS; i++) {
		if (off == offsetof(struct bpf_fuse_context, out_args[i].value)) {
			info->reg_type = PTR_TO_PACKET;
			info->data_id = i;
			return true;
		} else if (off == offsetof(struct bpf_fuse_context, out_args[i].end_offset)) {
			info->reg_type = PTR_TO_PACKET_END;
			info->data_id = i;
			return true;
		}
	}
	if (type != BPF_READ)
		return false;

	return true;
}

static int fuse_prog_get_prologue(struct bpf_insn *insn_buf,
				   bool direct_write,
				   const struct bpf_prog *prog)
{
	/* TODO: Do we need to do something here? Current thought is no.
	 * But perhaps if we need to clone a buffer or something it goes here?
	 */
	return 0;
}

static int buff_size(struct fuse_bpf_arg *arg)
{
	return ((char *)arg->end_offset - (char *)arg->value);
}

void *bpf_fuse_get_writeable(struct bpf_fuse_context *ctx, bool is_out, u32 index,
		u64 size, bool copy)
{
	struct bpf_fuse_args *fa = container_of(ctx, struct bpf_fuse_args, ctx);
	struct fuse_bpf_arg *arg;
	void *writeable_val;
	uint32_t *flags;

	if (is_out) {
		if (index >= FUSE_MAX_OUT_ARGS || index >= ctx->out_numargs)
			return 0;
	} else {
		if (index >= FUSE_MAX_IN_ARGS || index >= ctx->in_numargs)
			return 0;
	}

	arg = is_out ? &ctx->out_args[index] : &ctx->in_args[index];
	flags = is_out ? &fa->out_flags[index] : &fa->in_flags[index];

	if (*flags & BPF_FUSE_IMMUTABLE)
		return 0;

	if (size <= buff_size(arg) &&
			(!(*flags & BPF_FUSE_MUST_ALLOCATE) || (*flags & BPF_FUSE_ALLOCATED))) {
		if (*flags & BPF_FUSE_VARIABLE_SIZE)
			arg->size = size;
		*flags |= BPF_FUSE_MODIFIED;
		return arg->value;
	}
	/* Variable sized arrays must stay below max size. If the buffer must be fixed size,
	 * don't change the allocated size. Verifier will enforce requested size for accesses
	 */
	if (*flags & BPF_FUSE_VARIABLE_SIZE) {
		if (size > arg->max_size)
			return 0;
	} else {
		if (size > arg->size)
			return 0;
		size = arg->size;
	}

	if (size != arg->size && size > arg->max_size)
		return 0;
	writeable_val = kzalloc(size, GFP_KERNEL);
	if (!writeable_val)
		return 0;

	/* If we're copying the buffer, assume the same amount is used. If that isn't the case,
	 * caller must change size. Otherwise, assume entirety of new buffer is used.
	 */
	if (copy)
		memcpy(writeable_val, arg->value, (arg->size > size) ? size : arg->size);
	else
		arg->size = size;

	if (*flags & BPF_FUSE_ALLOCATED)
		kfree(arg->value);
	arg->value = writeable_val;
	arg->end_offset = (char *)writeable_val + size;

	*flags |= BPF_FUSE_ALLOCATED | BPF_FUSE_MODIFIED;

	return arg->value;
}
EXPORT_SYMBOL(bpf_fuse_get_writeable);

BPF_CALL_4(bpf_fuse_get_writeable_in, struct bpf_fuse_context *, ctx, u32, index, u64, size,
		bool, copy)
{
	return (unsigned long) bpf_fuse_get_writeable(ctx, false, index, size, copy);
}

BPF_CALL_4(bpf_fuse_get_writeable_out, struct bpf_fuse_context *, ctx, u32, index, u64, size,
		bool, copy)
{
	return (unsigned long) bpf_fuse_get_writeable(ctx, true, index, size, copy);
}

const struct bpf_func_proto bpf_fuse_get_writeable_in_proto = {
	.func		= bpf_fuse_get_writeable_in,
	.ret_type	= RET_PTR_TO_ALLOC_MEM_OR_NULL,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_CONST_ALLOC_SIZE_OR_ZERO,
	.arg4_type	= ARG_ANYTHING,
	.gpl_only	= false,
	.pkt_access	= true,
};

const struct bpf_func_proto bpf_fuse_get_writeable_out_proto = {
	.func		= bpf_fuse_get_writeable_out,
	.ret_type	= RET_PTR_TO_ALLOC_MEM_OR_NULL,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_CONST_ALLOC_SIZE_OR_ZERO,
	.arg4_type	= ARG_ANYTHING,
	.gpl_only	= false,
	.pkt_access	= true,
};


const struct bpf_verifier_ops fuse_verifier_ops = {
	.get_func_proto  = fuse_prog_func_proto,
	.is_valid_access = fuse_prog_is_valid_access,
	.gen_prologue = fuse_prog_get_prologue,
};

const struct bpf_prog_ops fuse_prog_ops = {
};

