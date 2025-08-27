// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 Samsung Electronics Co., Ltd.
 *            http://www.samsung.com/
 */

#include <linux/debugfs.h>

#include "exynos-hvc.h"
#include "hvm_drv.h"

/* hvm debugfs root directory */
struct dentry *hvm_debugfs_dir;

static uint64_t get_addr_from_string(char *st, int len)
{
	uint64_t ret = 0;
	int i = 0;

	// skip '0x' prefix
	if (st[0] == '0' && st[1] == 'x')
		i = 2;

	// convert ascii to address
	for (; i < len; i++) {
		ret *= 16;

		if ('0' <= st[i] && st[i] <= '9')
			ret += (st[i] - '0');
		else if ('a' <= st[i] && st[i] <= 'f')
			ret += (st[i] - 'a' + 10);
		else if ('A' <= st[i] && st[i] <= 'F')
			ret += (st[i] - 'A' + 10);
		else
			return -EINVAL;
	}

	return ret;
}

static ssize_t translate_addr_ops_get_pa(struct file *file, char __user *buf,
					size_t len, loff_t *offset)
{
	char tmp[64] = {0, };
	struct hvm *vm = file->private_data;
	uint64_t ipa, pa;
	uint64_t ret;

	if (*offset == 0) {
		// get translated pa
		ipa = vm->vm_debug.ipa_requested;
		if (ipa == -EINVAL) {
			snprintf(tmp, sizeof(tmp),
				"Invalid address: Incorrect format, [a-fA-F0-9] only\n");
			goto exit;
		}

		ret = exynos_hvc(HVC_FID_HVM_DEBUG, HVM_DEBUG_TRANSLATE_IPA2PA,
				vm->vm_id, ipa, 0);

		if (ret == -ENOMEM) {
			snprintf(tmp, sizeof(tmp),
				"Invalid address 0x%llx: Outside of vm memory\n", ipa);
			goto exit;
		} else if (ret == -EFAULT) {
			snprintf(tmp, sizeof(tmp),
				"Invalid address 0x%llx: Not mapped\n", ipa);
			goto exit;
		}

		pa = ret;

		// Is ipa shared with host?
		ret = exynos_hvc(HVC_FID_HVM_DEBUG, HVM_DEBUG_IS_SHARED_PAGE,
				vm->vm_id, ipa, 0);

		if (ret == -EFAULT) {
			snprintf(tmp, sizeof(tmp),
				"Invalid address 0X%llx: Not mapped\n", ipa);
			goto exit;
		}

		snprintf(tmp, sizeof(tmp), "0x%llx -> 0x%llx: %s\n",
				ipa, pa, (ret ? "Shared w/ host" : "Protected"));

exit:
		if (copy_to_user(buf, tmp, sizeof(tmp)))
			return -EFAULT;

		*offset += sizeof(tmp);

		return sizeof(tmp);
	}

	return 0;
}

static ssize_t translate_addr_ops_set_ipa(struct file *file, const char __user *buf,
					size_t len, loff_t *offset)
{
	char tmp[64] = {0, };
	struct hvm *vm = file->private_data;

	if (*offset == 0) {
		// set ipa to be translated
		if (copy_from_user(tmp, buf, sizeof(tmp)))
			return -EFAULT;

		vm->vm_debug.ipa_requested = get_addr_from_string(tmp, len - 1);

		return len;
	}

	return 0;
}

static const struct file_operations translate_addr_ops = {
	.open = simple_open,
	.read = translate_addr_ops_get_pa,
	.write = translate_addr_ops_set_ipa,
};

int hvm_drv_debug_init(void)
{
	hvm_debugfs_dir = debugfs_create_dir("hvm", NULL);
	if (!hvm_debugfs_dir)
		return -ENOMEM;

	return 0;
}

int hvm_create_vm_debugfs(struct hvm *hvm)
{
	struct dentry *dent;
	char dname[16];

	if (!hvm_debugfs_dir) {
		pr_err("hvm debugfs directory is not exists\n");
		return -EFAULT;
	}

	if (hvm->debugfs_dir) {
		pr_warn("VM debugfs directory is already exists\n");
		return 0;
	}

	snprintf(dname, sizeof(dname), "VM-%d", hvm->vm_id);

	dent = debugfs_lookup(dname, hvm_debugfs_dir);
	if (dent) {
		pr_warn("VM debugfs directory is already exists\n");
		return 0;
	}
	dent = debugfs_create_dir(dname, hvm_debugfs_dir);
	hvm->debugfs_dir = dent;

	debugfs_create_file("stage2_ipa_to_pa", 0644, dent, hvm, &translate_addr_ops);

	return 0;
}

int hvm_destroy_vm_debugfs(struct hvm *hvm)
{
	debugfs_remove_recursive(hvm->debugfs_dir);
	return 0;
}

