#include <asm/alternative-macros.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_pkvm_module.h>
#include <asm/io.h>

static unsigned long uart_addr;

#define TX_OFFSET (UART_TX << CONFIG_SERIAL_PKVM_8250_REG_SHIFT)
#define LSR_OFFSET (UART_LSR << CONFIG_SERIAL_PKVM_8250_REG_SHIFT)

static inline void hyp_wait_lsr_empty(void *base)
{
	u32 lsr;

	do {
		lsr = readl(base + LSR_OFFSET);
	} while (!uart_lsr_tx_empty(lsr));
	dmb(sy);
}

static void hyp_8250_putc(char c)
{
	void *base = (void *)uart_addr;

	hyp_wait_lsr_empty(base);
	writel(c, base + TX_OFFSET);
	hyp_wait_lsr_empty(base);
}

int hyp_8250_init(const struct pkvm_module_ops *ops)
{
	int ret;

	ret = ops->create_private_mapping(CONFIG_SERIAL_PKVM_8250_BASE_PHYS,
					  PAGE_SIZE, PAGE_HYP_DEVICE,
					  &uart_addr);
	if (ret)
		return ret;

	ret = ops->register_serial_driver(hyp_8250_putc);
	if (ret)
		return ret;

	ops->puts("pKVM 8250 UART driver loaded\n");

	return 0;
}
