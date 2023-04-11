.. SPDX-License-Identifier: GPL-2.0

======================
GenieZone Introduction
======================


Overview
========
GenieZone is MediaTek proprietary hypervisor solution, and it is running in EL2
stand alone as a type-I hypervisor. It is a pure EL2 implementation which
implies it does not rely any specific host VM, and this behavior improves
GenieZone's security as it limits its interface.

To enable guest VMs running, a driver (gzvm) is provided for VMM (virtual
machine monitor) to operate. Currently, the gzvm driver supports only crosvm.


Supported Architecture
======================
GenieZone now only supports MediaTek arm64 SoC.


Platform Virtualization
=======================
We leverages arm64's timer virtualization and gic virtualization for timer and
interrupt controller.


Device Virtualizaton
====================
We adopt VMM's virtio devices emulations by passing io trap to VMM, and virtio
is a well-known and widely used virtual device implementation.

