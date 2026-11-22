/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ps_log.h - 统一日志
 *
 * 全部走 pr_fmt 前缀，方便 dmesg | grep pathshield 一次抓全。
 * PS_DBG 默认不编入；定义 PS_DEBUG 后才会输出，避免正式版多做字符串操作。
 */
#ifndef PS_LOG_H
#define PS_LOG_H

#include <linux/printk.h>

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "pathshield: " fmt

/* 常开：仅关键路径，数量可控 */
#define PS_INF(fmt, ...)  pr_info(fmt, ##__VA_ARGS__)
#define PS_WRN(fmt, ...)  pr_warn(fmt, ##__VA_ARGS__)
#define PS_ERR(fmt, ...)  pr_err(fmt, ##__VA_ARGS__)

/* 调试：默认编为空，零开销 */
#ifdef PS_DEBUG
#define PS_DBG(fmt, ...)  pr_info("[dbg] " fmt, ##__VA_ARGS__)
#else
#define PS_DBG(fmt, ...)  do { } while (0)
#endif

#endif /* PS_LOG_H */
