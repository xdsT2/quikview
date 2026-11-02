#!/usr/bin/env bash
# 版本号读取 —— 全仓唯一实现。
#
# 唯一真源：CMakeLists.txt 第 2 行 `project(quikview VERSION x.y.z ...)`。
# `tools/package.sh` 与 `tools/package-installer.sh` 都 source 这里，
# 禁止在任何地方再写第三份 sed（历史教训：两份正则各自演化，产物版本对不上）。
#
# 与旧写法的区别：旧正则 `[0-9.]*` 遇到 `0.4.0-rc1` 会静默截成 `0.4.0`。
# 这里先取完整 token 再校验，非纯数字直接失败 —— 预发布后缀只允许出现在
# git tag 与展示名里，绝不能溜进 project(VERSION)。

qv_read_version() {
    local root="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
    local cmake_file="$root/CMakeLists.txt"
    local ver

    [ -f "$cmake_file" ] || { echo "找不到 $cmake_file" >&2; return 1; }

    ver="$(sed -n 's/^project(quikview[[:space:]]\+VERSION[[:space:]]\+\([^[:space:])]*\).*/\1/p' \
            "$cmake_file" | head -1)"

    if [ -z "$ver" ]; then
        echo "读不到 $cmake_file 的 project(quikview VERSION ...)" >&2
        return 1
    fi
    # 必须三段纯数字：一二段可选多段，但至少 a.b.c
    if ! printf '%s' "$ver" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
        echo "版本号不是三段纯数字：'$ver'（预发布后缀只能进 git tag，不能进 project(VERSION)）" >&2
        return 1
    fi
    printf '%s\n' "$ver"
}

# 四段纯数字版（VIProductVersion / FILEVERSION 只吃这种）
qv_version_rc() {
    printf '%s.0\n' "$1"
}
