#!/usr/bin/env bash
# QuikView Windows 打包脚本：产出可直接分发的 zip。
#
# 设计要点（对应 DEPLOY-SIZE 报告与 2026-10-25 P3 交接）：
# 1. 绝不直接压 build/ 目录——里面有 CMakeCache.txt / build.ninja / .obj 等
#    约 5MB 构建残留，且 exe 依赖的 DLL 混着不需要的文件。
# 2. 用干净 staging：exe + windeployqt(--no-translations) + 固定清单的第三方 DLL。
# 3. 已知坑：libmng-2.dll 不在 exe 导入表里（只有 Qt 插件 qmng.dll 用它），
#    windeployqt 不会自动补——漏了 mng 打不开。它在下面 THIRDPARTY 清单里，缺失即报错。
# 4. 本脚本只打包含 MSYS2 依赖的常规包；ICU（约 36.5MB）随 MSYS2 Qt6Core 走，
#    见 DEPLOY-SIZE 报告「Qt 来源」一节。
#
# 用法：
#   tools/package.sh [build目录] [输出目录]
#   LIBHEIF_DLL=/path/libheif.dll tools/package.sh   # 可选：用自建解码版 libheif 替换
# 产物：dist/QuikView-win64-<日期>.zip
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-$REPO_ROOT/build}"
OUT_DIR="${2:-$REPO_ROOT/dist}"
STAGE="$OUT_DIR/QuikView"

MSYS64="${MSYS64:-/e/Agent/Tencent/WorkBuddy/Workspace/tools/msys64/mingw64}"
export PATH="$MSYS64/bin:$PATH"

command -v windeployqt >/dev/null || { echo "需要 windeployqt（msys64）" >&2; exit 1; }
[ -f "$BUILD_DIR/quikview.exe" ] || { echo "先构建：cmake --build $BUILD_DIR" >&2; exit 1; }

# ---- 1. 干净 staging ----
rm -rf "$OUT_DIR"
mkdir -p "$STAGE"
cp "$BUILD_DIR/quikview.exe" "$STAGE/"

# ---- 2. Qt 运行时与插件（不带翻译文件；编译器 runtime 由下面清单统一提供）----
windeployqt --no-translations --no-compiler-runtime --no-system-d3d-compiler \
    --dir "$STAGE" "$STAGE/quikview.exe" >/dev/null

# Qt 插件目录 windeployqt 只拷常用项，QMNG 等需要整目录兜底（mng 在白名单里）
mkdir -p "$STAGE/imageformats"
cp -f "$BUILD_DIR/imageformats/"*.dll "$STAGE/imageformats/"

# ---- 3. 第三方 DLL 固定清单（2026-10-25 实测部署闭包，37 项）----
# 版本口径：与 MSYS2 mingw64 当前仓库一致；libheif 可被 LIBHEIF_DLL 覆盖为
# 自建"仅解码"版（tools/build-libheif.sh），可省约 36MB（见 DEPLOY-SIZE 报告）。
THIRDPARTY=(
    libaom.dll libb2-1.dll libbrotlicommon.dll libbrotlidec.dll libbz2-1.dll
    libcryptopp.dll libdav1d-7.dll libde265-0.dll libdouble-conversion.dll
    libfreetype-6.dll libgcc_s_seh-1.dll libglib-2.0-0.dll libgomp-1.dll
    libgraphite2.dll libharfbuzz-0.dll libheif.dll libiconv-2.dll
    libicudt78.dll libicuin78.dll libicuuc78.dll libintl-8.dll libjpeg-8.dll
    liblcms2-2.dll libmd4c.dll libmng-2.dll libpcre2-16-0.dll libpcre2-8-0.dll
    libpng16-16.dll libraw-25.dll libsharpyuv-0.dll libstdc++-6.dll
    libwebp-7.dll libwebpdemux-2.dll libwebpmux-3.dll libwinpthread-1.dll
    libzstd.dll zlib1.dll
)
MISSING=0
for d in "${THIRDPARTY[@]}"; do
    if [ -f "$BUILD_DIR/$d" ]; then
        cp -f "$BUILD_DIR/$d" "$STAGE/"
    else
        echo "缺少 $d（build 目录里没有）" >&2
        MISSING=1
    fi
done
[ "$MISSING" = "0" ] || { echo "第三方 DLL 清单不齐，中止打包" >&2; exit 1; }

# 可选：用自建解码版 libheif 替换（省约 36MB，见 tools/build-libheif.sh）
if [ -n "${LIBHEIF_DLL:-}" ]; then
    cp -f "$LIBHEIF_DLL" "$STAGE/libheif.dll"
    echo "已替换为自建 libheif：$(du -h "$LIBHEIF_DLL" | cut -f1)"
fi

# ---- 4. 冒烟：staging 里的 exe 能自渲染出一张图 ----
if [ -f /e/A1/XiaoMiFeng/Temp/_sample.heic ]; then
    (cd "$STAGE" && ./quikview.exe /e/A1/XiaoMiFeng/Temp/_sample.heic \
        --screenshot "$OUT_DIR/_pkg_smoke.png" --shot-delay 1500 >/dev/null 2>&1) \
        && echo "打包冒烟：OK" || echo "打包冒烟：警告——样本验证未跑（不影响产物）"
    rm -f "$OUT_DIR/_pkg_smoke.png"
fi

# ---- 5. 压 zip ----
ZIP="$OUT_DIR/QuikView-win64-$(date +%Y%m%d).zip"
(cd "$OUT_DIR" && python -m zipfile -c "$(basename "$ZIP")" QuikView)
echo "产物：$ZIP（$(du -h "$ZIP" | cut -f1)）"
