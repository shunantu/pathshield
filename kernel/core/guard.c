/* SPDX-License-Identifier: GPL-2.0 */
/*
 * guard.c - 每 CPU 重入保护的实现
 *
 * 实现说明
 * --------
 * 用 DEFINE_PER_CPU 而不是 atomic：hook 在同一个 CPU 上不会并发进入
 * （除非发生抢占，而那正是我们要拦的情况），per-CPU 变量无需原子操作，
 * 一次 this_cpu_ptr + 读写就够了，比 atomic_inc 便宜。
 *
 * barrier() 的作用：防止编译器把 *d = 1 与后续的 hook 判断重排。
 */

#include <linux/percpu.h>
#include <linux/compiler.h>

#include "ps_guard.h"

static DEFINE_PER_CPU(int, ps_guard_depth);

bool ps_guard_enter(void)
{
	int *d = this_cpu_ptr(&ps_guard_depth);

	if (*d)
		return false;

	*d = 1;
	barrier();
	return true;
}

void ps_guard_leave(void)
{
	int *d = this_cpu_ptr(&ps_guard_depth);

	barrier();
	*d = 0;
}
