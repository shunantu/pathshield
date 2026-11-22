#!/system/bin/sh
# pathshield installer for KernelSU
SKIPUNZIP=1

ui_print "*******************************"
ui_print "  PathShield 路径遮罩 (4.19)"
ui_print "*******************************"

if [ "$KSU" != "true" ]; then
  ui_print "! KernelSU required"
  abort "! aborting"
fi

KREL="$(uname -r)"
ui_print "- Kernel: $KREL"
case "$KREL" in
  4.19.152-perf+*) ui_print "- Kernel match OK" ;;
  *) ui_print "! This build targets 4.19.152-perf+" ; abort "! wrong kernel" ;;
esac

ui_print "- Extracting module files"
for f in module.prop service.sh post-fs-data.sh uninstall.sh target_path.conf; do
  extract "$ZIPFILE" "$f" "$MODPATH" || abort "! missing $f"
done

if ! unzip -o "$ZIPFILE" 'pathshield.ko' -d "$MODPATH" >&2 2>/dev/null; then
  ui_print "! pathshield.ko missing from zip"
  abort "! build it first (see docs/BUILD.md)"
fi
[ -s "$MODPATH/pathshield.ko" ] || abort "! pathshield.ko is empty"

ui_print "- Extracting WebUI"
mkdir -p "$MODPATH/webroot"
unzip -o "$ZIPFILE" 'webroot/*' -d "$MODPATH" >&2 2>/dev/null
if [ -f "$MODPATH/webroot/index.html" ]; then
  ui_print "- WebUI installed"
else
  ui_print "! WebUI missing (module still works via conf files)"
fi

set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/pathshield.ko" 0 0 0644
set_perm "$MODPATH/customize.sh" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/post-fs-data.sh" 0 0 0755

# 初始化配置目录：把内置项写进 target_path.conf
CFG=/data/adb/pathshield
mkdir -p "$CFG"
if [ ! -f "$CFG/target_path.conf" ]; then
  cat "$MODPATH/target_path.conf" > "$CFG/target_path.conf"
  ui_print "- Default target list installed"
fi
chmod 700 "$CFG"

ui_print "- Done. Reboot or use the WebUI to activate."
