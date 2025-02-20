# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2022 The Android Open Source Project

"""
This module contains a full list of kernel modules
 compiled by GKI.
"""

_COMMON_GKI_MODULES_LIST = [
    # keep sorted
#    "arch/arm64/geniezone/gzvm.ko",
#    "drivers/char/hw_random/cctrng.ko",
#    "drivers/misc/open-dice.ko",
#    "drivers/ptp/ptp_kvm.ko",
#    "lib/kunit/kunit.ko",
#    "drivers/base/regmap/regmap-kunit.ko",
#    "drivers/base/regmap/regmap-ram.ko",
#    "drivers/base/regmap/regmap-raw-ram.ko",
#    "drivers/hid/hid-uclogic-test.ko",
#    "drivers/iio/test/iio-test-format.ko",
#    "drivers/input/tests/input_test.ko",
#    "drivers/rtc/lib_test.ko",
#    "fs/ext4/ext4-inode-test.ko",
#    "fs/fat/fat_test.ko",
#    "kernel/time/time_test.ko",
#    "lib/kunit/kunit-example-test.ko",
#    "lib/kunit/kunit-test.ko",
#    "net/core/dev_addr_lists_test.ko",
#    "sound/soc/soc-topology-test.ko",
#    "sound/soc/soc-utils-test.ko",
#    "drivers/clk/clk-gate_test.ko",
#    "drivers/clk/clk_test.ko",
]

# Deprecated - Use `get_gki_modules_list` function instead.
COMMON_GKI_MODULES_LIST = _COMMON_GKI_MODULES_LIST

_ARM_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/ptp/ptp_kvm.ko",
]

_ARM64_GKI_MODULES_LIST = [
    # keep sorted
#    "arch/arm64/geniezone/gzvm.ko",
    "drivers/char/hw_random/cctrng.ko",
    "drivers/misc/open-dice.ko",
#    "drivers/ptp/ptp_kvm.ko",
    "net/netfilter/nf_nat_tftp.ko",
    "fs/hfsplus/hfsplus.ko",
    "fs/nfs/nfsv2.ko",
    "drivers/usb/usbip/usbip-core.ko",
    "drivers/hid/wacom.ko",
    "drivers/net/wireless/realtek/rtw88/rtw88_8822b.ko",
    "net/sunrpc/sunrpc.ko",
    "drivers/net/tun.ko",
    "sound/soc/codecs/snd-soc-nau8825.ko",
    "drivers/net/usb/cdc_ether.ko",
    "drivers/usb/serial/usb-serial-simple.ko",
    "drivers/remoteproc/mtk_scp.ko",
    "drivers/net/phy/phylink.ko",
    "drivers/usb/misc/ezusb.ko",
    "net/802/psnap.ko",
    "drivers/net/wireless/mediatek/mt76/mt7921/mt7921-common.ko",
    "sound/soc/mediatek/mt8188/mt8188-mt6359.ko",
    "drivers/usb/gadget/function/usb_f_fs.ko",
    "drivers/net/mii.ko",
    "drivers/iio/proximity/sx9324.ko",
    "fs/nfs/nfsv3.ko",
    "drivers/media/platform/mediatek/vcodec/decoder/mtk-vcodec-dec.ko",
    "net/802/stp.ko",
    "crypto/lz4hc.ko",
    "sound/soc/sof/snd-sof-utils.ko",
    "drivers/hid/hid-nintendo.ko",
    "drivers/bluetooth/btbcm.ko",
    "drivers/net/wireless/ralink/rt2x00/rt2x00lib.ko",
    "drivers/hid/hid-primax.ko",
    "drivers/input/rmi4/rmi_core.ko",
    "net/sched/sch_netem.ko",
    "drivers/base/regmap/regmap-raw-ram.ko",
    "drivers/hid/hid-vivaldi.ko",
    "drivers/media/platform/mediatek/mdp/mtk-mdp.ko",
    "drivers/platform/chrome/cros_ec_rpmsg.ko",
    "drivers/bluetooth/hci_uart.ko",
    "drivers/usb/serial/ftdi_sio.ko",
    "drivers/iio/common/cros_ec_sensors/cros_ec_sensors_core.ko",
    "sound/drivers/snd-aloop.ko",
    "drivers/iio/pressure/cros_ec_baro.ko",
    "drivers/soc/mediatek/mtk-svs.ko",
    "drivers/net/usb/r8153_ecm.ko",
    "drivers/iio/common/cros_ec_sensors/cros_ec_lid_angle.ko",
    "sound/soc/mediatek/mt8173/mt8173-rt5650-rt5676.ko",
    "net/bluetooth/bluetooth.ko",
    "net/ipv6/ip6_udp_tunnel.ko",
    "drivers/watchdog/softdog.ko",
    "net/ipv4/tcp_lp.ko",
    "drivers/bluetooth/btmtksdio.ko",
    "net/ipv4/udp_tunnel.ko",
    "drivers/net/usb/aqc111.ko",
    "drivers/usb/gadget/legacy/g_mass_storage.ko",
    "drivers/usb/serial/keyspan.ko",
    "drivers/iio/trigger/iio-trig-hrtimer.ko",
    "drivers/net/usb/smsc75xx.ko",
    "drivers/gpu/drm/panel/panel-himax-hx83102.ko",
    "drivers/net/wireless/ath/ath10k/ath10k_sdio.ko",
    "fs/lockd/lockd.ko",
    "drivers/bluetooth/bfusb.ko",
    "drivers/media/platform/mediatek/vcodec/decoder/mtk-vcodec-dec-hw.ko",
    "drivers/usb/gadget/udc/dummy_hcd.ko",
    "net/netfilter/xt_cgroup.ko",
    "drivers/bluetooth/btmrvl.ko",
    "drivers/iio/common/cros_ec_sensors/cros_ec_sensors.ko",
    "drivers/hid/hid-holtekff.ko",
    "drivers/md/dm-flakey.ko",
    "drivers/usb/serial/sierra.ko",
    "net/xfrm/xfrm_interface.ko",
    "lib/crypto/libchacha.ko",
    "sound/usb/snd-usbmidi-lib.ko",
    "drivers/net/wireless/mediatek/mt76/mt7921/mt7921s.ko",
    "net/8021q/8021q.ko",
    "crypto/cmac.ko",
    "lib/zstd/zstd_compress.ko",
    "drivers/net/wireless/marvell/mwifiex/mwifiex_sdio.ko",
    "drivers/net/usb/rndis_host.ko",
    "sound/core/seq/snd-seq-midi.ko",
    "drivers/cdrom/cdrom.ko",
    "drivers/net/wireless/mediatek/mt76/mt7921/mt7921e.ko",
    "drivers/media/platform/mediatek/vcodec/common/mtk-vcodec-dbgfs.ko",
    "drivers/net/usb/ipheth.ko",
    "drivers/media/platform/mediatek/mdp3/mtk-mdp3.ko",
    "drivers/scsi/sr_mod.ko",
    "drivers/usb/class/cdc-wdm.ko",
    "drivers/media/platform/mediatek/vcodec/encoder/mtk-vcodec-enc.ko",
    "net/bluetooth/rfcomm/rfcomm.ko",
    "sound/core/snd-seq-device.ko",
    "drivers/media/v4l2-core/v4l2-h264.ko",
    "drivers/net/phy/ax88796b.ko",
    "drivers/net/usb/net1080.ko",
    "net/ipv6/esp6.ko",
    "lib/lz4/lz4_compress.ko",
    "drivers/usb/usbip/vhci-hcd.ko",
    "drivers/iio/buffer/kfifo_buf.ko",
    "drivers/media/common/videobuf2/videobuf2-common.ko",
    "drivers/rpmsg/rpmsg_core.ko",
    "drivers/media/platform/mediatek/vcodec/common/mtk-vcodec-common.ko",
    "drivers/usb/serial/pl2303.ko",
    "sound/soc/mediatek/mt8173/mt8173-rt5650-rt5514.ko",
    "drivers/net/wireless/realtek/rtw88/rtw88_core.ko",
    "drivers/net/wireless/realtek/rtw88/rtw88_8822c.ko",
    "drivers/iio/industrialio-configfs.ko",
    "drivers/hid/hid-magicmouse.ko",
    "drivers/media/common/videobuf2/videobuf2-vmalloc.ko",
    "drivers/media/common/uvc.ko",
    "crypto/lz4.ko",
    "drivers/net/wireless/mediatek/mt76/mt792x-lib.ko",
    "sound/soc/codecs/snd-soc-rt5677.ko",
    "drivers/thermal/mediatek/soc_temp_lvts_mt8192.ko",
    "drivers/hid/hid-google-hammer.ko",
    "lib/test_firmware.ko",
    "drivers/bluetooth/btusb.ko",
    "drivers/net/wireless/ralink/rt2x00/rt2800usb.ko",
    "drivers/firmware/google/vpd-sysfs.ko",
    "crypto/ccm.ko",
    "net/bridge/bridge.ko",
    "drivers/hid/hid-sony.ko",
    "drivers/bluetooth/btsdio.ko",
    "sound/drivers/snd-dummy.ko",
    "drivers/net/usb/cdc_ncm.ko",
    "drivers/net/usb/r8152.ko",
    "net/netfilter/nf_nat_ftp.ko",
    "drivers/gpu/drm/evdi/evdi.ko",
    "drivers/usb/serial/option.ko",
    "net/netfilter/xt_MASQUERADE.ko",
    "drivers/net/usb/smsc95xx.ko",
    "drivers/base/test/test_async_driver_probe.ko",
    "drivers/remoteproc/mtk_scp_ipi.ko",
    "drivers/media/platform/mediatek/jpeg/mtk_jpeg.ko",
    "crypto/ecdh_generic.ko",
    "drivers/thermal/mediatek/soc_temp_lvts.ko",
    "drivers/cpufreq/mediatek-cpufreq-hw.ko",
    "drivers/input/joystick/iforce/iforce-usb.ko",
    "sound/usb/snd-usb-audio.ko",
    "drivers/net/usb/mcs7830.ko",
    "drivers/i2c/i2c-stub.ko",
    "drivers/mmc/core/mmc_test.ko",
    "arch/arm64/crypto/chacha-neon.ko",
    "sound/soc/codecs/snd-soc-tas2781-i2c.ko",
    "lib/crc-itu-t.ko",
    "drivers/hid/hid-plantronics.ko",
    "drivers/hid/hid-cherry.ko",
    "drivers/net/wireless/marvell/mwifiex/mwifiex.ko",
    "sound/soc/codecs/snd-soc-tas2781-comlib.ko",
    "drivers/input/joystick/iforce/iforce.ko",
    "net/sched/sch_fq_codel.ko",
    "fs/udf/udf.ko",
    "sound/soc/sof/snd-sof-of.ko",
    "crypto/xor.ko",
    "net/ipv6/sit.ko",
    "drivers/hid/hid-logitech-hidpp.ko",
    "lib/crypto/libcurve25519-generic.ko",
    "drivers/net/wireless/mediatek/mt76/mt76.ko",
    "drivers/bluetooth/btintel.ko",
    "crypto/async_tx/async_xor.ko",
    "drivers/net/wireless/realtek/rtw88/rtw88_pci.ko",
    "drivers/usb/serial/cp210x.ko",
    "drivers/usb/serial/usbserial.ko",
    "drivers/net/wireless/ralink/rt2x00/rt2800lib.ko",
    "fs/nfs_common/grace.ko",
    "drivers/bluetooth/btrtl.ko",
    "drivers/platform/chrome/cros_hps_i2c.ko",
    "drivers/hid/hid-logitech-dj.ko",
    "sound/core/seq/snd-seq-midi-event.ko",
    "drivers/net/usb/rtl8150.ko",
    "net/802/p8022.ko",
    "net/netfilter/nf_conntrack_ftp.ko",
    "drivers/media/i2c/ov5695.ko",
    "drivers/media/v4l2-core/v4l2-mem2mem.ko",
    "drivers/hid/hid-microsoft.ko",
    "drivers/hid/hid-rmi.ko",
    "sound/core/snd-hwdep.ko",
    "drivers/net/wireless/legacy/rndis_wlan.ko",
    "sound/core/snd-rawmidi.ko",
    "drivers/net/phy/smsc.ko",
    "drivers/input/touchscreen/usbtouchscreen.ko",
    "drivers/media/i2c/dw9768.ko",
    "drivers/net/usb/ax88179_178a.ko",
    "drivers/gpu/drm/panel/panel-ilitek-ili9882t.ko",
    "drivers/input/joydev.ko",
    "drivers/net/usb/dm9601.ko",
    "net/dns_resolver/dns_resolver.ko",
    "drivers/net/veth.ko",
    "drivers/hid/hid-apple.ko",
    "drivers/bluetooth/hci_vhci.ko",
    "sound/soc/sof/mediatek/mt8186/snd-sof-mt8186.ko",
    "drivers/input/misc/uinput.ko",
    "sound/core/seq/snd-seq-dummy.ko",
    "sound/soc/sof/xtensa/snd-sof-xtensa-dsp.ko",
    "drivers/input/serio/serio.ko",
    "sound/core/snd-hrtimer.ko",
    "drivers/net/wireless/ath/ath.ko",
    "lib/crypto/libchacha20poly1305.ko",
    "drivers/usb/gadget/function/usb_f_mass_storage.ko",
    "drivers/iio/light/cros_ec_light_prox.ko",
    "net/sched/sch_tbf.ko",
    "crypto/async_tx/async_tx.ko",
    "net/wireless/cfg80211.ko",
    "drivers/md/dm-integrity.ko",
    "drivers/media/common/videobuf2/videobuf2-memops.ko",
    "drivers/net/usb/pegasus.ko",
    "fs/nls/nls_utf8.ko",
    "drivers/media/platform/mediatek/jpeg/mtk-jpeg-enc-hw.ko",
    "drivers/hid/hid-logitech.ko",
    "drivers/bluetooth/btqca.ko",
    "drivers/iio/common/cros_ec_sensors/cros_ec_sensors_sync.ko",
    "drivers/media/common/videobuf2/videobuf2-dma-contig.ko",
    "drivers/media/i2c/ov8856.ko",
    "drivers/input/serio/serport.ko",
    "sound/soc/sof/mediatek/mtk-adsp-common.ko",
    "drivers/media/v4l2-core/v4l2-vp9.ko",
    "drivers/usb/serial/ch341.ko",
    "lib/test_module.ko",
    "drivers/iio/buffer/industrialio-triggered-buffer.ko",
    "drivers/net/usb/asix.ko",
    "net/llc/llc.ko",
    "drivers/media/platform/mediatek/jpeg/mtk-jpeg-dec-hw.ko",
    "net/sched/sch_codel.ko",
    "drivers/net/usb/usbnet.ko",
    "arch/arm64/lib/xor-neon.ko",
    "sound/soc/mediatek/mt8173/mt8173-afe-pcm.ko",
    "drivers/rpmsg/mtk_rpmsg.ko",
    "sound/soc/codecs/snd-soc-rt5677-spi.ko",
    "drivers/input/joystick/xpad.ko",
    "crypto/zstd.ko",
    "net/ipv6/ah6.ko",
    "crypto/ecc.ko",
    "drivers/net/wireless/ath/ath10k/ath10k_core.ko",
    "drivers/iio/trigger/iio-trig-sysfs.ko",
    "drivers/iio/common/cros_ec_sensors/cros_ec_activity.ko",
    "drivers/hid/hid-holtek-mouse.ko",
    "drivers/hid/hid-wiimote.ko",
    "net/sched/cls_u32.ko",
    "kernel/time/test_udelay.ko",
    "lib/lz4/lz4hc_compress.ko",
    "drivers/media/i2c/ov02a10.ko",
    "drivers/hid/hid-lg-g15.ko",
    "drivers/bluetooth/btmtk.ko",
    "sound/soc/mediatek/mt8173/mt8173-rt5650.ko",
    "net/bluetooth/hidp/hidp.ko",
    "sound/soc/sof/snd-sof.ko",
    "drivers/iio/industrialio-sw-trigger.ko",
    "drivers/net/wireless/realtek/rtw88/rtw88_8822be.ko",
    "drivers/hid/hid-vivaldi-common.ko",
    "drivers/net/wireguard/wireguard.ko",
    "drivers/hid/hid-kensington.ko",
    "drivers/misc/vcpu_stall_detector.ko",
    "sound/soc/codecs/snd-soc-rt5514.ko",
    "drivers/net/wireless/mediatek/mt76/mt76-sdio.ko",
    "drivers/media/common/videobuf2/videobuf2-v4l2.ko",
    "sound/soc/codecs/snd-soc-tas2781-fmwlib.ko",
    "fs/nfs/nfsv4.ko",
    "drivers/usb/serial/qcserial.ko",
    "drivers/usb/serial/oti6858.ko",
    "drivers/net/wireless/ralink/rt2x00/rt2x00usb.ko",
    "drivers/hid/hid-holtek-kbd.ko",
    "drivers/usb/serial/usb_wwan.ko",
    "drivers/hid/hid-led.ko",
    "arch/arm64/crypto/poly1305-neon.ko",
    "drivers/net/usb/cdc_mbim.ko",
    "net/sched/sch_htb.ko",
    "drivers/net/wireless/mediatek/mt76/mt76-connac-lib.ko",
    "lib/crc7.ko",
    "drivers/usb/gadget/libcomposite.ko",
    "sound/core/seq/snd-seq.ko",
    "fs/nfs/nfs.ko",
    "fs/exfat/exfat.ko",
    "net/netfilter/nf_conntrack_tftp.ko",
    "lib/crypto/libcurve25519.ko",
    "drivers/platform/chrome/cros-ec-sensorhub.ko",
    "net/mac80211/mac80211.ko",
    "drivers/hid/hid-chicony.ko",
    "net/sched/sch_ingress.ko",
    "drivers/bluetooth/btmrvl_sdio.ko",
    "drivers/iio/proximity/sx_common.ko",
    "drivers/hid/hid-quickstep.ko",
    "drivers/net/wireless/realtek/rtw88/rtw88_8822ce.ko",
    "fs/isofs/isofs.ko",
    "drivers/media/usb/uvc/uvcvideo.ko",

]

_X86_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/ptp/ptp_kvm.ko",
]

_X86_64_GKI_MODULES_LIST = [
    # keep sorted
    "drivers/acpi/fan.ko",
    "drivers/powercap/intel_rapl_common.ko",
    "drivers/ptp/ptp_kvm.ko",
    "drivers/thermal/intel/int340x_thermal/acpi_thermal_rel.ko",
    "drivers/thermal/intel/int340x_thermal/int3400_thermal.ko",
    "drivers/thermal/intel/int340x_thermal/int3401_thermal.ko",
    "drivers/thermal/intel/int340x_thermal/int3402_thermal.ko",
    "drivers/thermal/intel/int340x_thermal/int3403_thermal.ko",
    "drivers/thermal/intel/int340x_thermal/int340x_thermal_zone.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_device.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_device_pci.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_device_pci_legacy.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_mbox.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_rapl.ko",
    "drivers/thermal/intel/int340x_thermal/processor_thermal_rfim.ko",
    "drivers/thermal/intel/intel_soc_dts_iosf.ko",
    "drivers/thermal/intel/intel_soc_dts_thermal.ko",
    "drivers/thunderbolt/thunderbolt.ko",
]

# buildifier: disable=unnamed-macro
def get_gki_modules_list(arch = None):
    """ Provides the list of GKI modules.

    Args:
      arch: One of [arm, arm64, i386, x86_64].

    Returns:
      The list of GKI modules for the given |arch|.
    """
    gki_modules_list = [] + _COMMON_GKI_MODULES_LIST
    if arch == "arm":
        gki_modules_list += _ARM_GKI_MODULES_LIST
    elif arch == "arm64":
        gki_modules_list += _ARM64_GKI_MODULES_LIST
    elif arch == "i386":
        gki_modules_list += _X86_GKI_MODULES_LIST
    elif arch == "x86_64":
        gki_modules_list += _X86_64_GKI_MODULES_LIST
    else:
        fail("{}: arch {} not supported. Use one of [arm, arm64, i386, x86_64]".format(
            str(native.package_relative_label(":x")).removesuffix(":x"),
            arch,
        ))

    return gki_modules_list

_KUNIT_FRAMEWORK_MODULES = [
    "lib/kunit/kunit.ko",
]

# Common Kunit test modules
_KUNIT_COMMON_MODULES_LIST = [
    # keep sorted
    "drivers/base/regmap/regmap-kunit.ko",
    "drivers/base/regmap/regmap-ram.ko",
#    "drivers/base/regmap/regmap-raw-ram.ko",
#    "drivers/hid/hid-uclogic-test.ko",
    "drivers/iio/test/iio-test-format.ko",
    "drivers/input/tests/input_test.ko",
    "drivers/rtc/lib_test.ko",
    "fs/ext4/ext4-inode-test.ko",
    "fs/fat/fat_test.ko",
    "kernel/time/time_test.ko",
    "lib/kunit/kunit-example-test.ko",
    "lib/kunit/kunit-test.ko",
    # "mm/kfence/kfence_test.ko",
#    "net/core/dev_addr_lists_test.ko",
    "sound/soc/soc-topology-test.ko",
    "sound/soc/soc-utils-test.ko",
]

# KUnit test module for arm64 only
_KUNIT_CLK_MODULES_LIST = [
    "drivers/clk/clk-gate_test.ko",
    "drivers/clk/clk_test.ko",
]

# buildifier: disable=unnamed-macro
def get_kunit_modules_list(arch = None):
    """ Provides the list of GKI modules.

    Args:
      arch: One of [arm, arm64, i386, x86_64].

    Returns:
      The list of KUnit modules for the given |arch|.
    """
    kunit_modules_list = _KUNIT_FRAMEWORK_MODULES + _KUNIT_COMMON_MODULES_LIST
    if arch == "arm":
        kunit_modules_list += _KUNIT_CLK_MODULES_LIST
    elif arch == "arm64":
        kunit_modules_list += _KUNIT_CLK_MODULES_LIST
    elif arch == "i386":
        kunit_modules_list += []
    elif arch == "x86_64":
        kunit_modules_list += []
    else:
        fail("{}: arch {} not supported. Use one of [arm, arm64, i386, x86_64]".format(
            str(native.package_relative_label(":x")).removesuffix(":x"),
            arch,
        ))

    return kunit_modules_list

_COMMON_UNPROTECTED_MODULES_LIST = [
    "drivers/block/zram/zram.ko",
    "kernel/kheaders.ko",
    "mm/zsmalloc.ko",
]

# buildifier: disable=unnamed-macro
def get_gki_protected_modules_list(arch = None):
    all_gki_modules = get_gki_modules_list(arch) + get_kunit_modules_list(arch)
    unprotected_modules = _COMMON_UNPROTECTED_MODULES_LIST
    protected_modules = [mod for mod in all_gki_modules if mod not in unprotected_modules]
    return protected_modules
