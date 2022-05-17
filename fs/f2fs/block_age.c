// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/block_age.c
 *
 * Copyright (c) 2022 xiaomi Co., Ltd.
 *             http://www.xiaomi.com/
 */
#include <linux/fs.h>
#include <linux/f2fs_fs.h>

#include "f2fs.h"
#include "segment.h"
#include <trace/events/f2fs.h>

static struct kmem_cache *age_extent_tree_slab;
static struct kmem_cache *age_extent_node_slab;


static inline void f2fs_inc_data_block_alloc(struct f2fs_sb_info *sbi)
{
	atomic64_inc(&sbi->total_data_alloc);
}

static void f2fs_init_block_age_info(struct f2fs_sb_info *sbi)
{
	atomic64_set(&sbi->total_data_alloc, 0);
}

static inline bool f2fs_may_age_extent_tree(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);

	/*
	 * for recovered files during mount do not create extents
	 * if shrinker is not registered.
	 */
	if (list_empty(&sbi->s_list))
		return false;

	if (!test_opt(sbi, AGE_EXTENT_CACHE))
		return false;

	/* don't cache block age info for cold file */
	if (is_inode_flag_set(inode, FI_COMPRESSED_FILE) ||
			file_is_cold(inode))
		return false;

	return S_ISREG(inode->i_mode)
			|| S_ISDIR(inode->i_mode);
}

static void f2fs_init_age_cache_info(struct f2fs_sb_info *sbi)
{
	INIT_RADIX_TREE(&sbi->age_extent_tree_root, GFP_NOIO);
	mutex_init(&sbi->age_extent_tree_lock);
	INIT_LIST_HEAD(&sbi->age_extent_list);
	spin_lock_init(&sbi->age_extent_lock);
	atomic_set(&sbi->total_age_ext_tree, 0);
	INIT_LIST_HEAD(&sbi->zombie_age_list);
	atomic_set(&sbi->total_zombie_age_tree, 0);
	atomic_set(&sbi->total_age_ext_node, 0);
}

#ifdef CONFIG_QUOTA
static void f2fs_init_quota_age_extent_cache(struct f2fs_sb_info *sbi)
{
	struct quota_info *dqopt = sb_dqopt(sbi->sb);
	struct inode *qinode;
	int cnt;

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (!sb_has_quota_active(sbi->sb, cnt))
			continue;

		qinode = dqopt->files[cnt];
		f2fs_init_age_extent_tree(qinode);
	}
}
#endif

static void __detach_age_extent_node(struct f2fs_sb_info *sbi,
				struct age_extent_tree *et, struct age_extent_node *en)
{
	rb_erase_cached(&en->rb_node, &et->root);
	atomic_dec(&et->node_cnt);
	atomic_dec(&sbi->total_age_ext_node);

	if (et->cached_en == en)
		et->cached_en = NULL;

	trace_f2fs_detach_age_extent_node(sbi, et, &en->ei);
	kmem_cache_free(age_extent_node_slab, en);
}

/*
 * Flow to release an age_extent_node:
 * 1. list_del_init
 * 2. __detach_age_extent_node
 * 3. kmem_cache_free.
 */
static void __release_age_extent_node(struct f2fs_sb_info *sbi,
			struct age_extent_tree *et, struct age_extent_node *en)
{
	spin_lock(&sbi->age_extent_lock);
	f2fs_bug_on(sbi, list_empty(&en->list));
	list_del_init(&en->list);
	spin_unlock(&sbi->age_extent_lock);

	__detach_age_extent_node(sbi, et, en);
}

static unsigned int __free_age_extent_tree(struct f2fs_sb_info *sbi,
					struct age_extent_tree *et)
{
	struct rb_node *node, *next;
	struct age_extent_node *en;
	unsigned int count = atomic_read(&et->node_cnt);

	node = rb_first_cached(&et->root);
	while (node) {
		next = rb_next(node);
		en = rb_entry(node, struct age_extent_node, rb_node);
		__release_age_extent_node(sbi, et, en);
		node = next;
	}

	return count - atomic_read(&et->node_cnt);
}

unsigned int f2fs_drop_age_extent_node(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct age_extent_tree *et = F2FS_I(inode)->age_extent_tree;
	unsigned int node_cnt = 0;

	if (!et || !atomic_read(&et->node_cnt))
		return 0;

	write_lock(&et->lock);
	node_cnt = __free_age_extent_tree(sbi, et);
	write_unlock(&et->lock);

	return node_cnt;
}

unsigned int f2fs_shrink_age_extent_tree(struct f2fs_sb_info *sbi, int nr_shrink)
{
	struct age_extent_tree *et, *next;
	struct age_extent_node *en;
	unsigned int node_cnt = 0, tree_cnt = 0;
	int remained;

	if (!atomic_read(&sbi->total_zombie_age_tree))
		goto free_node;

	if (!mutex_trylock(&sbi->age_extent_tree_lock))
		goto out;

	/* 1. remove unreferenced extent tree */
	list_for_each_entry_safe(et, next, &sbi->zombie_age_list, list) {
		if (atomic_read(&et->node_cnt)) {
			write_lock(&et->lock);
			node_cnt += __free_age_extent_tree(sbi, et);
			write_unlock(&et->lock);
		}
		f2fs_bug_on(sbi, atomic_read(&et->node_cnt));
		list_del_init(&et->list);
		radix_tree_delete(&sbi->age_extent_tree_root, et->ino);
		kmem_cache_free(age_extent_tree_slab, et);
		atomic_dec(&sbi->total_age_ext_tree);
		atomic_dec(&sbi->total_zombie_age_tree);
		tree_cnt++;

		if (node_cnt + tree_cnt >= nr_shrink)
			goto unlock_out;
		cond_resched();
	}
	mutex_unlock(&sbi->age_extent_tree_lock);

free_node:
	/* 2. remove LRU extent entries */
	if (!mutex_trylock(&sbi->age_extent_tree_lock))
		goto out;

	remained = nr_shrink - (node_cnt + tree_cnt);

	spin_lock(&sbi->age_extent_lock);
	for (; remained > 0; remained--) {
		if (list_empty(&sbi->age_extent_list))
			break;
		en = list_first_entry(&sbi->age_extent_list,
					struct age_extent_node, list);
		et = en->et;
		if (!write_trylock(&et->lock)) {
			/* refresh this extent node's position in extent list */
			list_move_tail(&en->list, &sbi->age_extent_list);
			continue;
		}

		list_del_init(&en->list);
		spin_unlock(&sbi->age_extent_lock);

		__detach_age_extent_node(sbi, et, en);

		write_unlock(&et->lock);
		node_cnt++;
		spin_lock(&sbi->age_extent_lock);
	}
	spin_unlock(&sbi->age_extent_lock);

unlock_out:
	mutex_unlock(&sbi->age_extent_tree_lock);
out:
	trace_f2fs_shrink_age_extent_tree(sbi, nr_shrink, node_cnt, tree_cnt);

	return node_cnt + tree_cnt;
}

static bool f2fs_lookup_age_extent_tree(struct inode *inode, pgoff_t pgofs,
							struct age_extent_info *ei)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct age_extent_tree *et = F2FS_I(inode)->age_extent_tree;
	struct age_extent_node *en;

	if (!et)
		return false;

	trace_f2fs_lookup_age_extent_tree_start(inode, pgofs);

	read_lock(&et->lock);

	en = (struct age_extent_node *)f2fs_lookup_rb_tree(&et->root,
				(struct rb_entry *)et->cached_en, pgofs);
	if (!en) {
		read_unlock(&et->lock);
		return false;
	}

	if (ei)
		*ei = en->ei;
	spin_lock(&sbi->age_extent_lock);
	if (!list_empty(&en->list)) {
		list_move_tail(&en->list, &sbi->age_extent_list);
		et->cached_en = en;
	}
	spin_unlock(&sbi->age_extent_lock);
	read_unlock(&et->lock);

	trace_f2fs_lookup_age_extent_tree_end(inode, pgofs, &en->ei);
	return true;
}

bool f2fs_lookup_age_extent_cache(struct inode *inode, pgoff_t pgofs,
					struct age_extent_info *ei)
{
	if (!f2fs_may_age_extent_tree(inode))
		return false;

	return f2fs_lookup_age_extent_tree(inode, pgofs, ei);
}

static inline bool __is_age_extent_mergeable(struct age_extent_info *back,
						struct age_extent_info *front)
{
	return (back->fofs + back->len == front->fofs &&
			back->age == front->age &&
			back->last_blocks == front->last_blocks);
}

static inline bool __is_back_age_ext_mergeable(struct age_extent_info *cur,
						struct age_extent_info *back)
{
	return __is_age_extent_mergeable(back, cur);
}

static inline bool __is_front_age_ext_mergeable(struct age_extent_info *cur,
						struct age_extent_info *front)
{
	return __is_age_extent_mergeable(cur, front);
}

static inline void set_age_extent_info(struct age_extent_info *ei, unsigned int fofs,
				unsigned int len, unsigned long long age,
				unsigned long long last_blocks)
{
	ei->fofs = fofs;
	ei->len = len;
	ei->age = age;
	ei->last_blocks = last_blocks;
}

static struct age_extent_node *__try_merge_age_extent_node(struct f2fs_sb_info *sbi,
				struct age_extent_tree *et, struct age_extent_info *ei,
				struct age_extent_node *prev_ex,
				struct age_extent_node *next_ex)
{
	struct age_extent_node *en = NULL;

	if (prev_ex && __is_back_age_ext_mergeable(ei, &prev_ex->ei)) {
		prev_ex->ei.len += ei->len;
		ei = &prev_ex->ei;
		en = prev_ex;
	}

	if (next_ex && __is_front_age_ext_mergeable(ei, &next_ex->ei)) {
		next_ex->ei.fofs = ei->fofs;
		next_ex->ei.len += ei->len;
		if (en)
			__release_age_extent_node(sbi, et, prev_ex);

		en = next_ex;
	}

	if (!en)
		return NULL;

	spin_lock(&sbi->age_extent_lock);
	if (!list_empty(&en->list)) {
		list_move_tail(&en->list, &sbi->age_extent_list);
		et->cached_en = en;
	}
	spin_unlock(&sbi->age_extent_lock);

	trace_f2fs_merged_age_extent_node(sbi, et, &en->ei);
	return en;
}

static struct age_extent_node *__attach_age_extent_node(struct f2fs_sb_info *sbi,
				struct age_extent_tree *et, struct age_extent_info *ei,
				struct rb_node *parent, struct rb_node **p,
				bool leftmost)
{
	struct age_extent_node *en;

	en = kmem_cache_alloc(age_extent_node_slab, GFP_ATOMIC);
	if (!en)
		return NULL;

	en->ei = *ei;
	INIT_LIST_HEAD(&en->list);
	en->et = et;

	rb_link_node(&en->rb_node, parent, p);
	rb_insert_color_cached(&en->rb_node, &et->root, leftmost);
	atomic_inc(&et->node_cnt);
	atomic_inc(&sbi->total_age_ext_node);

	trace_f2fs_attach_age_extent_node(sbi, en->et, &en->ei);
	return en;
}

static struct age_extent_node *__insert_age_extent_tree(struct f2fs_sb_info *sbi,
				struct age_extent_tree *et, struct age_extent_info *ei,
				struct rb_node **insert_p,
				struct rb_node *insert_parent,
				bool leftmost)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct age_extent_node *en = NULL;

	if (insert_p && insert_parent) {
		parent = insert_parent;
		p = insert_p;
		goto do_insert;
	}

	leftmost = true;

	p = f2fs_lookup_rb_tree_for_insert(sbi, &et->root, &parent,
						ei->fofs, &leftmost);
do_insert:
	en = __attach_age_extent_node(sbi, et, ei, parent, p, leftmost);
	if (!en)
		return NULL;

	/* update in global extent list */
	spin_lock(&sbi->age_extent_lock);
	list_add_tail(&en->list, &sbi->age_extent_list);
	et->cached_en = en;
	spin_unlock(&sbi->age_extent_lock);

	return en;
}

static void f2fs_update_age_extent_tree_range(struct inode *inode,
				pgoff_t fofs, unsigned int len, unsigned long long age,
				unsigned long long last_blks)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct age_extent_tree *et = F2FS_I(inode)->age_extent_tree;
	struct age_extent_node *en = NULL, *en1 = NULL;
	struct age_extent_node *prev_en = NULL, *next_en = NULL;
	struct age_extent_info ei, dei;
	struct rb_node **insert_p = NULL, *insert_parent = NULL;
	unsigned int end = fofs + len;
	unsigned int pos = (unsigned int)fofs;
	bool leftmost = false;

	if (!et)
		return;

	trace_f2fs_update_age_extent_tree_range(inode, fofs, len, age, last_blks);

	write_lock(&et->lock);

	/* 1. lookup first extent node in range [fofs, fofs + len - 1] */
	en = (struct age_extent_node *)f2fs_lookup_rb_tree_ret(&et->root,
					(struct rb_entry *)et->cached_en, fofs,
					(struct rb_entry **)&prev_en,
					(struct rb_entry **)&next_en,
					&insert_p, &insert_parent, false,
					&leftmost);
	if (!en)
		en = next_en;

	/* 2. invlidate all age extent nodes in range [fofs, fofs + len - 1] */
	while (en && en->ei.fofs < end) {
		unsigned int org_end;
		int parts = 0;	/* # of parts current age extent split into */

		next_en = en1 = NULL;

		dei = en->ei;
		org_end = dei.fofs + dei.len;
		f2fs_bug_on(sbi, pos >= org_end);

		if (pos > dei.fofs) {
			en->ei.len = pos - en->ei.fofs;
			prev_en = en;
			parts = 1;
		}

		if (end < org_end) {
			if (parts) {
				set_age_extent_info(&ei, end,
						org_end - end, dei.age, dei.last_blocks);
				en1 = __insert_age_extent_tree(sbi, et, &ei,
							NULL, NULL, true);
				next_en = en1;
			} else {
				en->ei.fofs = end;
				en->ei.len -= end - dei.fofs;
				en->ei.age = dei.age;
				en->ei.last_blocks = dei.last_blocks;
				next_en = en;
			}
			parts++;
		}

		if (!next_en) {
			struct rb_node *node = rb_next(&en->rb_node);

			next_en = rb_entry_safe(node, struct age_extent_node,
						rb_node);
		}

		if (!parts)
			__release_age_extent_node(sbi, et, en);

		/*
		 * if original extent is split into zero or two parts, extent
		 * tree has been altered by deletion or insertion, therefore
		 * invalidate pointers regard to tree.
		 */
		if (parts != 1) {
			insert_p = NULL;
			insert_parent = NULL;
		}
		en = next_en;
	}

	/* 3. update extent in extent cache */
	if (last_blks) {
		set_age_extent_info(&ei, fofs, len, age, last_blks);
		if (!__try_merge_age_extent_node(sbi, et, &ei, prev_en, next_en))
			__insert_age_extent_tree(sbi, et, &ei,
					insert_p, insert_parent, leftmost);
	}

	write_unlock(&et->lock);
}

void f2fs_update_age_extent_cache(struct inode *inode, pgoff_t fofs,
					unsigned int len, unsigned long long age,
					unsigned long long cur_blk_alloced)
{
	if (!f2fs_may_age_extent_tree(inode))
		return;

	f2fs_update_age_extent_tree_range(inode, fofs, len, age, cur_blk_alloced);
}

void f2fs_truncate_age_extent_cache(struct inode *inode, pgoff_t fofs, unsigned int len)
{
	f2fs_update_age_extent_cache(inode, fofs, len, 0, 0);
}

void f2fs_destroy_age_extent_tree(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct age_extent_tree *et = F2FS_I(inode)->age_extent_tree;
	unsigned int node_cnt = 0;

	if (!et)
		return;

	if (inode->i_nlink && !is_bad_inode(inode) &&
					atomic_read(&et->node_cnt)) {
		mutex_lock(&sbi->age_extent_tree_lock);
		list_add_tail(&et->list, &sbi->zombie_age_list);
		atomic_inc(&sbi->total_zombie_age_tree);
		mutex_unlock(&sbi->age_extent_tree_lock);
		return;
	}

	/* free all extent info belong to this extent tree */
	node_cnt = f2fs_drop_age_extent_node(inode);

	/* delete extent tree entry in radix tree */
	mutex_lock(&sbi->age_extent_tree_lock);
	f2fs_bug_on(sbi, atomic_read(&et->node_cnt));
	radix_tree_delete(&sbi->age_extent_tree_root, inode->i_ino);
	kmem_cache_free(age_extent_tree_slab, et);
	atomic_dec(&sbi->total_age_ext_tree);
	mutex_unlock(&sbi->age_extent_tree_lock);

	F2FS_I(inode)->age_extent_tree = NULL;

	trace_f2fs_destroy_age_extent_tree(inode, node_cnt);
}

void f2fs_init_data_seperation_info(struct f2fs_sb_info *sbi)
{
	f2fs_init_block_age_info(sbi);
	f2fs_init_age_cache_info(sbi);

	f2fs_init_age_extent_tree(sbi->sb->s_root->d_inode);
#ifdef CONFIG_QUOTA
	f2fs_init_quota_age_extent_cache(sbi);
#endif
}

void f2fs_inc_block_alloc_count(struct f2fs_sb_info *sbi, int type)
{
	if (IS_DATASEG(type))
		f2fs_inc_data_block_alloc(sbi);
}

int __init f2fs_create_age_extent_cache(void)
{
	age_extent_tree_slab = f2fs_kmem_cache_create("f2fs_age_extent_tree",
			sizeof(struct age_extent_tree));
	if (!age_extent_tree_slab)
		return -ENOMEM;
	age_extent_node_slab = f2fs_kmem_cache_create("f2fs_age_extent_node",
			sizeof(struct age_extent_node));
	if (!age_extent_node_slab) {
		kmem_cache_destroy(age_extent_tree_slab);
		return -ENOMEM;
	}
	return 0;
}

bool f2fs_init_age_extent_tree(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct age_extent_tree *et;
	nid_t ino = inode->i_ino;

	if (!f2fs_may_age_extent_tree(inode))
		return false;

	mutex_lock(&sbi->age_extent_tree_lock);
	et = radix_tree_lookup(&sbi->age_extent_tree_root, ino);
	if (!et) {
		et = f2fs_kmem_cache_alloc(age_extent_tree_slab, GFP_NOFS);
		f2fs_radix_tree_insert(&sbi->age_extent_tree_root, ino, et);
		memset(et, 0, sizeof(struct age_extent_tree));
		et->ino = ino;
		et->root = RB_ROOT_CACHED;
		et->cached_en = NULL;
		rwlock_init(&et->lock);
		INIT_LIST_HEAD(&et->list);
		atomic_set(&et->node_cnt, 0);
		atomic_inc(&sbi->total_age_ext_tree);
	} else {
		atomic_dec(&sbi->total_zombie_age_tree);
		list_del_init(&et->list);
	}
	mutex_unlock(&sbi->age_extent_tree_lock);

	/* never died until evict_inode */
	F2FS_I(inode)->age_extent_tree = et;

	trace_f2fs_init_age_extent_tree(inode, atomic_read(&et->node_cnt));
	return true;
}

unsigned long long f2fs_total_age_cache_size(struct f2fs_sb_info *sbi)
{
	return atomic_read(&sbi->total_age_ext_tree) *
				sizeof(struct age_extent_tree) +
				atomic_read(&sbi->total_age_ext_node) *
				sizeof(struct age_extent_node);
}

unsigned long f2fs_count_age_extent_cache(struct f2fs_sb_info *sbi)
{
	return atomic_read(&sbi->total_zombie_age_tree) +
				atomic_read(&sbi->total_age_ext_node);
}

void f2fs_destroy_age_extent_cache(void)
{
	kmem_cache_destroy(age_extent_node_slab);
	kmem_cache_destroy(age_extent_tree_slab);
}
