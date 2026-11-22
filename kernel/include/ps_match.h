/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ps_match.h - 路径匹配
 *
 * 两层匹配，各自独立可测：
 *   inode 层   ps_match_inode()   热路径，O(n) 但 n<=32，全是整数比较
 *   name  层   ps_match_name()    只在 filldir 用，做字符串比较
 *
 * 分离的原因：inode 匹配不需要任何字符串操作，放到独立函数里可以让
 * 编译器把它内联进 hook，而字符串部分保持 out-of-line，减少热路径体积。
 */
#ifndef PS_MATCH_H
#define PS_MATCH_H

#include <linux/types.h>
#include "ps_types.h"

/* inode 精确匹配。命中返回目标指针，未命中返回 NULL。 */
struct ps_target *ps_match_inode(const struct ps_config *cfg,
				 dev_t dev, ino_t ino);

/* 名字匹配（filldir 用）。name 不保证 NUL 结尾，必须用 len。 */
bool ps_match_name(const struct ps_config *cfg, const char *name, size_t len);

/* 单个 glob 匹配，pat 与 str 都不假设 NUL 结尾 */
bool ps_glob_n(const char *pat, size_t plen, const char *str, size_t slen);

/* UID 是否在作用域内 */
bool ps_match_uid(const struct ps_config *cfg, uid_t uid);

#endif /* PS_MATCH_H */
