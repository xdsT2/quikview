# QuikView Shell 扩展

> **已作废（2026-11-20）**：本目录的 `build\libquikview_shell.dll` 与 `register.bat`
> 指向旧的 CLSID 组，与正式 `quikthumb.dll` 注册冲突，误执行会覆盖正式注册、
> 造成缩略图/右键错乱。`register.bat` 现已改为只拒绝执行（非 0 退出）。
>
> **正确入口**：[`../shell-ext/README.md`](../shell-ext/README.md) 的「注册 / 反注册（管理员 PowerShell）」一节，
> 使用 `shell-ext\scripts\Register-QuikThumb.ps1` / `Unregister-QuikThumb.ps1`（64+32 位双视图）。
> 本目录仅作为历史实现保留。

Windows 资源管理器缩略图提供者（IThumbnailProvider），对照 Honeyview 的 HVShell32/64.dll。

## 功能

- 为常见图片格式（png/jpg/bmp/gif/webp/tif/ico + RAW）在资源管理器里生成缩略图
- 纯 Win32 COM + WIC（Windows Imaging Component），**不依赖 Qt**——避免把 Qt 运行时拖进 explorer.exe

## 构建

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

## 注册（已作废，见上方说明）

本目录的 `register.bat` 已被禁用（执行只会打印提示并以非 0 退出），不再作为注册入口。
请改用 [`../shell-ext/README.md`](../shell-ext/README.md) 的「注册 / 反注册（管理员 PowerShell）」一节：

```powershell
powershell -ExecutionPolicy Bypass -File shell-ext\scripts\Register-QuikThumb.ps1
powershell -ExecutionPolicy Bypass -File shell-ext\scripts\Unregister-QuikThumb.ps1
```

注册后若缩略图未立即生效，重启 `explorer.exe` 或注销重登录。

## 实现要点

| 组件 | 说明 |
|---|---|
| `ThumbnailProvider` | IThumbnailProvider + IInitializeWithStream，WIC 解码 → 等比缩放 → 32bppPBGRA → HBITMAP |
| `PreviewHandler` | IPreviewHandler + IInitializeWithStream，WIC 解码 → 等比缩放 → StretchDIBits 绘制（深色背景） |
| `dllmain.cpp` | 类工厂 + DllRegisterServer（写 CLSID + 缩略图 ShellEx 槽位 + SystemFileAssociations 预览槽位） |
| 缩略图 CLSID | {C5B3E2D1-4A6F-4B7E-9C2D-1E8A3F5B7D90} |
| 预览 CLSID | {D4E5F6A7-8B9C-4D0E-8F1A-2B3C4D5E6F70} |

## 已知限制

- 依赖 WIC 内置编解码器；WIC 不支持的格式（如部分 RAW）需额外 WIC 编解码器。
