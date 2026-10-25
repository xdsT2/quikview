#!/usr/bin/env bash
# QuikView Windows 打包：产出可直接分发的 zip。
#
# 用法：
#   tools/package.sh [build目录] [输出目录]
#
# 设计要点：
# 1. 绝不直接压 build/——里面混着 CMakeCache / .obj 与不需要的 DLL。
# 2. 干净 staging：exe → windeployqt → 依赖闭包 → 自检 → 压包。
# 3. 依赖闭包用 objdump 逐层读导入表，从 Qt 的 bin 目录补齐。
#    不用固定清单：MSYS2 换版本时 DLL 名字会变（libraw-25 这种带 ABI 号），
#    写死清单迟早漏；缺失项一律打印出来，不静默吞。
# 4. qjp2 / libjasper 必须剔除：MSYS2 的 jasper-4.2.9 带断言，插件一旦能
#    加载，打开 .jp2 就撞 jas_init 的 !jas_global.initialized，弹出
#    「Microsoft Visual C++ Runtime Library」中止框，用户看到的是「点了没反应」。
#    JP2/J2K 本来就不在解码白名单里，不发插件即可干净地回退到「不支持」。
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-$REPO_ROOT/build}"
OUT_DIR="${2:-$REPO_ROOT/dist}"

# 统一转绝对路径：脚本后面会 cd 到 staging 里跑冒烟，相对路径会写错地方
case "$BUILD_DIR" in /*|[A-Za-z]:*) ;; *) BUILD_DIR="$PWD/$BUILD_DIR" ;; esac
case "$OUT_DIR"   in /*|[A-Za-z]:*) ;; *) OUT_DIR="$PWD/$OUT_DIR"     ;; esac

VERSION="$(sed -n 's/^project(quikview VERSION \([0-9.]*\).*/\1/p' "$REPO_ROOT/CMakeLists.txt" | head -1)"
[ -n "$VERSION" ] || { echo "读不到 CMakeLists 里的版本号" >&2; exit 1; }

STAGE_NAME="quikview-${VERSION}-win64"
STAGE="$OUT_DIR/$STAGE_NAME"

command -v windeployqt >/dev/null || { echo "需要 windeployqt（MSYS2 mingw64 在 PATH 里）" >&2; exit 1; }
command -v objdump >/dev/null || { echo "需要 objdump" >&2; exit 1; }
QT_BIN="$(cd "$(dirname "$(command -v windeployqt)")" && pwd)"
[ -f "$BUILD_DIR/quikview.exe" ] || { echo "先构建：cmake --build $BUILD_DIR" >&2; exit 1; }

echo "[1/5] staging"
rm -rf "$OUT_DIR/$STAGE_NAME"
mkdir -p "$STAGE"
cp "$BUILD_DIR/quikview.exe" "$STAGE/"

echo "[2/5] windeployqt（Qt 运行时 + 插件）"
windeployqt --release --no-translations --no-opengl-sw \
    --no-system-d3d-compiler --no-system-dxc-compiler --no-quick-import \
    --dir "$STAGE" "$STAGE/quikview.exe" >/dev/null

# 图像格式插件整目录兜底：windeployqt 有时只挑它认为用得到的
if [ -d "$QT_BIN/../share/qt6/plugins/imageformats" ]; then
    mkdir -p "$STAGE/imageformats"
    cp -f "$QT_BIN/../share/qt6/plugins/imageformats/"*.dll "$STAGE/imageformats/"
fi

echo "[3/5] 补齐非 Qt 依赖（objdump 闭包）"
declare -A SEEN=()
QUEUE=()
while IFS= read -r -d '' f; do QUEUE+=("$f"); done \
    < <(find "$STAGE" -type f \( -name '*.exe' -o -name '*.dll' \) -print0)

# Windows 自带的，不复制
is_system() {
    case "$(echo "$1" | tr 'A-Z' 'a-z')" in
        kernel32.dll|user32.dll|gdi32.dll|advapi32.dll|shell32.dll|ole32.dll) return 0 ;;
        oleaut32.dll|comdlg32.dll|comctl32.dll|shlwapi.dll|dwmapi.dll|uxtheme.dll) return 0 ;;
        winmm.dll|ws2_32.dll|ntdll.dll|msvcrt.dll|msimg32.dll|version.dll) return 0 ;;
        opengl32.dll|glu32.dll|imm32.dll|wtsapi32.dll|userenv.dll|netapi32.dll) return 0 ;;
        psapi.dll|secur32.dll|crypt32.dll|bcrypt.dll|dbghelp.dll|setupapi.dll) return 0 ;;
        winspool.drv|d3d11.dll|d3d12.dll|d3d9.dll|dxgi.dll|dwrite.dll|d2d1.dll) return 0 ;;
        dcomp.dll|mpr.dll|wldap32.dll|authz.dll|pdh.dll|powrprof.dll|propsys.dll) return 0 ;;
        rpcrt4.dll|sspicli.dll|iphlpapi.dll|dnsapi.dll|winhttp.dll|mswsock.dll) return 0 ;;
        normaliz.dll|gdiplus.dll|mscms.dll|wintrust.dll|shcore.dll|usp10.dll) return 0 ;;
        ncrypt.dll|twinapi.dll|cfgmgr32.dll|api-ms-win-*|ext-ms-win-*) return 0 ;;
        *) return 1 ;;
    esac
}

COPIED=0
MISSING=()
while [ ${#QUEUE[@]} -gt 0 ]; do
    cur="${QUEUE[0]}"
    QUEUE=("${QUEUE[@]:1}")
    while IFS= read -r dep; do
        [ -n "$dep" ] || continue
        key="$(echo "$dep" | tr 'A-Z' 'a-z')"
        [ -n "${SEEN[$key]:-}" ] && continue
        SEEN[$key]=1
        is_system "$dep" && continue
        [ -f "$STAGE/$dep" ] && continue
        if [ -f "$QT_BIN/$dep" ]; then
            cp -f "$QT_BIN/$dep" "$STAGE/"
            COPIED=$((COPIED + 1))
            QUEUE+=("$STAGE/$dep")
        else
            MISSING+=("$dep ← $(basename "$cur")")
        fi
    done < <(objdump -p "$cur" 2>/dev/null | sed -n 's/.*DLL Name: *//p')
done
echo "  补齐 $COPIED 个"
if [ ${#MISSING[@]} -gt 0 ]; then
    echo "  找不到（需人工确认）：" >&2
    printf '    ! %s\n' "${MISSING[@]}" >&2
fi

echo "[4/5] 剔除不支持的格式插件"
for name in qjp2.dll libjasper.dll; do
    find "$STAGE" -name "$name" -type f -print0 | while IFS= read -r -d '' hit; do
        rm -f "$hit"
        echo "  删除 ${hit#"$STAGE"/}"
    done
done

# 自检：缺任何一个，用户拿到的就是「起不来」或「格式打不开」
REQUIRED=(
    quikview.exe Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6PrintSupport.dll
    platforms/qwindows.dll styles/qmodernwindowsstyle.dll
    imageformats/qjpeg.dll imageformats/qgif.dll imageformats/qico.dll
    imageformats/qwebp.dll imageformats/qtiff.dll imageformats/qicns.dll
    imageformats/qmng.dll imageformats/qtga.dll imageformats/qwbmp.dll
    libheif.dll libraw-25.dll
)
BAD=0
for r in "${REQUIRED[@]}"; do
    if [ ! -f "$STAGE/$r" ]; then
        echo "  自检缺文件：$r" >&2
        BAD=1
    fi
done
[ "$BAD" = "0" ] || { echo "自检失败，中止" >&2; exit 1; }

# 可选冒烟：SAMPLE=/path/to/img 时验证 staging 里的 exe 能自渲染
if [ -n "${SAMPLE:-}" ] && [ -f "$SAMPLE" ]; then
    if (cd "$STAGE" && ./quikview.exe "$SAMPLE" --screenshot "$OUT_DIR/_smoke.png" \
            --shot-delay 1500 >/dev/null 2>&1) && [ -f "$OUT_DIR/_smoke.png" ]; then
        echo "  冒烟：OK"
    else
        echo "  冒烟：失败（产物仍会生成，但请复查）" >&2
    fi
    rm -f "$OUT_DIR/_smoke.png"
fi

echo "[5/5] 压包"
ZIP="$OUT_DIR/$STAGE_NAME.zip"
rm -f "$ZIP"
cd "$OUT_DIR"
if command -v zip >/dev/null 2>&1; then
    zip -qr "$(basename "$ZIP")" "$STAGE_NAME"
elif command -v python >/dev/null 2>&1; then
    python -m zipfile -c "$(basename "$ZIP")" "$STAGE_NAME"
elif command -v powershell >/dev/null 2>&1; then
    powershell -NoProfile -Command \
        "Compress-Archive -Path '$STAGE_NAME' -DestinationPath '$(basename "$ZIP")' -Force"
else
    echo "没有可用的压缩工具（zip / python / powershell）" >&2
    exit 1
fi
cd "$REPO_ROOT"
SIZE="$(du -h "$ZIP" | cut -f1)"
echo
echo "产物：$ZIP（$SIZE）"
sha256sum "$ZIP" 2>/dev/null || true
