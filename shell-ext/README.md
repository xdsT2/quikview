# shell-ext —— QuikView 壳扩展（缩略图 + 上下文菜单）

Windows 资源管理器壳扩展，一个 DLL 装两个 COM 类：

- **缩略图提供程序**（`IThumbnailProvider`，交办单阶段 2）：不启动 QuikView，
  Explorer 直接在图标/缩略图视图里显示图片内容；
- **上下文菜单**（`IContextMenu`，交办单阶段 1）：右键图片注入
  「用 QuikView 浏览」「用 QuikView 转换」两项。

纯 Win32 裸 COM 实现，无 Qt、无 ATL/WTL、零第三方依赖。

## 架构

```
src/
  guids.h/.cpp   GUID 定义（含 initguid 实例化 WIC 的 CLSID/IID/像素格式 GUID）
  quikthumb.h    自带 QvInitializeWithStream / IThumbnailProvider 声明与提供程序类、
                 模块锁与支持扩展名的共享声明
  thumbnail.cpp  QuikThumbProvider：解码链实现
  contextmenu.h  IShellExtInit / IContextMenu 直接用 shobjidl.h 系统声明
  contextmenu.cpp QuikContextMenu：菜单注入 / 动词分发 / 启动 quikview.exe
  module.cpp     DllMain / DllGetClassObject / DllCanUnloadNow /
                 DllRegisterServer / DllUnregisterServer（regsvr32 自注册）
  selftest.cpp   thumb_selftest：无注册表加载自测器（构建目标，非交付物）
  fake_quikview.cpp ctx_fake_quikview：selftest 的 quikview.exe 替身
                 （EXCLUDE_FROM_ALL，非交付物）
scripts/
  Register-QuikThumb.ps1 / Unregister-QuikThumb.ps1
```

- 交办单硬约束 1：所有 COM 入口 `try/catch(...)` 包裹，异常一律 `E_FAIL`，
  绝不穿出 DLL（防资源管理器连带崩溃）。
- 导出：`DllGetClassObject` / `DllCanUnloadNow` / `DllRegisterServer` /
  `DllUnregisterServer`，无装饰名。64 位由 MinGW `dllexport` 导出；32 位
  MSVC 因系统头已有声明（C2375），定义保持 `STDAPI`、由 CMake `/EXPORT`
  链接选项导出（x86 显式映射 `_Xxx@n`）。链接期 LNK4104 为装饰性警告，
  自测已验证导出行为正确。
- 头文件坑：MinGW 的 `shobjidl.h` 会连带 `propsys.h`，其 `IInitializeWithStream`
  方法名与官方 IDL 不一致（误写作 Initialize）——故流接口自带声明并改名
  `QvInitializeWithStream`（vtable 布局与 IID 不变），`shobjidl.h` 的其余
  系统接口（IShellExtInit/IContextMenu/CMINVOKECOMMANDINFO 等）照常使用。

### 缩略图解码链（阶段 2）

`CreateDecoderFromStream` →（内嵌缩略图不小于目标尺寸则用之，否则整帧）
`IWICBitmapScaler` 等比缩到 cx 盒内（小图不放大）→ `IWICFormatConverter`
转 32bppPBGRA（预乘 alpha）→ `CopyPixels` 直写 `CreateDIBSection` 缓冲 →
返回 `HBITMAP` + `WTSAT_ARGB`。Explorer 通过 `IInitializeWithStream` 塞文件
流；DLL 不落盘、不留全局状态。

### 上下文菜单（阶段 1）

- 菜单两项：分隔线 + 「用 QuikView 浏览」→ `quikview.exe "<path>"`、
  「用 QuikView 转换」→ `quikview.exe --batch "<path>"`（`CreateProcessW`
  启动，失败返回失败 HRESULT，Explorer 静默忽略，绝不弹窗/不崩）。
- **多选策略：只处理第一个选中文件**（交办单允许二选一；初始化只保留
  CF_HDROP 第一项）。
- 过滤：第一个选中文件扩展名不在支持列表内（见下）不注入菜单项；
  `CMF_DEFAULTONLY`（双击等默认动词场景）不注入。
- 动词：`QuikView.Browse` / `QuikView.Convert`（`GetCommandString` 提供
  GCS_VERBW/GCS_VERBA）。**坑：`MAKEINTRESOURCEA(0) == nullptr`** ——
  Explorer 调第一个命令传来的 `lpVerb` 就是空指针，分发顺序必须为
  lpVerbW（Unicode 字符串）→ lpVerb 为空（=序号 0）→ 整数序号 → ANSI 字符串。
- **quikview.exe 解析顺序（稳妥方案）**：
  1. DLL 同目录（绿色安装/自测首选，和 DLL 一起部署即可用）；
  2. 注册表 `HKCU\Software\QuikView` 的 `InstallDir`（按用户安装预留）、
  3. `HKLM\Software\QuikView` 的 `InstallDir`（安装器现行，
     `packaging/quikview.nsi` 写入）。
     交办单写的 `InstallPath` 作为预留别名一并读取；读取固定走 64 位视图
     （`KEY_WOW64_64KEY`），安装器只写一处，32 位 DLL 也能读到。
     都找不到时菜单仍会显示，点击静默失败。

## 支持格式

png / jpg / jpeg / bmp / gif / webp / tif / tiff（系统 WIC 解码器），
缩略图与上下文菜单过滤共用同一份列表（`module.cpp` 的 `kSupportedExts`，
改动须同步 scripts/*.ps1 的 `$Exts`）。

- **webp**：需 Windows 10 1809+ 的内置 WIC WebP 解码器（本机实测可解码）；
  更老系统该格式静默回退默认图标，不影响其它格式。
- **PSD/PSB**（交办单 `ctxmenu/thumb_psd`、`thumb_psb`）：未支持，需复合文档
  解析，交办单允许延后。
- **`--batch` 现状**：主程序 `src/main.cpp` 尚未定义该选项（QCommandLineParser
  会拒绝未知选项），「用 QuikView 转换」在主程序接线 `ctxmenu/transform_dlg`
  之前点了不会有转换行为——属主程序批次的工作，本 DLL 按交办单口径先传参。
- 预览窗格（交办单阶段 3，`IPreviewHandler`）不在本 DLL 范围，属后续阶段。

## 注册模型（32/64 双视图）

| 注册表项 | 内容 | 说明 |
|---|---|---|
| `HKLM\Software\Classes\<ext>\ShellEx\{e357fccd-…}` | `={缩略图CLSID}` | `.ext` 键为 32/64 共享视图，写一份 |
| `HKLM\Software\Classes\*\shellex\ContextMenuHandlers\QuikView` | `={上下文菜单CLSID}` | `*` 键为字面键名；64/32 双视图各写一份 |
| `HKLM\Software\Classes\CLSID\{…}\InprocServer32` | 64 位 DLL 路径 | 两个 CLSID 都写；`ThreadingModel=Apartment` |
| `HKLM\Software\Classes\WOW6432Node\CLSID\{…}\InprocServer32` | 32 位 DLL 路径 | 32 位进程走这里 |

- `CLSID(缩略图) = {D8A1C4E2-7B35-4F69-92A4-5C6D7E8F1A2B}`
- `CLSID(上下文菜单) = {C9FFAFAD-7C1C-4CCC-A37C-A075ED1F27AF}`

（`src/module.cpp` 与脚本常量需同步改。）

> **交办单笔误更正**：阶段 2 写的注册 GUID `{8895b1c6-b41f-4c1c-a562-0d564250836f}`
> 是**预览窗格处理器（IPreviewHandler）**的 ShellEx 类别；缩略图处理器类别是
> `{e357fccd-a995-4576-b01f-234630154e96}`（恰与 `IID_IThumbnailProvider` 相同）。
> 本工程按后者实现。同理，`InstallPath` 实为安装器写入的 `InstallDir`，两者都读。

## 构建

64 位（MinGW-w64 g++ 16.2，交付主 DLL，`-static` 全静态、仅依赖系统 DLL）：

```powershell
$env:PATH = "E:\Agent\Tencent\WorkBuddy\Workspace\tools\msys64\mingw64\bin;$env:PATH"
cmake -S shell-ext -B build-shell64 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-shell64
cmake --build build-shell64 --target thumb_selftest
cmake --build build-shell64 --target ctx_fake_quikview
```

32 位（本机 `msys64\mingw32` 无编译器，改用 MSVC 2022 Community x86 交叉构建）：

```powershell
$env:PATH = "E:\Agent\Tencent\WorkBuddy\Workspace\tools\msys64\mingw64\bin;$env:PATH"  # 复用其中的 cmake
cmake -S shell-ext -B build-shell32 -G "Visual Studio 17 2022" -A Win32
cmake --build build-shell32 --config Release
cmake --build build-shell32 --config Release --target thumb_selftest
cmake --build build-shell32 --config Release --target ctx_fake_quikview
```

产物：`build-shell64\quikthumb.dll`、`build-shell32\quikthumb.dll`
（构建目录已被根 `.gitignore` 的 `build-*/` 覆盖，不入库）。

## 自测（不写注册表）

`thumb_selftest` 走 `LoadLibrary → DllGetClassObject → CreateInstance` 全链路：

- 缩略图：喂图片流 → `GetThumbnail` → 校验 HBITMAP 尺寸/位深；
- 上下文菜单：自建 CF_HDROP 数据对象 → `Initialize` → `QueryContextMenu`
  枚举菜单项 → `GetCommandString` 动词 → `InvokeCommand` **真启动进程**——
  quikview.exe 由 `ctx_fake_quikview.exe` 顶替（selftest 自动拷到 DLL 同目录、
  结束即删），替身把命令行写进标记文件供断言，不会拉起真实应用。

注意 selftest 必须动态链接 MinGW 运行时：静态 exe + 静态 DLL 同进程会在
DLL 加载阶段崩（两份静态 winpthread/libgcc 冲突），故 CMake 里 selftest
不用 `-static`，交付 DLL 保持 `-static` 自包含；运行 selftest 时 PATH 需含
`mingw64\bin`（找 libstdc++-6.dll 等）。

```powershell
build-shell64\thumb_selftest.exe <仓库根>\build-shell64\quikthumb.dll a.png a.jpg a.bmp a.gif a.webp
build-shell32\Release\thumb_selftest.exe <仓库根>\build-shell32\quikthumb.dll a.png a.jpg
```

预期：逐图 `PASS: ... -> 256x170 alpha=2`；随后 `--- IContextMenu ---` 逐条
`PASS: ctx-N ...`（本机若装过 QuikView，ctx-7 打印 SKIP 属预期——避免拉起
真实安装的 exe）；结尾 `RESULT: PASS`。当前两份数均为 **21/21 PASS**
（缩略图 7 + 上下文菜单 14）。

## 注册 / 反注册（管理员 PowerShell）

```powershell
powershell -ExecutionPolicy Bypass -File shell-ext\scripts\Register-QuikThumb.ps1
powershell -ExecutionPolicy Bypass -File shell-ext\scripts\Unregister-QuikThumb.ps1
```

- DLL 路径相对脚本位置解析（默认取仓库 `build-shell64\` / `build-shell32\`），
  可用 `-Dll64` / `-Dll32` 覆盖（如安装后指向 `$INSTDIR\shell-ext\quikthumb.dll`）；
  32 位 DLL 缺失时仅注册 64 位视图并告警。
- 生效：重启 `explorer.exe`，或先 `ie4uinit.exe -show` 再刷新视图。
- 也可 `regsvr32 quikthumb.dll` 自注册——但一次只注册本位数控视图
  （32 位 regsvr32 对 `Software\Classes` 的写自动落在 WOW6432Node），
  推荐用脚本一次注册 64+32 双视图。
- 脚本只删仍指向本产品 CLSID 的项（含 `*\shellex\ContextMenuHandlers\QuikView`
  两个视图），别家 Handler 不动；`*` 是字面键名，脚本一律 `-LiteralPath`
  （否则会被 PowerShell 当通配符展开）。

## 人工验收清单（阶段 1，注册后进行）

自动化自测覆盖不了资源管理器真机行为，注册后按交办单验收表逐项手测：

| 判据 | 做法 |
|---|---|
| 菜单 | 右键 jpg/png/webp → 出现分隔线 + 两项，点击能打开对应文件 |
| 多选 | 选中 3 张图右键 → 菜单项存在，点击后只处理第一张（本工程策略） |
| 转换 | 「用 QuikView 转换」等主程序支持 `--batch` 后复测 |
| 卸载 | 反注册后 `CLSID\{…}`（两 CLSID）与 `ContextMenuHandlers\QuikView` 全部消失，重启资源管理器无残留菜单 |
| 稳定性 | 反复右键 50 次、打开 20 个文件夹，资源管理器不崩 |
| 杀软 | 装有安全软件的环境实测一次并记录（交办单硬约束 4） |

## 已知限制与说明

- **未签名**：首次加载可能触发 SmartScreen / 杀软提示，属预期（交办单硬约束 3），
  代码不做任何绕过。
- **杀软误报**：壳扩展 DLL 常被误报，需在装有安全软件的环境实测并记录
  （交办单硬约束 4）；本会话按约定未执行注册，该项待安装器集成后补测。
- **安装器集成**：`ctxmenu/use_shell_menu` 总开关关闭时安装器不得注册本
  Handler（写注册项前判断，不是注册后靠代码判断）——属后续安装器 commit。
- 本会话构建验证 = 编译链接通过 + 无注册表自测通过（两份 DLL 21/21 PASS）；
  注册脚本仅交付未执行（验证仅限 PSParser 语法校验 0 错误与无管理员守卫路径）。
