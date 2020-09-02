// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 Google LLC
 */
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/falloc.h>
#include <linux/slab.h>
#include <linux/crc32.h>
#include <linux/kernel.h>

#include "format.h"
#include "data_mgmt.h"

loff_t incfs_get_end_offset(struct file *f)
{
	/*
	 * This function assumes that file size and the end-offset
	 * are the same. This is not always true.
	 */
	return i_size_read(file_inode(f));
}

static int write_to_bf(struct file *backing_file, const void *buf,
			size_t count, loff_t pos)
{
	ssize_t res = incfs_kwrite(backing_file, buf, count, pos);

	if (res < 0)
		return res;
	if (res != count)
		return -EIO;
	return 0;
}

static int append_zeros_no_fallocate(struct file *backing_file,
				     size_t file_size, size_t len)
{
	u8 buffer[256] = {};
	size_t i;

	for (i = 0; i < len; i += sizeof(buffer)) {
		int to_write = len - i > sizeof(buffer)
			? sizeof(buffer) : len - i;
		int err = write_to_bf(backing_file, buffer, to_write,
				      file_size + i);

		if (err)
			return err;
	}

	return 0;
}

/* Append a given number of zero bytes to the end of the backing file. */
static int append_zeros(struct file *backing_file, size_t len)
{
	loff_t file_size = 0;
	loff_t new_last_byte_offset = 0;
	int result;

	if (len == 0)
		return 0;

	/*
	 * Allocate only one byte at the new desired end of the file.
	 * It will increase file size and create a zeroed area of
	 * a given size.
	 */
	file_size = incfs_get_end_offset(backing_file);
	new_last_byte_offset = file_size + len - 1;
	result = vfs_fallocate(backing_file, 0, new_last_byte_offset, 1);
	if (result != -EOPNOTSUPP)
		return result;

	return append_zeros_no_fallocate(backing_file, file_size, len);
}

int incfs_write_file_header_flags(struct file *backing_file, u32 flags)
{
	return write_to_bf(backing_file, &flags, sizeof(flags),
			   offsetof(struct incfs_file_header, fh_flags));
}

/*
 * Reserve 0-filled space for the blockmap body, and append
 * incfs_blockmap metadata record pointing to it.
 */
int incfs_write_blockmap_to_backing_file(struct file *backing_file,
					 u32 block_count,
					 struct incfs_file_header *fh)
{
	int result = 0;
	loff_t file_end = 0;
	size_t map_size = block_count * sizeof(struct incfs_blockmap_entry);

	/* Reserve 0-filled space for the blockmap body in the backing file. */
	file_end = incfs_get_end_offset(backing_file);
	result = append_zeros(backing_file, map_size);
	if (result)
		return result;

	fh->fh_blockmap.fs_size = cpu_to_le64(map_size);
	fh->fh_blockmap.fs_offset = cpu_to_le64(file_end);

	return 0;
}

int incfs_write_signature_to_backing_file(struct file *backing_file,
					  struct mem_range sig, u32 tree_size,
					  struct incfs_file_header *fh)
{
	int result = 0;

	if (sig.data != NULL && sig.len > 0) {
		loff_t pos = incfs_get_end_offset(backing_file);

		fh->fh_signature.fs_size = cpu_to_le64(sig.len);
		fh->fh_signature.fs_offset = cpu_to_le64(pos);

		result = write_to_bf(backing_file, sig.data, sig.len, pos);
		if (result)
			return result;
	}

	if (tree_size > 0) {
		loff_t tree_area_pos = incfs_get_end_offset(backing_file);
		size_t alignment = 0;

		if (tree_size > 5 * INCFS_DATA_FILE_BLOCK_SIZE) {
			/*
			 * If hash tree is big enough, it makes sense to
			 * align in the backing file for faster access.
			 */
			loff_t offset = round_up(tree_area_pos, PAGE_SIZE);

			alignment = offset - tree_area_pos;
			tree_area_pos = offset;
		}

		/*
		 * If root hash is not the only hash in the tree.
		 * reserve 0-filled space for the tree.
		 */
		result = append_zeros(backing_file, tree_size + alignment);
		if (result)
			return result;

		fh->fh_hash_tree.fs_size = cpu_to_le64(tree_size);
		fh->fh_hash_tree.fs_offset = cpu_to_le64(tree_area_pos);
	}

	return 0;
}

static struct incfs_file_header *create_file(struct file *backing_file,
				incfs_uuid_t *uuid, u64 file_size, u64 offset,
				u32 flags)
{
	struct incfs_file_header *fh = kzalloc(sizeof(*fh), GFP_NOFS);
	int error = 0;

	BUILD_BUG_ON(sizeof(struct incfs_file_header) != 256);
	BUILD_BUG_ON(offsetof(struct incfs_file_header, fh_blockmap) != 128);

	if (!fh)
		return ERR_PTR(-ENOMEM);

	fh->fh_magic = cpu_to_le32(INCFS_MAGIC_NUMBER);
	fh->fh_version = cpu_to_le32(INCFS_FORMAT_CURRENT_VER);
	fh->fh_header_size = cpu_to_le16(sizeof(*fh));
	fh->fh_data_block_size = cpu_to_le16(INCFS_DATA_FILE_BLOCK_SIZE);
	fh->fh_file_size = cpu_to_le64(file_size);
	fh->fh_uuid = *uuid;
	fh->fh_offset = cpu_to_le64(offset);
	fh->fh_flags = cpu_to_le32(flags);

	if (incfs_get_end_offset(backing_file) != 0) {
		error = -EEXIST;
		goto err;
	}

	error = incfs_update_file_header(backing_file, fh);
	if (error)
		goto err;

	return fh;

err:
	kfree(fh);
	return ERR_PTR(error);
}

/*
 * Write a backing file header
 * It should always be called only on empty file.
 * fh.fh_first_md_offset is 0 for now, but will be updated
 * once first metadata record is added.
 */
struct incfs_file_header *incfs_create_backing_file(struct file *backing_file,
				incfs_uuid_t *uuid, u64 file_size)
{
	return create_file(backing_file, uuid, file_size, 0, 0);
}

/*
 * Write a backing file header for a mapping file
 * It should always be called only on empty file.
 */
struct incfs_file_header *incfs_create_mapping_file(struct file *backing_file,
				incfs_uuid_t *uuid, u64 file_size, u64 offset)
{
	return create_file(backing_file, uuid, file_size, offset,
			   INCFS_FILE_MAPPED);
}

int incfs_update_file_header(struct file *backing_file,
			     struct incfs_file_header *fh)
{
	if (!fh)
		return -EFAULT;

	return write_to_bf(backing_file, fh, sizeof(*fh), 0);
}

/* Write a given data block and update file's blockmap to point it. */
int incfs_write_data_block_to_backing_file(struct file *backing_file,
				     struct mem_range block, int block_index,
				     loff_t bm_base_off, u16 flags)
{
	struct incfs_blockmap_entry bm_entry = {};
	int result = 0;
	loff_t data_offset = 0;
	loff_t bm_entry_off =
		bm_base_off + sizeof(struct incfs_blockmap_entry) * block_index;

	if (block.len >= (1 << 16) || block_index < 0)
		return -EINVAL;

	data_offset = incfs_get_end_offset(backing_file);
	if (data_offset <= bm_entry_off) {
		/* Blockmap entry is beyond the file's end. It is not normal. */
		return -EINVAL;
	}

	/* Write the block data at the end of the backing file. */
	result = write_to_bf(backing_file, block.data, block.len, data_offset);
	if (result)
		return result;

	/* Update the blockmap to point to the newly written data. */
	bm_entry.me_data_offset_lo = cpu_to_le32((u32)data_offset);
	bm_entry.me_data_offset_hi = cpu_to_le16((u16)(data_offset >> 32));
	bm_entry.me_data_size = cpu_to_le16((u16)block.len);
	bm_entry.me_flags = cpu_to_le16(flags);

	return write_to_bf(backing_file, &bm_entry, sizeof(bm_entry),
				bm_entry_off);
}

int incfs_write_hash_block_to_backing_file(struct file *backing_file,
					   struct mem_range block,
					   int block_index,
					   loff_t hash_area_off,
					   loff_t bm_base_off,
					   loff_t file_size)
{
	struct incfs_blockmap_entry bm_entry = {};
	int result;
	loff_t data_offset = 0;
	loff_t file_end = 0;
	loff_t bm_entry_off =
		bm_base_off +
		sizeof(struct incfs_blockmap_entry) *
			(block_index + get_blocks_count_for_size(file_size));

	data_offset = hash_area_off + block_index * INCFS_DATA_FILE_BLOCK_SIZE;
	file_end = incfs_get_end_offset(backing_file);
	if (data_offset + block.len > file_end) {
		/* Block is located beyond the file's end. It is not normal. */
		return -EINVAL;
	}

	result = write_to_bf(backing_file, block.data, block.len, data_offset);
	if (result)
		return result;

	bm_entry.me_data_offset_lo = cpu_to_le32((u32)data_offset);
	bm_entry.me_data_offset_hi = cpu_to_le16((u16)(data_offset >> 32));
	bm_entry.me_data_size = cpu_to_le16(INCFS_DATA_FILE_BLOCK_SIZE);

	return write_to_bf(backing_file, &bm_entry, sizeof(bm_entry),
			   bm_entry_off);
}

int incfs_read_blockmap_entry(struct file *backing_file, int block_index,
			loff_t bm_base_off,
			struct incfs_blockmap_entry *bm_entry)
{
	int error = incfs_read_blockmap_entries(backing_file, bm_entry,
						block_index, 1,	bm_base_off);

	if (error < 0)
		return error;

	if (error == 0)
		return -EIO;

	if (error != 1)
		return -EFAULT;

	return 0;
}

int incfs_read_blockmap_entries(struct file *backing_file,
		struct incfs_blockmap_entry *entries,
		int start_index, int blocks_number,
		loff_t bm_base_off)
{
	loff_t bm_entry_off =
		bm_base_off + sizeof(struct incfs_blockmap_entry) * start_index;
	const size_t bytes_to_read = sizeof(struct incfs_blockmap_entry)
					* blocks_number;
	int result = 0;

	if (!entries)
		return -EFAULT;

	if (start_index < 0 || bm_base_off <= 0)
		return -ENODATA;

	result = incfs_kread(backing_file, entries, bytes_to_read,
			     bm_entry_off);
	if (result < 0)
		return result;
	return result / sizeof(*entries);
}

struct incfs_file_header *incfs_read_file_header(struct file *backing_file)
{
	ssize_t bytes_read = 0;
	struct incfs_file_header *fh = kzalloc(sizeof(*fh), GFP_NOFS);
	int error = 0;

	if (!fh)
		return ERR_PTR(-ENOMEM);

	bytes_read = incfs_kread(backing_file, fh, sizeof(*fh), 0);
	if (bytes_read < 0) {
		error = bytes_read;
		goto err;
	}

	if (bytes_read < sizeof(*fh)) {
		error = -EBADMSG;
		goto err;
	}

	if (le32_to_cpu(fh->fh_magic) != INCFS_MAGIC_NUMBER ||
	    le32_to_cpu(fh->fh_version) > INCFS_FORMAT_CURRENT_VER ||
	    le16_to_cpu(fh->fh_data_block_size) != INCFS_DATA_FILE_BLOCK_SIZE ||
	    le16_to_cpu(fh->fh_header_size) != sizeof(*fh)) {
		error = -EILSEQ;
		goto err;
	}

	return fh;

err:
	kfree(fh);
	return ERR_PTR(error);
}

ssize_t incfs_kread(struct file *f, void *buf, size_t size, loff_t pos)
{
	return kernel_read(f, buf, size, &pos);
}

ssize_t incfs_kwrite(struct file *f, const void *buf, size_t size, loff_t pos)
{
	return kernel_write(f, buf, size, &pos);
}
