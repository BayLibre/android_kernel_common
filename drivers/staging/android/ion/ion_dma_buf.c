// SPDX-License-Identifier: GPL-2.0
/*
 * ION Memory Allocator dmabuf operations
 *
 * Copyright (C) 2019 Google, Inc.
 */

#include <linux/export.h>
#include <linux/slab.h>
#include <linux/ion.h>

#include "../uapi/ion.h"

static void _ion_buffer_destroy(struct ion_buffer *buffer)
{
	struct ion_heap *heap = buffer->heap;
	struct ion_device *dev = buffer->dev;

	mutex_lock(&dev->buffer_lock);
	rb_erase(&buffer->node, &dev->buffers);
	mutex_unlock(&dev->buffer_lock);

	if (heap->flags & ION_HEAP_FLAG_DEFER_FREE)
		ion_heap_freelist_add(heap, buffer);
	else
		ion_buffer_destroy(buffer);
}

static void *ion_buffer_kmap_get(struct ion_buffer *buffer)
{
	void *vaddr;

	if (buffer->kmap_cnt) {
		buffer->kmap_cnt++;
		return buffer->vaddr;
	}
	vaddr = buffer->heap->ops->map_kernel(buffer->heap, buffer);
	if (WARN_ONCE(!vaddr,
		      "heap->ops->map_kernel should return ERR_PTR on error"))
		return ERR_PTR(-EINVAL);
	if (IS_ERR(vaddr))
		return vaddr;
	buffer->vaddr = vaddr;
	buffer->kmap_cnt++;
	return vaddr;
}

static void ion_buffer_kmap_put(struct ion_buffer *buffer)
{
	buffer->kmap_cnt--;
	if (!buffer->kmap_cnt) {
		buffer->heap->ops->unmap_kernel(buffer->heap, buffer);
		buffer->vaddr = NULL;
	}
}

static struct sg_table *dup_sg_table(struct sg_table *table)
{
	struct sg_table *new_table;
	int ret, i;
	struct scatterlist *sg, *new_sg;

	new_table = kzalloc(sizeof(*new_table), GFP_KERNEL);
	if (!new_table)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(new_table, table->nents, GFP_KERNEL);
	if (ret) {
		kfree(new_table);
		return ERR_PTR(-ENOMEM);
	}

	new_sg = new_table->sgl;
	for_each_sg(table->sgl, sg, table->nents, i) {
		memcpy(new_sg, sg, sizeof(*sg));
		new_sg->dma_address = 0;
		new_sg = sg_next(new_sg);
	}

	return new_table;
}

static void free_duped_table(struct sg_table *table)
{
	sg_free_table(table);
	kfree(table);
}

struct ion_dma_buf_attachment {
	struct device *dev;
	struct sg_table *table;
	struct list_head list;
};

int ion_dma_buf_attach(struct dma_buf *dmabuf,
		       struct dma_buf_attachment *attachment)
{
	struct ion_dma_buf_attachment *a;
	struct sg_table *table;
	struct ion_buffer *buffer = dmabuf->priv;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	table = dup_sg_table(buffer->sg_table);
	if (IS_ERR(table)) {
		kfree(a);
		return -ENOMEM;
	}

	a->table = table;
	a->dev = attachment->dev;
	INIT_LIST_HEAD(&a->list);

	attachment->priv = a;

	mutex_lock(&buffer->lock);
	list_add(&a->list, &buffer->attachments);
	mutex_unlock(&buffer->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(ion_dma_buf_attach);

void ion_dma_buf_detatch(struct dma_buf *dmabuf,
			 struct dma_buf_attachment *attachment)
{
	struct ion_dma_buf_attachment *a = attachment->priv;
	struct ion_buffer *buffer = dmabuf->priv;

	mutex_lock(&buffer->lock);
	list_del(&a->list);
	mutex_unlock(&buffer->lock);
	free_duped_table(a->table);

	kfree(a);
}
EXPORT_SYMBOL_GPL(ion_dma_buf_detatch);

struct sg_table *ion_map_dma_buf(struct dma_buf_attachment *attachment,
				 enum dma_data_direction direction)
{
	struct ion_dma_buf_attachment *a = attachment->priv;
	struct sg_table *table;

	table = a->table;

	if (!dma_map_sg(attachment->dev, table->sgl, table->nents,
			direction))
		return ERR_PTR(-ENOMEM);

	return table;
}
EXPORT_SYMBOL_GPL(ion_map_dma_buf);

void ion_unmap_dma_buf(struct dma_buf_attachment *attachment,
		       struct sg_table *table,
		       enum dma_data_direction direction)
{
	dma_unmap_sg(attachment->dev, table->sgl, table->nents, direction);
}
EXPORT_SYMBOL_GPL(ion_unmap_dma_buf);

int ion_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct ion_buffer *buffer = dmabuf->priv;
	int ret = 0;

	if (!buffer->heap->ops->map_user) {
		pr_err("%s: this heap does not define a method for mapping to userspace\n",
		       __func__);
		return -EINVAL;
	}

	if (!(buffer->flags & ION_FLAG_CACHED))
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	mutex_lock(&buffer->lock);
	/* now map it to userspace */
	ret = buffer->heap->ops->map_user(buffer->heap, buffer, vma);
	mutex_unlock(&buffer->lock);

	if (ret)
		pr_err("%s: failure mapping buffer to userspace\n",
		       __func__);

	return ret;
}
EXPORT_SYMBOL_GPL(ion_mmap);

void ion_dma_buf_release(struct dma_buf *dmabuf)
{
	struct ion_buffer *buffer = dmabuf->priv;

	_ion_buffer_destroy(buffer);
}
EXPORT_SYMBOL_GPL(ion_dma_buf_release);

void *ion_dma_buf_kmap(struct dma_buf *dmabuf, unsigned long offset)
{
	struct ion_buffer *buffer = dmabuf->priv;

	return buffer->vaddr + offset * PAGE_SIZE;
}

int ion_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
				 enum dma_data_direction direction)
{
	struct ion_buffer *buffer = dmabuf->priv;
	void *vaddr;
	struct ion_dma_buf_attachment *a;
	int ret = 0;

	/*
	 * TODO: Move this elsewhere because we don't always need a vaddr
	 */
	if (buffer->heap->ops->map_kernel) {
		mutex_lock(&buffer->lock);
		vaddr = ion_buffer_kmap_get(buffer);
		if (IS_ERR(vaddr)) {
			ret = PTR_ERR(vaddr);
			goto unlock;
		}
		mutex_unlock(&buffer->lock);
	}

	mutex_lock(&buffer->lock);
	list_for_each_entry(a, &buffer->attachments, list) {
		dma_sync_sg_for_cpu(a->dev, a->table->sgl, a->table->nents,
				    direction);
	}

unlock:
	mutex_unlock(&buffer->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(ion_dma_buf_begin_cpu_access);

int ion_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
			       enum dma_data_direction direction)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_dma_buf_attachment *a;

	if (buffer->heap->ops->map_kernel) {
		mutex_lock(&buffer->lock);
		ion_buffer_kmap_put(buffer);
		mutex_unlock(&buffer->lock);
	}

	mutex_lock(&buffer->lock);
	list_for_each_entry(a, &buffer->attachments, list) {
		dma_sync_sg_for_device(a->dev, a->table->sgl, a->table->nents,
				       direction);
	}
	mutex_unlock(&buffer->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(ion_dma_buf_end_cpu_access);

struct dma_buf *ion_dma_buf_export(struct ion_buffer *buffer,
				   const struct dma_buf_ops *ops)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dma_buf;

	if (!buffer || !ops)
		return ERR_PTR(-EINVAL);

	exp_info.ops = ops;
	exp_info.size = buffer->size;
	exp_info.flags = O_RDWR;
	exp_info.priv = buffer;

	dma_buf = dma_buf_export(&exp_info);
	if (IS_ERR(dma_buf))
		_ion_buffer_destroy(buffer);

	return dma_buf;
}
EXPORT_SYMBOL_GPL(ion_dma_buf_export);
