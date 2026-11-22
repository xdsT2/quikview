#!/usr/bin/env bash
# 自建 libheif：只保留解码器，砍掉全部编码器。
#
# 背景：MSYS2 的 libheif 1.23.6 把 9 个编解码器写进静态导入表
#   （aom/dav1d/de265/kvazaar/openh264/rav1e/SvtAv1Enc/x264/x265），
#   部署目录被连带要求放进 x265(21.62MB)/aom(9.24MB)/SvtAv1Enc(7.22MB)/cryptopp(4.38MB)
#   /rav1e(3.36MB)/x264(1.93MB)/openh264(0.86MB)/openjph(0.62MB)/kvazaar(0.54MB)/openjp2(0.50MB)，
#   合计约 50.3MB——而本程序只解码 HEIC/AVIF，从不编码。
# 做法：从源码构建 libheif，精确关闭全部编码器与 JPEG2000/HTJ2K（HEIC/AVIF 不需要），
#   保留 HEVC（libde265）与 AV1（dav1d）解码。
#   不保留 aom 解码器：AVIF 的 AV1 解码由 dav1d 全权负责（libheif 内部优先 dav1d），
#   aom 只是同一格式的第二个解码器，多带 9.24MB 却不解码任何 dav1d 解不了的文件。
#
# 用法（在仓库根目录执行）：
#   tools/build-libheif.sh [源码目录] [安装前缀]
# 默认：源码 build-libheif/src，安装 build-libheif/prefix
#
# 构建完成后让本项目链接它：
#   PKG_CONFIG_PATH="<前缀>/lib/pkgconfig:$PKG_CONFIG_PATH" cmake --build build --clean-first
#   并把 <前缀>/bin/libheif*.dll 拷进部署目录替换 MSYS2 版。
# 验收：objdump -p <新 libheif.dll> 的导入表应只剩解码相关 DLL，
#   且 HEIC / AVIF 真实样本能正常显示。
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="${1:-$REPO_ROOT/build-libheif/src}"
PREFIX="${2:-$REPO_ROOT/build-libheif/prefix}"
LIBHEIF_VER="1.23.6"

# Git Bash 的 PATH 只认 POSIX 风格路径
MSYS64="${MSYS64:-/e/Agent/Tencent/WorkBuddy/Workspace/tools/msys64/mingw64}"
export PATH="$MSYS64/bin:$PATH"

command -v cmake >/dev/null || { echo "需要 cmake（msys64）" >&2; exit 1; }
command -v ninja >/dev/null || { echo "需要 ninja（msys64）" >&2; exit 1; }

# CMake(MinGW) 不认 /e/... 形式，转成 C:/... 混合形式传参
SRC_WIN="$(cygpath -m "$SRC_DIR")"
PREFIX_WIN="$(cygpath -m "$PREFIX")"

# ---- 1. 取源码（已存在则跳过；GitHub 直连慢时可设 HTTPS_PROXY）----
if [ ! -f "$SRC_DIR/CMakeLists.txt" ]; then
    mkdir -p "$(dirname "$SRC_DIR")"
    git clone --depth 1 --branch "v$LIBHEIF_VER" \
        https://github.com/strukturag/libheif.git "$SRC_DIR"
fi

# ---- 2. 配置：解码器保留，编码器与 J2K 全关 ----
# 说明：libheif 1.23.6 的编解码开关由 CMakeLists.txt 的 plugin_option() 定义
#   （option 名 = WITH_<名字>[_ENCODER/_DECODER][_PLUGIN]），未声明即 OFF，
#   所以只需显式关掉默认 ON 的编码器、打开要保留的解码器。
#   _PLUGIN=OFF 把解码器编成内置（built-in），不留独立插件 DLL，部署清单最短。
#   用户口径里的 "WITH_UNCODED" 即源码的 WITH_UNCOMPRESSED_CODEC（默认已 OFF）。
BUILD_DIR="$REPO_ROOT/build-libheif/build"
cmake -S "$SRC_WIN" -B "$(cygpath -m "$BUILD_DIR")" -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$PREFIX_WIN" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON \
    \
    -DWITH_LIBDE265=ON \
    -DWITH_DAV1D=ON \
    -DWITH_LIBSHARPYUV=ON \
    -DWITH_JPEG_DECODER=ON \
    \
    -DWITH_LIBDE265_PLUGIN=OFF \
    -DWITH_DAV1D_PLUGIN=OFF \
    -DWITH_JPEG_DECODER_PLUGIN=OFF \
    \
    -DWITH_AOM_DECODER=OFF \
    -DWITH_X265=OFF \
    -DWITH_X264=OFF \
    -DWITH_AOM_ENCODER=OFF \
    -DWITH_RAV1E=OFF \
    -DWITH_SvtEnc=OFF \
    -DWITH_OPENH264=OFF \
    -DWITH_OpenH264_DECODER=OFF \
    -DWITH_KVAZAAR=OFF \
    -DWITH_Uncompressed_CODEC=OFF \
    -DWITH_UNCOMPRESSED_CODEC=OFF \
    -DWITH_OpenJPEG_ENCODER=OFF \
    -DWITH_OpenJPEG_DECODER=OFF \
    -DWITH_OPENJPH_ENCODER=OFF \
    \
    -DWITH_EXAMPLES=OFF \
    -DWITH_GDK_PIXBUF=OFF \
    -DBUILD_TESTING=OFF \
    -DBUILD_DOCUMENTATION=OFF \
    -DWITH_FUZZERS=OFF

# ---- 3. 构建 + 安装 ----
cmake --build "$BUILD_DIR"
cmake --install "$BUILD_DIR"

# MSYS2 的发布库是 stripped 的；自建产物带符号表，不剥会白占约 1.3MB（3.4MB → 2.1MB）
if [ -f "$PREFIX/bin/libheif.dll" ]; then
    strip --strip-unneeded "$PREFIX/bin/libheif.dll" 2>/dev/null || true
fi

# ---- 4. 导入表对比 ----
NEW_DLL="$PREFIX/bin/libheif.dll"
OLD_DLL="$MSYS64/bin/libheif.dll"
if [ -f "$NEW_DLL" ]; then
    echo
    echo "==== 新库 $(du -h "$NEW_DLL" | cut -f1)（旧 MSYS2 库 $(du -h "$OLD_DLL" | cut -f1)） ===="
    echo "---- 新导入表 ----"
    objdump -p "$NEW_DLL" | grep -i "DLL Name" | awk '{$1="";print "  "$0}'
    echo "---- 旧导入表（对照）----"
    objdump -p "$OLD_DLL" | grep -i "DLL Name" | awk '{$1="";print "  "$0}'
    echo
    echo "安装前缀：$PREFIX"
    echo "下一步：PKG_CONFIG_PATH=\"$PREFIX/lib/pkgconfig:\$PKG_CONFIG_PATH\" 重新构建本项目，"
    echo "       并用 \$PREFIX/bin/libheif*.dll 替换部署目录里的 MSYS2 版，再用 HEIC/AVIF 样本验证解码。"
fi
