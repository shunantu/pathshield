/* SPDX-License-Identifier: GPL-2.0 */
/*
 * glob.c - 极简 glob 匹配（只支持 ? 和 *）
 *
 * 为什么不用内核自带的 fnmatch：本机内核没有导出 fnmatch，
 * 而它要完整正则语义也用不上。这里只需要两个元字符。
 *
 * 算法：迭代式回溯，O(n*m) 最坏，实践中 m 很小（目录名长度）。
 * 不用递归，避免深目录名导致栈溢出。
 *
 * 两个参数都不假设 NUL 结尾 —— filldir 给的 name 没有终止符。
 */

#include <linux/types.h>

#include "ps_match.h"

bool ps_glob_n(const char *pat, size_t plen, const char *str, size_t slen)
{
	size_t pi = 0, si = 0;
	size_t star_pi = 0, star_si = 0;
	bool has_star = false;

	while (si < slen) {
		if (pi < plen && (pat[pi] == '?' ||
				  (pat[pi] != '*' && pat[pi] == str[si]))) {
			pi++;
			si++;
			continue;
		}
		if (pi < plen && pat[pi] == '*') {
			has_star = true;
			star_pi = ++pi;
			star_si = si;
			continue;
		}
		if (has_star) {
			pi = star_pi;
			si = ++star_si;
			continue;
		}
		return false;
	}
	while (pi < plen && pat[pi] == '*')
		pi++;
	return pi == plen;
}
