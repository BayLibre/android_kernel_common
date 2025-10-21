// SPDX-License-Identifier: GPL-2.0
/*
 * UCSI USB4 Mode Support
 *
 * Copyright 2025 Google LLC
 */

#include <linux/usb/typec_tbt.h>
#include <linux/usb/pd_vdo.h>

#include "ucsi.h"

struct ucsi_usb4 {
	struct ucsi_connector *con;
};

static int ucsi_usb4_send_set_usb(struct typec_altmode *alt,
				const bool enter)
{
	struct ucsi_usb4 *usb4 = typec_altmode_get_drvdata(alt);
	u64 command = UCSI_SET_USB | UCSI_CONNECTOR_NUMBER(usb4->con->num);
	struct enter_usb_data data;
	int ret;


	if (enter) {
		command |= UCSI_USB4_ENABLE;
		data.eudo = EUDO_USB_MODE_USB4 << EUDO_USB_MODE_SHIFT;
		data.eudo |= (EUDO_CABLE_SPEED_USB4_GEN2 << EUDO_CABLE_SPEED_SHIFT);
		data.eudo |= (EUDO_CABLE_SPEED_USB4_GEN3 << EUDO_CABLE_SPEED_SHIFT);
	} else {
		command |= UCSI_USB3_ENABLE;
		data.eudo = EUDO_USB_MODE_USB3 << EUDO_USB_MODE_SHIFT;
		data.eudo |= (EUDO_CABLE_SPEED_USB3_GEN1 << EUDO_CABLE_SPEED_SHIFT);
	}

	data.eudo |= EUDO_PCIE_SUPPORT;
	data.eudo |= EUDO_DP_SUPPORT;
	data.eudo |= EUDO_TBT_SUPPORT;
	data.eudo |= EUDO_HOST_PRESENT;
	command |= ((u64)data.eudo << 29);

	if(!ucsi_con_mutex_lock(usb4->con))
		return -ENOTCONN;

	ret = ucsi_send_command(usb4->con->ucsi, command, NULL, 0);

	ucsi_con_mutex_unlock(usb4->con);

	return ret;
}

static int ucsi_usb4_enter(struct typec_altmode *alt, u32 *vdo)
{
	return ucsi_usb4_send_set_usb(alt, true);
}

static int ucsi_usb4_exit(struct typec_altmode *alt)
{
	return ucsi_usb4_send_set_usb(alt, false);
}

static const struct typec_altmode_ops ucsi_usb4_ops = {
	.enter = ucsi_usb4_enter,
	.exit = ucsi_usb4_exit,
};

struct typec_altmode *ucsi_register_usb4(struct ucsi_connector *con,
						bool override, int offset,
						struct typec_altmode_desc *desc)
{
	struct typec_altmode *alt = typec_port_register_altmode(con->port, desc);
	struct ucsi_usb4 *usb4;

	if (!IS_ERR(alt) && override) {
		usb4 = devm_kzalloc(&alt->dev, sizeof(*usb4), GFP_KERNEL);
		if (!usb4) {
			typec_unregister_altmode(alt);
			return ERR_PTR(-ENOMEM);
		}

		usb4->con = con;
		typec_altmode_set_drvdata(alt, usb4);
		typec_altmode_set_ops(alt, &ucsi_usb4_ops);
	}

	return alt;
}
