# QuikView 架构

> 当前版本 **0.4.0**（2026-11-03 发布）。本文按 `src/` 实际代码写，改动后请同步。
> 视觉与交互规范另见 [docs/design/QUIKVIEW-DESIGN.md](docs/design/QUIKVIEW-DESIGN.md)。

## 模块划分

`src/` 共 21 个 `.cpp` + 22 个 `.h`，约 1.76 万行 C++。按职责分五层：

**入口与外壳**

| 模块 | 行数 | 职责 |
|---|---|---|
| `main.cpp` | 364 | 入口、命令行解析（`--smoke` / `--screenshot` / `--screenshot-ui`）、翻译装载 |
| `MainWindow` | 4523 + 393 | 主窗口：菜单、状态栏、拖放、全屏、快捷键、翻页调度、打印、设壁纸入口 |
| `Chrome` | 2135 + 377 | 界面外壳：顶部条、底部胶囊控制栏、翻页滑块、两侧渐显箭头、信息浮层 |
| `PillToggle` | 115 + 30 | 胶囊式开关控件 |

**画布与图像**

| 模块 | 行数 | 职责 |
|---|---|---|
| `ImageCanvas` | 1259 + 221 | 画布：缩放 / 平移 / 适配 / 裁剪框 / 自绘渲染 |
| `ImageCache` | 397 + 79 | 二级缓存，按字节计成本，支持空闲预读 |
| `ImageEffects` | 102 + 18 | 像素处理：反色、裁剪、旋转等 |
| `ToneMap` | 166 + 27 | HDR 色调映射 |
| `GammaDialog` | 81 + 28 | Gamma 调节对话框 |

**解码与数据**

| 模块 | 行数 | 职责 |
|---|---|---|
| `DirectoryLoader` | 251 + 59 | 目录扫描、文件序列与排序 |
| `RawImageLoader` | 180 + 19 | RAW 解码（libraw，可选） |
| `HeifLoader` | 186 + 11 | HEIC / AVIF 解码（libheif，可选） |
| `FrameNavigator` | 182 + 51 | 动图 GIF / 多页 TIFF 逐帧 |
| `ExifParser` | 239 + 19 | 自研 EXIF / GPS 解析，无 GPL 依赖 |

**界面**

| 模块 | 行数 | 职责 |
|---|---|---|
| `SettingsDialog` | 2375 + 234 | 设置页九类选项，含快捷键自定义与冲突检测 |
| `MetadataPanel` | 657 + 84 | EXIF / GPS / 直方图面板（`Tab` 呼出） |
| `ThumbnailBrowser` | 122 + 30 | 左侧缩略图栏，异步生成 |
| `BatchConvertDialog` | 772 + 82 | 批量格式转换 |

**基础设施**

| 模块 | 行数 | 职责 |
|---|---|---|
| `Config` | 462 + 133 | QSettings INI、便携判定、快捷键与设置持久化 |
| `UpdateChecker` | 357 + 69 | 检查更新（默认关，24 小时频控） |
| `Wallpaper` | 24 + 6 | 设为壁纸 |
| `UiStyle.h` | 513 | **UI 令牌唯一来源**：颜色 / 间距 / 字号常量，约 60 个令牌 |
| `Theme.h` | 131 | 深 / 浅主题切换 |

## 子项目

| 目录 | 说明 |
|---|---|
| `shell-ext/` | 资源管理器壳扩展：缩略图提供程序（`IThumbnailProvider`）+ 右键菜单（`IContextMenu`），纯 Win32 裸 COM，不依赖 Qt。64 位 MinGW 静态构建为主交付物 |
| `shell/` | 早期壳扩展实现（缩略图 + 预览窗格），保留备查，不再是主交付物 |
| `packaging/` | NSIS 安装包脚本（`quikview.nsi`） |
| `translations/` | 9 种语言的 `.ts`（简繁中文、英、日、韩、法、德、西、俄） |
| `resources/` | 图标与界面皮肤，全部程序化绘制，来源记录见 `PROVENANCE.json` |
| `tools/` | 皮肤生成器（`03_gen_skin.py`）与打包脚本 |
| `docs/` | 截图、素材来源、设计规范 |

## 关键选型与取舍

| 决策点 | Honeyview 的做法（逆向） | QuikView 的做法 | 理由 |
|---|---|---|---|
| 解码层 | 自研 Ark + 内嵌 libbpg/libraw | Qt 图像插件 + libraw/libheif | 用成熟开源，不重复造轮子 |
| 遥测 | config 驱动，默认上报 ana.bandi.so | **默认关，opt-in** | 逆向揭示的坑，反着做 |
| 便携 | config.ini 里 is_portable 标志 | 配置与程序同目录即自动判定 | 少一个隐藏开关 |
| 推广 | 打包 coupang.exe 联盟推广器 | **无** | 产品底线 |
| 多语言 | 自研 ini 机制 | Qt Linguist `.ts` | 标准化，工具链现成 |
| 配置格式 | 自研 config.ini（`[config]` 节） | QSettings INI | 与 Honeyview 同思路，跨平台 |
| EXIF | 内嵌第三方库 | 自研解析（`ExifParser`） | 避开 libexiv2 的 GPL |
| 壳扩展 | HVShell32/64.dll | `shell-ext/` 独立工程 | 不把 Qt 运行时拖进 explorer.exe |

## 三条「单一来源」硬规则

1. **版本号**：只有 `CMakeLists.txt` 的 `project(VERSION ...)`。它生成 `QUIKVIEW_VERSION` 宏，`setApplicationVersion()` 与「关于」对话框都读这个宏，不再各写各的字面量。
2. **UI 令牌**：只有 `src/UiStyle.h`。QSS 里写 `@Token@` 占位符，运行时由 `withColorTokens()` 替换成常量——界面代码不出现裸色值。
3. **翻译**：只有 `translations/*.ts`，运行时编译成 `.qm` 由 `QTranslator` 装载；中文（简体）是源语言，不额外装载。

## 依赖清单

- 构建：CMake ≥ 3.16 + Ninja；Qt 6（Widgets / Concurrent / PrintSupport / Network / LinguistTools）
- 可选解码：libraw、libheif（都走 `pkg_check_modules`，缺失时降级而非编译失败）
- Windows 专有：`winmm`（翻页音效，运行时合成 WAV 走 `PlaySoundW` 异步播放）
- 说明：Honeyview 用到的 libbpg 生态已停更，BPG 支持列为可选、不阻塞主线

## 冒烟入口

`--smoke <cmd[,cmd2]>` / `--screenshot` / `--screenshot-ui` 三个模式都会把配置重定向到临时 ini（环境变量 `QUIKVIEW_CONFIG_PATH`，优先级高于便携模式），**不污染用户 `config.ini`**。新增测试入口必须沿用这条路径。

## 许可证

本项目代码 MIT（见 LICENSE）。依赖库各自保留其许可证（libraw=LGPL/自定、libheif=LGPL、libwebp=BSD）。壳扩展为独立 COM 实现，不链接 Qt。
