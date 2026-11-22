/* SPDX-License-Identifier: GPL-2.0 */
/*
 * param.c - 模块参数定义
 *
 * 保持与 pathmask 的参数字面量兼容，这样已有的 conf 文件与
 * service.sh 不用改就能用：
 *   target_paths / scope_mode / deny_uids / hide_dirents / enabled
 *
 * 权限 0644：root 可写，但写入不会即时生效（需要在 WebUI 里热重载）。
 * 不做 callback 的原因是 module_param_cb 的写回调可能在原子上下文，
 * 重新解析配置并重装 hook 在那里做很危险。
 */

#include <linux/module.h>
#include <linux/moduleparam.h>

#include "ps_types.h"
#include "ps_param.h"
#include "ps_log.h"

static char *g_target_paths = "";
module_param_named(target_paths, g_target_paths, charp, 0644);
MODULE_PARM_DESC(target_paths, "Comma-separated absolute paths to mask");

static char *g_scope_mode = "deny";
module_param_named(scope_mode, g_scope_mode, charp, 0644);
MODULE_PARM_DESC(scope_mode, "Hide scope: deny | allow | global");

static char *g_deny_uids = "";
module_param_named(deny_uids, g_deny_uids, charp, 0644);
MODULE_PARM_DESC(deny_uids, "Comma-separated UIDs affected by the scope");

static int g_hide_dirents = 1;
module_param_named(hide_dirents, g_hide_dirents, int, 0644);
MODULE_PARM_DESC(hide_dirents, "1 = also filter directory listings");

static int g_enabled = 1;
module_param_named(enabled, g_enabled, int, 0644);
MODULE_PARM_DESC(enabled, "Master switch: 0 = pass everything through");

static struct ps_params g_params;

const struct ps_params *ps_params_get(void)
{
	return &g_params;
}

int ps_params_init(void)
{
	g_params.target_paths = g_target_paths;
	g_params.scope_mode   = g_scope_mode;
	g_params.deny_uids    = g_deny_uids;
	g_params.hide_dirents = g_hide_dirents;
	g_params.enabled      = g_enabled;

	PS_INF("params: paths='%s' scope=%s uids='%s' dirents=%d enabled=%d\n",
	       g_target_paths, g_scope_mode, g_deny_uids,
	       g_hide_dirents, g_enabled);
	return 0;
}

void ps_params_exit(void)
{
	/* module_param 由内核管理生命周期，这里无需释放 */
}
