/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _SELINUX_POLICYCAP_NAMES_H_
#define _SELINUX_POLICYCAP_NAMES_H_

#include "policycap.h"

/* clang-format off */
/* Policy capability names */
const char *const selinux_policycap_names[__POLICYDB_CAP_MAX] = {
	"network_peer_controls",
	"open_perms",
	"extended_socket_class",
	"always_check_network",
	"cgroup_seclabel",
	"nnp_nosuid_transition",
	"genfs_seclabel_symlinks",
	"ioctl_skip_cloexec",
	"userspace_initial_context",
<<<<<<< HEAD   (84595e7cd483e40c6b65961e7ffdc2b89af11413 ANDROID: GKI: x86: Disable IOMMU passthrough mode)
	"netlink_xperm",
	"netif_wildcard",
||||||| BASE   (189046767b10c7de900a253437d13f761f371dca Revert "ANDROID: mm: create vendor hooks for mm  proactive c)
=======
>>>>>>> BRANCH (4806465da60368a169da905e89f148f5783d7400 UPSTREAM: selinux: support wildcard match in genfscon)
	"genfs_seclabel_wildcard",
};
/* clang-format on */

#endif /* _SELINUX_POLICYCAP_NAMES_H_ */
