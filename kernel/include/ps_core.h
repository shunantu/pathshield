/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ps_core.h - 内核主控对外接口
 *
 * 这层是"胶水"：把配置、hook 点、匹配层粘起来。
 * 新增一个 hook 点只需要：
 *   1. 在 hook/points/ 下新建一个 .c，定义 struct ps_hook_ops
 *   2. 在 ps_core.c 的注册表数组里加一行
 * 不需要改任何其它文件。
 */
#ifndef PS_CORE_H
#define PS_CORE_H

#include "ps_types.h"
#include "ps_hook.h"

/* 一个 hook 点的描述。每个 hook 点 .c 文件导出一个这样的实例。 */
struct ps_hook_ops {
	const char          *name;	/* 日志用名                */
	const char          *symbol;	/* kallsyms 查的名字        */
	void                *replace;	/* 替换函数                */
	u32                  sig[2];	/* 期望签名                */
	struct ps_hook      *hook;	/* 引擎状态存储            */
};

/* 配置访问。所有 hook 都通过它拿 ps_cfg，便于将来换成 RCU 方案。 */
const struct ps_config *ps_cfg_get(void);
struct ps_config       *ps_cfg_mut(void);

/* 从参数解析配置 */
int  ps_cfg_parse(struct ps_config *cfg);

/* 在 guard 保护下解析路径为 (dev, ino) */
int  ps_cfg_resolve(struct ps_config *cfg);

/* 清空 */
void ps_cfg_clear(struct ps_config *cfg);

/* 各 hook 点的 ops，定义在 hook/points/*.c */
extern const struct ps_hook_ops ps_ops_inode_perm;
extern const struct ps_hook_ops ps_ops_vfs_getattr;
extern const struct ps_hook_ops ps_ops_filldir;

/* hook/manager.c */
int  ps_hooks_install_all(void);
void ps_hooks_remove_all(void);

#endif /* PS_CORE_H */
