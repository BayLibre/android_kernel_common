MTE Dynamic Carveout
====================

Requirements
============

To enable MTE dynamic carveout:

- CONFIG_ARM64_TAG_STORAGE=y
- system_supports_mte() returns true
- kasan_hw_tags_enabled() returns false
- correct DTB node (for the specification, see commit "arm64: mte: Reserve tag
  storage memory")

Check dmesg for the message "MTE tag storage enabled".

Implementation
==============

Tag storage pages are exposed to the page allocator via a new allocation
policy, MIGRATE_METADATA. This functions similarly to MIGRATE_CMA, while being
even more restrictive about when an allocation can use pages from
MIGRATE_METADATA:

- __GFP_TAGGED not set
- __GFP_NO_MIGRATE_METADATA not set

Initialization is split into two phases:

- DTB node scanned very early in the boot process, and the memory reserved
  from memblock (the pages will end up as PG_reserved in the page allocator).
- Pages moved to MIGRATE_METADATA as an initcall.

The reason for this is to allow MTE and KASAN with MTE to be initialized
before exposing the tag storage pages to the page allocator.

If !system_support_mte(), tag storage pages will be moved to MIGRATE_MOVABLE
instead.

When a tagged page is allocated, the corresponding tag storage is reserved
(via alloc_contig_range(), similar to CMA); if the tag storage page cannot be
allocated (for example, temporary pin), the the associated tagged page will be
freed (to the tail of the free_list to avoid being allocated immediately
after), then page allocator will allocate a new page and the process will be
repeated.

When all the associated tagged pages are freed, the tag storag page is freed
back to the page allocator. This can be done in an atomic context.

mprotect(VM_MTE) works in two steps:

- Migrate all present tag storage pages out of the VMA **before** VM_MTE is
  set.  This is to prevent a deadlock-type situation where the page allocated
  to replace the tag storage page has the associated tag page on the list of
  tag storage pages that are in the process of being migrated out (worst case
  scenario: the associated  tag storage page is the page that is being replaced
  by the new page)
- Reserve tag storage for all present pages **after** the VM_MTE flag has been
  set; if reserving that storage fails, then the pages are migrated out.

This is prone to races (see below).

Known issues and limitations
============================

I'll preface this by saying that there **will** be bugs.

Known issues:

- MTE enabled VM is not working. That's because KVM doesn't require the VMA
  backing the guest memory to be VM_MTE.

- Swap hasn't been tested. A quick look at the code seems to suggest that it
  might work - read_swap_cache_async uses vma_alloc_folio for the new page,
  but that is based on the assumption that it gets the correct VMA as a
  parameter, and not NULL/anonymous vma. Needs investigation.

- Migration can race with mprotect(PROT_MTE): the destination page can be
  allocated before the VM_MTE flag has been set (which means no tag storage
  reserved), and then after VM_MTE has been set the migration pte removed and
  the page mapped without associated tag storage.

- shmem only very lightly tested.

- Support for a block size larger than a PAGE_SIZE not added.
