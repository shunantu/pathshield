/* SPDX-License-Identifier: GPL-2.0 */
/*
 * text.c - 往内核 .text 写指令
 *
 * 困境
 * ----
 * 本机 (PD2118 / 4.19.152) 的内核配置：
 *
 *   CONFIG_STRICT_KERNEL_RWX=y     -> .text 页表只读
 *   text_poke / set_memory_rw      -> 两个符号都不存在（已用 /proc/kallsyms 确认）
 *   __flush_icache_range           -> 存在，可用于刷 I-cache
 *
 * 结论：没有官方的"临时解除只读"接口可用。
 *
 * 采用的办法
 * ----------
 * 走线性映射别名。内核把同一块物理页映射了两次：
 *
 *   镜像映射   addr            -> 只读（用来执行）
 *   线性映射   __va(__pa(addr)) -> 由 mark_linear_text_alias_ro() 决定
 *
 * 如果 mark_linear_text_alias_ro() 已经把线性别名也设只读了，
 * 这条路同样走不通。此时 memcpy_toio() 会触发页错误，内核明确报
 * "Unable to handle kernel paging request"，问题一定能被发现，
 * 代价是一次 panic —— 没有 text_poke 就无法安全探测，这是已知风险。
 *
 * 地址合法性判断（重要）
 * ----------------------
 * 绝不能拿 __pa() 的结果去和 PAGE_OFFSET 比较：
 *   PAGE_OFFSET  是虚拟地址常量 0xffffffc000000000
 *   __pa() 返回的物理地址是 0x80000000 量级
 *   两者比较恒成立，会让函数永远返回 NULL。
 *
 * 正确做法是先判断 addr 本身是否落在线性映射窗口内，
 * 用 virt_addr_valid() 做这个判断。
 *
 * 性能
 * ----
 * 只在加载/卸载时写 16 字节，热路径完全不受影响。
 * __flush_icache_range 是全局 I-cache 同步，只在加载时执行一次。
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <asm/cacheflush.h>
#include <asm/memory.h>
#include <asm/pgtable.h>

#include "ps_log.h"
#include "ps_arch.h"

/*
 * 取得 addr 的可写线性别名。
 *
 * virt_addr_valid() 判断该虚拟地址是否属于线性映射的有效页：
 * 内核镜像（_text 所在）在启动后被搬进线性映射的同一物理页上，
 * 因此 _text 范围内的地址会通过这个检查。
 *
 * 对 vmalloc / 模块地址返回 false，正是我们想要的行为 ——
 * 那些地址不该被 __pa 转换。
 */
static void __iomem *ps_linear_alias(unsigned long addr)
{
	phys_addr_t phys;

	if (!virt_addr_valid((const void *)addr)) {
		PS_ERR("addr %px is not in linear map\n", (void *)addr);
		return NULL;
	}

	phys = __pa(addr);
	if (!phys) {
		PS_ERR("__pa(%px) returned 0\n", (void *)addr);
		return NULL;
	}

	return (void __iomem *)__va(phys);
}

int ps_text_write(unsigned long addr, const void *buf, size_t len)
{
	void __iomem *alias;

	if (!addr || !buf || !len)
		return -EINVAL;
	if (addr & 0x3)
		return -EINVAL;		/* 必须 4 字节对齐 */

	alias = ps_linear_alias(addr);
	if (!alias)
		return -EFAULT;

	/*
	 * 到这里别名一定有效。memcpy_toio 走的是 __raw_writel 族，
	 * 不会有编译器重排问题，也不依赖 strict-align。
	 */
	memcpy_toio(alias, buf, len);

	return 0;
}

void ps_text_flush(unsigned long addr, size_t len)
{
	/*
	 * 必须按内核虚拟地址刷 I-cache（不是物理地址）。
	 * __flush_icache_range 内部会处理跨核同步。
	 */
	__flush_icache_range(addr, addr + len);
}

void ps_text_read(unsigned long addr, void *buf, size_t len)
{
	memcpy(buf, (const void *)addr, len);
}
