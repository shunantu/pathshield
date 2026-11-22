/* SPDX-License-Identifier: GPL-2.0 */
/*
 * module.c - 模块入口/出口
 *
 * 初始化顺序（每一步失败都能干净回滚）：
 *   1. 参数快照
 *   2. 解析配置（纯字符串，无副作用）
 *   3. 解析路径为 inode（有 VFS 调用，但在 guard 保护下）
 *   4. 安装 hook（唯一修改内核代码的步骤）
 *
 * 卸载严格逆序：先卸 hook，再清配置。
 * 顺序错了会导致 hook 还在跑但配置已被清空 → 空指针解引用。
 */

#include <linux/module.h>
#include <linux/kernel.h>

#include "ps_types.h"
#include "ps_log.h"
#include "ps_core.h"
#include "ps_param.h"
#include "ps_hook.h"

static int __init pathshield_init(void)
{
	struct ps_config *cfg;
	int ret;

	PS_INF("init v%s (targets<=%d uids<=%d)\n",
	       PS_VERSION_STR, PS_MAX_TARGETS, PS_MAX_UIDS);

	ret = ps_params_init();
	if (ret)
		return ret;

	cfg = ps_cfg_mut();

	ret = ps_cfg_parse(cfg);
	if (ret) {
		PS_ERR("config parse failed %d\n", ret);
		goto err_clear;
	}

	if (!cfg->nr_targets) {
		PS_ERR("no target path configured, refuse to load\n");
		ret = -EINVAL;
		goto err_clear;
	}

	ret = ps_cfg_resolve(cfg);
	if (ret) {
		PS_ERR("no resolvable target (all glob or missing), refuse\n");
		goto err_clear;
	}

	ret = ps_hooks_install_all();
	if (ret) {
		PS_ERR("hook install failed %d\n", ret);
		goto err_clear;
	}

	PS_INF("ready: %d target(s) %d uid(s) scope=%u dirents=%u\n",
	       cfg->nr_targets, cfg->nr_uids, cfg->scope,
	       cfg->hide_dirents);
	return 0;

err_clear:
	ps_cfg_clear(cfg);
	ps_params_exit();
	return ret;
}

static void __exit pathshield_exit(void)
{
	/* 必须先卸 hook：否则 hook 还在执行时 cfg 已被清空 */
	ps_hooks_remove_all();
	ps_cfg_clear(ps_cfg_mut());
	ps_params_exit();
	PS_INF("unloaded\n");
}

module_init(pathshield_init);
module_exit(pathshield_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("pathshield");
MODULE_DESCRIPTION("VFS-level path masking via inline hooks (Linux 4.19 arm64)");
MODULE_VERSION(PS_VERSION_STR);
