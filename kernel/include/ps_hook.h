/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ps_hook.h - inline hook 引擎接口
 *
 * 一个 hook 的生命周期：
 *   ps_hook_prepare()   分配 trampoline，检查签名，不写内存
 *   ps_hook_commit()    写补丁（唯一有副作用的步骤）
 *   ps_hook_revert()    恢复原字节
 *   ps_hook_release()   释放 trampoline
 *
 * 拆成四个阶段是为了让"准备失败"和"提交失败"可以分别处理：
 * 准备阶段失败时内核完全没被改动，可以直接返回；
 * 提交阶段才失败时（理论上不会，因为 prepare 已探过可写性）必须回滚。
 */
#ifndef PS_HOOK_H
#define PS_HOOK_H

#include <linux/types.h>
#include "ps_types.h"
#include "ps_arch.h"

struct ps_hook {
	/* 配置（调用者填） */
	const char *name;		/* 符号名，仅用于日志         */
	void       *replace;		/* 替换函数                   */
	u32         sig[2];		/* 期望的原函数前 8 字节       */

	/* 运行时（引擎填） */
	unsigned long       addr;	/* 目标函数运行期地址         */
	unsigned long       orig[PS_PATCH_WORDS];	/* 原 16 字节      */
	unsigned long      *tramp;	/* 可执行 trampoline          */
	u8                  state;	/* enum ps_hook_state         */
};

enum ps_hook_state {
	PS_HOOK_IDLE    = 0,
	PS_HOOK_PREPARED = 1,
	PS_HOOK_ACTIVE   = 2,
};

int  ps_hook_prepare(struct ps_hook *h, unsigned long addr);
int  ps_hook_commit(struct ps_hook *h);
void ps_hook_revert(struct ps_hook *h);
void ps_hook_release(struct ps_hook *h);

/* 一次性安装（prepare+commit），失败自动清理 */
int  ps_hook_install(struct ps_hook *h, unsigned long addr);

/*
 * 通过 trampoline 调用原函数。
 *
 * 为什么不能直接调用符号：inline hook 已经把函数前 16 字节换掉了，
 * 直接调符号会从被替换的入口跑，立刻又跳回 hook，形成死循环。
 * trampoline 里存的是原 16 字节 + 跳回 addr+16，才是"真正的原函数入口"。
 */
static inline unsigned long ps_hook_real(const struct ps_hook *h)
{
	return (unsigned long)h->tramp;
}

#endif /* PS_HOOK_H */
