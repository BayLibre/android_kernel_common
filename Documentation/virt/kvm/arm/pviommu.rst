.. SPDX-License-Identifier: GPL-2.0

==============
KVM pvIOMMU
==============

pvIOMMU is a paravirtual IOMMU interface exposed to guests.

This is mainly needed for protected virtual machines (pVM), and device
assignment, as the host can't map memory for the guest kernel in the IOMMU
(as it is not trusted).

So the host kernel would create a pvIOMMU device and attach it to a guest VM
while providing virtual (pvIOMMU, virtual SID(vsid)) mapping to
the physical device (IOMMU, SID) to the hypervisor to translate guest requests.

However, this interface can be used for both protected and non-protected
virtual machines.

The interface as described below, mainly follows the Linux IOMMU ops (
{attach, detach}_dev, {alloc, free}_domain, {map, unmap}_pages) which
makes the guest driver trivial to implement.

pvIOMMU ID, is chosen by the host and described to the guest in a platform
specific way, in pKVM reference implementation this is done through the device
tree.

This relies on a set of hypercalls defined in the KVM-specific range,
using the HVC64 calling convention.

``ARM_SMCCC_KVM_FUNC_IOMMU_VERSION``
--------------------------------------

Return the version of pvIOMMU hypervisor interface.

+---------------------+-------------------------------------------------------------+
| Presence:           | Optional.                                                   |
+---------------------+-------------------------------------------------------------+
| Calling convention: | HVC64                                                       |
+---------------------+----------+--------------------------------------------------+
| Function ID:        | (uint32) | 0xC60000023                                      |
+---------------------+----------+----+---------------------------------------------+
| Arguments:          | (uint64) | R1 | Reserved / Must be zero                     |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R2 | Reserved / Must be zero                     |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R3 | Reserved / Must be zero                     |
+---------------------+----------+----+---------------------------------------------+
| Return Values:      | (int64)  | R0 | ``SUCCESS (0)``                             |
|                     +----------+----+---------------------------------------------+
|                     | (uint32) | R1 | Supported version of the interface          |
+---------------------+----------+----+---------------------------------------------+


``ARM_SMCCC_KVM_FUNC_IOMMU_GET_FEATURE``
--------------------------------------

Probe features from a pvIOMMU.

Features ID (passed in R1):
- ``PVIOMMU_REQUEST_FEATURE_PGSZ_BITMAP``(0x1): Return the supported page size bitmap in R1.

+---------------------+-------------------------------------------------------------+
| Presence:           | Optional.                                                   |
+---------------------+-------------------------------------------------------------+
| Calling convention: | HVC64                                                       |
+---------------------+-------ARM_SMCCC_KVM_PVIOMMU_---+--------------------------------------------------+
| Function ID:        | (uint32) | 0xC60000024                                      |
+---------------------+----------+----+---------------------------------------------+
| Arguments:          | (uint64) | R1 | pvIOMMU ID                                  |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R2 | Feature ID                                  |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R3 | Reserved / Must be zero                     |
+---------------------+----------+----+---------------------------------------------+
| Return Values:      | (int64)  | R0 | ``SUCCESS (0)``                             |
|                     |          |    +---------------------------------------------+
|                     |          |    | ``INVALID_PARAMETER (-3)``                  |
|                     +----------+----+---------------------------------------------+
|                     | (uint32) | R1 | Optional return based on feature            |
+---------------------+----------+----+---------------------------------------------+

``ARM_SMCCC_KVM_FUNC_IOMMU_ALLOC_DOMAIN``
--------------------------------------

Allocate a domain, where domain is similar to a Linux which is an translation regime
(basically, page table).

+---------------------+-------------------------------------------------------------+
| Presence:           | Optional.                                                   |
+---------------------+-------------------------------------------------------------+
| Calling convention: | HVC64                                                       |
+---------------------+----------+--------------------------------------------------+
| Function ID:        | (uint32) | 0xC60000025                                      |
+---------------------+----------+----+---------------------------------------------+
| Arguments:          | (uint64) | R1 | Reserved / Must be zero                     |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R2 | Reserved / Must be zero                     |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R3 | Reserved / Must be zero                     |
+---------------------+----------+----+---------------------------------------------+
| Return Values:      | (int64)  | R0 | ``SUCCESS (0)``                             |
|                     |          |    +---------------------------------------------+
|                     |          |    | ``INVALID_PARAMETER (-3)``                  |
|                     +----------+----+---------------------------------------------+
|                     | (uint32) | R1 | domain ID in case of ``SUCCESS``            |
+---------------------+----------+----+---------------------------------------------+

``ARM_SMCCC_KVM_FUNC_IOMMU_FREE_DOMAIN``
--------------------------------------

Free a domain, previously allocated from ``ARM_SMCCC_KVM_FUNC_IOMMU_ALLOC_DOMAIN``

+---------------------+-------------------------------------------------------------+
| Presence:           | Optional.                                                   |
+---------------------+-------------------------------------------------------------+
| Calling convention: | HVC64                                                       |
+---------------------+----------+--------------------------------------------------+
| Function ID:        | (uint32) | 0xC60000026                                      |
+---------------------+----------+----+---------------------------------------------+
| Arguments:          | (uint64) | R1 | Domain ID                                   |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R2 | Reserved / Must be zero                     |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R3 | Reserved / Must be zero                     |
+---------------------+----------+----+---------------------------------------------+
| Return Values:      | (int64)  | R0 | ``SUCCESS (0)``                             |
|                     |          |    +---------------------------------------------+
|                     |          |    | ``INVALID_PARAMETER (-3)``                  |
+---------------------+----------+----+---------------------------------------------+

``ARM_SMCCC_KVM_FUNC_IOMMU_ATTACH_DEV``
--------------------------------------

Attach a device to a domain, previously allocated from ``ARM_SMCCC_KVM_FUNC_IOMMU_ALLOC_DOMAIN``

+---------------------+-------------------------------------------------------------+
| Presence:           | Optional.                                                   |
+---------------------+-------------------------------------------------------------+
| Calling convention: | HVC64                                                       |
+---------------------+----------+--------------------------------------------------+
| Function ID:        | (uint32) | 0xC60000021                                      |
+---------------------+----------+----+---------------------------------------------+
| Arguments:          | (uint64) | R1 | pvIOMMU ID                                   |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R2 | vSID (virtual SID)                          |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R3 | PASID (or 0)                                |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R4 | Domain ID                                   |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R5 | PASID_bits, the pasid space.                |
+---------------------+----------+----+---------------------------------------------+
| Return Values:      | (int64)  | R0 | ``SUCCESS (0)``                             |
|                     |          |    +---------------------------------------------+
|                     |          |    | ``INVALID_PARAMETER (-3)``                  |
+---------------------+----------+----+---------------------------------------------+

``ARM_SMCCC_KVM_FUNC_IOMMU_DETACH_DEV``
--------------------------------------

Attach a device from a domain, previously attached with ``ARM_SMCCC_KVM_FUNC_IOMMU_ATTACH_DEV``

+---------------------+-------------------------------------------------------------+
| Presence:           | Optional.                                                   |
+---------------------+-------------------------------------------------------------+
| Calling convention: | HVC64                                                       |
+---------------------+----------+--------------------------------------------------+
| Function ID:        | (uint32) | 0xC60000022                                      |
+---------------------+----------+----+---------------------------------------------+
| Arguments:          | (uint64) | R1 | pvIOMMU ID                                  |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R2 | vSID (virtual SID)                          |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R3 | PASID (or 0)                                |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R4 | Domain ID                                   |
+---------------------+----------+----+---------------------------------------------+
| Return Values:      | (int64)  | R0 | ``SUCCESS (0)``                             |
|                     |          |    +---------------------------------------------+
|                     |          |    | ``INVALID_PARAMETER (-3)``                  |
+---------------------+----------+----+---------------------------------------------+

``ARM_SMCCC_KVM_FUNC_IOMMU_MAP``
--------------------------------------

Map pages in a domain.

Page size(R4) must be a valid page as described by the page size bitmap.

prot(R6) encoded as a bitmask as follows:
- ARM_SMCCC_KVM_PVIOMMU_READ		(1 << 0)
- ARM_SMCCC_KVM_PVIOMMU_WRITE		(1 << 1)
- ARM_SMCCC_KVM_PVIOMMU_CACHE		(1 << 2)
- ARM_SMCCC_KVM_PVIOMMU_NOEXEC		(1 << 3)
- ARM_SMCCC_KVM_PVIOMMU_MMIO		(1 << 4)
- ARM_SMCCC_KVM_PVIOMMU_PRIV		(1 << 5)

+---------------------+-------------------------------------------------------------+
| Presence:           | Optional.                                                   |
+---------------------+-------------------------------------------------------------+
| Calling convention: | HVC64                                                       |
+---------------------+----------+--------------------------------------------------+
| Function ID:        | (uint32) | 0xC60000020                                      |
+---------------------+----------+----+---------------------------------------------+
| Arguments:          | (uint64) | R1 | Domain ID                                   |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R2 | IOVA                                        |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R3 | IPA                                         |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R4 | Page size                                   |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R5 | Page count                                  |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R6 | Protection                                  |
+---------------------+----------+----+---------------------------------------------+
| Return Values:      | (int64)  | R0 | ``SUCCESS (0)``                             |
|                     |          |    +---------------------------------------------+
|                     |          |    | ``INVALID_PARAMETER (-3)``                  |
+---------------------+----------+----+---------------------------------------------+
|                     | (uint64) | R1 | Number of mapped pages                      |
+---------------------+----------+----+---------------------------------------------+

``ARM_SMCCC_KVM_FUNC_IOMMU_UNMAP``
--------------------------------------

Unmap pages from a domain.

+---------------------+-------------------------------------------------------------+
| Presence:           | Optional.                                                   |
+---------------------+-------------------------------------------------------------+
| Calling convention: | HVC64                                                       |
+---------------------+----------+--------------------------------------------------+
| Function ID:        | (uint32) | 0xC60000021                                      |
+---------------------+----------+----+---------------------------------------------+
| Arguments:          | (uint64) | R1 | Domain ID                                   |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R2 | IOVA                                        |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R3 | Page size                                   |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R4 | Page count                                  |
+---------------------+----------+----+---------------------------------------------+
| Return Values:      | (int64)  | R0 | ``SUCCESS (0)``                             |
|                     |          |    +---------------------------------------------+
|                     |          |    | ``INVALID_PARAMETER (-3)``                  |
+---------------------+----------+----+---------------------------------------------+
|                     | (uint64) | R1 | Number of unmapped pages                    |
+---------------------+----------+----+---------------------------------------------+

``ARM_SMCCC_KVM_FUNC_DEV_REQ_DMA``
--------------------------------------

Verify a device IOMMUs matches what the hosts describe in the
device tree for a phyiscal device passthrough to a protected
virtual machine.

Called per IOMMU endpoint (pvIOMMU ID + vSID).

Returns a token(128 bit) that can be used to verify the resource
matching a trusted device containing the same token, which passed
through a platform specific way.

Must be called before any IOMMU access for protected virtual machines.

Ideally called from protected vm firmware.

+---------------------+-------------------------------------------------------------+
| Presence:           | Optional.                                                   |
+---------------------+-------------------------------------------------------------+
| Calling convention: | HVC64                                                       |
+---------------------+----------+--------------------------------------------------+
| Function ID:        | (uint32) | 0xC60000027                                      |
+---------------------+----------+----+---------------------------------------------+
| Arguments:          | (uint64) | R1 | pvIOMMU ID                                  |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R2 | vSID                                        |
+---------------------+----------+----+---------------------------------------------+
| Return Values:      | (int64)  | R0 | ``SUCCESS (0)``                             |
|                     |          |    +---------------------------------------------+
|                     |          |    | ``INVALID_PARAMETER (-3)``                  |
+---------------------+----------+----+---------------------------------------------+
|                     | (uint64) | R1 | Token1                                      |
+---------------------+----------+----+---------------------------------------------+
|                     | (uint64) | R2 | Token2                                      |
+---------------------+----------+----+---------------------------------------------+
