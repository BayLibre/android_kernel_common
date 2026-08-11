// SPDX-License-Identifier: GPL-2.0-only
/*
 * SpacemiT K3 (X100) console-clock keep-alive + observer.
 *
 * With the RCPU ESOS firmware running, the machine hard-freezes in an
 * all-idle lull ~11-42s into userspace: the serial console stops
 * mid-character and no further output appears. The AP core/cluster
 * power-down votes are NOT the cause (measured 0x0 throughout), which
 * points at a shared clock being gated while the AP is idle -- most
 * likely the console UART itself, matching the mid-character death.
 *
 * ttyS0 (uart0 @ d4017000) is clocked from the APBC syscon register
 * APBC_UART0_CLK_RST (0xd4015000 + 0x00): bit0 = APB bus clock gate,
 * bit1 = functional clock gate. This driver periodically re-asserts both
 * from Linux (where a timer actually fires -- OpenSBI never runs during a
 * bare WFI idle) so the console clock cannot be gated out from under us,
 * and edge-prints the pre-write value so any external gating attempt (e.g.
 * by the ESOS clock service) is captured before it is undone.
 *
 * Diagnostic + bring-up keep-alive. If forcing these bits keeps the board
 * alive past the lull, the freeze was console/APB clock gating; if it
 * still dies, the gate is upstream (apb_clk parent / fabric) or it is a
 * real bus hang, and the observer output narrows which.
 */

#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>

#define K3_APBC_BASE		0xd4015000
#define K3_APBC_SIZE		0x1000
#define APBC_UART0_CLK_RST	0x00		/* ttyS0 clock/reset control */
#define UART0_BUS_CLK_EN	BIT(0)
#define UART0_FUNC_CLK_EN	BIT(1)
#define UART0_CLK_KEEP		(UART0_BUS_CLK_EN | UART0_FUNC_CLK_EN)

#define K3_KEEPALIVE_PERIOD_US	200

static void __iomem *k3_apbc;
static struct hrtimer k3_keepalive_timer;

static enum hrtimer_restart k3_keepalive_tick(struct hrtimer *t)
{
	static u32 prev = ~0u;
	u32 v = readl(k3_apbc + APBC_UART0_CLK_RST);

	if (v != prev) {
		pr_info("k3-console-keepalive: uart0_clk_rst=0x%x\n", v);
		prev = v;
	}

	if ((v & UART0_CLK_KEEP) != UART0_CLK_KEEP)
		writel(v | UART0_CLK_KEEP, k3_apbc + APBC_UART0_CLK_RST);

	hrtimer_forward_now(t, us_to_ktime(K3_KEEPALIVE_PERIOD_US));
	return HRTIMER_RESTART;
}

static int __init k3_console_keepalive_init(void)
{
	if (!of_machine_is_compatible("spacemit,k3"))
		return 0;

	k3_apbc = ioremap(K3_APBC_BASE, K3_APBC_SIZE);
	if (!k3_apbc) {
		pr_err("k3-console-keepalive: ioremap failed\n");
		return -ENOMEM;
	}

	hrtimer_setup(&k3_keepalive_timer, k3_keepalive_tick, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL);
	hrtimer_start(&k3_keepalive_timer,
		      us_to_ktime(K3_KEEPALIVE_PERIOD_US), HRTIMER_MODE_REL);

	pr_info("k3-console-keepalive: armed (period %uus)\n",
		K3_KEEPALIVE_PERIOD_US);
	return 0;
}
device_initcall(k3_console_keepalive_init);
