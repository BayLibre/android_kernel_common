.. SPDX-License-Identifier: GPL-2.0

getfreq support for arm/arm64
=============================

Getfreq support is used to share frequency information from the host to guests.

* ARM_SMCCC_VENDOR_HYP_KVM_GET_CPUFREQ_FUNC_ID: 0x86000002

This hypercall uses the SMC32/HVC32 calling convention:

ARM_SMCCC_VENDOR_HYP_KVM_GET_CPUFREQ_FUNC_ID
    ==============    ========    =====================================
    Function ID:      (uint32)    0x86000002
    Return Values:    (int32)     NOT_SUPPORTED(-1) on error, or
                      (uint32)    Frequency of current CPU that the
                                  hypercall is being called from.
    Endianness:                   No Restrictions.
    ==============    ========    =====================================
