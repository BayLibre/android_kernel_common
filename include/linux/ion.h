/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 */

#ifndef _ION_KERNEL_H
#define _ION_KERNEL_H

#include <linux/dma-buf.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/kref.h>
#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/shrinker.h>
#include <linux/types.h>
#include <linux/miscdevice.h>

/**
 * enum ion_heap_types - list of all possible types of heaps
 * @ION_HEAP_TYPE_SYSTEM:	 memory allocated via vmalloc
 * @ION_HEAP_TYPE_SYSTEM_CONTIG: memory allocated via kmalloc
 * @ION_HEAP_TYPE_CARVEOUT:	 memory allocated from a prereserved
 *				 carveout heap, allocations are physically
 *				 contiguous
 * @ION_HEAP_TYPE_DMA:		 memory allocated via DMA API
 * @ION_NUM_HEAPS:		 helper for iterating over heaps, a bit mask
 *				 is used to identify the heaps, so only 32
 *				 total heap types are supported
 */
enum ion_heap_type {
	ION_HEAP_TYPE_SYSTEM,
	ION_HEAP_TYPE_SYSTEM_CONTIG,
	ION_HEAP_TYPE_CARVEOUT,
	ION_HEAP_TYPE_CHUNK,
	ION_HEAP_TYPE_DMA,
	ION_HEAP_TYPE_CUSTOM, /*
			       * must be last so device specific heaps always
			       * are at the end of this enum
			       */
};

#define ION_NUM_HEAP_IDS		(sizeof(unsigned int) * 8)

#ifdef __KERNEL__

/**
 * struct ion_buffer - metadata for a particular buffer
 * @node:		node in the ion_device buffers tree
 * @list:		element in list of deferred freeable buffers
 * @dev:		back pointer to the ion_device
 * @heap:		back pointer to the heap the buffer came from
 * @flags:		buffer specific flags
 * @private_flags:	internal buffer specific flags
 * @size:		size of the buffer
 * @priv_virt:		private data to the buffer representable as
 *			a void *
 * @lock:		protects the buffers cnt fields
 * @kmap_cnt:		number of times the buffer is mapped to the kernel
 * @vaddr:		the kernel mapping if kmap_cnt is not zero
 * @sg_table:		the sg table for the buffer
 * @attachments:	list of devices attached to this buffer
 */
struct ion_buffer {
	union {
		struct rb_node node;
		struct list_head list;
	};
	struct ion_device *dev;
	struct ion_heap *heap;
	unsigned long flags;
	unsigned long private_flags;
	size_t size;
	void *priv_virt;
	struct mutex lock;
	int kmap_cnt;
	void *vaddr;
	struct sg_table *sg_table;
	struct list_head attachments;
};

/**
 * struct ion_device - the metadata of the ion device node
 * @dev:		the actual misc device
 * @buffers:		an rb tree of all the existing buffers
 * @buffer_lock:	lock protecting the tree of buffers
 * @lock:		rwsem protecting the tree of heaps and clients
 */
struct ion_device {
	struct miscdevice dev;
	struct rb_root buffers;
	struct mutex buffer_lock;
	struct rw_semaphore lock;
	struct plist_head heaps;
	struct dentry *debug_root;
	int heap_cnt;
};

/**
 * struct ion_heap_ops - ops to operate on a given heap
 * @allocate:		allocate memory
 * @free:		free memory
 * @map_kernel		map memory to the kernel
 * @unmap_kernel	unmap memory to the kernel
 * @map_user		map memory to userspace
 *
 * allocate, phys, and map_user return 0 on success, -errno on error.
 * map_dma and map_kernel return pointer on success, ERR_PTR on
 * error. @free will be called with ION_PRIV_FLAG_SHRINKER_FREE set in
 * the buffer's private_flags when called from a shrinker. In that
 * case, the pages being free'd must be truly free'd back to the
 * system, not put in a page pool or otherwise cached.
 */
struct ion_heap_ops {
	int (*allocate)(struct ion_heap *heap,
			struct ion_buffer *buffer, unsigned long len,
			unsigned long flags);
	void (*free)(struct ion_buffer *buffer);
	void * (*map_kernel)(struct ion_heap *heap, struct ion_buffer *buffer);
	void (*unmap_kernel)(struct ion_heap *heap, struct ion_buffer *buffer);
	int (*map_user)(struct ion_heap *mapper, struct ion_buffer *buffer,
			struct vm_area_struct *vma);
	int (*shrink)(struct ion_heap *heap, gfp_t gfp_mask, int nr_to_scan);
};

/**
 * heap flags - flags between the heaps and core ion code
 */
#define ION_HEAP_FLAG_DEFER_FREE BIT(0)

/**
 * private flags - flags internal to ion
 */
/*
 * Buffer is being freed from a shrinker function. Skip any possible
 * heap-specific caching mechanism (e.g. page pools). Guarantees that
 * any buffer storage that came from the system allocator will be
 * returned to the system allocator.
 */
#define ION_PRIV_FLAG_SHRINKER_FREE BIT(0)

/**
 * struct ion_heap - represents a heap in the system
 * @node:		rb node to put the heap on the device's tree of heaps
 * @dev:		back pointer to the ion_device
 * @type:		type of heap
 * @ops:		ops struct as above
 * @flags:		flags
 * @id:			id of heap, also indicates priority of this heap when
 *			allocating.  These are specified by platform data and
 *			MUST be unique
 * @name:		used for debugging
 * @shrinker:		a shrinker for the heap
 * @free_list:		free list head if deferred free is used
 * @free_list_size	size of the deferred free list in bytes
 * @lock:		protects the free list
 * @waitqueue:		queue to wait on from deferred free thread
 * @task:		task struct of deferred free thread
 * @num_of_buffers	the number of currently allocated buffers
 * @num_of_alloc_bytes	the number of allocated bytes
 * @alloc_bytes_wm	the number of allocated bytes watermark
 *
 * Represents a pool of memory from which buffers can be made.  In some
 * systems the only heap is regular system memory allocated via vmalloc.
 * On others, some blocks might require large physically contiguous buffers
 * that are allocated from a specially reserved heap.
 */
struct ion_heap {
	struct plist_node node;
	struct ion_device *dev;
	enum ion_heap_type type;
	struct ion_heap_ops *ops;
	unsigned long flags;
	unsigned int id;
	const char *name;

	/* deferred free support */
	struct shrinker shrinker;
	struct list_head free_list;
	size_t free_list_size;
	spinlock_t free_lock;
	wait_queue_head_t waitqueue;
	struct task_struct *task;

	/* heap statistics */
	u64 num_of_buffers;
	u64 num_of_alloc_bytes;
	u64 alloc_bytes_wm;

	/* protect heap statistics */
	spinlock_t stat_lock;
};


#ifdef CONFIG_ION
/**
 * ion_device_add_heap - adds a heap to the ion device
 * @heap:		the heap to add
 */
void ion_device_add_heap(struct ion_heap *heap);

/**
 * ion_heap_init_shrinker
 * @heap:		the heap
 *
 * If a heap sets the ION_HEAP_FLAG_DEFER_FREE flag or defines the shrink op
 * this function will be called to setup a shrinker to shrink the freelists
 * and call the heap's shrink op.
 */
int ion_heap_init_shrinker(struct ion_heap *heap);

/**
 * ion_heap_init_deferred_free -- initialize deferred free functionality
 * @heap:		the heap
 *
 * If a heap sets the ION_HEAP_FLAG_DEFER_FREE flag this function will
 * be called to setup deferred frees. Calls to free the buffer will
 * return immediately and the actual free will occur some time later
 */
int ion_heap_init_deferred_free(struct ion_heap *heap);

/**
 * ion_heap_freelist_add - add a buffer to the deferred free list
 * @heap:		the heap
 * @buffer:		the buffer
 *
 * Adds an item to the deferred freelist.
 */
void ion_heap_freelist_add(struct ion_heap *heap, struct ion_buffer *buffer);

/**
 * ion_heap_freelist_drain - drain the deferred free list
 * @heap:		the heap
 * @size:		amount of memory to drain in bytes
 *
 * Drains the indicated amount of memory from the deferred freelist immediately.
 * Returns the total amount freed.  The total freed may be higher depending
 * on the size of the items in the list, or lower if there is insufficient
 * total memory on the freelist.
 */
size_t ion_heap_freelist_drain(struct ion_heap *heap, size_t size);

/**
 * ion_heap_freelist_shrink - drain the deferred free
 *				list, skipping any heap-specific
 *				pooling or caching mechanisms
 *
 * @heap:		the heap
 * @size:		amount of memory to drain in bytes
 *
 * Drains the indicated amount of memory from the deferred freelist immediately.
 * Returns the total amount freed.  The total freed may be higher depending
 * on the size of the items in the list, or lower if there is insufficient
 * total memory on the freelist.
 *
 * Unlike with @ion_heap_freelist_drain, don't put any pages back into
 * page pools or otherwise cache the pages. Everything must be
 * genuinely free'd back to the system. If you're free'ing from a
 * shrinker you probably want to use this. Note that this relies on
 * the heap.ops.free callback honoring the ION_PRIV_FLAG_SHRINKER_FREE
 * flag.
 */
size_t ion_heap_freelist_shrink(struct ion_heap *heap,
				size_t size);

/**
 * ion_heap_freelist_size - returns the size of the freelist in bytes
 * @heap:		the heap
 */
size_t ion_heap_freelist_size(struct ion_heap *heap);

/**
 * ion_heap_map_kernel - map the ion_buffer in kernel virtual address space.
 *
 * @heap:		the heap
 * @buffer:		buffer to be mapped
 *
 * Maps the buffer using vmap(). The function respects cache flags for the
 * buffer and creates the page table entries accordingly. Returns virtual
 * address at the beginning of the buffer or ERR_PTR.
 */
void *ion_heap_map_kernel(struct ion_heap *heap, struct ion_buffer *buffer);

/**
 * ion_heap_unmap_kernel - unmap ion_buffer
 *
 * @buffer:		buffer to be unmapped
 *
 * ION wrapper for vunmap() of the ion buffer.
 */
void ion_heap_unmap_kernel(struct ion_heap *heap, struct ion_buffer *buffer);

/**
 * ion_heap_map_user - map given ion buffer in provided vma
 *
 * @heap:		the heap this buffer belongs to
 * @buffer:		Ion buffer to be mapped
 * @vma:		vma of the process where buffer should be mapped.
 *
 * Maps the buffer using remap_pfn_range() into specific process's vma starting
 * with vma->vm_start. The vma size is expected to be >= ion buffer size.
 * If not, a partial buffer mapping may be created. Returns 0 on success.
 */
int ion_heap_map_user(struct ion_heap *heap, struct ion_buffer *buffer,
		      struct vm_area_struct *vma);

/**
 * ion_heap_buffer_zero - zeroes out the ion buffer
 *
 * @buffer:		Ion buffer to be zeroed out
 *
 * Temporarily maps the buffer and zeroes it out. Returns 0 on success.
 */
int ion_heap_buffer_zero(struct ion_buffer *buffer);

/**
 * ion_dma_buf_export - Exports given ion buffer as a dma buf object
 *
 * @buffer:		Ion buffer to be exported
 * @ops:		dma_buf_ops to be assigned to the exported buffer.
 *
 * Returns pointer to the newly created struct dma_buf object, ERR_PTR in
 * case of failure.
 */
struct dma_buf *ion_dma_buf_export(struct ion_buffer *buffer,
				   const struct dma_buf_ops *ops);

/**
 * ion_alloc - Allocates an ion buffer of given size from given heap
 *
 * @len:		size of the buffer to be allocated.
 * @heap_id_mask:	a bitwise maks of heap ids to allocate from
 * @flags:		ION_BUFFER_XXXX flags for the new buffer.
 *
 * The function exports a dma_buf object for the new ion buffer internally
 * and returns that to the caller. So, the buffer is ready to be used by other
 * drivers immediately. Returns ERR_PTR in case of failure.
 */
struct dma_buf *ion_alloc(size_t len, unsigned int heap_id_mask,
			  unsigned int flags);

/**
 * ion_buffer_destroy - free the given ion_buffer
 *
 * @buffer:		Ion buffer to be destroyed.
 *
 * The function will make sure the buffer is unapped from the kernel before
 * freeing it.
 */
void ion_buffer_destroy(struct ion_buffer *buffer);

/**
 * ion_map_dma_buf - maps given buffer in attached device's address space
 *
 * @attachment:		dma_buf_attachment
 * @direction:		dma data direction
 *
 * Returns the sg_table pointer that has the scatterlist after successful
 * dma mapping of the buffer. ERR_PTR on failure.
 */
struct sg_table *ion_map_dma_buf(struct dma_buf_attachment *attachment,
				 enum dma_data_direction direction);

/**
 * ion_unmap_dma_buf - unmaps buffer from attached device's dma mapping.
 *
 * @attachment:		dma buf attachment
 * @table:		the scatterlist representing dma mapping
 * @direction:		dma data direction
 *
 * The function is a wrapper to dma_unmap_sg().
 */
void ion_unmap_dma_buf(struct dma_buf_attachment *attachment,
		       struct sg_table *table,
		       enum dma_data_direction direction);

/*
 * ion_mmap - maps ion allocated dma-buf object in given vma
 *
 * @dmabuf:		the dma buf object for ion_buffer
 * @vma:		vma where the buffer should be mapped.
 *
 * The function simply unwraps the dma buf object and finds out
 * the heap associated with the underlying ion_buffer. Once found.
 * its a call to heap's map_user() function which has the same symantics
 * of ion_heap_map_user() described above. Returns 0 on success.
 */
int ion_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma);

/**
 * ion_dma_buf_release - free or return the ion allocated dmabuf to pool
 *
 * @dmabuf:		dmabuf object associated with the ion_buffer.
 *
 * The function unwraps the underlying ion_buffer and either destroys it
 * or returns to the freelist depending on the ION_HEAP_FLAG_DEFER_FREE heap
 * flag.
 */
void ion_dma_buf_release(struct dma_buf *dmabuf);

/**
 * ion_dma_buf_attach - attach dmabuf (ion_buffer) to a device
 *
 * @dmabuf:		dmabuf object associated with ion_buffer
 * @attachment:		dmabuf attachment.
 *
 * Returns 0 on success.
 */
int ion_dma_buf_attach(struct dma_buf *dmabuf,
		       struct dma_buf_attachment *attachment);
/**
 * ion_dma_buf_detach - detach dmabuf (ion_buffer) from given device
 *
 * @dmabuf:		dmabuf object associated with ion_buffer
 * @attachment:		dmabuf attachment.
 *
 */
void ion_dma_buf_detatch(struct dma_buf *dmabuf,
			 struct dma_buf_attachment *attachment);

/**
 * ion_dma_buf_kmap - Return kernel address for specific page within ion_buffer
 *
 * @dmabuf:		dmabuf object associated with ion_buffer
 * @offset:		offset into the dmabuf buffer
 *
 * Return kernel address for the offset within ion_buffer
 */
void *ion_dma_buf_kmap(struct dma_buf *dmabuf, unsigned long offset);

/**
 * ion_dma_buf_begin_cpu_access- Make ion_buffer accessible to cpu
 *
 * @dmabuf:		dmabuf object associated with ion_buffer
 * @direction:		dma data direction
 *
 * Returns 0 on success. The function makes sure the underlying ion_buffer
 * memory is available and sync'ed before CPU is allowed to start reading
 * or writing from it.
 */
int ion_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
				 enum dma_data_direction direction);
/**
 * ion_dma_buf_end_cpu_access - Release resources allocated at begin_cpu_access
 *			       for given ion_buffer
 *
 * @dmabuf:		dmabuf object associated with ion_buffer
 * @direction:		dma data direction
 *
 * Returns 0 on success. The function makes sure the underlying ion_buffer
 * memory cache is flushed depending on the data direction. Uses the generic
 * dma_sync_xxx APIs to achieve that.
 */
int ion_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
			       enum dma_data_direction direction);
#else

static inline void ion_device_add_heap(struct ion_heap *heap) {}

static inline int ion_heap_init_shrinker(struct ion_heap *heap)
{
	return 0;
}

static inline int ion_heap_init_deferred_free(struct ion_heap *heap)
{
	return 0;
}

static inline void ion_heap_freelist_add(struct ion_heap *heap,
					 struct ion_buffer *buffer) {}

static inline size_t ion_heap_freelist_drain(struct ion_heap *heap, size_t size)
{
	return 0;
}

static inline size_t ion_heap_freelist_shrink(struct ion_heap *heap,
					      size_t size)
{
	return 0;
}

static inline size_t ion_heap_freelist_size(struct ion_heap *heap)
{
	return 0;
}

static inline void *ion_heap_map_kernel(struct ion_heap *heap,
					struct ion_buffer *buffer)
{
	return ERR_PTR(-ENOMEM);
}

static inline void ion_heap_unmap_kernel(struct ion_heap *heap,
					 struct ion_buffer *buffer) {}

static inline int ion_heap_map_user(struct ion_heap *heap,
				    struct ion_buffer *buffer,
				    struct vm_area_struct *vma)
{
	return -EINVAL;
}

static inline int ion_heap_buffer_zero(struct ion_buffer *buffer)
{
	return -EINVAL;
}

static inline struct dma_buf *ion_dma_buf_export(struct ion_buffer *buffer,
						 const struct dma_buf_ops *ops)
{
	return ERR_PTR(-ENOMEM);
}

static inline struct dma_buf *ion_alloc(size_t len, unsigned int heap_id_mask,
					unsigned int flags)
{
	return ERR_PTR(-ENOMEM);
}

static inline void ion_buffer_destroy(struct ion_buffer *buffer) {}

static inline
struct sg_table *ion_map_dma_buf(struct dma_buf_attachment *attachment,
				 enum dma_data_direction direction)
{
	return ERR_PTR(-ENOMEM);
}

static inline void ion_unmap_dma_buf(struct dma_buf_attachment *attachment,
				     struct sg_table *table,
				     enum dma_data_direction direction) {}

static inline int ion_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	return -EINVAL;
}

static inline void ion_dma_buf_release(struct dma_buf *dmabuf) {}

static inline int ion_dma_buf_attach(struct dma_buf *dmabuf,
				     struct dma_buf_attachment *attachment)
{
	return -EINVAL;
}

static inline void ion_dma_buf_detatch(struct dma_buf *dmabuf,
				       struct dma_buf_attachment *attachment) {}

static inline void *ion_dma_buf_kmap(struct dma_buf *dmabuf,
				     unsigned long offset)
{
	return ERR_PTR(-ENOMEM);
}

static inline int ion_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
					       enum dma_data_direction direction)
{
	return -EINVAL;
}

static inline int ion_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
					     enum dma_data_direction direction);
{
	return -EINVAL;
}

#endif /* CONFIG_ION */
#endif /* __KERNEL__ */
#endif /* _ION_KERNEL_H */
