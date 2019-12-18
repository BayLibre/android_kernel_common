/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#include <linux/ioctl.h>

/* These ioctls are required by UART clients
 * for calline clock on/off of uart
 */

#define TIOCPMGET	0x544D	/* PM get */
#define TIOCPMPUT	0x544E	/* PM put */
#define TIOCPMACT	0x544F	/* PM is active */
