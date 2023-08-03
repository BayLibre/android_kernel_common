/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NETNS_NFTABLES_H_
#define _NETNS_NFTABLES_H_

#include <linux/list.h>
#include <linux/android_kabi.h>

struct netns_nftables {
	u8			gencursor;
<<<<<<< HEAD   (9710ae Merge branch 'android12-5.10' into branch 'android12-5.10-lt)
	u8			validate_state;

	ANDROID_KABI_RESERVE(1);
=======
>>>>>>> BRANCH (3602db Linux 5.10.188)
};

#endif
