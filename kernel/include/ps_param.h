/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ps_param.h - 模块参数
 *
 * 所有 module_param 集中在一个文件，方便一眼看到模块对外的全部接口。
 * 其它模块只通过 ps_params_get() 只读访问，不直接 extern 变量，
 * 避免"谁都能改参数"的混乱。
 */
#ifndef PS_PARAM_H
#define PS_PARAM_H

#include "ps_types.h"

/* 只读快照，返回内核参数当前值 */
const struct ps_params *ps_params_get(void);

int  ps_params_init(void);
void ps_params_exit(void);

#endif /* PS_PARAM_H */
