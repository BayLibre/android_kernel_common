/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2018 Google LLC
 */

#ifndef _INCFS_FORMAT_H
#define _INCFS_FORMAT_H
#include <linux/types.h>
#include <linux/kernel.h>
#include <uapi/linux/incrementalfs.h>

#include "internal.h"

#define INCFS_MAX_NAME_LEN 255
#define INCFS_FORMAT_V1 1
#define INCFS_FORMAT_CURRENT_VER INCFS_FORMAT_V1

enum incfs_metadata_type {
	INCFS_MD_NONE = 0,
	INCFS_MD_BLOCK_MAP = 1,
	INCFS_MD_SIGNATURE = 3
};

enum incfs_file_header_flags {
	INCFS_FILE_COMPLETE = 1 << 0,
	INCFS_FILE_MAPPED = 1 << 1,
};

/* Header included at the beginning of all metadata records on the disk. */
struct incfs_md_header {
	__u8 h_md_entry_type;

	/*
	 * Size of the metadata record.
	 * (e.g. inode, dir entry etc) not just this struct.
	 */
	__le16 h_record_size;

	/* Offset of the next metadata entry if any */
	__le64 h_next_md_offset;

} __packed;

/* Backing file header */
struct incfs_file_section {
	__le64 fs_size;
	__le64 fs_offset;
};

struct incfs_file_header {
	union {
		struct {
			/* Magic number: INCFS_MAGIC_NUMBER */
			__le32 fh_magic;

			/* Format version: INCFS_FORMAT_CURRENT_VER */
			__le32 fh_version;

			/* sizeof(incfs_file_header) */
			__le16 fh_header_size;

			/* INCFS_DATA_FILE_BLOCK_SIZE */
			__le16 fh_data_block_size;

			/* File flags, from incfs_file_header_flags */
			__le32 fh_flags;

			/* Full size of the file's content */
			__le64 fh_file_size;

			/* File uuid */
			incfs_uuid_t fh_uuid;

			/* Mapped files only - offset in original file */
			__le64 fh_offset;

			/* Number of sectors written out */
			__le64 fh_sectors_written;
		};

		u8 fh_filler[128];
	};

	union {
		struct {
			/* Blockmap */
			struct incfs_file_section fh_blockmap;

			/* File signature */
			struct incfs_file_section fh_signature;

			/* Hash tree */
			struct incfs_file_section fh_hash_tree;
		};

		struct incfs_file_section fh_sections_filler[8];
	};

} __packed;

enum incfs_block_map_entry_flags {
	INCFS_BLOCK_COMPRESSED_LZ4 = (1 << 0),
};

/* Block map entry pointing to an actual location of the data block. */
struct incfs_blockmap_entry {
	/* Offset of the actual data block. Lower 32 bits */
	__le32 me_data_offset_lo;

	/* Offset of the actual data block. Higher 16 bits */
	__le16 me_data_offset_hi;

	/* How many bytes the data actually occupies in the backing file */
	__le16 me_data_size;

	/* Block flags from incfs_block_map_entry_flags */
	__le16 me_flags;
} __packed;

/* In memory version of above */
struct incfs_df_signature {
	u32 sig_size;
	u64 sig_offset;
	u32 hash_size;
	u64 hash_offset;
};

/* State of the backing file. */
struct backing_file_context {
	/* Protects writes to bc_file */
	struct mutex bc_mutex;

	/* File object to read data from */
	struct file *bc_file;
};

loff_t incfs_get_end_offset(struct file *f);

/* Backing file context management */
struct backing_file_context *incfs_alloc_bfc(struct file *backing_file);

void incfs_free_bfc(struct backing_file_context *bfc);

/* Writing stuff */
struct incfs_file_header *incfs_create_backing_file(
				struct backing_file_context *bfc,
				incfs_uuid_t *uuid, u64 file_size);

struct incfs_file_header *incfs_create_mapping_file(
				struct backing_file_context *bfc,
				incfs_uuid_t *uuid, u64 file_size, u64 offset);

int incfs_update_file_header(struct backing_file_context *bfc,
			     struct incfs_file_header *fh);

int incfs_write_blockmap_to_backing_file(struct backing_file_context *bfc,
					 u32 block_count,
					 struct incfs_file_header *fh);

int incfs_write_data_block_to_backing_file(struct backing_file_context *bfc,
					   struct mem_range block,
					   int block_index, loff_t bm_base_off,
					   u16 flags);

int incfs_write_hash_block_to_backing_file(struct backing_file_context *bfc,
					   struct mem_range block,
					   int block_index,
					   loff_t hash_area_off,
					   loff_t bm_base_off,
					   loff_t file_size);

int incfs_write_signature_to_backing_file(struct backing_file_context *bfc,
					  struct mem_range sig, u32 tree_size,
					  struct incfs_file_header *fh);

int incfs_write_file_header_flags(struct backing_file_context *bfc, u32 flags);

/* Reading stuff */
struct incfs_file_header *incfs_read_file_header(
					struct backing_file_context *bfc);

int incfs_read_blockmap_entry(struct backing_file_context *bfc, int block_index,
			      loff_t bm_base_off,
			      struct incfs_blockmap_entry *bm_entry);

int incfs_read_blockmap_entries(struct backing_file_context *bfc,
		struct incfs_blockmap_entry *entries,
		int start_index, int blocks_number,
		loff_t bm_base_off);

ssize_t incfs_kread(struct file *f, void *buf, size_t size, loff_t pos);
ssize_t incfs_kwrite(struct file *f, const void *buf, size_t size, loff_t pos);

#endif /* _INCFS_FORMAT_H */
