# Windows 安装包（NSIS）

用 [NSIS](https://nsis.sourceforge.io) 把 `build/` 出来的程序打成标准安装向导 EXE。

## 打包步骤

一条命令出齐两个发布产物（不用再手敲 makensis）：

```bash
# 0. 工具链进 PATH（windeployqt / objdump / cmake 都在这）
export PATH="/e/Agent/Tencent/WorkBuddy/Workspace/tools/msys64/mingw64/bin:$PATH"

# 1. 编译
cmake --build build

# 2. 出包：dist/soleview-<ver>-win64.zip + dist/SoleView-<ver>-setup.exe
tools/package-installer.sh

# 只要免安装 zip：tools/package.sh
```

`package-installer.sh` 内部先跑 `package.sh` 产 staging（windeployqt + DLL 闭包 + 自检）与 zip，
再用同一份 staging 调 `makensis` —— 两个产物永远同源同版本。

### 版本从哪来

唯一真源是 `CMakeLists.txt` 第 2 行 `project(SoleView VERSION x.y.z ...)`：

| 消费点 | 怎么拿到 |
|---|---|
| zip / staging 目录名 | `tools/lib/version.sh` 的 `qv_read_version()` |
| `SoleView-<ver>-setup.exe` | 同上，经 `/DOUTFILE` 传给 nsi |
| 安装向导显示名 / 卸载列表 `DisplayVersion` | `/DAPPVERSION=<ver>` |
| 安装包文件版本（`VIProductVersion`） | `/DAPPVERSION_RC=<ver>.0`（四段纯数字） |
| exe 内嵌 `VERSIONINFO` | CMake `configure_file` 生成 `qv_version.h`，`resources/app.rc` 引用 |

`soleview.nsi` **没有默认版本**：漏传任何一个 `/D` 都会 `!error` 中止，
不会悄悄产出 `0.0.0` 或上一个版本的包。

### 手工调 makensis 时

```bash
makensis /DSRC="<staging 绝对路径>" /DOUTFILE="<setup 绝对路径>" \
         /DAPPVERSION=0.4.0 /DAPPVERSION_RC=0.4.0.0 packaging/soleview.nsi
```

注意两点：`/DSRC` 必须是**绝对路径**；`soleview.nsi` 是 **UTF-16LE + BOM**，
改完必须按原编码写回，否则 makensis 报 `Bad text encoding`。
Git Bash 下 `/D...` 参数会被 MSYS 当成路径改写，需 `MSYS2_ARG_CONV_EXCL='*'`。

## 说明

- **Qt SVG 模块（2026-11-18 起必须）**：功能图标由 `QSvgRenderer` 渲染，程序 import `Qt6Svg.dll`。`package.sh` 里 `windeployqt` 会按 import 自动收这一枚，并在收完后硬校验 `$STAGE/Qt6Svg.dll` 是否存在，缺了直接 `exit 1`——干净机器上少这一枚就是整屏缺图标，不能放进发布包。构建机的 Qt 套件也必须含 Svg 模块（本机是 MSYS2 `mingw-w64-x86_64-qt6-svg` 的 6.11.2 拷进官方套件，包含 `bin/Qt6Svg.dll`、`lib/libQt6Svg.dll.a`、`lib/cmake/Qt6Svg/`、`include/qt6/QtSvg/`）。
- 安装向导为简体中文，默认安装到 `D:\Entertainment\SoleView`（可在目录页修改），带开始菜单组、桌面快捷方式（可勾选）与标准卸载器。
- 安装器将 SoleView 登记为 PSD 的可选打开程序，并在勾选资源管理器集成时注册 PSD/PSB 内嵌缩略图处理器；不会静默抢占系统默认应用。
- 程序翻译在编译期嵌入资源 `:/i18n`，payload 无需 `.qm` 文件。
- `usp10.dll` 为系统组件，无需收集。
- 安装 / 升级 / 卸载**不会结束或重启 `explorer.exe`**，也**不会要求重启电脑**：壳扩展 DLL 用带版本号的文件名（`solethumb-<版本>.dll` / `solethumb32-<版本>.dll`），升级时新文件是全新路径，旧文件即使仍被资源管理器映射也不冲突，因此不需要「改名让路」，也**一律不用 `Delete /REBOOTOK`**（它删不掉就会置上重启标志，完成页随即变成「需要重启计算机」——0.5.3 那次就是它）。旧命名的 DLL、`.bak` 与更早安装器的重试残留会在安装时尽力删除，删不掉的留到下一次安装或卸载。安装与卸载各只调用一次 `SHChangeNotify(SHCNE_ASSOCCHANGED)` 通知 Shell 刷新。
- 右键菜单 Handler 自 0.5.4 起**逐扩展名注册**（png/jpg/jpeg/bmp/gif/webp/tif/tiff/psd/psb），不再挂 `*`：任意文件右键都不会再把壳扩展 DLL 注入资源管理器；升级时自动清掉旧的 `*` 注册。0.5.2 及以前「先 taskkill 强杀、装完再由提权进程 Exec 拉起」的做法已撤销——那会造成安装后桌面黑屏、需手动重启资源管理器。
- 安装器未做代码签名，SmartScreen 会提示「更多信息 → 仍要运行」。
- 产物命名 `soleview-<版本>-win64.zip` / `SoleView-<版本>-setup.exe`，挂到 GitHub Release。
