/* SPDX-License-Identifier: GPL-2.0 */
/*
 * engine.c - inline hook 核心
 *
 * trampoline 布局
 * ---------------
 *   ┌──────────────────────────────────────────┐
 *   │ 原函数前 16 字节（4 条指令，逐字节复制） │  +0x00
 *   ├──────────────────────────────────────────┤
 *   │ ldr x16, #8                              │  +0x10
 *   │ br  x16                                  │  +0x14
 *   │ .quad addr + 16                          │  +0x18
 *   └──────────────────────────────────────────┘
 *
 * 也就是说调用 tramp 等价于"执行被顶掉的原指令，然后跳回函数体第 17 字节"。
 *
 * 为什么必须逐字节复制原指令
 * --------------------------
 * 因为 trampoline 是 vmalloc 出来的新地址，原指令里任何 PC 相对寻址
 * （adr/adrp/ldr literal/b.cond）都会算错。本模块挑选的四个 hook 点
 * 前 16 字节内都只有 stp/str/mov/sub 这类不吃 PC 的指令（已逐一反汇编确认），
 * 所以可以直接复制。ps_hook_prepare() 会额外做一次检查：
 * 发现前 16 字节内出现 PC 相对指令就拒绝安装，绝不猜。
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/string.h>

#include "ps_hook.h"
#include "ps_log.h"
#include "ps_guard.h"

/*
 * 判断一条指令是否 PC 相对寻址。
 * 这些指令搬进 trampoline 后语义会变，必须拒绝。
 */
static bool ps_insn_is_pcrel(u32 insn)
{
	/* ADR / ADRP */
	if ((insn & 0x1F000000u) == 0x10000000u)
		return true;
	/* B / BL */
	if ((insn & 0x7C000000u) == 0x14000000u)
		return true;
	/* B.cond */
	if ((insn & 0xFF000010u) == 0x54000000u)
		return true;
	/* CBZ / CBNZ */
	if ((insn & 0x7E000000u) == 0x34000000u)
		return true;
	/* TBZ / TBNZ */
	if ((insn & 0x7E000000u) == 0x36000000u)
		return true;
	/* LDR (literal) */
	if ((insn & 0x3B000000u) == 0x18000000u)
		return true;
	return false;
}

static unsigned long *ps_tramp_alloc(const struct ps_hook *h)
{
	u32 *t;
	int i;
	u32 tail[PS_PATCH_WORDS];

	t = (u32 *)__vmalloc(PAGE_SIZE, GFP_KERNEL, PAGE_KERNEL_EXEC);
	if (!t)
		return NULL;

	for (i = 0; i < PS_PATCH_WORDS; i++)
		t[i] = (u32)h->orig[i];

	ps_insn_build_jump(tail, h->addr + PS_PATCH_LEN);
	for (i = 0; i < PS_PATCH_WORDS; i++)
		t[PS_PATCH_WORDS + i] = tail[i];

	ps_text_flush((unsigned long)t, PAGE_SIZE);
	return (unsigned long *)t;
}

int ps_hook_prepare(struct ps_hook *h, unsigned long addr)
{
	u32 got[PS_PATCH_WORDS];
	int i;

	if (!h || !h->replace || !addr)
		return -EINVAL;
	if (addr & 0x3)
		return -EINVAL;

	h->addr  = addr;
	h->state = PS_HOOK_IDLE;

	ps_text_read(addr, got, PS_PATCH_LEN);

	/* 签名校验：不符合就绝不往下走 */
	if (got[0] != h->sig[0] || got[1] != h->sig[1]) {
		PS_ERR("%s signature mismatch: got %08x %08x want %08x %08x\n",
		       h->name, got[0], got[1], h->sig[0], h->sig[1]);
		return -EINVAL;
	}

	/* 检查前 16 字节内有没有 PC 相对指令 */
	for (i = 0; i < PS_PATCH_WORDS; i++) {
		if (ps_insn_is_pcrel(got[i])) {
			PS_ERR("%s: insn[%d]=%08x is PC-relative, refuse\n",
			       h->name, i, got[i]);
			return -EINVAL;
		}
	}

	for (i = 0; i < PS_PATCH_WORDS; i++)
		h->orig[i] = got[i];

	h->tramp = ps_tramp_alloc(h);
	if (!h->tramp) {
		PS_ERR("%s: trampoline alloc failed\n", h->name);
		return -ENOMEM;
	}

	h->state = PS_HOOK_PREPARED;
	PS_DBG("%s prepared: addr=%px tramp=%px\n",
	       h->name, (void *)addr, h->tramp);
	return 0;
}

int ps_hook_commit(struct ps_hook *h)
{
	u32 patch[PS_PATCH_WORDS];
	int ret;

	if (!h || h->state != PS_HOOK_PREPARED)
		return -EINVAL;

	ps_insn_build_jump(patch, (unsigned long)h->replace);

	ret = ps_text_write(h->addr, patch, PS_PATCH_LEN);
	if (ret) {
		PS_ERR("%s: write failed %d\n", h->name, ret);
		return ret;
	}
	ps_text_flush(h->addr, PS_PATCH_LEN);

	h->state = PS_HOOK_ACTIVE;
	PS_INF("%s hooked at %px -> %px\n",
	       h->name, (void *)h->addr, h->replace);
	return 0;
}

void ps_hook_revert(struct ps_hook *h)
{
	if (!h || h->state != PS_HOOK_ACTIVE)
		return;

	ps_text_write(h->addr, h->orig, PS_PATCH_LEN);
	ps_text_flush(h->addr, PS_PATCH_LEN);
	h->state = PS_HOOK_PREPARED;
	PS_INF("%s reverted\n", h->name);
}

void ps_hook_release(struct ps_hook *h)
{
	if (!h)
		return;
	if (h->tramp) {
		vfree(h->tramp);
		h->tramp = NULL;
	}
	h->state = PS_HOOK_IDLE;
}

int ps_hook_install(struct ps_hook *h, unsigned long addr)
{
	int ret = ps_hook_prepare(h, addr);

	if (ret)
		return ret;

	ret = ps_hook_commit(h);
	if (ret) {
		ps_hook_release(h);
		return ret;
	}
	return 0;
}
