// SPDX-License-Identifier: GPL-2.0-only
/* Recovered from M2391 mz_enhance_qcom_wdog.ko:
 * ping .text+0x11c, IRQ sample+0x200, top ten+0x3d0, task+0x7bc,
 * SMP diagnostics+0x808. Types confirmed by stock split/base BTF.
 * kstat_irqs_usr is the exported RCU-protected form of the same kstat_irqs
 * logic inlined by stock; no private layout offsets are used here.
 */
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include "mz_enhance_qcom_wdog.h"

static unsigned int *irqs_last, *irqs_delta;
static unsigned int irq_capacity;
static unsigned int all_irqs_last, all_irqs_delta;
static DEFINE_RAW_SPINLOCK(diagnostics_lock);

/* These are BSS globals in the original module, never assigned by any
 * reachable code and not exported. Preserve their initial diagnostic values.
 */
static int smp_call_any_cpu;
static unsigned long smp_call_many_cpumask;

static void sample_irqs(void)
{
	unsigned int irq, count, total = 0;
	unsigned int limit = min_t(unsigned int, READ_ONCE(nr_irqs), irq_capacity);

	for (irq = 0; irq < limit; irq++) {
		struct irq_desc *desc;
		bool include;

		rcu_read_lock();
		desc = irq_to_desc(irq);
		if (!desc) {
			rcu_read_unlock();
			continue;
		}
		count = kstat_irqs_usr(irq);
		/* Stock counts unbound IRQs too when their counter is nonzero. */
		include = READ_ONCE(desc->action) || count;
		rcu_read_unlock();
		if (!include)
			continue;
		total += count;
		/* Individual counters reset to zero on wrap/reset, as in stock. */
		irqs_delta[irq] = count >= irqs_last[irq] ? count - irqs_last[irq] : 0;
		irqs_last[irq] = count;
	}
	all_irqs_delta = total - all_irqs_last;
	all_irqs_last = total;
}

static void print_top10_irqs(void)
{
	int top[10], rank, n;
	unsigned int irq, limit = min_t(unsigned int, READ_ONCE(nr_irqs), irq_capacity);

	memset(top, 0xff, sizeof(top));
	for (irq = 0; irq < limit; irq++) {
		for (rank = 0; rank < ARRAY_SIZE(top); rank++) {
			/* Strict comparison preserves ascending IRQ order on ties. */
			if (top[rank] >= 0 && irqs_delta[irq] <= irqs_delta[top[rank]])
				continue;
			for (n = ARRAY_SIZE(top) - 1; n > rank; n--)
				top[n] = top[n - 1];
			top[rank] = irq;
			break;
		}
	}
	pr_info("Top10 irqs since last: ");
	for (rank = 0; rank < ARRAY_SIZE(top); rank++) {
		struct irq_desc *desc;
		struct irqaction *action;
		unsigned long flags;

		if (top[rank] < 0)
			continue;
		rcu_read_lock();
		desc = irq_to_desc(top[rank]);
		if (desc && raw_spin_trylock_irqsave(&desc->lock, flags)) {
			/* Pair with IRQ action changes without blocking a watchdog.
			 * Never retain an action/name after dropping the lock.
			 */
			action = desc->action;
			if (action)
				printk("%s(%d):%u", action->name, top[rank],
						irqs_delta[top[rank]]);
			raw_spin_unlock_irqrestore(&desc->lock, flags);
		}
		rcu_read_unlock();
	}
	pr_info("Total: %u\n", all_irqs_delta);
}

void mz_get_cpu_ping_mask(cpumask_t *ping_mask, int *cpu_idle_pc_state)
{
	unsigned long flags;
	int cpu;

	if (!ping_mask || !cpu_idle_pc_state)
		return;
	cpumask_copy(ping_mask, cpu_online_mask);
	for_each_cpu(cpu, ping_mask)
		if (READ_ONCE(cpu_idle_pc_state[cpu]))
			cpumask_clear_cpu(cpu, ping_mask);
	/* Diagnostics can be invoked by a watchdog; contention must not stall
	 * its CPU ping path or recurse into a lock held by a stuck CPU.
	 */
	if (!raw_spin_trylock_irqsave(&diagnostics_lock, flags))
		return;
	sample_irqs();
	pr_info("[mz_wdog]cpu avail mask: 0x%lx; ping mask: 0x%lx; irqs since last: %u\n",
			cpumask_bits(cpu_online_mask)[0], cpumask_bits(ping_mask)[0],
			all_irqs_delta);
	print_top10_irqs();
	raw_spin_unlock_irqrestore(&diagnostics_lock, flags);
}
EXPORT_SYMBOL(mz_get_cpu_ping_mask);

void mz_dump_wdog_cpu(struct task_struct *w_task)
{
	unsigned long flags;

	if (raw_spin_trylock_irqsave(&diagnostics_lock, flags)) {
		sample_irqs();
		print_top10_irqs();
		raw_spin_unlock_irqrestore(&diagnostics_lock, flags);
	}
	if (w_task)
		pr_emerg("Watchdog work is pending at CPU(%d)\n", task_cpu(w_task));
}
EXPORT_SYMBOL(mz_dump_wdog_cpu);

void mz_dump_cpu_online_smp_call(void)
{
	/* Original's static32-byte buffer truncated large masks. Format the
	 * entire supported cpumask directly and avoid concurrent scratch writes.
	 */
	pr_info("cpu avail mask %*pb1\n", cpumask_pr_args(cpu_online_mask));
	pr_info("cpu of last smp_call_function_any: %d\n", READ_ONCE(smp_call_any_cpu));
	pr_info("cpumask of last smp_call_function_many: 0x%lx\n",
			READ_ONCE(smp_call_many_cpumask));
}
EXPORT_SYMBOL(mz_dump_cpu_online_smp_call);

static int __init mz_wd_init(void)
{
	/* Original allocates1200 entries and BUGs if nr_irqs exceeds1200.
	 * Retain at least that capacity, but size for the actual kernel too.
	 * Runtime IRQ namespace growth is bounded by irq_capacity during samples.
	 */
	irq_capacity = max_t(unsigned int, 1200, READ_ONCE(nr_irqs));
	irqs_last = kcalloc(irq_capacity, sizeof(*irqs_last), GFP_KERNEL);
	if (!irqs_last)
		return -ENOMEM;
	irqs_delta = kcalloc(irq_capacity, sizeof(*irqs_delta), GFP_KERNEL);
	if (!irqs_delta) {
		kfree(irqs_last);
		return -ENOMEM;
	}
	pr_warn("[MZ_WD] %s: start!\n", __func__);
	pr_warn("[MZ_WD] init_mz_watchdog: start\n");
	return 0;
}
static void __exit mz_wd_exit(void)
{
	kfree(irqs_delta);
	kfree(irqs_last);
}
module_init(mz_wd_init);
module_exit(mz_wd_exit);
MODULE_DESCRIPTION("Meizu watchdog CPU and interrupt diagnostics");
MODULE_LICENSE("GPL v2");
