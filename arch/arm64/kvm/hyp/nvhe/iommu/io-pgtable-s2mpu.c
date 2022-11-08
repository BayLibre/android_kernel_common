// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 - Google LLC
 */

#include <asm/io-pgtable-s2mpu.h>


#define GRAN_BYTE(gran)	((gran << V9_MPT_PROT_BITS) |  (gran))
#define GRAN_HWORD(gran)	((GRAN_BYTE(gran) << 8) | (GRAN_BYTE(gran)))
#define GRAN_WORD(gran)	(((u32)(GRAN_HWORD(gran) << 16) | (GRAN_HWORD(gran))))
#define GRAN_DWORD(gran)	((u64)((u64)GRAN_WORD(gran) << 32) | (u64)(GRAN_WORD(gran)))

#define SMPT_PG_NUM_TO_BYTE(x)  ((x) / SMPT_GRAN / SMPT_ELEMS_PER_BYTE(config_prot_bits))
#define BYTE_TO_SMPT_INDEX(x) ((x) / SMPT_WORD_BYTE_RANGE(config_prot_bits))


//page table ops can be configure only for one version at runtime
//this is a constraint
//this variables will hold version specific data set a run time init, to avoid
//having duplicate code or unnessery check during operations
static u32 config_prot_bits;
static u32 config_access_shift;
static const u64 *config_lut_prot;
static u32 config_gran_mask;
static u32 this_version;
#ifdef CONFIG_KVM_S2MPU_MERGE_PTE
static bool can_merge_pages;
#endif //CONFIG_KVM_S2MPU_MERGE_PTE

//page table entries for different protection look up table
//granuilty is compile time config, so we can do this also for
//this array without having duplicate arrays
static const u64 v9_mpt_prot_doubleword[] = {
	[MPT_PROT_NONE] = 0x0000000000000000,
	[MPT_PROT_R]    = 0x4444444444444444,
	[MPT_PROT_W]	= 0x8888888888888888,
	[MPT_PROT_RW]   = 0xcccccccccccccccc,
};
static const u64 def_mpt_prot_doubleword[] = {
	[MPT_PROT_NONE] = 0x0000000000000000,
	[MPT_PROT_R]    = 0x5555555555555555,
	[MPT_PROT_W]	= 0xffffffffffffffff,
	[MPT_PROT_RW]   = 0xffffffffffffffff,
};
static inline int pte_from_addr_smpt(u32 *smpt, u64 addr)
{
	u32 word_idx, idx, pte, val;

	word_idx = BYTE_TO_SMPT_INDEX(addr);
	val = READ_ONCE(smpt[word_idx]);
	idx  = (addr / SMPT_GRAN) % SMPT_ELEMS_PER_WORD(config_prot_bits);

	pte  = (val >> (idx * config_prot_bits)) & ((1 << config_prot_bits)-1);
	return pte;
}
static inline int prot_from_addr_smpt(u32 *smpt, u64 addr)
{
	int pte = pte_from_addr_smpt(smpt, addr);

	return (pte >> config_access_shift);

}
/* Set protection bits of SMPT in a given range without using memset. */
static inline void set_smpt_range_slow(u32 *smpt, size_t start_gb_byte,
					 size_t end_gb_byte, enum mpt_prot prot)
{
	size_t i, start_word_byte, end_word_byte, word_idx, first_elem, last_elem;
	u32 val;

	/* Iterate over u32 words. */
	start_word_byte = start_gb_byte;
	while (start_word_byte < end_gb_byte) {
		/* Determine the range of bytes covered by this word. */
		word_idx = start_word_byte / SMPT_WORD_BYTE_RANGE(config_prot_bits);
		end_word_byte = min(
			ALIGN(start_word_byte + 1, SMPT_WORD_BYTE_RANGE(config_prot_bits)),
			end_gb_byte);

		/* Identify protection bit offsets within the word. */
		first_elem = (start_word_byte / SMPT_GRAN) % SMPT_ELEMS_PER_WORD(config_prot_bits);
		last_elem = ((end_word_byte - 1) / SMPT_GRAN) % SMPT_ELEMS_PER_WORD(config_prot_bits);

		/* Modify the corresponding word. */
		val = READ_ONCE(smpt[word_idx]);
		for (i = first_elem; i <= last_elem; i++) {
			val &= ~(MPT_PROT_MASK << (i * config_prot_bits));
			val |= prot << (i * config_prot_bits + config_access_shift);
		}
		WRITE_ONCE(smpt[word_idx], val);
		start_word_byte = end_word_byte;
	}
}
#ifdef CONFIG_KVM_S2MPU_MERGE_PTE
static void break_smpt_merging(u32 *smpt, size_t addr, enum mpt_prot new_prot)
{
	u32 gran, pte, prot;
	size_t break_start;
	char prot_byte;

	pte = pte_from_addr_smpt(smpt, addr);
	gran  = pte&SMPT_GRAN_MASK;
	prot = (pte>>config_access_shift) & MPT_PROT_MASK;
	/**
	 * if no change in protection don't  do anything
	 * this typically happens when buffer borders are expanded
	 */
	if (prot == new_prot)
		return;
	prot_byte = (char)config_lut_prot[prot];
	if (gran == L1ENTRY_ATTR_GRAN_4K)
		return;
	if (gran == L1ENTRY_ATTR_GRAN_64K) {
		break_start = ALIGN_DOWN(addr, SZ_64K);
		memset(&smpt[BYTE_TO_SMPT_INDEX(break_start)], prot_byte | GRAN_BYTE(L1ENTRY_ATTR_GRAN_4K), SMPT_PG_NUM_TO_BYTE(SZ_64K));
	} else if (gran == L1ENTRY_ATTR_GRAN_2M) {
		break_start = ALIGN_DOWN(addr, SZ_2M);
		memset(&smpt[BYTE_TO_SMPT_INDEX(break_start)], prot_byte | GRAN_BYTE(L1ENTRY_ATTR_GRAN_4K), SMPT_PG_NUM_TO_BYTE(SZ_2M));
	}
}
static void smpt_optimal_up_boundary(u32 *smpt, size_t *start_gb_byte, enum mpt_prot prot)
{
	size_t up_2mb, up_64k;
	s64 addr_i;
	int pte_prot;

	up_2mb = ALIGN_DOWN((*start_gb_byte), SZ_2M);
	up_64k = ALIGN_DOWN((*start_gb_byte), SZ_64K);
	/*can we go up until 2mb aligned? */
	for (addr_i = up_2mb ; addr_i < (*start_gb_byte) ; addr_i += SMPT_GRAN) {
		pte_prot = prot_from_addr_smpt(smpt, addr_i);
		if (pte_prot  != prot)
			break;
	}
	/*if yes good, adjust start and return*/
	if (addr_i == (*start_gb_byte)) {
		*start_gb_byte = up_2mb;
		return;
	}
	/*can we go up until 64kb aligned? */
	for (addr_i = up_64k ; addr_i < (*start_gb_byte) ; addr_i += SMPT_GRAN) {
		pte_prot = prot_from_addr_smpt(smpt, addr_i);
		if (pte_prot  != prot)
			break;
	}
	/*if yes good, adjust start and return*/
	if (addr_i == (*start_gb_byte)) {
		*start_gb_byte = up_64k;
		return;
	}
}
static void smpt_optimal_down_boundary(u32 *smpt, size_t *end_gb_byte, enum mpt_prot prot)
{
	size_t down_2mb, down_64k;
	s64 addr_i;
	int pte_prot;

	down_2mb = ALIGN((*end_gb_byte), SZ_2M);
	down_64k = ALIGN((*end_gb_byte), SZ_64K);
	for (addr_i = (*end_gb_byte) ; addr_i < down_2mb; addr_i += SMPT_GRAN) {
		pte_prot = prot_from_addr_smpt(smpt, addr_i);
		if (pte_prot  != prot)
			break;
	}
	if (addr_i == down_2mb) {
		*end_gb_byte = down_2mb;
		return;
	}
	for (addr_i = (*end_gb_byte) ; addr_i < down_64k ; addr_i += SMPT_GRAN) {
		pte_prot = prot_from_addr_smpt(smpt, addr_i);
		if (pte_prot  != prot)
			break;
	}

	if (addr_i == down_64k) {
		*end_gb_byte = down_64k;
		return;
	}
}

static void smpt_optimal_boundries(u32 *smpt, size_t *start_gb_byte, size_t *end_gb_byte, enum mpt_prot prot)
{
	size_t old_start = *start_gb_byte, old_end = *end_gb_byte;

	smpt_optimal_up_boundary(smpt, start_gb_byte, prot);
	smpt_optimal_down_boundary(smpt, end_gb_byte, prot);

	/**
	 * revert new expanded boundries if they don't add value, otherwise we was time resetting them
	 */
	if (IS_ALIGNED((*start_gb_byte), SZ_2M) && !IS_ALIGNED(old_start, SZ_2M) && (*start_gb_byte + SZ_2M > *end_gb_byte))
		*start_gb_byte = ALIGN_DOWN(old_start, SZ_64K);

	if (IS_ALIGNED((*start_gb_byte), SZ_64K) && !IS_ALIGNED(old_start, SZ_64K) && (*start_gb_byte + SZ_64K > *end_gb_byte))
		*start_gb_byte = old_start;

	if (IS_ALIGNED((*end_gb_byte), SZ_2M) && !IS_ALIGNED(old_end, SZ_2M) && (*start_gb_byte + SZ_2M > *end_gb_byte))
		*start_gb_byte = ALIGN(old_end, SZ_64K);

	if (IS_ALIGNED((*end_gb_byte), SZ_64K) && !IS_ALIGNED(old_end, SZ_64K) &&  (*start_gb_byte + SZ_64K > *end_gb_byte))
		*start_gb_byte = old_end;

	/*
	 * if blocks above us are not aligned to either 64KB or 2MB, we can be already part of block with different protection
	 * If so break this block
	 */
	break_smpt_merging(smpt, *start_gb_byte, prot);
	break_smpt_merging(smpt, *end_gb_byte-1, prot);
}

static inline void handle_from4k(u32 *smpt, size_t interlude_start,
				    size_t interlude_end, char prot_byte)
{

	if (interlude_start >= interlude_end)
		return;

	memset(&smpt[BYTE_TO_SMPT_INDEX(interlude_start)], prot_byte | GRAN_BYTE(L1ENTRY_ATTR_GRAN_4K), SMPT_PG_NUM_TO_BYTE(interlude_end-interlude_start));
}
static inline void handle_from64k(u32 *smpt, size_t interlude_start,
				    size_t interlude_end, char prot_byte){

	size_t  start_64k, end_64k;

	if (interlude_start >= interlude_end)
		return;

	start_64k = ALIGN(interlude_start, SZ_64K);
	end_64k = ALIGN_DOWN(interlude_end, SZ_64K);
	if (start_64k < end_64k) {
		memset(&smpt[BYTE_TO_SMPT_INDEX(start_64k)], prot_byte | GRAN_BYTE(L1ENTRY_ATTR_GRAN_64K), SMPT_PG_NUM_TO_BYTE(end_64k-start_64k));
		handle_from4k(smpt, interlude_start, start_64k, prot_byte);
		handle_from4k(smpt, end_64k, interlude_end, prot_byte);
	} else {
		handle_from4k(smpt, interlude_start, interlude_end, prot_byte);
	}
}

#endif //CONFIG_KVM_S2MPU_MERGE_PTE

/* Set protection bits of SMPT in a given range. */
static inline void set_smpt_range(u32 *smpt, size_t start_gb_byte,
				    size_t end_gb_byte, enum mpt_prot prot)
{
	size_t interlude_start, interlude_end, interlude_bytes, word_idx;
#ifdef CONFIG_KVM_S2MPU_MERGE_PTE
	size_t start_2mb, end_2mb;
#endif //CONFIG_KVM_S2MPU_MERGE_PTE
	char prot_byte = (char)config_lut_prot[prot];

	if (start_gb_byte >= end_gb_byte)
		return;

	/* Check if range spans at least one full u32 word. */
	interlude_start = ALIGN(start_gb_byte, SMPT_WORD_BYTE_RANGE(config_prot_bits));
	interlude_end = ALIGN_DOWN(end_gb_byte, SMPT_WORD_BYTE_RANGE(config_prot_bits));

	/* If not, fall back to editing bits in the given range.
	 * sets bit for PTEs that are in less than 32 bits (can't be done by memset)
	 */
	if (interlude_start >= interlude_end) {
		set_smpt_range_slow(smpt, start_gb_byte, end_gb_byte, prot);
		return;
	}

	/* Use bit-editing for prologue/epilogue, memset for interlude. */
	word_idx = BYTE_TO_SMPT_INDEX(interlude_start);
	interlude_bytes = SMPT_PG_NUM_TO_BYTE(interlude_end - interlude_start);

	/* This are pages in the start and at then end that are not part of full 32 bit SMPT word */
	set_smpt_range_slow(smpt, start_gb_byte, interlude_start, prot);
	set_smpt_range_slow(smpt, interlude_end, end_gb_byte, prot);

#ifdef CONFIG_KVM_S2MPU_MERGE_PTE
	if (!can_merge_pages) {
#endif //CONFIG_KVM_S2MPU_MERGE_PTE
		memset(&smpt[word_idx], prot_byte, interlude_bytes);
#ifdef CONFIG_KVM_S2MPU_MERGE_PTE
		return;
	}
	/* for HW were we can merge pages (V9), generally a request to map a buffer looks like this */
	/*
	 * we can have this generic case where one or more of these blocks can be missing
	 *       ______________________
	 *      |     4KB pages        | => start_gb_byte
	 *      |______________________|
	 *      |     64KB pages       | => start_64k
	 *      |______________________|
	 *      |     2MB pages        | => start_2mb
	 *      |______________________| => end_2mb
	 *      |     64KB pages       |
	 *      |______________________| =>end_64kb
	 *      |     4KB pages        |
	 *      |______________________| => end_gb_byte
	 */


	start_2mb = ALIGN(interlude_start, SZ_2M);
	end_2mb  =  ALIGN_DOWN(interlude_end, SZ_2M);
	if (start_2mb < end_2mb) {
		memset(&smpt[BYTE_TO_SMPT_INDEX(start_2mb)], prot_byte | GRAN_BYTE(L1ENTRY_ATTR_GRAN_2M), SMPT_PG_NUM_TO_BYTE(end_2mb-start_2mb));
		handle_from64k(smpt, interlude_start, start_2mb, prot_byte);
		handle_from64k(smpt, end_2mb, interlude_end, prot_byte);
	} else {
		handle_from64k(smpt, interlude_start, interlude_end, prot_byte);
	}
#endif //CONFIG_KVM_S2MPU_MERGE_PTE

}

/* Returns true if all SMPT protection bits match 'prot'. */
static bool is_smpt_uniform(u32 *smpt, enum mpt_prot prot)
{
	size_t i;
	u64 *doublewords = (u64 *)smpt;
	//we need to mask granulity bits when doing comparsion for access only
	u64 prot_mask =	config_lut_prot[MPT_PROT_RW];

	for (i = 0; i < SMPT_NUM_WORDS(config_prot_bits) / 2; i++) {
		if (doublewords[i] != (config_lut_prot[prot] & prot_mask))
			return false;
	}
	return true;
}
/*
 * Set protection bits of FMPT/SMPT in a given range.
 * Returns flags specifying whether L1/L2 changes need to be made visible
 * to the device.
 */
static void set_fmpt_range(struct fmpt *fmpt, size_t start_gb_byte,
				    size_t end_gb_byte, enum mpt_prot prot)
{
	if (start_gb_byte == 0 && end_gb_byte >= SZ_1G) {
		/* Update covers the entire GB region. */
		if (fmpt->gran_1g && fmpt->prot == prot) {
			fmpt->flags = 0;
			return;
		}

		fmpt->gran_1g = true;
		fmpt->prot = prot;
		fmpt->flags = MPT_UPDATE_L1;
		return;
	}
	if (fmpt->gran_1g) {
		/* GB region currently uses 1G mapping. */
		if (fmpt->prot == prot) {
			fmpt->flags = 0;
			return;
		}

		/*
		 * Range has different mapping than the rest of the GB.
		 * Convert to PAGE_SIZE mapping.
		 */
		fmpt->gran_1g = false;
		set_smpt_range(fmpt->smpt, 0, start_gb_byte, fmpt->prot);
		set_smpt_range(fmpt->smpt, start_gb_byte, end_gb_byte, prot);
		set_smpt_range(fmpt->smpt, end_gb_byte, SZ_1G, fmpt->prot);
		fmpt->flags = MPT_UPDATE_L1 | MPT_UPDATE_L2;
		return;
	}

#ifdef CONFIG_KVM_S2MPU_MERGE_PTE
	/* We might need to break old entries to get more optimal pages, or they can have different protection.
	 * break pages if needed, adjust boundries to get more optimal pages
	 */
	smpt_optimal_boundries(fmpt->smpt, &start_gb_byte, &end_gb_byte, prot);
#endif //CONFIG_KVM_S2MPU_MERGE_PTE
	/* GB region currently uses PAGE_SIZE mapping. */
	set_smpt_range(fmpt->smpt, start_gb_byte, end_gb_byte, prot);

	/* Check if the entire GB region has the same prot bits. */
	if (!is_smpt_uniform(fmpt->smpt, prot)) {
		fmpt->flags = MPT_UPDATE_L2;
		return;
	}

	fmpt->gran_1g = true;
	fmpt->prot = prot;
	fmpt->flags = MPT_UPDATE_L1;
}


static u32 pg_smpt_size(void)
{
	return SMPT_SIZE(config_prot_bits);
}
static void pg_write_l1entry_attr_l2(bool l2_enable, u32 gran, enum mpt_prot prot, u32 vid, u32 gb, void *dev_va)
{
	writel(config_gran_mask | (L1ENTRY_ATTR_L2TABLE_EN&l2_enable) | L1ENTRY_ATTR_1G(prot), dev_va + REG_NS_L1ENTRY_ATTR(vid, gb));
}


static void pg_init_with_prot(void *dev_va, enum mpt_prot prot)
{
	unsigned int gb, vid;

	for_each_gb_and_vid(gb, vid)
		pg_write_l1entry_attr_l2(0, 0, prot, vid, gb, dev_va);
}

static void pg_init_with_mpt(void *dev_va, struct mpt *mpt)
{
	unsigned int gb, vid;
	struct fmpt *fmpt;

	for_each_gb_and_vid(gb, vid) {
		fmpt = &mpt->fmpt[gb];
		writel(L1ENTRY_L2TABLE_ADDR(__hyp_pa(fmpt->smpt)), dev_va + REG_NS_L1ENTRY_L2TABLE_ADDR(vid, gb));
		pg_write_l1entry_attr_l2(!fmpt->gran_1g, SMPT_GRAN_ATTR, fmpt->prot, vid, gb, dev_va);
	}
}

static void pg_apply_range(void *dev_va, struct mpt *mpt, u32 first_gb, u32 last_gb)
{
	unsigned int gb, vid;
	struct fmpt *fmpt;

	for_each_gb_in_range(gb, first_gb, last_gb) {
		fmpt = &mpt->fmpt[gb];
		if (fmpt->flags & MPT_UPDATE_L1) {
			for_each_vid(vid)
				pg_write_l1entry_attr_l2(!fmpt->gran_1g, SMPT_GRAN_ATTR, fmpt->prot, vid, gb, dev_va);
		}
	}
}
static void	 pg_prepare_range(phys_addr_t  first_byte, phys_addr_t last_byte, struct mpt *mpt, enum mpt_prot prot)
{
	unsigned int first_gb = first_byte / SZ_1G;
	unsigned int last_gb = last_byte / SZ_1G;
	size_t start_gb_byte, end_gb_byte;
	unsigned int gb;
	struct fmpt *fmpt;

	for_each_gb_in_range(gb, first_gb, last_gb) {
		fmpt = &mpt->fmpt[gb];
		start_gb_byte = (gb == first_gb) ? first_byte % SZ_1G : 0;
		end_gb_byte = (gb == last_gb) ? (last_byte % SZ_1G) + 1 : SZ_1G;

		set_fmpt_range(fmpt, start_gb_byte, end_gb_byte, prot);

		if (fmpt->flags & MPT_UPDATE_L2)
			kvm_flush_dcache_to_poc(fmpt->smpt, SMPT_SIZE(config_prot_bits));
	}
}

static const struct s2mpu_pgtable_ops this_ops = {
	.smpt_size =  pg_smpt_size,
	.init_with_prot = pg_init_with_prot,
	.init_with_mpt = pg_init_with_mpt,
	.apply_range = pg_apply_range,
	.prepare_range = pg_prepare_range,
	.pte_from_addr_smpt = pte_from_addr_smpt,
};
const struct s2mpu_pgtable_ops *s2mpu_alloc_pgtable_ops(struct s2mpu_pgtable_cfg cfg)
{

	//if called before with different version return NULL
	if (this_version && (this_version != cfg.version))
		return NULL;
	this_version = cfg.version;
	//2MB not supported in V9
	if ((this_version == S2MPU_VERSION_9) && (SMPT_GRAN_ATTR != L1ENTRY_ATTR_GRAN_2M)) {
		config_prot_bits = V9_MPT_PROT_BITS;
		config_access_shift = V9_MPT_ACCESS_SHIFT;
		config_lut_prot = v9_mpt_prot_doubleword;
		config_gran_mask = L1ENTRY_ATTR_GRAN(SMPT_GRAN_ATTR, V9_L1ENTRY_ATTR_GRAN_MASK);
#ifdef CONFIG_KVM_S2MPU_MERGE_PTE
		can_merge_pages = true;
#endif //CONFIG_KVM_S2MPU_MERGE_PTE
	return &this_ops;
	} else if  ((this_version == S2MPU_VERSION_2) || (this_version == S2MPU_VERSION_1)) {
		config_prot_bits = MPT_PROT_BITS;
		config_access_shift = MPT_ACCESS_SHIFT;
		config_lut_prot = def_mpt_prot_doubleword;
		config_gran_mask = L1ENTRY_ATTR_GRAN(SMPT_GRAN_ATTR, V9_L1ENTRY_ATTR_GRAN_MASK);
#ifdef CONFIG_KVM_S2MPU_MERGE_PTE
		can_merge_pages = false;
#endif //CONFIG_KVM_S2MPU_MERGE_PTE
	return &this_ops;

	}
	return NULL;
}
