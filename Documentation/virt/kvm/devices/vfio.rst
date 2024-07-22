.. SPDX-License-Identifier: GPL-2.0

===================
VFIO virtual device
===================

Device types supported:

  - KVM_DEV_TYPE_VFIO

Only one VFIO instance may be created per VM.  The created device
tracks VFIO files (group or device) in use by the VM and features
of those groups/devices important to the correctness and acceleration
of the VM.  As groups/devices are enabled and disabled for use by the
VM, KVM should be updated about their presence.  When registered with
KVM, a reference to the VFIO file is held by KVM.

Groups:
  KVM_DEV_VFIO_FILE
	alias: KVM_DEV_VFIO_GROUP
  KVM_DEV_VFIO_PVIOMMU

KVM_DEV_VFIO_FILE attributes:
  KVM_DEV_VFIO_FILE_ADD: Add a VFIO file (group/device) to VFIO-KVM device
	tracking

	kvm_device_attr.addr points to an int32_t file descriptor for the
	VFIO file.

  KVM_DEV_VFIO_FILE_DEL: Remove a VFIO file (group/device) from VFIO-KVM
	device tracking

	kvm_device_attr.addr points to an int32_t file descriptor for the
	VFIO file.

KVM_DEV_VFIO_GROUP (legacy kvm device group restricted to the handling of VFIO group fd):
  KVM_DEV_VFIO_GROUP_ADD: same as KVM_DEV_VFIO_FILE_ADD for group fd only

  KVM_DEV_VFIO_GROUP_DEL: same as KVM_DEV_VFIO_FILE_DEL for group fd only

  KVM_DEV_VFIO_GROUP_SET_SPAPR_TCE: attaches a guest visible TCE table
	allocated by sPAPR KVM.
	kvm_device_attr.addr points to a struct::

		struct kvm_vfio_spapr_tce {
			__s32	groupfd;
			__s32	tablefd;
		};

	where:

	- @groupfd is a file descriptor for a VFIO group;
	- @tablefd is a file descriptor for a TCE table allocated via
	  KVM_CREATE_SPAPR_TCE.

The FILE/GROUP_ADD operation above should be invoked prior to accessing the
device file descriptor via VFIO_GROUP_GET_DEVICE_FD in order to support
drivers which require a kvm pointer to be set in their .open_device()
callback.  It is the same for device file descriptor via character device
open which gets device access via VFIO_DEVICE_BIND_IOMMUFD.  For such file
descriptors, FILE_ADD should be invoked before VFIO_DEVICE_BIND_IOMMUFD
to support the drivers mentioned in prior sentence as well.

KVM_DEV_VFIO_PVIOMMU attributes:
  KVM_DEV_VFIO_PVIOMMU_ATTACH: Create and attach a pvIOMMU instance to the
    KVM VM associated with this device, returns a file descriptor "pviommufd"
    for this IOMMU which support some IOCTLs.

  KVM_DEV_VFIO_PVIOMMU_GET_INFO: Retrieve information about IOMMus for a VFIO
    devicefd, using the following struct:

	  struct kvm_vfio_iommu_info {
	      __s32 device_fd;
	      __u32 nr_sids;
	  };
	  where kvm_vfio_iommu_info.device_fd the input VFIO device ID, and
	  kvm_vfio_iommu_info.nr_sids is the number stream IDs(endpoint) for
	  this device, this similar to VFIO_DEVICE_GET_IRQ_INFO which return number
	  of irqs for a VFIO device.

  The rest of the IOCTLs are used to configure the pvIOMMU are part of
  the pviommu fd returned from the attach operation:
    KVM_PVIOMMU_SET_CONFIG: Configure pvIOMMU for an endpoint for a device,
      where the input struct:
        struct kvm_vfio_iommu_config {
        int device_fd;
        int sid_idx;
        int vsid;
        };
      kvm_vfio_iommu_config.device_fd is the VFIO devicefd,
      kvm_vfio_iommu_config.sid_idx is a valid index based on number of device
      from KVM_DEV_VFIO_PVIOMMU_GET_INFO.
      And kvm_vfio_iommu_config.vsid is the virtual sid seen by the guest,
      It is the VMM responsibility to describe this to the guest, the pvIOMMU
      would translate an IOMMU request with this vsid to the physical SID of
	  the device for the index specified.
      This similar to VFIO IOCTL: VFIO_DEVICE_SET_IRQS but for the IOMMU.

	KVM_PVIOMMU_SET_FAULT_IRQ: Configure page fauls IRQ, uses the struct:

	    struct kvm_vfio_iommu_faultfd {
	        int eventfd;
        };

	  Where eventfd, is an eventfd create by userspace, from this IOCTL, all
	  device currently part of this pvIOMMU instance would be configured to
	  use this eventfd to trigger interrupts upon page faults, the fault
	  information is not exposed to userspace but to the guest directly
	  through and HVC.
	  The guest can be configured to receive this IRQ with KVM_IRQFD, this is
	  an rising edge triggered interrupt so no resampling needed.
