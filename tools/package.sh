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

# 版本号：与 package-installer.sh 共用 tools/lib/version.sh，禁止各自再写 sed
# shellcheck source=lib/version.sh
source "$REPO_ROOT/tools/lib/version.sh"
VERSION="$(qv_read_version "$REPO_ROOT")" || exit 1

STAGE_NAME="quikview-${VERSION}-win64"
STAGE="$OUT_DIR/$STAGE_NAME"

command -v windeployqt >/dev/null || { echo "需要 windeployqt（MSYS2 mingw64 在 PATH 里）" >&2; exit 1; }
command -v objdump >/dev/null || { echo "需要 objdump" >&2; exit 1; }
QT_BIN="$(cd "$(dirname "$(command -v windeployqt)")" && pwd)"
[ -f "$BUILD_DIR/quikview.exe" ] || { echo "先构建：cmake --build $BUILD_DIR" >&2; exit 1; }

echo "[1/6] staging"
rm -rf "$OUT_DIR/$STAGE_NAME"
mkdir -p "$STAGE"
cp "$BUILD_DIR/quikview.exe" "$STAGE/"
# 安装向导的 MUI_ICON / MUI_UNICON 直接引用 ${SRC}\quikview.ico（exe 内已内嵌一份，
# 这里放一份是给 NSIS 编译期用的），缺了 package-installer.sh 会中止
cp "$REPO_ROOT/resources/quikview.ico" "$STAGE/"
# 壳扩展 DLL（shell-ext/ 独立工程产物）：随包分发；安装器按 SEC_SHELL 节注册，
# zip 用户可用 shell-ext/scripts/*.ps1 手动注册。属于产品组件，缺了直接中止。
[ -f "$REPO_ROOT/build-shell64/quikthumb.dll" ] || { echo "缺 build-shell64/quikthumb.dll：先构建 64 位壳扩展（见 shell-ext/README.md）" >&2; exit 1; }
cp "$REPO_ROOT/build-shell64/quikthumb.dll" "$STAGE/"
# 32 位壳扩展需要 MSVC x86（见 shell-ext/README.md）。GitHub Actions 的 Windows
# runner 没有 Visual Studio 实例，CI 只能做构建烟测：显式设置
# QUIKVIEW_CI_SKIP_SHELL32=1 才允许跳过这一枚 DLL，并打警告。
# 正式发布包（tools/package-installer.sh）不得设置该变量，两枚 DLL 必须齐备。
if [ -f "$REPO_ROOT/build-shell32/quikthumb.dll" ]; then
    cp "$REPO_ROOT/build-shell32/quikthumb.dll" "$STAGE/quikthumb32.dll"
elif [ "${QUIKVIEW_CI_SKIP_SHELL32:-0}" = "1" ]; then
    echo "  跳过 32 位壳扩展 DLL（CI 构建烟测；正式发布包不得设置此变量）" >&2
else
    echo "缺 build-shell32/quikthumb.dll：先构建 32 位壳扩展（见 shell-ext/README.md）" >&2
    exit 1
fi

echo "[2/6] windeployqt（Qt 运行时 + 插件）"
windeployqt --release --no-translations --no-opengl-sw \
    --no-system-d3d-compiler --no-system-dxc-compiler --no-quick-import \
    --dir "$STAGE" "$STAGE/quikview.exe" >/dev/null

# 图像格式插件整目录兜底：windeployqt 有时只挑它认为用得到的
if [ -d "$QT_BIN/../share/qt6/plugins/imageformats" ]; then
    mkdir -p "$STAGE/imageformats"
    cp -f "$QT_BIN/../share/qt6/plugins/imageformats/"*.dll "$STAGE/imageformats/"
fi

echo "[3/6] 补齐非 Qt 依赖（objdump 闭包）"
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

echo "[4/6] 剔除不支持的格式插件"
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
    libheif.dll
)
BAD=0
for r in "${REQUIRED[@]}"; do
    if [ ! -f "$STAGE/$r" ]; then
        echo "  自检缺文件：$r" >&2
        BAD=1
    fi
done
# libraw 的 DLL 名带 ABI 号（libraw-23 / libraw-25 …），MSYS2 版本漂移就会变，
# 与开头「不写死清单」的设计一致，按前缀检查而不是钉死某个数字。
if ! find "$STAGE" -maxdepth 1 -name 'libraw-*.dll' -print -quit | grep -q .; then
    echo "  自检缺文件：libraw-*.dll" >&2
    BAD=1
fi
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

# 安装清单：卸载时只删这里列出的文件，config.ini 与用户自己放进安装目录的
# 图片不在清单里 → 卸载后原样保留。路径用反斜杠，NSIS 那边直接拼 $INSTDIR。
echo "[5/6] 生成安装清单 quikview-manifest.txt"
( cd "$STAGE" && find . -type f ! -name 'quikview-manifest.txt' -printf '%P\n' \
    | sed 's|/|\\|g' | LC_ALL=C sort > quikview-manifest.txt )
echo "  清单 $(wc -l < "$STAGE/quikview-manifest.txt") 项"

echo "[6/6] 压包"
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
