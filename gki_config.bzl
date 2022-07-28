COMMON_KERNEL_CONFIGS = {
    "kernel_aarch64": {
        "module_outs": [
            "drivers/block/zram/zram.ko",
            "mm/zsmalloc.ko",
        ],
    },
    "kernel_aarch64_debug": {
        "module_outs": [
            "drivers/block/zram/zram.ko",
            "mm/zsmalloc.ko",
        ],
    },
}
