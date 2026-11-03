# shell-ext —— QuikView 缩略图壳扩展

Windows 资源管理器缩略图提供程序（`IThumbnailProvider`）：不启动 QuikView，
Explorer 直接在图标/缩略图视图里显示图片内容。纯 Win32 + WIC 实现，
无 Qt、无 ATL/WTL、零第三方依赖。

## 架构

```
src/
  guids.h/.cpp   GUID 定义（含 initguid 实例化 WIC 的 CLSID/IID/像素格式 GUID）
  quikthumb.h    自带 IInitializeWithStream / IThumbnailProvider 声明与提供程序类
  thumbnail.cpp  QuikThumbProvider：解码链实现
  module.cpp     DllMain / DllGetClassObject / DllCanUnloadNow /
                 DllRegisterServer / DllUnregisterServer（regsvr32 自注册）
  selftest.cpp   thumb_selftest：无注册表加载自测器（构建目标，非交付物）
scripts/
  Register-QuikThumb.ps1 / Unregister-QuikThumb.ps1
```

- 解码链：`CreateDecoderFromStream` →（内嵌缩略图不小于目标尺寸则用之，否则整帧）
  `IWICBitmapScaler` 等比缩到 cx 盒内（小图不放大）→ `IWICFormatConverter`
  转 32bppPBGRA（预乘 alpha）→ `CopyPixels` 直写 `CreateDIBSection` 缓冲 →
  返回 `HBITMAP` + `WTSAT_ARGB`。
- Explorer 通过 `IInitializeWithStream` 塞文件流；DLL 不落盘、不留全局状态。
- 交办单硬约束 1：所有 COM 入口 `try/catch(...)` 包裹，异常一律 `E_FAIL`，
  绝不穿出 DLL（防资源管理器连带崩溃）。
- 导出：`DllGetClassObject` / `DllCanUnloadNow` / `DllRegisterServer` /
  `DllUnregisterServer`，无装饰名。64 位由 MinGW `dllexport` 导出；32 位
  MSVC 因系统头已有声明（C2375），定义保持 `STDAPI`、由 CMake `/EXPORT`
  链接选项导出（x86 显式映射 `_Xxx@n`）。链接期 LNK4104 为装饰性警告，
  自测已验证导出行为正确。

### 注册模型（32/64 双视图）

| 注册表项 | 内容 | 说明 |
|---|---|---|
| `HKLM\Software\Classes\<ext>\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}` | `={CLSID}` | `.ext` 键为 32/64 共享视图，写一份 |
| `HKLM\Software\Classes\CLSID\{CLSID}\InprocServer32` | 64 位 DLL 路径 | `ThreadingModel=Apartment` |
| `HKLM\Software\Classes\WOW6432Node\CLSID\{CLSID}\InprocServer32` | 32 位 DLL 路径 | 32 位进程走这里 |

`CLSID = {D8A1C4E2-7B35-4F69-92A4-5C6D7E8F1A2B}`（`src/module.cpp` 与脚本常量需同步改）。

> **交办单笔误更正**：阶段 2 写的注册 GUID `{8895b1c6-b41f-4c1c-a562-0d564250836f}`
> 是**预览窗格处理器（IPreviewHandler）**的 ShellEx 类别；缩略图处理器类别是
> `{e357fccd-a995-4576-b01f-234630154e96}`（恰与 `IID_IThumbnailProvider` 相同）。
> 本工程按后者实现。

## 支持格式

png / jpg / jpeg / bmp / gif / webp / tif / tiff（系统 WIC 解码器）。

- **webp**：需 Windows 10 1809+ 的内置 WIC WebP 解码器（本机实测可解码）；
  更老系统该格式静默回退默认图标，不影响其它格式。
- **PSD/PSB**（交办单 `ctxmenu/thumb_psd`、`thumb_psb`）：未支持，需复合文档
  解析，交办单允许延后。
- 上下文菜单（交办单阶段 1 的 `IContextMenu`）与预览窗格（阶段 3）不在本
  DLL 范围，属后续阶段。

## 构建

64 位（MinGW-w64 g++ 16.2，交付主 DLL，`-static` 全静态、仅依赖系统 DLL）：

```powershell
$env:PATH = "E:\Agent\Tencent\WorkBuddy\Workspace\tools\msys64\mingw64\bin;$env:PATH"
cmake -S shell-ext -B build-shell64 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-shell64
cmake --build build-shell64 --target thumb_selftest
```

32 位（本机 `msys64\mingw32` 无编译器，改用 MSVC 2022 Community x86 交叉构建）：

```powershell
$env:PATH = "E:\Agent\Tencent\WorkBuddy\Workspace\tools\msys64\mingw64\bin;$env:PATH"  # 复用其中的 cmake
cmake -S shell-ext -B build-shell32 -G "Visual Studio 17 2022" -A Win32
cmake --build build-shell32 --config Release
cmake --build build-shell32 --config Release --target thumb_selftest
```

产物：`build-shell64\quikthumb.dll`、`build-shell32\quikthumb.dll`
（构建目录已被根 `.gitignore` 的 `build-*/` 覆盖，不入库）。

## 自测（不写注册表）

`thumb_selftest` 走 `LoadLibrary → DllGetClassObject → CreateInstance →
喂图片流 → GetThumbnail` 全链路，并校验未知 CLSID 拒绝与 `DllCanUnloadNow` 归零。
注意 selftest 必须动态链接 MinGW 运行时：静态 exe + 静态 DLL 同进程会在
DLL 加载阶段崩（两份静态 winpthread/libgcc 冲突），故 CMake 里 selftest
不用 `-static`，交付 DLL 保持 `-static` 自包含。

```powershell
build-shell64\thumb_selftest.exe <仓库根>\build-shell64\quikthumb.dll a.png a.jpg a.bmp a.gif a.webp
build-shell32\Release\thumb_selftest.exe <仓库根>\build-shell32\quikthumb.dll a.png a.jpg
```

预期输出逐图 `PASS: ... -> 256x170 alpha=2`，结尾 `RESULT: PASS`。

## 注册 / 反注册（管理员 PowerShell）

```powershell
powershell -ExecutionPolicy Bypass -File shell-ext\scripts\Register-QuikThumb.ps1
powershell -ExecutionPolicy Bypass -File shell-ext\scripts\Unregister-QuikThumb.ps1
```

- DLL 路径相对脚本位置解析（默认取仓库 `build-shell64\` / `build-shell32\`），
  可用 `-Dll64` / `-Dll32` 覆盖（如安装后指向 `$INSTDIR\shell-ext\quikthumb.dll`）；
  32 位 DLL 缺失时仅注册 64 位视图并告警。
- 生效：重启 `explorer.exe`，或先 `ie4uinit.exe -show` 再刷新视图。
- 也可 `regsvr32 quikthumb.dll` 自注册——但一次只注册本位数控视图，
  推荐用脚本一次注册 64+32 双视图。
- 脚本只删仍指向本产品 CLSID 的 ShellEx 项，别家 Handler 不动。

## 已知限制与说明

- **未签名**：首次加载可能触发 SmartScreen / 杀软提示，属预期（交办单硬约束 3），
  代码不做任何绕过。
- **杀软误报**：壳扩展 DLL 常被误报，需在装有安全软件的环境实测并记录
  （交办单硬约束 4）；本会话按约定未执行注册，该项待安装器集成后补测。
- **安装器集成**：`ctxmenu/use_shell_menu` 总开关关闭时安装器不得注册本
  Handler（写注册项前判断，不是注册后靠代码判断）——属后续安装器 commit。
- 本会话构建验证 = 编译链接通过 + 无注册表自测通过；注册脚本仅交付未执行
  （验证仅限语法解析与无管理员守卫路径）。
