/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_MEIZU_SMARTSWAP_H
#define _LINUX_MEIZU_SMARTSWAP_H
#include <linux/types.h>
struct mem_cgroup;
struct lruvec;
/* The caller pins its memcg and passes that memcg/node's lruvec. */
unsigned long meizu_smartshrink_anon(struct mem_cgroup *memcg,
	unsigned long nr_to_scan, struct lruvec *lruvec, bool active);
#endif
