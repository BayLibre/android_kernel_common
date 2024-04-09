/*
 * "IKCFG_ST" and "IKCFG_ED" are used to extract the config data from
 * a binary kernel image or a module. See scripts/extract-ikconfig.
 */

	.pushsection .rodata, "a"
	.ascii "IKCFG_ST"
	.global kernel_config_data
kernel_config_data:
	.incbin "kernel/config_data.gz"
	.global kernel_config_data_end
kernel_config_data_end:
	.ascii "IKCFG_ED"
	.popsection
