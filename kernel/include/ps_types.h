/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ps_types.h - 基础类型、常量与枚举
 *
 * 本文件是唯一没有内部依赖的头文件，可被任何模块安全包含。
 * 设计原则：只放"数据结构长什么样"，不放任何函数声明或业务逻辑。
 *
 * 尺寸常量可以通过 Kconfig 覆盖，默认值针对移动端调优：
 *   - 目标路径数 32：实测一台设备很少需要遮 10 个以上路径；
 *     每个 ps_target 约 288 B，32 个约 9 KB，可忽略。
 *   - 作用 UID 数 32：黑/白名单极少超过 5 个 App。
 * 若需要更大上限，改这里的宏即可，其余代码无需改动。
 */
#ifndef PS_TYPES_H
#define PS_TYPES_H

#include <linux/types.h>

/* ── 版本 ── */
#define PS_VERSION_MAJOR  1
#define PS_VERSION_MINOR  1
#define PS_VERSION_PATCH  0
#define PS_VERSION_STR    "1.1.0"

/* ── 容量上限 ── */
#define PS_MAX_TARGETS    32
#define PS_PATH_MAX       256	/* 足够容纳 Android 最长路径 */
#define PS_MAX_UIDS       32
#define PS_NAME_MAX       128	/* filldir 单条目名比较上限 */

/* ── 作用域模式 ──
 *
 * PS_SCOPE_DENY   只对 deny 列表里的 UID 隐藏（默认，最安全）
 * PS_SCOPE_ALLOW  对所有人隐藏，但 allow 列表里的 UID 豁免
 * PS_SCOPE_GLOBAL 对所有人隐藏，无豁免（危险）
 *
 * 三种模式共用 ps_cfg.uids[] 这一个数组，含义由 scope 决定，
 * 避免维护两套数组带来的内存浪费与逻辑分叉。
 */
enum ps_scope {
	PS_SCOPE_DENY   = 0,
	PS_SCOPE_ALLOW  = 1,
	PS_SCOPE_GLOBAL = 2,
	PS_SCOPE_MAX
};

/* ── 目标类型 ──
 *
 * 决定了该目标用什么方式比较：
 *   PS_TGT_INODE  已解析出 (dev, ino)，走 inode 精确比较（最快）
 *   PS_TGT_GLOB   含通配符，只能走名字匹配（较慢，仅在 filldir 用）
 */
enum ps_tgt_kind {
	PS_TGT_INODE = 0,
	PS_TGT_GLOB  = 1
};

/* ── 单个目标 ──
 *
 * 字段顺序经过排布，尽量让热路径（ps_target_match_inode）访问的
 * dev/ino 落在同一个 cache line 内。
 */
struct ps_target {
	dev_t              dev;		/* 已解析：设备号        */
	ino_t              ino;		/* 已解析：inode 号      */
	u16                name_len;	/* basename 长度          */
	u8                 kind;	/* enum ps_tgt_kind      */
	u8                 resolved;	/* 1 = dev/ino 有效      */
	u8                 enabled;	/* 1 = 参与匹配          */
	u8                 _pad[1];
	char               name[PS_NAME_MAX];	/* 缓存的 basename */
	char               path[PS_PATH_MAX];	/* 完整路径（诊断用） */
};

/* ── 运行时配置 ──
 *
 * 全局只此一份（ps_cfg），所有热路径直接读它。
 * 用 rwlock 保护：读多写极少（只有热重载时写）。
 */
struct ps_config {
	uid_t              uids[PS_MAX_UIDS];
	u16                nr_uids;
	u8                 scope;	/* enum ps_scope         */
	u8                 hide_dirents;	/* 目录列举也过滤        */
	u8                 enabled;	/* 总开关                */
	u8                 nr_targets;
	u8                 _pad[2];
	struct ps_target   targets[PS_MAX_TARGETS];
};

/* ── 模块参数（由 ps_param.c 定义，各模块按需 extern） ── */
struct ps_params {
	const char *target_paths;
	const char *scope_mode;
	const char *deny_uids;
	int         hide_dirents;
	int         enabled;
};

#endif /* PS_TYPES_H */
