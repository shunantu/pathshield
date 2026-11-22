# 打包 pathshield 为 KSU 模块 zip
#
# 前提：pathshield.ko 已经编译好（见 docs/BUILD.md）
#
# 用法：
#   pwsh -File tools/package.ps1 -Ko .\pathshield.ko
#   pwsh -File tools/package.ps1 -Ko .\pathshield.ko -Out .\out

param(
  [Parameter(Mandatory=$true)][string]$Ko,
  [string]$Out = ".",
  [string]$Ver = ""
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$modDir = Join-Path $root 'module'

if (-not (Test-Path -LiteralPath $Ko)) { throw "pathshield.ko not found: $Ko" }
$koItem = Get-Item -LiteralPath $Ko
Write-Host ("[i] ko: {0}  {1} bytes" -f $koItem.Name, $koItem.Length)

# 校验 vermagic
$bytes = [System.IO.File]::ReadAllBytes($koItem.FullName)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)
$idx = $text.IndexOf('vermagic=')
if ($idx -lt 0) {
  Write-Warning "!! no vermagic string found in ko - it will not load"
} else {
  $end = $text.IndexOf([char]0, $idx)
  $vm = $text.Substring($idx + 9, $end - $idx - 9)
  Write-Host ("[i] vermagic: {0}" -f $vm)
  if ($vm -notlike '4.19.152-perf+*') {
    Write-Warning "!! vermagic does not match device (expect 4.19.152-perf+ ...)"
  }
}

# 校验 __versions 段非空
if ($text -notmatch '__versions') {
  Write-Warning "!! no __versions section - modversions will reject this module"
}

New-Item -ItemType Directory -Force -Path $Out | Out-Null
$zipName = if ($Ver) { "pathshield-$Ver-ksu.zip" } else { "pathshield-ksu.zip" }
$zipPath = Join-Path $Out $zipName
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::Open($zipPath, 'Create')

function Add-File($zip, $srcPath, $entryName) {
  $e = $zip.CreateEntry($entryName)
  $s = $e.Open()
  $b = [System.IO.File]::ReadAllBytes($srcPath)
  $s.Write($b, 0, $b.Length)
  $s.Close()
  Write-Host ("    + {0,-24} {1,8} B" -f $entryName, $b.Length)
}

Write-Host '[*] packing module files'
foreach ($f in @('module.prop','customize.sh','service.sh','post-fs-data.sh','uninstall.sh','target_path.conf')) {
  $p = Join-Path $modDir $f
  if (Test-Path -LiteralPath $p) { Add-File $zip $p $f }
  else { Write-Warning "missing module file: $f" }
}

Write-Host '[*] packing pathshield.ko'
Add-File $zip $koItem.FullName 'pathshield.ko'

$zip.Dispose()

$zi = Get-Item -LiteralPath $zipPath
Write-Host ("[OK] {0}  {1} bytes" -f $zi.FullName, $zi.Length)
Write-Host ''
Write-Host 'deploy:'
Write-Host ("  adb push '{0}' /data/local/tmp/" -f $zi.FullName)
Write-Host '  adb shell "su -c ''/data/adb/ksud module install /data/local/tmp/pathshield-ksu.zip''"'
Write-Host '  adb shell "su -c ''/data/adb/ksud soft-reboot''"'
