/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM meizu_vip

#if !defined(_TRACE_MEIZU_VIP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MEIZU_VIP_H

#include <linux/tracepoint.h>

TRACE_EVENT(vip_debug,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf),
	TP_STRUCT__entry(__vstring(message, vaf->fmt, vaf->va)),
	TP_fast_assign(__assign_vstr(message, vaf->fmt, vaf->va);),
	TP_printk("%s", __get_str(message))
);

#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/events
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE meizu_vip
#include <trace/define_trace.h>
