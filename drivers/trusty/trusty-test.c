// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Google, Inc.
 */

#include <linux/ctype.h>
#include <linux/platform_device.h>
#include <linux/trusty/smcall.h>
#include <linux/trusty/trusty.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include "trusty-test.h"

struct trusty_test_state {
	struct device *dev;
	struct device *trusty_dev;
};

static int trusty_test_rw(struct trusty_test_state *s, size_t size,
			  trusty_shared_mem_id_t mem_id, u64 *buf)
{
	int ret;
	size_t i;

	for (i = 0; i < size / sizeof(*buf); i++)
		buf[i] = i;

	ret = trusty_std_call32(s->trusty_dev, SMC_SC_TEST_SHARED_MEM_RW,
				(u32)(mem_id), (u32)(mem_id >> 32), size);
	if (ret < 0) {
		dev_err(s->dev,
			"trusty std call (SMC_SC_TEST_SHARED_MEM_RW) failed: %d 0x%llx\n",
			ret, mem_id);
		return ret;
	}

	for (i = 0; i < size / sizeof(*buf); i++) {
		if (buf[i] != size - i) {
			dev_err(s->dev,
				"input mismatch at %zd, got 0x%llx instead of 0x%zx\n",
				i, buf[i], size - i);
			return -EIO;
		}
	}
	dev_info(s->dev, "[ PASSED ] mem_id 0x%llx, size %zd\n", mem_id, size);

	return 0;
}

static int trusty_test_run_size(struct trusty_test_state *s, size_t page_count,
				size_t repeat_share, size_t repeat_access)
{
	void *buf;
	int ret;
	int rret;
	size_t i;
	size_t j;
	struct sg_table sgt;
	trusty_shared_mem_id_t mem_id;
	struct page **pages;
	size_t size = page_count * PAGE_SIZE;

	pages = kmalloc_array(page_count, sizeof(*pages), GFP_KERNEL);
	if (!pages) {
		ret = -ENOMEM;
		dev_err(s->dev, "failed to allocate page array, count %zd\n",
			page_count);
		goto err_alloc_pages;
	}

	for (i = 0; i < page_count; i++) {
		pages[i] = alloc_page(GFP_KERNEL);
		if (!pages[i]) {
			ret = -ENOMEM;
			dev_err(s->dev, "failed to allocate page %zd/%zd\n",
				i, page_count);
			goto err_alloc_page;
		}
		if (i > 0 && pages[i - 1] + 1 == pages[i]) {
			/* swap adacent pages to increase fragmentation */
			swap(pages[i - 1], pages[i]);
		}
	}

	buf = vmap(pages, page_count, VM_MAP, PAGE_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		dev_err(s->dev, "failed to map test buffer page count %zd\n",
			page_count);
		goto err_map_pages;
	}

	ret = sg_alloc_table_from_pages(&sgt, pages, page_count, 0, size,
					GFP_KERNEL);
	if (ret) {
		dev_err(s->dev, "sg_alloc_table_from_pages failed: %d\n", ret);
		goto err_alloc_sgt;
	}
	dev_info(s->dev, "buffer has %d page runs\n", sgt.nents);

	for (j = 0; j < repeat_share; j++) {
		ret = trusty_share_memory(s->trusty_dev, &mem_id, sgt.sgl,
					  sgt.nents, PAGE_KERNEL);
		if (ret) {
			dev_err(s->dev, "trusty_share_memory failed: %d, j=%zd/%zd, size=%zd\n",
				ret, j, repeat_share, size);
			goto err_share_memory;
		}

		for (i = 0; i < repeat_access; i++) {
			/*
			 * Repeat test in case the memory attributes don't match
			 * and either side see old data.
			 */
			ret = trusty_test_rw(s, size, mem_id, buf);
			if (ret) {
				dev_err(s->dev, "test failed: %d, j=%zd/%zd, size=%zd\n",
					ret, j, repeat_share, size);
				break;
			}
		}

		rret = trusty_revoke_memory(s->trusty_dev, mem_id, sgt.sgl,
					    sgt.nents);
		if (rret) {
			dev_err(s->dev,
				"trusty_revoke_memory failed: %d 0x%llx\n",
				rret, mem_id);
			ret = -EIO;
			/*
			 * It is not safe to free this memory if
			 * trusty_revoke_memory fails. Leak it in that case.
			 */
			goto err_revoke;
		}
		if (ret)
			break;
	}
err_share_memory:
	sg_free_table(&sgt);
err_alloc_sgt:
	vunmap(buf);
err_map_pages:
	for (i = page_count; i > 0; i--) {
		__free_page(pages[i - 1]);
err_alloc_page:
		;
	}
	kfree(pages);
err_revoke:
err_alloc_pages:
	return ret;
}

static size_t trusty_test_get_arg(const char **buf, size_t default_val)
{
	char *buf_next;
	size_t ret;

	if (**buf != ',')
		return default_val;

	(*buf)++;
	ret = simple_strtoul(*buf, &buf_next, 0);
	if (buf_next == *buf)
		return default_val;

	*buf = buf_next;

	return ret;
}

static ssize_t trusty_test_run_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct trusty_test_state *s = platform_get_drvdata(pdev);
	size_t size;
	size_t repeat_share;
	size_t repeat_access;
	int ret;
	char *buf_next;

	while (true) {
		while (isspace(*buf))
			buf++;
		size = simple_strtoul(buf, &buf_next, 0);
		if (buf_next == buf)
			return count;
		buf = buf_next;
		repeat_share = trusty_test_get_arg(&buf, 1);
		repeat_access = trusty_test_get_arg(&buf, 3);

		ret = trusty_test_run_size(s, DIV_ROUND_UP(size, PAGE_SIZE),
					   repeat_share, repeat_access);
		if (ret)
			return ret;
	}
}

DEVICE_ATTR_WO(trusty_test_run);

static int trusty_test_probe(struct platform_device *pdev)
{
	struct trusty_test_state *s;
	int ret;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	ret = trusty_std_call32(pdev->dev.parent, SMC_SC_TEST_VERSION,
				TRUSTY_STDCALLTEST_API_VERSION, 0, 0);
	if (ret != TRUSTY_STDCALLTEST_API_VERSION) {
		ret = -ENOENT;
		goto err_unsupported;
	}

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s) {
		ret = -ENOMEM;
		goto err_alloc_state;
	}

	s->dev = &pdev->dev;
	s->trusty_dev = s->dev->parent;

	platform_set_drvdata(pdev, s);

	ret = device_create_file(&pdev->dev, &dev_attr_trusty_test_run);
	if (ret)
		goto err_create_file;

	return 0;

err_create_file:
	kfree(s);
err_alloc_state:
err_unsupported:
	return ret;
}

static int trusty_test_remove(struct platform_device *pdev)
{
	struct trusty_log_state *s = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "%s\n", __func__);

	device_remove_file(&pdev->dev, &dev_attr_trusty_test_run);
	kfree(s);

	return 0;
}

static const struct of_device_id trusty_test_of_match[] = {
	{ .compatible = "android,trusty-test-v1", },
	{},
};

static struct platform_driver trusty_test_driver = {
	.probe = trusty_test_probe,
	.remove = trusty_test_remove,
	.driver = {
		.name = "trusty-test",
		.owner = THIS_MODULE,
		.of_match_table = trusty_test_of_match,
	},
};

module_platform_driver(trusty_test_driver);
