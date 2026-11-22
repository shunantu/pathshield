/* SPDX-License-Identifier: GPL-2.0 */
/*
 * parse.c - 配置字符串解析（纯函数）
 *
 * 这一层只做"字符串 → 结构体"，不做任何路径解析、不碰 VFS、
 * 不分配内存。好处：
 *   - 可以在用户态单独测试（拿同一份源码编个 host 程序即可）
 *   - 出错时内核状态完全没动，回滚成本为零
 *
 * 解析规则
 * --------
 *   target_paths / deny_uids 都是逗号分隔。
 *   允许条目前后有空格（会被 trim）。
 *   空条目静默跳过，不报错 —— 因为 "a,,b" 这种手写配置很常见。
 *   非法条目（非绝对路径、超长、含空白）跳过并计数。
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>

#include "ps_types.h"
#include "ps_core.h"
#include "ps_param.h"
#include "ps_log.h"

/* 就地把两端空白压掉，返回新的起点 */
static char *ps_trim(char *s)
{
	char *end;

	if (!s)
		return NULL;
	while (*s && isspace(*s))
		s++;
	if (!*s)
		return s;
	end = s + strlen(s) - 1;
	while (end > s && isspace(*end))
		*end-- = '\0';
	return s;
}

/*
 * 解析一个逗号分隔列表。buf 必须可写（函数会就地插入 '\0'）。
 * cb 对每个合法条目调用一次，返回值 <0 表示中止。
 */
static int ps_foreach_csv(char *buf, int (*cb)(char *item, void *priv),
			  void *priv)
{
	char *p = buf;
	char *next;

	if (!buf)
		return 0;

	for (;;) {
		next = strchr(p, ',');
		if (next)
			*next++ = '\0';

		p = ps_trim(p);
		if (p && *p) {
			if (cb(p, priv) < 0)
				return -EINVAL;
		}

		if (!next)
			break;
		p = next;
	}
	return 0;
}

struct ps_path_ctx {
	struct ps_config *cfg;
	int added;
	int rejected;
};

static int ps_take_path(char *item, void *priv)
{
	struct ps_path_ctx *ctx = priv;
	struct ps_target *t;
	size_t len;
	size_t i;

	len = strlen(item);

	if (item[0] != '/') {
		ctx->rejected++;
		PS_DBG("path not absolute: '%s'\n", item);
		return 0;
	}
	if (len >= PS_PATH_MAX) {
		ctx->rejected++;
		PS_WRN("path too long (%zu): '%s'\n", len, item);
		return 0;
	}
	/* 空白字符会让后续 shell 处理变得不可靠，直接拒绝 */
	for (i = 0; i < len; i++) {
		if (isspace(item[i])) {
			ctx->rejected++;
			PS_WRN("path contains space: '%s'\n", item);
			return 0;
		}
	}
	if (ctx->cfg->nr_targets >= PS_MAX_TARGETS) {
		PS_WRN("target table full (%d), '%s' dropped\n",
		       PS_MAX_TARGETS, item);
		return 0;
	}

	t = &ctx->cfg->targets[ctx->cfg->nr_targets];
	strscpy(t->path, item, PS_PATH_MAX);
	t->enabled = 1;
	ctx->cfg->nr_targets++;
	ctx->added++;
	return 0;
}

struct ps_uid_ctx {
	struct ps_config *cfg;
};

static int ps_take_uid(char *item, void *priv)
{
	struct ps_uid_ctx *ctx = priv;
	u32 v = 0;
	int any = 0;

	while (*item == ' ' || *item == '\t')
		item++;
	while (*item >= '0' && *item <= '9') {
		if (v > 999999) {	/* 溢出保护 */
			any = 0;
			break;
		}
		v = v * 10 + (u32)(*item - '0');
		item++;
		any = 1;
	}
	if (!any) {
		PS_DBG("bad uid entry skipped\n");
		return 0;
	}
	if (ctx->cfg->nr_uids >= PS_MAX_UIDS)
		return -EINVAL;

	ctx->cfg->uids[ctx->cfg->nr_uids++] = (uid_t)v;
	return 0;
}

static u8 ps_parse_scope(const char *s)
{
	if (!s)
		return PS_SCOPE_DENY;
	if (!strcmp(s, "global"))
		return PS_SCOPE_GLOBAL;
	if (!strcmp(s, "allow"))
		return PS_SCOPE_ALLOW;
	return PS_SCOPE_DENY;
}

int ps_cfg_parse(struct ps_config *cfg)
{
	const struct ps_params *p = ps_params_get();
	char *buf;
	struct ps_path_ctx pctx;
	struct ps_uid_ctx uctx;
	int ret = 0;

	if (!cfg || !p)
		return -EINVAL;

	ps_cfg_clear(cfg);

	cfg->scope         = ps_parse_scope(p->scope_mode);
	cfg->hide_dirents  = !!(p->hide_dirents);
	cfg->enabled       = !!(p->enabled);

	/* ── 路径 ── */
	buf = kstrdup(p->target_paths ? p->target_paths : "", GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pctx.cfg = cfg;
	pctx.added = 0;
	pctx.rejected = 0;
	if (ps_foreach_csv(buf, ps_take_path, &pctx) < 0)
		ret = -EINVAL;
	kfree(buf);

	if (ret)
		return ret;

	PS_INF("parsed %d path(s), %d rejected\n",
	       pctx.added, pctx.rejected);

	/* ── UID ── */
	buf = kstrdup(p->deny_uids ? p->deny_uids : "", GFP_KERNEL);
	if (!buf) {
		ps_cfg_clear(cfg);
		return -ENOMEM;
	}

	uctx.cfg = cfg;
	ps_foreach_csv(buf, ps_take_uid, &uctx);
	kfree(buf);

	PS_INF("parsed %d uid(s), scope=%u\n", cfg->nr_uids, cfg->scope);

	/* 白名单模式下若没给 UID，默认放行系统身份，
	 * 否则系统服务会被自己的规则挡死。 */
	if (cfg->scope == PS_SCOPE_ALLOW && cfg->nr_uids == 0) {
		cfg->uids[cfg->nr_uids++] = 0;
		cfg->uids[cfg->nr_uids++] = 1000;
		cfg->uids[cfg->nr_uids++] = 2000;
		PS_INF("allow mode without uid list, defaulted to 0/1000/2000\n");
	}

	return 0;
}
