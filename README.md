# PathShield — 4.19 arm64 内核级路径遮罩

给 iQOO Neo5 活力版（PD2118 / 4.19.152-perf+）写的 VFS 层路径遮罩。
在 VFS 层拦截，所以**裸 `svc #0` 系统调用也绕不过**。

---

## 架构

代码按职责分层，每个目录只做一件事。新增功能时改动范围可控。

```
kernel/
├── include/          公共头文件（8 个）
│   ├── ps_types.h      类型/常量/结构体，零依赖，可被任何文件包含
│   ├── ps_log.h        日志宏，PS_DBG 默认编为空
│   ├── ps_guard.h      重入保护接口
│   ├── ps_arch.h       arm64 指令编码 + 内核代码写入
│   ├── ps_hook.h       hook 引擎接口（prepare/commit/revert/release）
│   ├── ps_match.h      匹配层接口
│   ├── ps_param.h      模块参数接口
│   └── ps_core.h       主控接口 + hook 点注册表
│
├── core/             基础设施（40 行）
│   └── guard.c         每 CPU 重入保护
│
├── arch/             架构层（158 行）
│   ├── insn.c          指令编码，纯函数，无副作用，可单测
│   └── text.c          写内核 .text，有副作用，失败明确报错
│
├── hook/             hook 层（421 行）
│   ├── engine.c        四阶段生命周期：prepare→commit→revert→release
│   ├── manager.c       注册表，必需/可选分流
│   └── points/         每个 hook 点一个文件
│       ├── inode_perm.c    inode_permission
│       ├── getattr.c       vfs_getattr
│       └── filldir.c       filldir64
│
├── match/            匹配层（163 行）
│   ├── inode.c         inode 精确匹配（热路径）
│   ├── name.c          名字匹配 + UID 作用域
│   └── glob.c          极简 glob（? 和 *）
│
├── config/           配置层（332 行）
│   ├── param.c         module_param 定义
│   ├── parse.c         纯字符串解析，无副作用，可单测
│   ├── resolve.c       路径 → (dev, ino)
│   └── store.c         全局配置唯一存储点
│
└── api/              对外接口（92 行）
    └── module.c        模块入口/出口
```

**总计 1,822 行 / 48,727 字节**，注释占比 12%~68%，核心文件平均 30%。

---

## 扩展方式（不需要改多处）

### 新增一个 hook 点

1. 在 `hook/points/` 新建一个 `.c`，定义 `struct ps_hook_ops`
2. 在 `ps_core.h` 加一行 `extern`
3. 在 `hook/manager.c` 的注册表数组加一行
4. 在 `Kbuild` 的 `pathshield-y` 加一行

**其它文件一律不动。**

### 新增一种匹配方式

在 `match/` 新建文件，在 `ps_match.h` 加声明。
`ps_types.h` 里 `enum ps_tgt_kind` 加一个枚举值。

### 新增一个配置项

1. `config/param.c` 加 `module_param`
2. `ps_types.h` 的 `struct ps_params` / `struct ps_config` 加字段
3. `config/parse.c` 加解析逻辑

---

## 性能设计

| 关注点 | 做法 |
|---|---|
| 热路径 | `ps_match_inode()` 只有整数比较，无锁无分配 |
| 分支预测 | 全部用 `likely/unlikely` 标注，未命中走冷路径 |
| 缓存友好 | `dev/ino/resolved/enabled` 同 cache line |
| 内存占用 | `ps_config` 约 9.5 KB（32 目标 + 32 UID），静态分配，无堆 |
| 无锁设计 | 配置装完 hook 后只读，读者之间无需互斥 |
| 调试开销 | `PS_DBG` 默认编为空，一行宏不存在 |
| 功耗 | hook 本身只是几次比较，不引入轮询/定时器/唤醒 |
| I-cache | 只在加载时刷一次，之后不影响 |
| 指令缓存 | `ps_match_inode` 可被内联，字符串比较 out-of-line |

---

## 为什么不是 pathmask

`KDJCPM/LKM-PathMask` 用 `kretprobe`，而这台机器：

```
# CONFIG_KPROBES is not set
CONFIG_HAVE_KPROBES=y
# CONFIG_FUNCTION_TRACER is not set
```

`/proc/kallsyms` 里 `register_kprobe` / `register_kretprobe` 命中数 **0**。
kprobe 和 ftrace 两条路都被内核配置关死了。

PathShield 改用 **inline hook**：直接改目标函数开头 16 字节为
`ldr x16,#8; br x16; .quad hook`，原指令搬进 trampoline。

---

## 为什么不是 libc hook（FuseHide 那条路）

Maple（`com.maple.detect` v2.8.0，`libmapledetect.so` 1,528,416 B）反汇编实测：

```
nr=56   openat       x84   mov w8,#0x38 ; svc #0
nr=79   newfstatat   x58   mov w8,#0x4f ; svc #0
nr=291  statx        x56   mov w8,#0x123; svc #0
nr=61   getdents64   x16
nr=78   readlinkat   x13
nr=48   faccessat    x9
```

**全部是裸系统调用，不经 libc。** 任何 `dlsym`/`PLT` hook 都拦不住。

---

## Hook 点（已用内核镜像反汇编验证）

正确公式：`file_off = addr - 0xffffff8008080000`（**不加 0x80000**）

| 函数 | 偏移 | 首条指令 | 用途 |
|---|---|---|---|
| `inode_permission` | `0x2957c8` | `a9bf7bfd` | stat/open/access/exec 全走这里 |
| `vfs_getattr` | `0x28f018` | `a9bd7bfd` | stat/statx/fstatat |
| `filldir64` | `0x2a1c80` | `a9ba7bfd` | 目录列举过滤 |

`inode_permission` 在这台机器上只有 32 字节（`+0x18` 就是 `ret`），是最干净的 patch 点。

---

## 编译

见 [docs/BUILD.md](docs/BUILD.md)。最省事：推到 GitHub，Actions 自动编。

---

## WebUI

模块自带 WebUI（`module/webroot/index.html`），在 KSU 管理器里点开即可。

功能：
- 输入路径 → 校验（6 道检查）→ 通过才能添加
- 内置必隐藏项单独展示，不可删
- 受保护路径自动拒绝并标红
- 黑/白/全局三种作用域
- UID 列表编辑
- 保存 / 保存并热重载 / 暂停隐藏 / 刷新状态
- 实时日志

---

## 已知限制

1. **目标路径必须在加载时存在**。不存在就无法解析 `(dev, ino)`，会被跳过（但会留在列表里标 disabled，dmesg 可见）。
2. **只能遮 VFS 层**。`/proc/modules` 里的 `kernelsu`、`TracerPid`、`SUSFS` 时间侧信道不在范围内。
3. **Maple 还有 12 类 `nativeProbe*` 检测**，本模块只处理路径类。
4. **首次加载风险**：写 `.text` 依赖线性别名可写。如果 `mark_linear_text_alias_ro` 已把它设只读，加载时会有一次 page fault。这是已知风险，见 `arch/text.c` 注释。

---

## 目录

```
kernel/      内核模块源码（分层）
module/      KSU 模块包装 + WebUI
docs/        编译与移植文档
tools/       打包脚本
.github/     CI 编译
_archive/    v1 扁平版备份（供对比）
```

---

## 许可

GPL-2.0。内核模块必须遵守 GPL。
