/* SPDX-License-Identifier: GPL-2.0 */
/*
 * inode.c - inode 层匹配（热路径）
 *
 * 这是整个模块被调用最频繁的函数：每一次 stat/open/access 都会走到。
 * 因此实现上做了三件事：
 *
 *   1. 不加锁
 *      调用方（hook）已经在 guard 保护下；ps_cfg 只在模块加载时写，
 *      装完 hook 之后就是只读的，读者之间无需互斥。
 *
 *   2. 先比 ino 再比 dev
 *      Android 上 inode 号分布较散，ino 不同的概率远高于 dev 不同，
 *      先比 ino 能更早短路，减少一次比较。
 *
 *   3. 热字段前置
 *      dev/ino/resolved/enabled 全部落在结构体头部同一 cache line，
 *      一次访存就能决定是否命中。
 */

#include <linux/types.h>

#include "ps_types.h"
#include "ps_match.h"

struct ps_target *ps_match_inode(const struct ps_config *cfg,
				 dev_t dev, ino_t ino)
{
	int i;

	if (unlikely(!cfg || !cfg->nr_targets))
		return NULL;

	for (i = 0; i < cfg->nr_targets; i++) {
		struct ps_target *t = &cfg->targets[i];

		/*
		 * unlikely 提示编译器：绝大多数调用不会命中，
		 * 让命中分支落到冷路径，减小热路径的指令占用。
		 */
		if (unlikely(!t->resolved || !t->enabled))
			continue;
		if (likely(t->ino != ino))
			continue;
		if (likely(t->dev != dev))
			continue;
		return t;
	}
	return NULL;
}
