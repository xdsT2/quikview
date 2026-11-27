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

echo "[1/7] staging"
rm -rf "$OUT_DIR/$STAGE_NAME"
mkdir -p "$STAGE"
cp "$BUILD_DIR/quikview.exe" "$STAGE/"
# 安装向导的 MUI_ICON / MUI_UNICON 直接引用 ${SRC}\quikview.ico（exe 内已内嵌一份，
# 这里放一份是给 NSIS 编译期用的），缺了 package-installer.sh 会中止
cp "$REPO_ROOT/resources/quikview.ico" "$STAGE/"
# 壳扩展 DLL（shell-ext/ 独立工程产物）：随包分发；安装器按 SEC_SHELL 节注册，
# zip 用户可用 shell-ext/scripts/*.ps1 手动注册。属于产品组件，缺了直接中止。
#
# 落盘文件名带版本号（quikthumb-<ver>.dll）：升级时新 DLL 是全新路径，旧文件
# 即使仍被资源管理器映射也不与新文件冲突 —— 安装器因此不用「改名让路」，
# 也就不会因为「删不掉旧文件」而要求重启电脑（0.5.3 的重启提示来自这里）。
# 构建产物仍叫 quikthumb.dll，只在本脚本改名。
[ -f "$REPO_ROOT/build-shell64/quikthumb.dll" ] || { echo "缺 build-shell64/quikthumb.dll：先构建 64 位壳扩展（见 shell-ext/README.md）" >&2; exit 1; }
cp "$REPO_ROOT/build-shell64/quikthumb.dll" "$STAGE/quikthumb-${VERSION}.dll"
# 32 位壳扩展需要 MSVC x86（见 shell-ext/README.md）。GitHub Actions 的 Windows
# runner 没有 Visual Studio 实例，CI 只能做构建烟测：显式设置
# QUIKVIEW_CI_SKIP_SHELL32=1 才允许跳过这一枚 DLL，并打警告。
# 正式发布包（tools/package-installer.sh）不得设置该变量，两枚 DLL 必须齐备。
if [ -f "$REPO_ROOT/build-shell32/quikthumb.dll" ]; then
    cp "$REPO_ROOT/build-shell32/quikthumb.dll" "$STAGE/quikthumb32-${VERSION}.dll"
elif [ "${QUIKVIEW_CI_SKIP_SHELL32:-0}" = "1" ]; then
    echo "  跳过 32 位壳扩展 DLL（CI 构建烟测；正式发布包不得设置此变量）" >&2
else
    echo "缺 build-shell32/quikthumb.dll：先构建 32 位壳扩展（见 shell-ext/README.md）" >&2
    exit 1
fi

echo "[2/7] windeployqt（Qt 运行时 + 插件）"
windeployqt --release --no-translations --no-opengl-sw \
    --no-system-d3d-compiler --no-system-dxc-compiler --no-quick-import \
    --dir "$STAGE" "$STAGE/quikview.exe" >/dev/null

# 图像格式插件整目录兜底：windeployqt 有时只挑它认为用得到的
if [ -d "$QT_BIN/../share/qt6/plugins/imageformats" ]; then
    mkdir -p "$STAGE/imageformats"
    cp -f "$QT_BIN/../share/qt6/plugins/imageformats/"*.dll "$STAGE/imageformats/"
fi

# 图像解码：换用自建「纯解码」libheif（tools/build-libheif.sh 产出）。
# MSYS2 的 libheif 把 9 个编解码器写成静态导入表，连带拖进 x265(21.62MB)/
# aom(9.24MB)/SvtAv1Enc(7.22MB)/cryptopp(4.38MB)/rav1e(3.36MB) 等约 50MB 编码器 DLL；
# 本程序只解码 HEIC/AVIF、从不编码，换成只带 libde265 + dav1d 的版本后，
# 依赖闭包自然不再收这些编码器 —— 不是事后删文件：仍被导入的 DLL 删了会起不来。
LIBHEIF_PREFIX="${QUIKVIEW_LIBHEIF_PREFIX:-$REPO_ROOT/build-libheif/prefix}"
if [ -f "$LIBHEIF_PREFIX/bin/libheif.dll" ]; then
    cp -f "$LIBHEIF_PREFIX/bin/libheif.dll" "$STAGE/libheif.dll"
    echo "  libheif：自建纯解码版 $(du -h "$STAGE/libheif.dll" | cut -f1)"
elif [ "${QUIKVIEW_REQUIRE_DECODE_ONLY_LIBHEIF:-0}" = "1" ]; then
    echo "缺 $LIBHEIF_PREFIX/bin/libheif.dll：先跑 tools/build-libheif.sh" >&2
    exit 1
else
    echo "  警告：没有自建纯解码 libheif，退回 MSYS2 版（会连带约 50MB 编码器 DLL）" >&2
fi

# SVG 渲染（图标统一 2026-11-18）：功能图标全部走 QSvgRenderer，Qt6Svg.dll 由
# windeployqt 按 import 自动收；收不到就是干净机器上满屏缺图标，直接拦下来。
[ -f "$STAGE/Qt6Svg.dll" ] || { echo "缺 Qt6Svg.dll：windeployqt 未收取 Svg 模块" >&2; exit 1; }

echo "[3/7] 补齐非 Qt 依赖（objdump 闭包）"
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

echo "[4/7] 剔除不支持的格式插件"
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
# CI 构建烟测：Qt 6.12 起移除 qmng/qtga 等旧格式插件，CI 环境用它做构建验证
# 而非正式分发，放宽可选插件的必需检查；正式发布包不得设置此变量。
CI_BUILD_SMOKE="${QUIKVIEW_CI_BUILD_SMOKE:-0}"
for r in "${REQUIRED[@]}"; do
    if [ ! -f "$STAGE/$r" ]; then
        if [ "$CI_BUILD_SMOKE" = "1" ] && [[ "$r" == imageformats/qmng.dll || "$r" == imageformats/qtga.dll || "$r" == imageformats/qwbmp.dll ]]; then
            echo "  CI 烟测：跳过可选插件 $r" >&2
            continue
        fi
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
# 编码器 DLL 不该出现在包里：它们只服务 libheif 的编码路径，本程序不用。
# 出现即说明 libheif 换版失败或依赖闭包又从别处把它们拉进来了，包体会多约 50MB。
# CI 用 MSYS2 版 libheif（带编码器），烟测场景不要求纯解码包。
if [ "$CI_BUILD_SMOKE" != "1" ]; then
for pat in 'libx265-*.dll' 'libSvtAv1Enc-*.dll' 'librav1e.dll' 'libaom.dll' \
           'libx264-*.dll' 'libkvazaar-*.dll' 'libcryptopp.dll' \
           'libopenh264-*.dll' 'libopenjp2-*.dll' 'libopenjph-*.dll'; do
    hit="$(find "$STAGE" -maxdepth 1 -name "$pat" -print -quit)"
    if [ -n "$hit" ]; then
        echo "  自检发现编码器 DLL：$(basename "$hit")" >&2
        BAD=1
    fi
done
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
echo "[5/7] 360° 全景查看器组件载荷"
# 组件是独立进程。载荷**故意不放进 staging**：主安装节是 File /r ${SRC}\*.*，
# 只要组件在 staging 里就有被递归解出的风险（0.5.6 实测 /x "extensions\*.*" 并没有
# 拦住它，静默安装后 extensions\quikview-pano.exe 照样落进了安装目录）。
# 现在载荷放 $OUT_DIR/_pano-payload，用 /DEXTSRC 单独指给 SEC_PANO：
# 主安装节递归的目录里根本没有它，「不勾选就不会装」由目录布局保证，不靠排除规则。
PANO_EXE="$BUILD_DIR/quikview-pano.exe"
if [ -f "$PANO_EXE" ]; then
    PANO_PAYLOAD="$OUT_DIR/_pano-payload"
    rm -rf "$PANO_PAYLOAD"
    mkdir -p "$PANO_PAYLOAD"
    cp "$PANO_EXE" "$PANO_PAYLOAD/"
    # 版本文件随可选载荷一起落目录：版本号取自 version.sh 单一真源，
    # 与主程序 major.minor 兼容比对口径一致；NSIS 可选节直接 File 进
    # $INSTDIR（Manager::versionFilePath() 期望的位置），不再依赖安装时手写。
    printf '%s\n' "$VERSION" > "$PANO_PAYLOAD/pano-version.txt"
    # 组件包（设置页「安装」的下载物）：zip 内含 exe + 版本文件，同址 .sha256
    # 摘要。正式分发由受控下载入口提供；离线与本地验证也用这份当测试源。
    PANO_ZIP="$OUT_DIR/quikview-pano-${VERSION}.zip"
    PANO_PKG="$OUT_DIR/_pano-pkg"
    rm -rf "$PANO_PKG"
    mkdir -p "$PANO_PKG"
    cp "$PANO_EXE" "$PANO_PKG/"
    printf '%s\n' "$VERSION" > "$PANO_PKG/pano-version.txt"
    rm -f "$PANO_ZIP" "$PANO_ZIP.sha256"
    # 打 zip 的三种后端：zip / python / powershell。MSYS2 的精简安装常常没有 zip，
    # 这里必须退得下去 —— 组件包是设置页「安装」的下载物，缺了就没法装组件。
    if command -v zip >/dev/null 2>&1; then
        ( cd "$PANO_PKG" && zip -qr "$PANO_ZIP" . )
    elif command -v python >/dev/null 2>&1; then
        ( cd "$PANO_PKG" && python -m zipfile -c "$PANO_ZIP" quikview-pano.exe pano-version.txt )
    elif command -v powershell >/dev/null 2>&1; then
        PANO_PKG_WIN="$(cygpath -w "$PANO_PKG" 2>/dev/null || echo "$PANO_PKG")"
        PANO_ZIP_WIN="$(cygpath -w "$PANO_ZIP" 2>/dev/null || echo "$PANO_ZIP")"
        powershell -NoProfile -Command \
            "Compress-Archive -Path '${PANO_PKG_WIN}\\*' -DestinationPath '${PANO_ZIP_WIN}' -Force"
    else
        echo "没有可用的压缩工具（zip / python / powershell），无法产出组件包" >&2
        exit 1
    fi
    [ -f "$PANO_ZIP" ] || { echo "组件包未生成：$PANO_ZIP" >&2; exit 1; }
    rm -rf "$PANO_PKG"
    ( cd "$OUT_DIR" && sha256sum "$(basename "$PANO_ZIP")" > "$(basename "$PANO_ZIP").sha256" )
    echo "  组件载荷 $(du -h "$PANO_ZIP" | cut -f1)"
elif [ "${QUIKVIEW_CI_SKIP_PANO:-0}" = "1" ]; then
    echo "  跳过组件载荷（CI 构建烟测；正式发布包不得设置此变量）" >&2
else
    echo "缺 $PANO_EXE：先构建组件（cmake --build $BUILD_DIR --target quikview-pano）" >&2
    exit 1
fi

echo "[6/7] 生成安装清单 quikview-manifest.txt"
# extensions/ 是可选组件载荷，不在主安装节里（quikview.nsi 用 /x 排除），
# 清单是卸载时的删除依据，必须与「实际装了哪些文件」一致：把组件排除在外，
# 免得卸载按清单去删一个用户根本没勾选安装的文件。
( cd "$STAGE" && find . -type f ! -name 'quikview-manifest.txt' ! -path './extensions/*' -printf '%P\n' \
    | sed 's|/|\\|g' | LC_ALL=C sort > quikview-manifest.txt )
echo "  清单 $(wc -l < "$STAGE/quikview-manifest.txt") 项"

echo "[7/7] 压包"
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
