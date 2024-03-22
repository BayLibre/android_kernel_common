/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMSCAN_H
#define _LINUX_VMSCAN_H

int __remove_mapping(struct address_space *mapping, struct folio *folio,
			    bool reclaimed, struct mem_cgroup *target_memcg);

void folio_putback_lru(struct folio *folio);

#endif

