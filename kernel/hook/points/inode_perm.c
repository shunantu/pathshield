/* SPDX-License-Identifier: GPL-2.0 */
/*
 * inode_perm.c - inode_permission hook
 *
 * 为什么选它
 * ----------
 * inode_permission(inode, mask) 是 VFS 权限检查的唯一入口。
 * stat / open / access / exec / readlink 只要涉及路径解析，最终都到这里。
 * 在这里返回 -ENOENT，调用方（may_lookup / do_filp_open）会当作
 * "文件不存在"处理，与真实的 EN OENT 无法区分。
 *
 * 它也是四个 hook 点里最短的：本机实现只有 32 字节
 *   +0x00 stp x29,x30,[sp,#-16]!
 *   +0x04 mov x29, sp
 *   +0x08 ...
 *   +0x18 ret
 * 前 16 字节内全是寄存器搬运，无 PC 相对指令，适合 inline hook。
 */

#include <linux/module.h>
#include <linux/fs.h>

#include "ps_types.h"
#include "ps_hook.h"
#include "ps_match.h"
#include "ps_guard.h"
#include "ps_core.h"
#include "ps_log.h"

typedef int (*ps_inode_permission_fn)(struct inode *inode, int mask);

static struct ps_hook ps_hook_inode_perm;

static int ps_replace_inode_permission(struct inode *inode, int mask)
{
	ps_inode_permission_fn real;
	const struct ps_config *cfg = ps_cfg_get();
	bool masked = false;

	real = (ps_inode_permission_fn)ps_hook_real(&ps_hook_inode_perm);

	if (unlikely(!cfg || !cfg->enabled))
		return real(inode, mask);

	if (!ps_guard_enter())
		return real(inode, mask);

	if (ps_match_uid(cfg, current_fsuid().val) &&
	    ps_match_inode(cfg, inode->i_sb->s_dev, inode->i_ino))
		masked = true;

	ps_guard_leave();

	return masked ? -ENOENT : real(inode, mask);
}

const struct ps_hook_ops ps_ops_inode_perm = {
	.name    = "inode_permission",
	.symbol  = "inode_permission",
	.replace = ps_replace_inode_permission,
	.sig     = { 0xa9bf7bfdu, 0x910003fdu },	/* stp x29,x30,#-16; mov x29,sp */
	.hook    = &ps_hook_inode_perm,
};
