# Windows 安装包（NSIS）

用 [NSIS](https://nsis.sourceforge.io) 把 `build/` 出来的程序打成标准安装向导 EXE。

## 打包步骤

一条命令出齐两个发布产物（不用再手敲 makensis）：

```bash
# 0. 工具链进 PATH（windeployqt / objdump / cmake 都在这）
export PATH="/e/Agent/Tencent/WorkBuddy/Workspace/tools/msys64/mingw64/bin:$PATH"

# 1. 编译
cmake --build build

# 2. 出包：dist/quikview-<ver>-win64.zip + dist/QuikView-<ver>-setup.exe
tools/package-installer.sh

# 只要免安装 zip：tools/package.sh
```

`package-installer.sh` 内部先跑 `package.sh` 产 staging（windeployqt + DLL 闭包 + 自检）与 zip，
再用同一份 staging 调 `makensis` —— 两个产物永远同源同版本。

### 版本从哪来

唯一真源是 `CMakeLists.txt` 第 2 行 `project(quikview VERSION x.y.z ...)`：

| 消费点 | 怎么拿到 |
|---|---|
| zip / staging 目录名 | `tools/lib/version.sh` 的 `qv_read_version()` |
| `QuikView-<ver>-setup.exe` | 同上，经 `/DOUTFILE` 传给 nsi |
| 安装向导显示名 / 卸载列表 `DisplayVersion` | `/DAPPVERSION=<ver>` |
| 安装包文件版本（`VIProductVersion`） | `/DAPPVERSION_RC=<ver>.0`（四段纯数字） |
| exe 内嵌 `VERSIONINFO` | CMake `configure_file` 生成 `qv_version.h`，`resources/app.rc` 引用 |

`quikview.nsi` **没有默认版本**：漏传任何一个 `/D` 都会 `!error` 中止，
不会悄悄产出 `0.0.0` 或上一个版本的包。

### 手工调 makensis 时

```bash
makensis /DSRC="<staging 绝对路径>" /DOUTFILE="<setup 绝对路径>" \
         /DAPPVERSION=0.4.0 /DAPPVERSION_RC=0.4.0.0 packaging/quikview.nsi
```

注意两点：`/DSRC` 必须是**绝对路径**；`quikview.nsi` 是 **UTF-16LE + BOM**，
改完必须按原编码写回，否则 makensis 报 `Bad text encoding`。
Git Bash 下 `/D...` 参数会被 MSYS 当成路径改写，需 `MSYS2_ARG_CONV_EXCL='*'`。

## 说明

- 安装向导为简体中文，默认安装到 `D:\Entertainment\QuikView`（可在目录页修改），带开始菜单组、桌面快捷方式（可勾选）与标准卸载器。
- 安装器将 QuikView 登记为 PSD 的可选打开程序，并在勾选资源管理器集成时注册 PSD/PSB 内嵌缩略图处理器；不会静默抢占系统默认应用。
- 程序翻译在编译期嵌入资源 `:/i18n`，payload 无需 `.qm` 文件。
- `usp10.dll` 为系统组件，无需收集。
- 安装器未做代码签名，SmartScreen 会提示「更多信息 → 仍要运行」。
- 产物命名 `quikview-<版本>-win64.zip` / `QuikView-<版本>-setup.exe`，挂到 GitHub Release。
