/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NETNS_NFTABLES_H_
#define _NETNS_NFTABLES_H_

#include <linux/list.h>
#include <linux/android_kabi.h>

struct netns_nftables {
	u8			gencursor;
<<<<<<< HEAD   (90de35 Merge 5.4.250 into android12-5.4-lts)
	u8			validate_state;

	ANDROID_KABI_RESERVE(1);
=======
>>>>>>> BRANCH (887433 Linux 5.4.251)
};

#endif
