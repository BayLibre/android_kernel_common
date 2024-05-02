/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_DBITMAP_H
#define _LINUX_DBITMAP_H
#include <linux/bitmap.h>

#define NBITS_MIN	BITS_PER_TYPE(unsigned long)

struct dbitmap {
	unsigned int nbits;
	unsigned long *map;
};

static inline int dbitmap_enabled(struct dbitmap *dmap)
{
	return dmap->map != NULL;
}

static inline void dbitmap_free(struct dbitmap *dmap)
{
	dmap->nbits = 0;
	kfree(dmap->map);
	dmap->map = NULL;
}

static inline unsigned int dbitmap_shrink_nbits(struct dbitmap *dmap)
{
	unsigned int bit;

	if (dmap->nbits <= NBITS_MIN)
		return 0;

	bit = find_last_bit(dmap->map, dmap->nbits);
	if (unlikely(bit == dmap->nbits))
		return NBITS_MIN;

	if (unlikely(bit <= (dmap->nbits >> 2)))
		return dmap->nbits >> 1;

	return 0;
}

static inline void
dbitmap_replace(struct dbitmap *dmap, unsigned long *new, unsigned int nbits)
{
	bitmap_copy(new, dmap->map, min(dmap->nbits, nbits));
	kfree(dmap->map);
	dmap->map = new;
	dmap->nbits = nbits;
}

static inline void
dbitmap_shrink(struct dbitmap *dmap, unsigned long *new, unsigned int nbits)
{
	/*
	 * This raced with another call, either another expand or a
	 * dbitmap_free(). Either way, the @new bitmap is no longer
	 * needed, so release it and move on.
	 */
	if (unlikely(!new || !dmap->map)) {
		kfree(new);
		return;
	}

	if (unlikely(dbitmap_shrink_nbits(dmap) != nbits)) {
		kfree(new);
		return;
	}

	dbitmap_replace(dmap, new, nbits);
}

static inline unsigned int
dbitmap_expand_nbits(struct dbitmap *dmap)
{
	return dmap->nbits << 1;
}

static inline void
dbitmap_expand(struct dbitmap *dmap, unsigned long *new, unsigned int nbits)
{
	/*
	 * This raced with another call, either another expand or a
	 * dbitmap_free(). Either way, the @new bitmap is no longer
	 * needed, so release it and move on.
	 */
	if (unlikely(!dmap->map || nbits <= dmap->nbits)) {
		kfree(new);
		return;
	}

	/*
	 * ENOMEM is finally checked here as now we can discard a race
	 * with another successful expand. In such case, disable the
	 * dbitmap and fallback to slow_desc_lookup_olocked().
	 */
	if (unlikely(!new)) {
		dbitmap_free(dmap);
		return;
	}

	dbitmap_replace(dmap, new, nbits);
}

static inline int
dbitmap_get_first_zero_bit(struct dbitmap *dmap, unsigned long *bit)
{
	unsigned long n;

	n = find_first_zero_bit(dmap->map, dmap->nbits);
	if (unlikely(n == dmap->nbits))
		return -ENOSPC;

	*bit = n;
	set_bit(n, dmap->map);

	return 0;
}

static inline void
dbitmap_clear_bit(struct dbitmap *dmap, unsigned long bit)
{
	clear_bit(bit, dmap->map);
}

static inline int dbitmap_init(struct dbitmap *dmap)
{
	dmap->map = bitmap_zalloc(NBITS_MIN, GFP_KERNEL);
	if (!dmap->map) {
		dmap->nbits = 0;
		return -ENOMEM;
	}

	dmap->nbits = NBITS_MIN;
	/* 0 is reserved for the context manager */
	set_bit(0, dmap->map);

	return 0;
}
#endif
