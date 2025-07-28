/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __TRACE_MISC_SPED_H__
#define __TRACE_MISC_SPED_H__

/* Scheduler action after SPED has inspected a task for privilege elevation. */
enum sped_task_action {
	// Continue with the scheduling of the inspected task.
	// This occurs when:
	//    * The inspected task does not have elevated privileges.
	//    * SPED is disabled or we are in the early stages of the boot
	//      process and the Vendor Hook is not initialized yet.
	//    * A soft error has occurred, but it should be ignored.
	SPED_TASK_CONTINUE = 0,c

	// Do not schedule the inspected task for execution.
	// This occurs when:
	//    * The inspected task does have elevated privileges.
	//    * A hard error has occurred and the current task should not be
	//      scheduled for execution.
	SPED_TASK_NOT_SCHEDULE = 1,

	// Halt the device.
	// This occurs when a fatal error has occurred -because the device has
	// been compromised- and SPED cannot continue operating properly.
	SPED_TASK_ABORT = -1
};

#endif  // __TRACE_MISC_SPED_H__
