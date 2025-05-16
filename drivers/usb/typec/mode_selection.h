/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/usb/typec_dp.h>
#include <linux/usb/typec_tbt.h>

int typec_mode_set_priority(struct typec_altmode *adev, const int priority);
int typec_mode_get_priority(struct typec_altmode *adev, int *priority);
void typec_mode_selection_destroy(struct typec_port *port);

/**
 * The mode selection process follows a lifecycle tied to the USB-C partner
 * device. The API is designed to first build a set of desired modes and then
 * trigger the selection process. The expected sequence of calls is as follows:
 *
 * Creation and Configuration:
 * call typec_mode_selection_add_partner() when the partner device is being set
 * up. After creation, call typec_mode_selection_add_mode() and
 * typec_mode_selection_add_cable() to define the parameters for the
 * selection process.
 *
 * Execution:
 * Call typec_mode_selection_start() to trigger the mode selection.
 * Call typec_mode_selection_reset() to prematurely stop the selection
 * process and clear any stored results.
 *
 * Destruction:
 * Before destroying a partner, call typec_mode_selection_remove_partner()
 */
void typec_mode_selection_add_partner(struct typec_partner *partner);
void typec_mode_selection_remove_partner(struct typec_partner *partner);
int typec_mode_selection_start(struct typec_partner *partner);
int typec_mode_selection_reset(struct typec_partner *partner);
void typec_mode_selection_add_mode(struct typec_partner *partner,
		struct typec_altmode *alt);
void typec_mode_selection_add_cable(struct typec_partner *partner,
		struct typec_cable *cable);
int typec_mode_selection_get_state(struct typec_partner *partner, char *buf);
