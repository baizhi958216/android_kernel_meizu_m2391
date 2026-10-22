/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _UFS_MEIZU_H
#define _UFS_MEIZU_H
struct ufs_hba;
int ufs_fbo_init(struct ufs_hba *hba);
void ufs_fbo_exit(struct ufs_hba *hba);
int ufs_health_init_sysfs(struct ufs_hba *hba);
void ufs_health_exit(struct ufs_hba *hba);
#endif
