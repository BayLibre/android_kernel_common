# SPDX-License-Identifier: GPL-2.0

# clk-helper.o and clk-pll.o are combined into a single zhihe-clk-core
# module (clk-helper.c calls into clk-pll.c's zhihe_pll() directly) so
# that internal linkage doesn't need EXPORT_SYMBOL. clk-a210.o is a
# separate module and only needs the four symbols zhihe-clk-core
# exports (zhihe_register_clock, zhihe_unregister_clocks,
# zhihe_parse_regbase, zhihe_clk_fake_pll_fixed_ops).
obj-$(CONFIG_ZHIHE_CLK) += zhihe-clk-core.o
zhihe-clk-core-y := clk-helper.o clk-pll.o

obj-$(CONFIG_CLK_A210)  += clk-a210.o
