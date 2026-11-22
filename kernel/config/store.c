/* SPDX-License-Identifier: GPL-2.0 */
/*
 * store.c - 全局配置的唯一存储点
 *
 * 只暴露两个访问器：
 *   ps_cfg_get()  只读指针，热路径用
 *   ps_cfg_mut()  可写指针，只在初始化/重载时用
 *
 * 为什么不做锁
 * ------------
 * ps_cfg 的生命周期被严格约束：
 *   1. 模块 init 时写满
 *   2. hook 安装之后不再写
 *   3. 模块 exit 时先卸 hook 再清空
 *
 * 也就是说"写"发生在没有任何 hook 会读它的时候。
 * 加锁只会在热路径上白白付出原子操作代价。
 *
 * 如果将来要实现运行期热重载而不 rmmod，就必须改成 RCU 方案：
 *   - 分配新 ps_config
 *   - rcu_assign_pointer 换指针
 *   - synchronize_rcu 后释放旧的
 * 接口已经预留（ps_cfg_mut 只在这里被调用），改动范围可控。
 */

#include <linux/types.h>
#include <linux/string.h>

#include "ps_types.h"
#include "ps_core.h"

static struct ps_config g_cfg;

const struct ps_config *ps_cfg_get(void)
{
	return &g_cfg;
}

struct ps_config *ps_cfg_mut(void)
{
	return &g_cfg;
}

void ps_cfg_clear(struct ps_config *cfg)
{
	if (cfg)
		memset(cfg, 0, sizeof(*cfg));
}
