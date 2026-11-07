/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ZRAM_SLOT_H
#define _ZRAM_SLOT_H
#include <linux/blkdev.h>
#include <linux/wait_bit.h>
#include "zram_drv.h"

#define slot_dep_map(zram, index) (&(zram)->table[(index)].dep_map)

static inline void slot_lock(struct zram *zram, u32 index)
{
	unsigned long *lock = &zram->table[index].__lock;

	mutex_acquire(slot_dep_map(zram, index), 0, 0, _RET_IP_);
	wait_on_bit_lock(lock, ZRAM_ENTRY_LOCK_BIT, TASK_UNINTERRUPTIBLE);
	lock_acquired(slot_dep_map(zram, index), _RET_IP_);
}

static inline void slot_unlock(struct zram *zram, u32 index)
{
	unsigned long *lock = &zram->table[index].__lock;

	mutex_release(slot_dep_map(zram, index), _RET_IP_);
	clear_and_wake_up_bit(ZRAM_ENTRY_LOCK_BIT, lock);
}


static inline struct zram *dev_to_zram(struct device *dev)
{
	return (struct zram *)dev_to_disk(dev)->private_data;
}

static inline unsigned long get_slot_handle(struct zram *zram, u32 index)
{
	return zram->table[index].handle;
}

static inline void set_slot_handle(struct zram *zram, u32 index, unsigned long handle)
{
	zram->table[index].handle = handle;
}

static inline bool test_slot_flag(struct zram *zram, u32 index,
			   enum zram_pageflags flag)
{
	return zram->table[index].attr.flags & BIT(flag);
}

static inline void set_slot_flag(struct zram *zram, u32 index,
			  enum zram_pageflags flag)
{
	zram->table[index].attr.flags |= BIT(flag);
}

static inline void clear_slot_flag(struct zram *zram, u32 index,
			    enum zram_pageflags flag)
{
	zram->table[index].attr.flags &= ~BIT(flag);
}

static inline size_t get_slot_size(struct zram *zram, u32 index)
{
	return zram->table[index].attr.flags & (BIT(ZRAM_FLAG_SHIFT) - 1);
}

static inline void set_slot_size(struct zram *zram, u32 index, size_t size)
{
	unsigned long flags = zram->table[index].attr.flags >> ZRAM_FLAG_SHIFT;

	zram->table[index].attr.flags = (flags << ZRAM_FLAG_SHIFT) | size;
}


#endif
