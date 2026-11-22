# 编译 pathshield.ko

## 核心问题

编译内核模块需要三样：

| 需要 | 有没有 | 说明 |
|---|---|---|
| 4.19.152 内核源码树 | ❌ | vivo 没公开（GitHub 搜 `vivo PD2118 kernel` = 0 结果） |
| 与设备一致的 clang | ⚠️ | 设备 banner 写 `clang 10.0.5`，可用 AOSP r383902 |
| `Module.symvers`（符号 CRC 来源） | ❌ | 只能由内核树生成 |

**结论：拿不到 vivo 原树，就用邻居 4.19 树编，再修 `module_layout` CRC。**

---

## 为什么邻居树能编出可用模块

三份独立编译产物的 CRC 实测：

| 符号 | perm10.ko | kernelsu.ko | ksu_run.ko |
|---|---|---|---|
| `printk` | `0x7c32d0f0` | `0x7c32d0f0` | `0x7c32d0f0` |
| `kfree` | — | `0x037a0cba` | `0x037a0cba` |
| `module_layout` | `0xb383512a` | `0x9bf9618f` | `0xb383512a` |

**普通函数符号的 CRC 跨编译完全一致**（由内核源码 + `.config` 决定，与编译器细节无关）。
只有 `module_layout` 这种**结构体布局相关**的 CRC 会变。

所以策略是：

```
1. 用邻居 4.19 树编 → 大部分符号 CRC 正确，module_layout 不对
2. 用 koreloc 把 module_layout CRC 改成设备的值
3. insmod 成功
```

---

## 路线 A：GitHub Actions（推荐）

### 1. 建仓库

```sh
cd DevMod/pathshield
git init
git add .
git commit -m "pathshield: 4.19 arm64 VFS path masking"
git remote add origin https://github.com/<you>/pathshield.git
git push -u origin main
```

### 2. 等 Actions

`.github/workflows/build.yml` 会自动：

- 装 AOSP clang r383902
- clone `UtsavBalar1231/kernel_xiaomi_sm8250`（4.19-stable，★233）
- 关掉 LTO / CFI
- 出 `vmlinux.symvers` → `Module.symvers`
- 编 `pathshield.ko`
- 上传 artifact

### 3. 下载

Actions 页面 → 最新 run → `pathshield-ko` artifact。

### 4. 换内核树

如果小米树编不过，手动触发 workflow 时改输入：

```
kernel_repo: diphons/D8G_Kernel        # 另一个 4.19 SM8250
kernel_ref:  main
```

其它候选：

- `UtsavBalar1231/kernel_xiaomi_sm8250` ★233（首选）
- `PixelPlusUI-Devices/kernel_xiaomi_sm8250`（4.19.245）
- `Dedrimer/android_kernel_motorola_sm8250`（LA.UM.9.12.r1-14000）

---

## 路线 B：本机 WSL

```sh
# 1. 装 Ubuntu
wsl --install -d Ubuntu-22.04

# 2. 装依赖
sudo apt update && sudo apt install -y bc bison flex libssl-dev libelf-dev \
  git make gcc python3 zip unzip

# 3. 拿 clang
curl -L "https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+archive/refs/tags/android-11.0.0_r48/clang-r383902b.tar.gz" \
  -o clang.tar.gz
mkdir -p ~/clang && tar -xzf clang.tar.gz -C ~/clang
export PATH=~/clang/bin:$PATH

# 4. 拿内核树
git clone --depth=1 https://github.com/UtsavBalar1231/kernel_xiaomi_sm8250 kernel
cd kernel

# 5. 配置（关 LTO/CFI，否则编不过也吃不消内存）
make ARCH=arm64 CC=clang LLVM=1 LLVM_IAS=1 O=out defconfig
./scripts/config --file out/.config -d LTO_CLANG -d LTO_CLANG_THIN -d CFI_CLANG
make ARCH=arm64 CC=clang LLVM=1 LLVM_IAS=1 O=out olddefconfig

# 6. 出 vmlinux.symvers（不需要完整 vmlinux）
make ARCH=arm64 CC=clang LLVM=1 LLVM_IAS=1 O=out -j$(nproc) vmlinux
ln -sf vmlinux.symvers out/Module.symvers

# 7. 编模块
cd /mnt/e/IQOO\ Neo5活力版临时root/DevMod/pathshield/kernel
make -C /path/to/kernel M=$(pwd) ARCH=arm64 CC=clang LLVM=1 LLVM_IAS=1 O=/path/to/kernel/out modules

# 8. 验证
llvm-readelf -p .modinfo pathshield.ko | grep vermagic
llvm-readelf -S pathshield.ko | grep __versions   # size 必须非 0
```

---

## 路线 C：容器里编

```sh
docker run --rm -it -v "$PWD:/work" ubuntu:22.04 bash
# 里面按路线 B 的第 2~8 步走
```

本机 WSL 已经有 `docker-desktop` 发行版，可以用。

---

## 编完必须做的两步

### 1. 检查 vermagic

```sh
llvm-readelf -p .modinfo pathshield.ko | grep vermagic
```

期望：

```
vermagic=4.19.152-perf+ SMP preempt mod_unload modversions aarch64
```

**如果不是这个字符串，模块一定加载失败。** 邻居树的 `utsrelease` 通常不一样，
需要改内核树里的 `make kernelrelease`，或直接 patch `.modinfo` 段里的 vermagic 字符串
（长度必须一致或更短，用 `\0` 补齐）。

### 2. 修 module_layout CRC

用作者原版里的 `koreloc`：

```sh
# koreloc 传进来了吗：在 /data/local/tmp/pdroot/koreloc
# 但 pdroot 可能被删了，从 APK 里重新解

# 用法: koreloc syms.txt <base_hex> in.ko out.ko --from-text
koreloc syms.txt ffffff8008080000 pathshield.ko pathshield_fixed.ko --from-text
```

输出会打印：

```
syms loaded: 212
from-text: LINK_BASE=...
modversions: module_layout crc -> 44017a5c (patched=1)
relocated 211 symbols -> ... (undefined-left=0)
```

**`undefined-left=0` 是关键判据**，不为 0 说明还有符号没解析，加载会失败。

---

## 加载

```sh
adb push pathshield.ko /data/local/tmp/
adb shell "su -c '/data/adb/ksud insmod /data/local/tmp/pathshield.ko target_paths=/data/local/tmp/su scope_mode=deny deny_uids=10306'"
adb shell "grep '^pathshield ' /proc/modules"
```

失败时看：

```sh
dmesg | grep -iE 'pathshield|unknown symbol|invalid module|module_layout|exec format'
```

| errno / 日志 | 原因 |
|---|---|
| `ENOEXEC` | vermagic 或 modversions 不匹配 |
| `Unknown symbol xxx` | 邻居树有、设备没有的符号；改代码避开 |
| `EPERM` | `sig_enforce` 还是 1 |
| 加载后立刻 panic | hook 点签名验证没拦住，patch 到了错的函数 |

---

## 出错排查顺序

1. `vermagic` 对吗
2. `__versions` 段大小非 0 吗
3. `dmesg` 里第一个报错是什么
4. `module_layout` CRC 改成设备值了吗
5. hook 点的 4 字节签名和设备镜像一致吗
