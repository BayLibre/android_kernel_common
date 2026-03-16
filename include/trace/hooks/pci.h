<<<<<<< HEAD   (f8aa98c56fed7dab94b8480e9a84c5b9c41c6d40 ANDROID: GKI: update symbol list for xiaomi)
||||||| BASE   (18ca40f8c454986a4e52dd4ffcc9c2c9582fd718 ANDROID: ABI: Update pixel symbol list)
/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM pci
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_PCI_VH_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_PCI_VH_H
#include <trace/hooks/vendor_hooks.h>

struct pci_dev;
typedef int __bitwise pci_power_t;

DECLARE_HOOK(android_vh_platform_pci_power_manageable,
		TP_PROTO(struct pci_dev *dev, bool *manageable),
		TP_ARGS(dev, manageable));
DECLARE_HOOK(android_vh_platform_pci_set_power_state,
		TP_PROTO(struct pci_dev *dev, pci_power_t t, int *ret),
		TP_ARGS(dev, t, ret));
DECLARE_HOOK(android_vh_platform_pci_get_power_state,
		TP_PROTO(struct pci_dev *dev, pci_power_t *state),
		TP_ARGS(dev, state));
DECLARE_HOOK(android_vh_platform_pci_choose_state,
		TP_PROTO(struct pci_dev *dev, pci_power_t *state),
		TP_ARGS(dev, state));

#endif /* _TRACE_HOOK_PCI_VH_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
=======
/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM pci
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_PCI_VH_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_PCI_VH_H
#include <trace/hooks/vendor_hooks.h>

struct pci_dev;
typedef int __bitwise pci_power_t;

DECLARE_HOOK(android_vh_platform_pci_power_manageable,
		TP_PROTO(struct pci_dev *dev, bool *manageable),
		TP_ARGS(dev, manageable));
DECLARE_HOOK(android_vh_platform_pci_set_power_state,
		TP_PROTO(struct pci_dev *dev, pci_power_t t, int *ret),
		TP_ARGS(dev, t, ret));
DECLARE_HOOK(android_vh_platform_pci_get_power_state,
		TP_PROTO(struct pci_dev *dev, pci_power_t *state),
		TP_ARGS(dev, state));
DECLARE_HOOK(android_vh_platform_pci_choose_state,
		TP_PROTO(struct pci_dev *dev, pci_power_t *state),
		TP_ARGS(dev, state));
DECLARE_HOOK(android_vh_pci_pm_verify_state,
		TP_PROTO(int *state_ret, pci_power_t *state),
		TP_ARGS(state_ret, state));

#endif /* _TRACE_HOOK_PCI_VH_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
>>>>>>> CHANGE (fc86a6fc0647e6f534e7c2f2ea51e3f3960be4e6 ANDROID: PCI: Add vendor hook for power-up state verificatio)
