/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/usb/typec_altmode.h>

int typec_mode_set_priority(struct typec_altmode *alt, const u8 priority);

/**
 * The mode selection process follows a lifecycle tied to the USB-C partner
 * device. The API is designed to first build a set of desired modes and then
 * trigger the selection process. The expected sequence of calls is as follows:
 *
 * Creation and Configuration:
 * Call typec_mode_selection_add_partner() when the partner device is being set
 * up.
 *
 * Execution:
 * Call typec_mode_selection_start() to trigger the mode selection.
 * typec_mode_selection_is_pending() returns true if the process is in progress
 * or complete.
 * Call typec_mode_selection_reset() to stop the selection process and exit
 * the currently active mode.
 *
 * Destruction:
 * Before destroying a partner, call typec_mode_selection_remove_partner()
 */
void typec_mode_selection_add_partner(struct typec_partner *partner);
void typec_mode_selection_remove_partner(struct typec_partner *partner);
int typec_mode_selection_start(struct typec_partner *partner);
bool typec_mode_selection_is_pending(struct typec_partner *partner);
int typec_mode_selection_reset(struct typec_partner *partner);
