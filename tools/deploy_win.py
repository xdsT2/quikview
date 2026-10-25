"""组装 Windows 发布目录：exe + Qt 插件 + MSYS2 依赖闭包。

用法（在仓库根目录）：
    python tools/deploy_win.py --build-dir build --out Temp/dist/quikview-0.2.0-win64

为什么需要这个脚本：CMakeLists 目前没有 install() 规则，windeployqt 又只认
Qt 自己的库，MSYS2 的 libheif / libraw / ICU 它一律不管。缺一个 DLL 就是
"程序起不来"，所以这里把两步串起来，并且对结果做自检。

关于 qjp2：MSYS2 的 Qt 用 Jasper 解 JP2，而 jasper-4.2.9 这个构建带断言。
插件只要能被加载，打开 .jp2 就会撞 `jas_init` 的 `!jas_global.initialized`
（Jasper 被初始化两次），弹出 "Microsoft Visual C++ Runtime Library" 错误框，
表现为「点了没反应」。项目已决定不支持 JP2/J2K，所以这里直接把插件和它
依赖的 libjasper 从发布目录剔除——插件不加载，Qt 就干净地报「不支持的格式」。
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

# 不随包发布的插件/库：原因见文件头
EXCLUDE_FILES = {
    "qjp2.dll",        # Jasper 断言，打开 .jp2 即弹中止对话框
    "libjasper.dll",   # 只被 qjp2 用到
}

# 发布目录自检：缺任何一个，程序都会起不来或格式打不开
REQUIRED = [
    "quikview.exe",
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Widgets.dll",
    "Qt6PrintSupport.dll",
    "platforms/qwindows.dll",
    "styles/qmodernwindowsstyle.dll",
    "imageformats/qjpeg.dll",
    "imageformats/qgif.dll",
    "imageformats/qico.dll",
    "imageformats/qwebp.dll",
    "imageformats/qtiff.dll",
    "imageformats/qicns.dll",
    "imageformats/qmng.dll",
    "imageformats/qtga.dll",
    "imageformats/qwbmp.dll",
    "libheif.dll",
    "libraw-25.dll",
]

SYSTEM_PREFIXES = (
    "kernel32", "user32", "gdi32", "advapi32", "shell32", "ole32", "oleaut32",
    "comdlg32", "comctl32", "shlwapi", "dwmapi", "uxtheme", "winmm", "ws2_32",
    "api-ms-win", "ext-ms-win", "ntdll", "msvcrt", "msimg32", "version",
    "opengl32", "glu32", "imm32", "wtsapi32", "userenv", "netapi32", "psapi",
    "secur32", "crypt32", "bcrypt", "dbghelp", "setupapi", "winspool",
    "d3d11", "d3d12", "d3d9", "dxgi", "dwrite", "d2d1", "dcomp", "mpr",
    "wldap32", "authz", "pdh", "powrprof", "propsys", "rpcrt4", "sspicli",
    "iphlpapi", "dnsapi", "winhttp", "mswsock", "normaliz", "gdiplus", "mscms",
    "wintrust", "shcore", "usp10", "ncrypt", "dcomp", "twinapi", "cfgmgr32",
)


def is_system(name: str) -> bool:
    low = name.lower()
    return any(low.startswith(p) for p in SYSTEM_PREFIXES)


def imports_of(path: Path) -> list[str]:
    out = subprocess.run(["objdump", "-p", str(path)],
                         capture_output=True, text=True, errors="replace").stdout
    return [ln.split(":", 1)[1].strip()
            for ln in out.splitlines() if ln.strip().startswith("DLL Name:")]


def run(cmd: list[str]) -> None:
    print("  $", " ".join(cmd))
    subprocess.run(cmd, check=True)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--out", required=True)
    ap.add_argument("--qt-bin", required=True,
                    help="Qt 的 bin 目录（windeployqt 所在处）")
    args = ap.parse_args()

    build = Path(args.build_dir).resolve()
    out = Path(args.out).resolve()
    qt_bin = Path(args.qt_bin).resolve()
    exe_src = build / "quikview.exe"
    if not exe_src.is_file():
        print(f"找不到 {exe_src}", file=sys.stderr)
        return 2

    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    shutil.copy2(exe_src, out / "quikview.exe")
    print(f"[1/4] 复制 exe → {out}")

    print("[2/4] windeployqt")
    run([str(qt_bin / "windeployqt6.exe"), "--release", "--no-translations",
         "--no-opengl-sw", "--no-system-d3d-compiler", "--no-system-dxc-compiler",
         "--no-quick-import", "--dir", str(out), str(out / "quikview.exe")])

    print("[3/4] 补齐非 Qt 依赖")
    msvc_dir = qt_bin  # MSYS2 里 Qt 与依赖同目录
    copied, missing, seen = [], {}, set()
    queue = [p for p in out.rglob("*") if p.suffix.lower() in (".exe", ".dll")]
    while queue:
        cur = queue.pop()
        for dep in imports_of(cur):
            key = dep.lower()
            if key in seen:
                continue
            seen.add(key)
            if is_system(dep) or (out / dep).is_file():
                continue
            src = msvc_dir / dep
            if src.is_file():
                shutil.copy2(src, out / dep)
                copied.append(dep)
                queue.append(out / dep)
            else:
                missing.setdefault(dep, cur.name)
    print(f"  补齐 {len(copied)} 个依赖")
    if missing:
        real = {k: v for k, v in missing.items() if not is_system(k)}
        if real:
            print("  找不到（需要人工确认）:")
            for k, v in sorted(real.items()):
                print(f"    ! {k:30s} ← {v}")

    print("[4/4] 剔除不支持格式的插件")
    for name in EXCLUDE_FILES:
        for hit in out.rglob(name):
            hit.unlink()
            print(f"  删除 {hit.relative_to(out)}")

    bad = [r for r in REQUIRED if not (out / r).is_file()]
    if bad:
        print("\n自检失败，缺文件:", file=sys.stderr)
        for b in bad:
            print(f"  - {b}", file=sys.stderr)
        return 1

    total = sum(p.stat().st_size for p in out.rglob("*") if p.is_file())
    n = sum(1 for p in out.rglob("*") if p.is_file())
    print(f"\n自检通过：{n} 个文件，{total / 1048576:.1f} MB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
