/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _QCOM_Q6V5_RSTINFO_H
#define _QCOM_Q6V5_RSTINFO_H
#include <linux/types.h>
/* Original callback ABI. Registration/removal can sleep and must never be
 * called by the callback. Unregister outside locks acquired by the callback.
 */
extern void (*send_ssrinfo_to_rstinfo)(const char *, char *, char *, size_t);
int qcom_q6v5_register_rstinfo(void (*callback)(const char *, char *, char *, size_t));
void qcom_q6v5_unregister_rstinfo(void (*callback)(const char *, char *, char *, size_t));
#endif
