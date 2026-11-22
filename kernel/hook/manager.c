/* SPDX-License-Identifier: GPL-2.0 */
/*
 * manager.c - hook 点注册表
 *
 * 唯一需要改动来"新增一个 hook 点"的地方。
 * 每个 hook 点自己负责签名、替换逻辑；这里只管遍历安装/卸载。
 *
 * 安装策略
 * --------
 *   inode_permission、vfs_getattr 是必需的，失败则整体回滚。
 *   filldir64 是可选增强，失败只告警不中断 —— 少一层目录过滤
 *   不影响核心功能，不该因此让整个模块加载失败。
 *
 * 卸载顺序与安装相反，保证依赖关系正确。当前三个 hook 之间没有
 * 依赖，但将来若加入有先后约束的 hook，这个顺序已经写对了。
 *
 * 为什么用显式计数而不是依赖数组末尾的 NULL
 * --------------------------------------------
 * 用 PS_ARRAY_LEN(x) - 2 计算倒序起点，等于假设"末尾一定有一个 NULL"。
 * 一旦有人改动数组时忘了保留 NULL，循环会越界读。
 * 这里改成显式的 PS_NR_REQUIRED / PS_NR_OPTIONAL 常量，
 * 由编译器在数组长度不一致时报错，比运行时越界安全得多。
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>

#include "ps_core.h"
#include "ps_hook.h"
#include "ps_log.h"

/* 必需：失败即整体回滚，模块不加载 */
static const struct ps_hook_ops *const ps_required[] = {
	&ps_ops_inode_perm,
	&ps_ops_vfs_getattr,
};

/* 可选：失败仅告警，不影响核心功能 */
static const struct ps_hook_ops *const ps_optional[] = {
	&ps_ops_filldir,
};

#define PS_NR_REQUIRED (sizeof(ps_required) / sizeof(ps_required[0]))
#define PS_NR_OPTIONAL (sizeof(ps_optional) / sizeof(ps_optional[0]))

/*
 * 编译期校验：数组长度必须追加上面的常量保持一致。
 * 如果有人加了 hook 却忘了同步常量，这里会直接编不过。
 */
static_assert(PS_NR_REQUIRED == 2, "update PS_NR_REQUIRED");
static_assert(PS_NR_OPTIONAL == 1, "update PS_NR_OPTIONAL");

static int ps_install_one(const struct ps_hook_ops *ops)
{
	unsigned long addr;
	int ret;

	addr = kallsyms_lookup_name(ops->symbol);
	if (!addr) {
		PS_WRN("symbol %s not found\n", ops->symbol);
		return -ENOENT;
	}

	ret = ps_hook_install(ops->hook, addr);
	if (ret)
		PS_ERR("%s install failed: %d\n", ops->name, ret);
	return ret;
}

int ps_hooks_install_all(void)
{
	unsigned int i;
	int ret;

	for (i = 0; i < PS_NR_REQUIRED; i++) {
		ret = ps_install_one(ps_required[i]);
		if (ret) {
			/* 回滚已装的那些 */
			while (i-- > 0) {
				ps_hook_revert(ps_required[i]->hook);
				ps_hook_release(ps_required[i]->hook);
			}
			return ret;
		}
	}

	for (i = 0; i < PS_NR_OPTIONAL; i++) {
		if (ps_install_one(ps_optional[i]))
			PS_WRN("%s skipped (optional)\n",
			       ps_optional[i]->name);
	}

	return 0;
}

void ps_hooks_remove_all(void)
{
	unsigned int i;

	/* 逆序卸载：与安装顺序相反 */
	for (i = PS_NR_OPTIONAL; i-- > 0;) {
		ps_hook_revert(ps_optional[i]->hook);
		ps_hook_release(ps_optional[i]->hook);
	}
	for (i = PS_NR_REQUIRED; i-- > 0;) {
		ps_hook_revert(ps_required[i]->hook);
		ps_hook_release(ps_required[i]->hook);
	}
}
