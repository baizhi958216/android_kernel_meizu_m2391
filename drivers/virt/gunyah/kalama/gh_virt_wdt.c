// SPDX-License-Identifier: GPL-2.0-only
/* Qualcomm Kalama Gunyah virtual watchdog transport. */
#include <linux/arm-smccc.h>
#include <linux/gunyah/gh_errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include "kalama-watchdog.h"

#define GH_WDT_CALL(n) ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_32, \
					 ARM_SMCCC_OWNER_VENDOR_HYP, (n))

static int gh_wdt_enable(bool on)
{
	struct arm_smccc_res res;

	/* Gunyah reserves bit 1, which must be set for both operations. */
	arm_smccc_1_1_smc(GH_WDT_CALL(5), on ? 3 : 2, &res);
	return gh_remap_error((int)res.a0);
}

static int gh_wdt_status(bool *enabled, u32 *elapsed)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_smc(GH_WDT_CALL(6), 0, &res);
	if (res.a0)
		return gh_remap_error((int)res.a0);
	*enabled = !!(res.a1 & BIT(0));
	*elapsed = res.a2;
	return 0;
}

static int gh_wdt_pet(void)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_smc(GH_WDT_CALL(7), 0, &res);
	return gh_remap_error((int)res.a0);
}

static int gh_wdt_set_time(u16 bark_ms, u16 bite_ms)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_smc(GH_WDT_CALL(8), bark_ms, bite_ms, &res);
	return gh_remap_error((int)res.a0);
}

static const struct kalama_wdt_ops gh_wdt_ops = {
	.enable = gh_wdt_enable,
	.set_time = gh_wdt_set_time,
	.pet = gh_wdt_pet,
	.status = gh_wdt_status,
};

static int gh_wdt_probe(struct platform_device *pdev)
{
	return kalama_wdt_register(pdev, &gh_wdt_ops);
}

static const struct of_device_id gh_wdt_match[] = {
	{ .compatible = "qcom,gh-watchdog" },
	{ .compatible = "qcom,hh-watchdog" },
	{}
};
MODULE_DEVICE_TABLE(of, gh_wdt_match);

static struct platform_driver gh_wdt_driver = {
	.probe = gh_wdt_probe,
	.shutdown = kalama_wdt_shutdown,
	.driver = {
		.name = "gh-watchdog",
		.of_match_table = gh_wdt_match,
		.pm = pm_ptr(&kalama_wdt_pm),
	},
};
module_platform_driver(gh_wdt_driver);

MODULE_DESCRIPTION("Kalama Gunyah virtual watchdog");
MODULE_LICENSE("GPL");
