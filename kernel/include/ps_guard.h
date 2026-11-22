/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ps_guard.h - 每 CPU 重入保护
 *
 * 为什么需要：
 *   hook 的 inode_permission 会被 VFS 自身反复调用（kern_path 内部就调）。
 *   如果 hook 里再去做路径解析，就会无限递归。
 *
 * 实现：
 *   每 CPU 一个深度计数。进入 hook 时若计数非 0，说明本 CPU 已在 hook 内，
 *   直接放行不做任何判断，避免递归也避免死锁。
 *
 * 为什么不用 per-task：hook 可能在中断/软中断上下文被触发，
 *   per-CPU 语义更明确，且访问开销更低（this_cpu_ptr 无需禁用抢占以外的操作）。
 */
#ifndef PS_GUARD_H
#define PS_GUARD_H

#include <linux/types.h>

/*
 * 尝试进入临界区。返回 true 表示可以继续执行判定逻辑；
 * 返回 false 表示已在内层，调用方必须直接调用原函数。
 *
 * 成对使用，任何 return 路径都必须调用 ps_guard_leave()。
 */
bool ps_guard_enter(void);
void ps_guard_leave(void);

#endif /* PS_GUARD_H */
