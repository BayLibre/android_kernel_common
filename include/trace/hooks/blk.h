/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM blk

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_BLK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_BLK_H

#include <trace/hooks/vendor_hooks.h>

struct bio;
struct blk_mq_hw_ctx;
struct block_device;
struct gendisk;
struct path;
struct readahead_control;
struct request;
struct request_queue;
struct vfsmount;
typedef __u32 __bitwise blk_opf_t;

DECLARE_HOOK(android_vh_bd_link_disk_holder,
	TP_PROTO(struct block_device *bdev, struct gendisk *disk),
	TP_ARGS(bdev, disk));
<<<<<<< HEAD   (ff78c78d65d3b2e46c6cd4c1b59e8e0e6150cdae ANDROID: GKI: update symbol list for allwinner)

struct path;
struct vfsmount;

||||||| BASE   (20130e92480a0cca90f69d9ccb1d42a06a6e07f0 FROMGIT: HID: logitech-hidpp: Prevent use-after-free on forc)
DECLARE_HOOK(android_vh_blk_fill_rwbs,
	TP_PROTO(char *rwbs, unsigned int opf),
	TP_ARGS(rwbs, opf));

struct path;
struct vfsmount;

=======
DECLARE_HOOK(android_vh_blk_fill_rwbs,
	TP_PROTO(char *rwbs, unsigned int opf),
	TP_ARGS(rwbs, opf));
>>>>>>> CHANGE (f24ea7b32d8c735089ce1b8e0ad166144db79017 ANDROID: block: Move forward declarations in <trace/hooks/bl)
DECLARE_HOOK(android_vh_do_new_mount_fc,
	TP_PROTO(struct path *mountpoint, struct vfsmount *mnt),
	TP_ARGS(mountpoint, mnt));
<<<<<<< HEAD   (ff78c78d65d3b2e46c6cd4c1b59e8e0e6150cdae ANDROID: GKI: update symbol list for allwinner)

struct blk_mq_hw_ctx;
struct request_queue;

||||||| BASE   (20130e92480a0cca90f69d9ccb1d42a06a6e07f0 FROMGIT: HID: logitech-hidpp: Prevent use-after-free on forc)

struct readahead_control;
typedef __u32 __bitwise blk_opf_t;

DECLARE_HOOK(android_vh_f2fs_ra_op_flags,
	TP_PROTO(blk_opf_t *op_flag, struct readahead_control *rac),
	TP_ARGS(op_flag, rac));

struct blk_mq_hw_ctx;
struct request_queue;

=======
DECLARE_HOOK(android_vh_f2fs_ra_op_flags,
	TP_PROTO(blk_opf_t *op_flag, struct readahead_control *rac),
	TP_ARGS(op_flag, rac));
>>>>>>> CHANGE (f24ea7b32d8c735089ce1b8e0ad166144db79017 ANDROID: block: Move forward declarations in <trace/hooks/bl)
DECLARE_HOOK(android_vh_blk_mq_delay_run_hw_queue,
	TP_PROTO(int cpu, struct blk_mq_hw_ctx *hctx, unsigned long delay, bool *skip),
	TP_ARGS(cpu, hctx, delay, skip));
DECLARE_HOOK(android_vh_blk_mq_kick_requeue_list,
	TP_PROTO(struct request_queue *q, unsigned long delay, bool *skip),
	TP_ARGS(q, delay, skip));
<<<<<<< HEAD   (ff78c78d65d3b2e46c6cd4c1b59e8e0e6150cdae ANDROID: GKI: update symbol list for allwinner)

struct readahead_control;
typedef __u32 __bitwise blk_opf_t;

DECLARE_HOOK(android_vh_f2fs_ra_op_flags,
	TP_PROTO(blk_opf_t *op_flag, struct readahead_control *rac),
	TP_ARGS(op_flag, rac));
||||||| BASE   (20130e92480a0cca90f69d9ccb1d42a06a6e07f0 FROMGIT: HID: logitech-hidpp: Prevent use-after-free on forc)

struct bio;

DECLARE_HOOK(android_vh_check_set_ioprio,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio));

struct request;
DECLARE_HOOK(android_vh_loop_skip_queue_work,
        TP_PROTO(struct request *req, bool *skip),
        TP_ARGS(req, skip));

DECLARE_HOOK(android_vh_bio_add_page_merge_bypass,
	TP_PROTO(struct bio *bio, bool *skip),
	TP_ARGS(bio, skip));
=======
DECLARE_HOOK(android_vh_check_set_ioprio,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio));
DECLARE_HOOK(android_vh_loop_skip_queue_work,
        TP_PROTO(struct request *req, bool *skip),
        TP_ARGS(req, skip));
DECLARE_HOOK(android_vh_bio_add_page_merge_bypass,
	TP_PROTO(struct bio *bio, bool *skip),
	TP_ARGS(bio, skip));
>>>>>>> CHANGE (f24ea7b32d8c735089ce1b8e0ad166144db79017 ANDROID: block: Move forward declarations in <trace/hooks/bl)

#endif /* _TRACE_HOOK_BLK_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
