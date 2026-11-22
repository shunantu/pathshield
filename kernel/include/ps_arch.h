/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ps_arch.h - arm64 指令编码与内核代码写入
 *
 * 把"什么指令长什么样"与"怎么写到内核里"分开：
 *   arch/insn.c   纯编码，无副作用，可单元测试
 *   arch/text.c   写内核 .text，有副作用，失败必须回滚
 *
 * 这样以后移植到别的内核版本，只需换 text.c，insn.c 不动。
 */
#ifndef PS_ARCH_H
#define PS_ARCH_H

#include <linux/types.h>

/* ── 指令编码结果 ── */
#define PS_INSN_LEN       4
#define PS_PATCH_LEN      16	/* ldr+br+quad = 4+4+8 */
#define PS_PATCH_WORDS    (PS_PATCH_LEN / PS_INSN_LEN)

/*
 * 生成"跳到 dest"的 16 字节补丁：
 *   +0x00  ldr x16, #8     从 +8 处加载目标地址
 *   +0x04  br  x16         跳过去
 *   +0x08  .quad dest      目标地址
 *
 * 用 x16 的原因：AArch64 ABI 规定 x16/x17 是 intra-procedure-call
 * 临时寄存器（IP0/IP1），任何符合 ABI 的函数都不该假设它们跨调用保持，
 * 因此在 .text 任意位置写这两条都不会破坏现场。
 */
void ps_insn_build_jump(u32 out[PS_PATCH_WORDS], unsigned long dest);

/* ── 函数序言识别 ──
 *
 * 返回该字的 stp 偏移（字节，负数表示向下），非 stp 返回 0。
 * 仅识别 stp x29, x30, [sp, #-N]! 这一种最常见的序言。
 */
int ps_insn_stp_offset(u32 insn);

/* ── 写内核代码段 ──
 *
 * 写成对出现：ps_text_write() 成功后才允许 ps_text_flush()。
 *
 * 注意：ps_text_write() 返回 0 表示全部写入成功；返回非 0 时
 * 可能已经写入了部分字节（memcpy_toio 不是原子的）。调用方在
 * 失败时不应假设目标内存完好，而应视为该 hook 点已损坏 ——
 * 这也是 ps_hook_prepare() 把可写性检查放在最前面的原因：
 * 只要进入 commit 阶段，就要承担不可回滚的风险。
 */
int  ps_text_write(unsigned long addr, const void *buf, size_t len);
void ps_text_flush(unsigned long addr, size_t len);

/* 读内核代码（用于签名比对） */
void ps_text_read(unsigned long addr, void *buf, size_t len);

#endif /* PS_ARCH_H */
