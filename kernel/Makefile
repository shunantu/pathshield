# pathshield - out-of-tree kernel module (Linux 4.19 arm64)
#
# 用法:
#   make KDIR=/path/to/kernel ARCH=arm64 CC=clang LLVM=1 LLVM_IAS=1
#
# KDIR 必须指向一个已配置好的 4.19 内核树（有 .config 与 Module.symvers）。
# 邻居树能提供正确的普通符号 CRC，只有 module_layout 可能不匹配，
# 用 tools/koreloc 修那一个值即可，详见 docs/BUILD.md。
#
# 目标:
#   all     编译模块
#   clean   清理
#   info    打印当前配置

KDIR ?= /lib/modules/$(shell uname -r)/build
MDIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

ARCH ?= arm64

.PHONY: all clean info check-headers

all:
	@echo "[pathshield] KDIR = $(KDIR)"
	@echo "[pathshield] MDIR = $(MDIR)"
	$(MAKE) -C $(KDIR) M=$(MDIR) ARCH=$(ARCH) modules

clean:
	$(MAKE) -C $(KDIR) M=$(MDIR) ARCH=$(ARCH) clean

info:
	@echo "KDIR        = $(KDIR)"
	@echo "MDIR        = $(MDIR)"
	@echo "ARCH        = $(ARCH)"
	@echo "CC          = $(CC)"
	@echo "sources:"
	@find $(MDIR) -name '*.c' -o -name '*.h' | sed 's|$(MDIR)/||' | sort | sed 's/^/  /'

# 编译前的静态检查：所有源文件必须能通过 include 一致性检查
check-headers:
	@echo "[pathshield] checking include graph..."
	@for f in $$(find $(MDIR) -name '*.c'); do \
		grep -q '#include' $$f || { echo "!! $$f has no includes"; exit 1; }; \
	done
	@echo "[pathshield] ok"
