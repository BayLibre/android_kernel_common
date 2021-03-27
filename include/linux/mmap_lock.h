#ifndef _LINUX_MMAP_LOCK_H
#define _LINUX_MMAP_LOCK_H

#include <linux/mmdebug.h>

#ifdef CONFIG_SPECULATIVE_PAGE_FAULT
#define MMAP_LOCK_SEQ_INITIALIZER(name) \
	.mmap_seq = 0,
#else
#define MMAP_LOCK_SEQ_INITIALIZER(name)
#endif

#define MMAP_LOCK_INITIALIZER(name)				\
	.mmap_lock = __RWSEM_INITIALIZER((name).mmap_lock),	\
	MMAP_LOCK_SEQ_INITIALIZER(name)

static inline void mmap_init_lock(struct mm_struct *mm)
{
	init_rwsem(&mm->mmap_lock);
#ifdef CONFIG_SPECULATIVE_PAGE_FAULT
	mm->mmap_seq = 0;
#endif
}

static inline void __mmap_seq_write_lock(struct mm_struct *mm)
{
#ifdef CONFIG_SPECULATIVE_PAGE_FAULT
	VM_BUG_ON_MM(mm->mmap_seq & 1, mm);
	mm->mmap_seq++;
	smp_wmb();
#endif
}

static inline void __mmap_seq_write_unlock(struct mm_struct *mm)
{
#ifdef CONFIG_SPECULATIVE_PAGE_FAULT
	smp_wmb();
	mm->mmap_seq++;
	VM_BUG_ON_MM(mm->mmap_seq & 1, mm);
#endif
}

#ifdef CONFIG_SPECULATIVE_PAGE_FAULT
static inline unsigned long mmap_seq_read_start(struct mm_struct *mm)
{
	unsigned long seq;

	seq = READ_ONCE(mm->mmap_seq);
	smp_rmb();
	return seq;
}

static inline bool mmap_seq_read_check(struct mm_struct *mm, unsigned long seq)
{
	smp_rmb();
	return seq == READ_ONCE(mm->mmap_seq);
}
#endif

static inline void mmap_write_lock(struct mm_struct *mm)
{
	down_write(&mm->mmap_lock);
	__mmap_seq_write_lock(mm);
}

static inline void mmap_write_lock_nested(struct mm_struct *mm, int subclass)
{
	down_write_nested(&mm->mmap_lock, subclass);
	__mmap_seq_write_lock(mm);
}

static inline int mmap_write_lock_killable(struct mm_struct *mm)
{
	int error;

	error = down_write_killable(&mm->mmap_lock);
	if (likely(!error))
		__mmap_seq_write_lock(mm);
	return error;
}

static inline bool mmap_write_trylock(struct mm_struct *mm)
{
	bool ok;

	ok = down_write_trylock(&mm->mmap_lock) != 0;
	if (likely(ok))
		__mmap_seq_write_lock(mm);
	return ok;
}

static inline void mmap_write_unlock(struct mm_struct *mm)
{
	__mmap_seq_write_unlock(mm);
	up_write(&mm->mmap_lock);
}

static inline void mmap_write_downgrade(struct mm_struct *mm)
{
	__mmap_seq_write_unlock(mm);
	downgrade_write(&mm->mmap_lock);
}

static inline void mmap_read_lock(struct mm_struct *mm)
{
	down_read(&mm->mmap_lock);
}

static inline int mmap_read_lock_killable(struct mm_struct *mm)
{
	int error;

	error = down_read_killable(&mm->mmap_lock);
	return error;
}

static inline bool mmap_read_trylock(struct mm_struct *mm)
{
	bool ok;

	ok = down_read_trylock(&mm->mmap_lock) != 0;
	return ok;
}

static inline void mmap_read_unlock(struct mm_struct *mm)
{
	up_read(&mm->mmap_lock);
}

static inline bool mmap_read_trylock_non_owner(struct mm_struct *mm)
{
	if (down_read_trylock(&mm->mmap_lock)) {
		rwsem_release(&mm->mmap_lock.dep_map, _RET_IP_);
		return true;
	}
	return false;
}

static inline void mmap_read_unlock_non_owner(struct mm_struct *mm)
{
	up_read_non_owner(&mm->mmap_lock);
}

static inline void mmap_assert_locked(struct mm_struct *mm)
{
	lockdep_assert_held(&mm->mmap_lock);
	VM_BUG_ON_MM(!rwsem_is_locked(&mm->mmap_lock), mm);
}

static inline void mmap_assert_write_locked(struct mm_struct *mm)
{
	lockdep_assert_held_write(&mm->mmap_lock);
	VM_BUG_ON_MM(!rwsem_is_locked(&mm->mmap_lock), mm);
}

static inline bool mmap_lock_is_contended(struct mm_struct *mm)
{
	return rwsem_is_contended(&mm->mmap_lock) != 0;
}

#endif /* _LINUX_MMAP_LOCK_H */
