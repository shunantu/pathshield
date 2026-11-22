/* SPDX-License-Identifier: GPL-2.0 */
/*
 * getattr.c - vfs_getattr hook
 *
 * 覆盖的调用：
 *   stat / lstat / fstatat / statx / fstat  最终都进 vfs_getattr。
 *
 * 为什么 inode_permission 之外还要它：
 * 有些检测器先用 open 探存在性（走 inode_permission），
 * 有些直接 stat（虽然也走 inode_permission，但语义上更希望拿到
 * 明确的 ENOENT 而不是权限错误）。两个都挂，覆盖面最稳。
 *
 * 本机签名：
 *   +0x00 stp x29,x30,[sp,#-48]!    a9bd7bfd
 *   +0x04 stp x22,x21,[sp,#16]      a90157f6
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/path.h>
#include <linux/dcache.h>

#include "ps_types.h"
#include "ps_hook.h"
#include "ps_match.h"
#include "ps_guard.h"
#include "ps_core.h"

typedef int (*ps_vfs_getattr_fn)(const struct path *path, struct kstat *stat,
				 u32 request_mask, unsigned int flags);

static struct ps_hook ps_hook_vfs_getattr;

static int ps_replace_vfs_getattr(const struct path *path, struct kstat *stat,
				  u32 request_mask, unsigned int flags)
{
	ps_vfs_getattr_fn real;
	const struct ps_config *cfg = ps_cfg_get();
	bool masked = false;

	real = (ps_vfs_getattr_fn)ps_hook_real(&ps_hook_vfs_getattr);

	if (unlikely(!cfg || !cfg->enabled || !path || !path->dentry))
		return real(path, stat, request_mask, flags);

	if (!ps_guard_enter())
		return real(path, stat, request_mask, flags);

	if (ps_match_uid(cfg, current_fsuid().val) &&
	    ps_match_inode(cfg, path->dentry->d_sb->s_dev,
			   path->dentry->d_inode->i_ino))
		masked = true;

	ps_guard_leave();

	return masked ? -ENOENT : real(path, stat, request_mask, flags);
}

const struct ps_hook_ops ps_ops_vfs_getattr = {
	.name    = "vfs_getattr",
	.symbol  = "vfs_getattr",
	.replace = ps_replace_vfs_getattr,
	.sig     = { 0xa9bd7bfdu, 0xa90157f6u },
	.hook    = &ps_hook_vfs_getattr,
};
