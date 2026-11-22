/* SPDX-License-Identifier: GPL-2.0 */
/*
 * name.c - 名字层匹配（filldir 用）
 *
 * filldir64 给的是 (name, namelen)，name 不带 NUL 终止符。
 * 所有比较都必须显式带长度，绝不能用 strlen/memcmp 假设终止。
 *
 * 性能：只在列目录时调用，每次目录项一次。为了减少比较次数，
 * 先比长度（O(1)），长度不等直接跳过，只有长度相等才 memcmp。
 */

#include <linux/types.h>
#include <linux/string.h>

#include "ps_types.h"
#include "ps_match.h"

bool ps_match_name(const struct ps_config *cfg, const char *name, size_t len)
{
	int i;

	if (unlikely(!cfg || !cfg->nr_targets || !name || !len))
		return false;
	if (unlikely(len > PS_NAME_MAX))
		return false;

	for (i = 0; i < cfg->nr_targets; i++) {
		const struct ps_target *t = &cfg->targets[i];

		if (unlikely(!t->enabled))
			continue;
		/* 长度先比，省一次 memcmp */
		if (t->name_len != len)
			continue;

		if (t->kind == PS_TGT_GLOB) {
			if (ps_glob_n(t->name, t->name_len, name, len))
				return true;
			continue;
		}
		if (!memcmp(t->name, name, len))
			return true;
	}
	return false;
}

bool ps_match_uid(const struct ps_config *cfg, uid_t uid)
{
	int i;

	if (unlikely(!cfg))
		return false;

	if (cfg->scope == PS_SCOPE_GLOBAL)
		return true;

	for (i = 0; i < cfg->nr_uids; i++) {
		if (cfg->uids[i] == uid)
			return cfg->scope == PS_SCOPE_DENY;
	}
	/* 不在名单里：白名单模式视为要隐藏，黑名单模式视为不隐藏 */
	return cfg->scope == PS_SCOPE_ALLOW;
}
