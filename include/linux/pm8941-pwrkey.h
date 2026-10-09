/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_PM8941_PWRKEY_H
#define _LINUX_PM8941_PWRKEY_H

/* Register after panel initialization; unregister before panel destruction.
 * Unregister waits for running callbacks. Neither API may be called by a
 * callback, which runs inside an SRCU read section.
 */
int pm8941_pwrkey_register_bark_callback(unsigned int (*callback)(void));
void pm8941_pwrkey_unregister_bark_callback(unsigned int (*callback)(void));

#endif
