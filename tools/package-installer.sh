#!/usr/bin/env bash
# QuikView 完整发布产物：免安装 zip（package.sh）+ NSIS 安装包（makensis）。
#
# 用法：
#   tools/package-installer.sh [build目录] [输出目录]
#
# 为什么要这个脚本：以前安装包是人工按 packaging/README.md 手敲 4 条命令产出的，
# 于是出现过「setup 比最后一次代码提交还早」的过期包。现在只有一条命令，
# 版本从 CMakeLists.txt 读，zip 与 setup 用同一份 staging payload。
#
# 版本链路（唯一真源 → 四个消费点）：
#   CMakeLists.txt project(VERSION 0.4.0)
#     ├─ zip 名            quikview-0.4.0-win64.zip
#     ├─ setup 名          QuikView-0.4.0-setup.exe
#     ├─ /DAPPVERSION      安装向导显示名 + DisplayVersion
#     └─ /DAPPVERSION_RC   VIProductVersion 0.4.0.0
#   exe 内的 VERSIONINFO 由 CMake configure_file 生成，无需脚本介入。
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-$REPO_ROOT/build}"
OUT_DIR="${2:-$REPO_ROOT/dist}"

# shellcheck source=lib/version.sh
source "$REPO_ROOT/tools/lib/version.sh"

VERSION="$(qv_read_version "$REPO_ROOT")" || exit 1
VERSION_RC="$(qv_version_rc "$VERSION")"

case "$BUILD_DIR" in /*|[A-Za-z]:*) ;; *) BUILD_DIR="$PWD/$BUILD_DIR" ;; esac
case "$OUT_DIR"   in /*|[A-Za-z]:*) ;; *) OUT_DIR="$PWD/$OUT_DIR"     ;; esac

STAGE="$OUT_DIR/quikview-${VERSION}-win64"
ZIP="$OUT_DIR/quikview-${VERSION}-win64.zip"
SETUP="$OUT_DIR/QuikView-${VERSION}-setup.exe"

# makensis 不一定在 PATH 里（NSIS 装在 Program Files）
MAKENSIS="${MAKENSIS:-}"
if [ -z "$MAKENSIS" ]; then
    if command -v makensis >/dev/null 2>&1; then
        MAKENSIS="$(command -v makensis)"
    else
        for cand in "/c/Program Files (x86)/NSIS/makensis.exe" \
                    "/c/Program Files/NSIS/makensis.exe"; do
            if [ -x "$cand" ]; then MAKENSIS="$cand"; break; fi
        done
    fi
fi
[ -n "$MAKENSIS" ] || { echo "找不到 makensis（装 NSIS 3.x，或用 MAKENSIS=<路径> 指定）" >&2; exit 1; }

echo "版本：${VERSION}（安装包 RC ${VERSION_RC}）"
echo "makensis：$MAKENSIS"

echo "[1/3] payload —— tools/package.sh 产 staging 与 zip（同一份 payload）"
"$REPO_ROOT/tools/package.sh" "$BUILD_DIR" "$OUT_DIR"

# MUI_ICON 直接引用 ${SRC}\quikview.ico，缺了就编不过；这里先给出明确信息
[ -f "$STAGE/quikview.ico" ] || { echo "staging 缺 quikview.ico（MUI_ICON 依赖），中止" >&2; exit 1; }

echo "[2/3] makensis 编译安装包"
# makensis 只吃 Windows 路径；Git Bash 会把 /D... 当成路径改写，必须关掉转换
SRC_WIN="$(cygpath -w "$STAGE")"
OUT_WIN="$(cygpath -w "$SETUP")"
rm -f "$SETUP"
cd "$REPO_ROOT"
MSYS2_ARG_CONV_EXCL='*' "$MAKENSIS" \
    "/DSRC=$SRC_WIN" \
    "/DOUTFILE=$OUT_WIN" \
    "/DAPPVERSION=$VERSION" \
    "/DAPPVERSION_RC=$VERSION_RC" \
    "packaging/quikview.nsi"

echo "[3/3] 校验产物"
[ -f "$SETUP" ] || { echo "安装包没产出：$SETUP" >&2; exit 1; }

echo
for f in "$ZIP" "$SETUP"; do
    [ -f "$f" ] || { echo "缺产物：$f" >&2; exit 1; }
    printf '%s\n  大小 %s B（%s）\n  SHA256 %s\n' \
        "$f" "$(stat -c %s "$f")" "$(du -h "$f" | cut -f1)" \
        "$(sha256sum "$f" | cut -d' ' -f1)"
done
