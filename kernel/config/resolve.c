/* SPDX-License-Identifier: GPL-2.0 */
/*
 * resolve.c - 把路径解析成 (dev, ino)
 *
 * 为什么要解析
 * ------------
 * VFS hook 拿到的是 inode 指针，早就没有原始路径字符串了。
 * 唯一稳定的身份就是 (dev, ino) —— 只要文件还在，这两个值不变。
 *
 * 为什么要 guard
 * --------------
 * 解析必须调 kern_path()，而 kern_path 内部会调 inode_permission。
 * 热重载场景下 hook 可能已经装上了，如果不进 guard，解析自己的目标
 * 路径时会被自己的规则拦住，得到 -ENOENT，于是永远解析失败。
 *
 * 通配符处理
 * ----------
 * 含 ? 或 * 的路径无法解析成 inode（它本身不对应具体文件），
 * 这类目标标记为 PS_TGT_GLOB，只参与 filldir 的名字匹配。
 * 同时记录父目录信息以便后续需要时使用。
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/namei.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/string.h>

#include "ps_types.h"
#include "ps_core.h"
#include "ps_guard.h"
#include "ps_log.h"

/* 从完整路径取出 basename（不修改原串） */
static void ps_set_basename(struct ps_target *t)
{
	const char *slash;
	size_t n;

	slash = strrchr(t->path, '/');
	slash = slash ? slash + 1 : t->path;
	n = strlen(slash);

	if (n >= PS_NAME_MAX)
		n = PS_NAME_MAX - 1;

	memcpy(t->name, slash, n);
	t->name[n] = '\0';
	t->name_len = (u16)n;
}

static bool ps_has_glob(const char *s)
{
	for (; *s; s++) {
		if (*s == '?' || *s == '*')
			return true;
	}
	return false;
}

/* 解析单个目标。返回 true 表示该目标参与匹配。 */
static bool ps_resolve_one(struct ps_target *t)
{
	struct path p;
	struct kstat st;
	int err;

	ps_set_basename(t);

	if (ps_has_glob(t->path)) {
		/*
		 * 通配符：没有 inode 身份，只靠名字匹配。
		 * 标记 resolved=0 使 inode 层跳过它，但 filldir 仍会用。
		 */
		t->kind = PS_TGT_GLOB;
		t->resolved = 0;
		PS_DBG("glob target: %s\n", t->path);
		return true;
	}

	t->kind = PS_TGT_INODE;

	if (!ps_guard_enter())
		return false;

	err = kern_path(t->path, LOOKUP_FOLLOW, &p);
	if (err) {
		ps_guard_leave();
		PS_WRN("target missing, skipped: %s\n", t->path);
		return false;
	}

	err = vfs_getattr(&p, &st, STATX_INO, 0);
	path_put(&p);
	ps_guard_leave();

	if (err) {
		PS_WRN("getattr failed %d: %s\n", err, t->path);
		return false;
	}

	t->dev      = st.dev;
	t->ino      = st.ino;
	t->resolved = 1;

	PS_INF("target ok: %s -> dev=%u ino=%lu\n",
	       t->path, (unsigned)t->dev, (unsigned long)t->ino);
	return true;
}

int ps_cfg_resolve(struct ps_config *cfg)
{
	int i;
	int kept = 0;
	int dropped = 0;

	if (!cfg)
		return -EINVAL;

	/*
	 * 不压缩数组：失败的目标保留在原位但标记 enabled=0。
	 * 保留的理由是诊断 —— dmesg 与 /sys/module/pathshield/parameters
	 * 里都能看到"你配了这个但没生效"，比静默消失有用。
	 * 匹配层会跳过 enabled=0 的项，运行时无额外开销。
	 */
	for (i = 0; i < cfg->nr_targets; i++) {
		struct ps_target *src = &cfg->targets[i];
		bool ok = ps_resolve_one(src);

		if (!ok) {
			/*
			 * 设备上路径不存在时保留记录但标记 disabled，
			 * 这样 dmesg 里能看到"你配了这个但没生效"，
			 * 而不是静默消失。
			 */
			src->enabled = 0;
			dropped++;
			kept++;		/* 仍占位置，仅供诊断 */
			continue;
		}
		kept++;
	}

	PS_INF("resolve done: %d kept, %d dropped\n", kept, dropped);

	/*
	 * 至少要有一个可用的 inode 目标，否则 hook 装了也没意义。
	 * 全是 glob 或全失败的情况下，调用方应该拒绝加载。
	 */
	for (i = 0; i < cfg->nr_targets; i++) {
		if (cfg->targets[i].enabled && cfg->targets[i].resolved)
			return 0;
	}
	return -ENOENT;
}
