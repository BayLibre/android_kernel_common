// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <drm/drm_drv.h>

#include "xe_user.h"


/**
 * DOC: Xe User
 *
 * Xe User adds support for handling UID (i.e. persistent, unique ID of the
 * Android app) based requirements for Android platforms.
 *
 * For Android GPU work period event we need to track the runtime on the GPU
 * for each UID. This means we can have multiple xe files opened by different
 * processes/threads that belongs to the same UID. All these xe files need to
 * be grouped together so that one can easily identify them while calculating
 * the run time for the given UID.
 *
 * Currently, the xe driver doesn't record the user id of the calling process.
 * Also, all the xe files created using open call are clubbed together inside
 * the xe device structure with no way to distinguish between them based on
 * the UID of the calling process.
 *
 * To remedy these limitations we are adding another layer of indirection
 * between the xe device and the xe file. xe device will now also have a list
 * of xe users each with a given UID, and each xe user will have a list of xe
 * files that are created by a process that belongs to this UID.
 *
 * The lifetime of a xe user structure should be between when a process with
 * a new UID has first opened the xe device, and when the last xe file
 * belonging to this UID is closed.
 *
 * In order to implement this we maintain an xarray of xe user structures
 * inside our xe device instance. Whenever a new xe file is created via an
 * open call, we check if the calling process' UID is already present in our
 * xarray. If so, we increment the refcount for the associated xe user and add
 * our newly created xe file to the list of xe files belonging to this xe user.
 * Otherwise, we allocate a new xe user structure for this UID and initialize
 * its file list with our newly create xe file.
 *
 * Whenever an xe file is being destroyed, we decrement the refcount of the
 * associated xe user. When the last xe file in the xe user's file list is
 * destroyed, the xe user refcount should drop to zero and the xe user should
 * be cleaned up. During the cleanup path we remove the xarray entry in our xe
 * device for this xe user and free up its memory.
 */




/**
 * xe_user_alloc() - Allocate xe user
 * @void: No arg
 *
 * Allocate xe user struct to track activity on the gpu
 * by the application. Call this API whenever a new app
 * has opened xe device.
 *
 * Return: pointer to user struct or NULL if can't allocate
 */
static struct xe_user *xe_user_alloc(void)
{
	struct xe_user *user;

	user = kzalloc(sizeof(*user), GFP_KERNEL);
	if (!user)
		return NULL;

	kref_init(&user->refcount);
	mutex_init(&user->filelist_lock);
	INIT_LIST_HEAD(&user->filelist);
	INIT_WORK(&user->work, work_period_worker);
	return user;
}

/**
 * __xe_user_free() - Free user struct
 * @kref: The reference
 *
 * Return: void
 */
void __xe_user_free(struct kref *kref)
{
	struct xe_user *user =
		container_of(kref, struct xe_user, refcount);
	struct xe_device *xe = user->xe;
	void *lookup;

	lookup = xa_erase(&xe->work_period.users, user->id);
	xe_assert(xe, lookup == user);

	drm_dev_put(&user->xe->drm);
	kfree(user);
}

static struct xe_user *xe_user_lookup(struct xe_device *xe, u32 uid)
{
	struct xe_user *user = NULL;
	unsigned long i;

	/*
	 * We don't know if any of the xe_user pointers in
	 * our work_period.users Xarray is in the process
	 * of being freed. Therefore, our lookup function
	 * will only return valid pointer if its refcount
	 * has not yet dropped to zero. Otherwise we return
	 * a pointer NULL and let the xe_user_alloc to
	 * recreate a new struct xe_user with this UID.
	 */

	mutex_lock(&xe->work_period.lock);
	xa_for_each(&xe->work_period.users, i, user) {
		if (user && xe_user_get_unless_zero(user)) {
			if (user->uid == uid) {
				mutex_unlock(&xe->work_period.lock);
				return user;
			}
			// Drop the reference taken earlier if the UID
			// doesn't match user
			xe_user_put(user);
		}
	}
	mutex_unlock(&xe->work_period.lock);

	return NULL;
}

int xe_user_init(struct xe_device *xe, struct xe_file *xef, unsigned int uid)
{
	struct xe_user *user = NULL;
	int ret;
	u32 idx;
	/*
	 * Check if the calling process/uid has already been registered
	 * with the xe device during a previous open call. If so then
	 * take a reference to this xe user and add this xe file to the
	 * filelist belonging to this xe user
	 */
	user = xe_user_lookup(xe, uid);
	if (!user) {
		/*
		 * We couldn't find an existing xe user for the calling process.
		 * Allocate a new struct xe_user and register it with this xe
		 * device
		 */
		user = xe_user_alloc();
		if (!user)
			return -ENOMEM;


		user->uid = uid;
		user->last_timestamp_ns = ktime_get_raw_ns();
		user->xe = xe;

		ret = xa_alloc(&xe->work_period.users, &idx, user, xa_limit_32b, GFP_KERNEL);

		if (ret < 0)
			return ret;

		user->id = idx;
		drm_dev_get(&xe->drm);
	}

	mutex_lock(&user->filelist_lock);
	list_add(&xef->user_link, &user->filelist);
	mutex_unlock(&user->filelist_lock);
	xef->user = user;

	return 0;
}
