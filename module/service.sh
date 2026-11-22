#!/system/bin/sh
# pathshield service.sh
# 在 late boot 读取配置、合并内置项、加载内核模块

MODDIR=${0%/*}
CFG=/data/adb/pathshield
KO="$MODDIR/pathshield.ko"
LOGK() { echo "pathshield: $1" > /dev/kmsg 2>/dev/null; }

mkdir -p "$CFG"
chmod 700 "$CFG" 2>/dev/null

[ -f "$KO" ] || { LOGK "ko missing, abort"; exit 0; }

# 内置必隐藏项（与 WebUI 里 BUILTIN 保持一致）
BUILTIN="/sys/module/kernelsu
/data/local/tmp/su
/data/local/tmp/su2
/data/local/tmp/pdroot"

# 合并：内置项 + 用户配置，去重，只保留加载时存在的
load_targets() {
  {
    echo "$BUILTIN"
    grep -v '^#' "$CFG/target_path.conf" 2>/dev/null | grep -v '^$'
  } | awk '!seen[$0]++' | while IFS= read -r p; do
    [ -z "$p" ] && continue
    case "$p" in
      /*) ;;
      *) continue ;;
    esac
    # 通配符路径只检查父目录
    chk="$p"
    case "$p" in
      *\**|*\?*)
        d=${p%%[?*]*}
        chk=${d%/*}
        [ -z "$chk" ] && chk=/
        ;;
    esac
    [ -e "$chk" ] && printf '%s,' "$p"
  done | sed 's/,$//'
}

T="$(load_targets)"
if [ -z "$T" ]; then
  LOGK "no resolvable target, skip load"
  exit 0
fi

S="$(cat "$CFG/scope_mode.conf" 2>/dev/null || echo deny)"
U="$(grep -v '^#' "$CFG/deny_uids.conf" 2>/dev/null | grep -v '^$' | tr '\n' ',' | sed 's/,$//')"
H="$(cat "$CFG/hide_dirents.conf" 2>/dev/null || echo 1)"

# 白名单模式自动放行系统 UID
if [ "$S" = "allow" ] && [ -z "$U" ]; then
  U="0,1000,2000"
fi

LOGK "loading targets=$T scope=$S uids=$U hide_dirents=$H"

grep -q '^pathshield ' /proc/modules && rmmod pathshield 2>/dev/null
sleep 0.3

/data/adb/ksud insmod "$KO" \
  target_paths="$T" \
  scope_mode="$S" \
  deny_uids="$U" \
  hide_dirents="$H" \
  enabled=1 2>&1 | while IFS= read -r l; do LOGK "$l"; done

if grep -q '^pathshield ' /proc/modules; then
  LOGK "loaded OK"
else
  LOGK "load FAILED"
fi
