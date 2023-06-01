// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014-2017 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include <linux/elf.h>
#include <linux/ftrace.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sort.h>

static struct plt_entry __get_adrp_add_pair(u64 dst, u64 pc,
					    enum aarch64_insn_register reg)
{
	u32 adrp, add;

	adrp = aarch64_insn_gen_adr(pc, dst, reg, AARCH64_INSN_ADR_TYPE_ADRP);
	add = aarch64_insn_gen_add_sub_imm(reg, reg, dst % SZ_4K,
					   AARCH64_INSN_VARIANT_64BIT,
					   AARCH64_INSN_ADSB_ADD);

	return (struct plt_entry){ cpu_to_le32(adrp), cpu_to_le32(add) };
}

struct plt_entry get_plt_entry(u64 dst, void *pc)
{
	struct plt_entry plt;
	static u32 br;

	if (!br)
		br = aarch64_insn_gen_branch_reg(AARCH64_INSN_REG_16,
						 AARCH64_INSN_BRANCH_NOLINK);

	plt = __get_adrp_add_pair(dst, (u64)pc, AARCH64_INSN_REG_16);
	plt.br = cpu_to_le32(br);

	return plt;
}

static bool plt_entries_equal(const struct plt_entry *a,
			      const struct plt_entry *b)
{
	u64 p, q;

	/*
	 * Check whether both entries refer to the same target:
	 * do the cheapest checks first.
	 * If the 'add' or 'br' opcodes are different, then the target
	 * cannot be the same.
	 */
	if (a->add != b->add || a->br != b->br)
		return false;

	p = ALIGN_DOWN((u64)a, SZ_4K);
	q = ALIGN_DOWN((u64)b, SZ_4K);

	/*
	 * If the 'adrp' opcodes are the same then we just need to check
	 * that they refer to the same 4k region.
	 */
	if (a->adrp == b->adrp && p == q)
		return true;

	return (p + aarch64_insn_adrp_get_offset(le32_to_cpu(a->adrp))) ==
	       (q + aarch64_insn_adrp_get_offset(le32_to_cpu(b->adrp)));
}

static bool in_init(const struct module *mod, void *loc)
{
	return (u64)loc - (u64)mod->init_layout.base < mod->init_layout.size;
}

u64 module_emit_plt_entry(struct module *mod, Elf64_Shdr *sechdrs,
			  void *loc, const Elf64_Rela *rela,
			  Elf64_Sym *sym)
{
	struct mod_plt_sec *pltsec = !in_init(mod, loc) ? &mod->arch.core :
							  &mod->arch.init;
	struct plt_entry *plt = (struct plt_entry *)sechdrs[pltsec->plt_shndx].sh_addr;
	int i = pltsec->plt_num_entries;
	int j = i - 1;
	u64 val = sym->st_value + rela->r_addend;

	if (is_forbidden_offset_for_adrp(&plt[i].adrp))
		i++;

	plt[i] = get_plt_entry(val, &plt[i]);

	/*
	 * Check if the entry we just created is a duplicate. Given that the
	 * relocations are sorted, this will be the last entry we allocated.
	 * (if one exists).
	 */
	if (j >= 0 && plt_entries_equal(plt + i, plt + j))
		return (u64)&plt[j];

	pltsec->plt_num_entries += i - j;
	if (WARN_ON(pltsec->plt_num_entries > pltsec->plt_max_entries))
		return 0;

	return (u64)&plt[i];
}

#ifdef CONFIG_ARM64_ERRATUM_843419
u64 module_emit_veneer_for_adrp(struct module *mod, Elf64_Shdr *sechdrs,
				void *loc, u64 val)
{
	struct mod_plt_sec *pltsec = !in_init(mod, loc) ? &mod->arch.core :
							  &mod->arch.init;
	struct plt_entry *plt = (struct plt_entry *)sechdrs[pltsec->plt_shndx].sh_addr;
	int i = pltsec->plt_num_entries++;
	u32 br;
	int rd;

	if (WARN_ON(pltsec->plt_num_entries > pltsec->plt_max_entries))
		return 0;

	if (is_forbidden_offset_for_adrp(&plt[i].adrp))
		i = pltsec->plt_num_entries++;

	/* get the destination register of the ADRP instruction */
	rd = aarch64_insn_decode_register(AARCH64_INSN_REGTYPE_RD,
					  le32_to_cpup((__le32 *)loc));

	br = aarch64_insn_gen_branch_imm((u64)&plt[i].br, (u64)loc + 4,
					 AARCH64_INSN_BRANCH_NOLINK);

	plt[i] = __get_adrp_add_pair(val, (u64)&plt[i], rd);
	plt[i].br = cpu_to_le32(br);

	return (u64)&plt[i];
}
#endif

int module_frob_arch_sections(Elf_Ehdr *ehdr, Elf_Shdr *sechdrs,
			      char *secstrings, struct module *mod)
{
	unsigned long core_plts = 0;
	unsigned long init_plts = 0;
	Elf64_Sym *syms = NULL;
	Elf_Shdr *pltsec, *tramp = NULL;
	int i;

	/*
	 * Find the empty .plt section so we can expand it to store the PLT
	 * entries. Record the symtab address as well.
	 */
	for (i = 0; i < ehdr->e_shnum; i++) {
		if (!strcmp(secstrings + sechdrs[i].sh_name, ".plt"))
			mod->arch.core.plt_shndx = i;
		else if (!strcmp(secstrings + sechdrs[i].sh_name, ".init.plt"))
			mod->arch.init.plt_shndx = i;
		else if (!strcmp(secstrings + sechdrs[i].sh_name,
				 ".text.ftrace_trampoline"))
			tramp = sechdrs + i;
		else if (sechdrs[i].sh_type == SHT_SYMTAB)
			syms = (Elf64_Sym *)sechdrs[i].sh_addr;
	}

	if (!mod->arch.core.plt_shndx || !mod->arch.init.plt_shndx) {
		pr_err("%s: module PLT section(s) missing\n", mod->name);
		return -ENOEXEC;
	}
	if (!syms) {
		pr_err("%s: module symtab section missing\n", mod->name);
		return -ENOEXEC;
	}
	
	pltsec = sechdrs + mod->arch.core.plt_shndx;
	pltsec->sh_type = SHT_NOBITS;
	pltsec->sh_flags = SHF_EXECINSTR | SHF_ALLOC;
	pltsec->sh_addralign = L1_CACHE_BYTES;
	pltsec->sh_size = (core_plts  + 1) * sizeof(struct plt_entry);
	mod->arch.core.plt_num_entries = 0;
	mod->arch.core.plt_max_entries = core_plts;

	pltsec = sechdrs + mod->arch.init.plt_shndx;
	pltsec->sh_type = SHT_NOBITS;
	pltsec->sh_flags = SHF_EXECINSTR | SHF_ALLOC;
	pltsec->sh_addralign = L1_CACHE_BYTES;
	pltsec->sh_size = (init_plts + 1) * sizeof(struct plt_entry);
	mod->arch.init.plt_num_entries = 0;
	mod->arch.init.plt_max_entries = init_plts;

	if (tramp) {
		tramp->sh_type = SHT_NOBITS;
		tramp->sh_flags = SHF_EXECINSTR | SHF_ALLOC;
		tramp->sh_addralign = __alignof__(struct plt_entry);
		tramp->sh_size = NR_FTRACE_PLTS * sizeof(struct plt_entry);
	}

	return 0;
}
