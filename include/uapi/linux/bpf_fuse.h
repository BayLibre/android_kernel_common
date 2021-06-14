#ifndef _UAPI__LINUX_BPF_FUSE_H__
#define _UAPI__LINUX_BPF_FUSE_H__

#include <uapi/linux/limits.h>

struct bpf_fuse_data {
	__s8 name[NAME_MAX];
};

#endif /* _UAPI__LINUX_BPF_FUSE_H__ */
