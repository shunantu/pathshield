/* SPDX-License-Identifier: GPL-2.0 */
/*
 * insn.c - arm64 指令编码，纯函数，无内核副作用
 *
 * 本文件不碰内存、不加锁、不分配，可脱离内核单独编译测试。
 */

#include <linux/types.h>
#include <linux/string.h>

#include "ps_arch.h"

/* ldr x16, #8   →  imm19=2, Rt=16, opc=01 */
#define PS_INSN_LDR_X16   0x58000050u
/* br x16        →  Rn=16 */
#define PS_INSN_BR_X16    0xd61f0200u

void ps_insn_build_jump(u32 out[PS_PATCH_WORDS], unsigned long dest)
{
	u64 q = (u64)dest;

	out[0] = PS_INSN_LDR_X16;
	out[1] = PS_INSN_BR_X16;
	/*
	 * 用 memcpy 而不是 *(u64 *)&out[2]：
	 * out 是 u32 数组，&out[2] 只保证 4 字节对齐，
	 * 在 strict-align 内核上直接做 u64 store 会触发 alignment fault。
	 */
	memcpy(&out[2], &q, sizeof(q));
}

int ps_insn_stp_offset(u32 insn)
{
	u32 imm7;
	int off;

	/* STP (pre-index, 64-bit): 位段 [31:30]=10, [29:27]=101, [26]=0, [25:23]=011 */
	if ((insn & 0xFFC00000u) != 0xA9800000u)
		return 0;
	/* 只接受 pre-index（opc=2） */
	if (((insn >> 22) & 0x3u) != 0x2u)
		return 0;
	/* 只接受 64-bit（V=0 已由掩码保证，size 隐含） */

	imm7 = (insn >> 15) & 0x7Fu;
	off = (int)imm7;
	if (off & 0x40)		/* sign extend 7 bit */
		off -= 0x80;

	return off * 8;
}
