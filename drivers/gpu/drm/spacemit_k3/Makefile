# SPDX-License-Identifier: GPL-2.0

# K3 (saturn-hee/saturn-edp) DPU master + CRTC.  Distinct module from the K1
# spacemit-drm.ko; ported from the vendor BSP spacemit-drm-y object list.
spacemit-drm-k3-y :=	spacemit_drm.o \
			spacemit_crtc.o \
			spacemit_cmdlist.o \
			spacemit_wb.o \
			spacemit_planes.o \
			spacemit_dsi.o \
			spacemit_dphy.o \
			spacemit_lib.o \
			spacemit_gem.o \
			spacemit_dmmu.o \
			spacemit_bootloader.o \
			common/spacemit_drm_notifier.o \
			sysfs/sysfs_class.o \
			sysfs/sysfs_dpu.o \
			sysfs/sysfs_dsi.o \
			sysfs/sysfs_dphy.o \
			sysfs/sysfs_mipi_panel.o \
			dpu/dpu_debug.o \
			dpu/dpu_saturn.o \
			dpu/post_process_hee.o \
			dpu/dpu_saturn_hee.o \
			dpu/saturn_fbcmem.o \
			dsi/spacemit_dptc_drv.o \
			dsi/spacemit_dsi_drv.o \
			dphy/spacemit_dphy_drv.o

obj-$(CONFIG_DRM_SPACEMIT_K3_DPU) += spacemit-drm-k3.o

obj-$(CONFIG_SPACEMIT_INNO_DP) += spacemit_inno_dp.o
