#include <linux/filter.h>

static const struct bpf_func_proto *
fuse_prog_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	default:
		return bpf_tracing_func_proto(func_id, prog);
	}
}

static bool fuse_prog_is_valid_access(int off, int size, enum bpf_access_type type,
				    const struct bpf_prog *prog,
				    struct bpf_insn_access_aux *info)
{
	if (off < sizeof(void *) || off >= 32)
		return false;
	if (type != BPF_READ)
		return false;
	if (off % size != 0)
		return false;

	return true;
}

const struct bpf_verifier_ops fuse_verifier_ops = {
	.get_func_proto  = fuse_prog_func_proto,
	.is_valid_access = fuse_prog_is_valid_access,
};

const struct bpf_prog_ops fuse_prog_ops = {
};
