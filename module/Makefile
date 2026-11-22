# pathshield KSU module - build zip
# Usage: make   (expects pathshield.ko in this directory or ../kernel)
KO ?= ../kernel/pathshield.ko
OUT ?= ../out
NAME := pathshield-ksu.zip

FILES := module.prop customize.sh service.sh post-fs-data.sh uninstall.sh target_path.conf

.PHONY: all clean check

all: $(OUT)/$(NAME)

check:
	@test -f $(KO) || { echo "!! $(KO) missing - build the kernel module first"; exit 1; }
	@strings $(KO) | grep -q 'vermagic=4.19.152-perf+' || \
	  echo "!! warning: vermagic does not look like 4.19.152-perf+"

$(OUT)/$(NAME): check $(FILES)
	@mkdir -p $(OUT)
	@rm -f $@
	zip -q -j $@ $(FILES) $(KO)
	@echo "built $@  $$(stat -c%s $@) bytes"

clean:
	rm -f $(OUT)/$(NAME)
