/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_MEIZU_SMARTSWAP_H
#define _LINUX_MEIZU_SMARTSWAP_H

#include <linux/types.h>

struct mem_cgroup;
struct lruvec;
void meizu_smartput_memcg_id(struct mem_cgroup *memcg);
unsigned long meizu_smartshrink_anon(struct mem_cgroup *memcg, unsigned long nr_to_scan,
				    struct lruvec *lruvec, bool active);
#endif
