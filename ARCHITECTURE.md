# QuikView 架构

> 当前版本 **0.5.6**（2026-11-21 发布，tag `v0.5.6`）。`0.5.1` 从未发布、已跳过，其变更并入 0.5.2。本文按 `src/` 实际代码写，改动后请同步。
> 视觉与交互规范另见 [docs/design/QUIKVIEW-DESIGN.md](docs/design/QUIKVIEW-DESIGN.md)。

## 模块划分

`src/` 共 33 个 `.cpp` + 39 个 `.h`，合计 34,870 行（`.cpp` 29,971 + `.h` 4,899）。按职责分五层（表内行数为模块规模快照，2026-11-21 实测，以文件实际内容为准）。

**入口与外壳**

| 模块 | 行数 | 职责 |
|---|---|---|
| `main.cpp` | 342 | 入口、命令行解析（`--smoke` / `--screenshot` / `--screenshot-ui`）、翻译装载 |
| `MainWindow` | 8233 + 562 | 主窗口：菜单、状态栏、拖放、全屏、快捷键、翻页调度、打印、设壁纸入口 |
| `Chrome` | 2479 + 390 | 界面外壳：顶部条、底部胶囊控制栏、翻页滑块、两侧渐显箭头、信息浮层 |
| `PillToggle` | 223 + 51 | 胶囊式开关控件 |

**画布与图像**

| 模块 | 行数 | 职责 |
|---|---|---|
| `ImageCanvas` | 1908 + 277 | 画布：缩放 / 平移 / 适配 / 裁剪框 / 自绘渲染 |
| `ImageCache` | 616 + 110 | 二级缓存，按字节计成本，空闲预读；显式跳转可抢占旧解码（代次隔离），全图解码走专用线程池（上限 2） |
| `ImageEffects` | 90 + 12 | 像素处理：反色、裁剪、旋转等 |
| `ToneMap` | 149 + 20 | HDR 色调映射 |
| `GammaDialog` | 161 + 25 | Gamma 调节对话框 |
| `OverviewPanel` | 409 + 73 | 底栏「图片总览」：全窗网格、按原始宽高比排布、单击选中 / 双击或 Enter 打开 / `Esc` 返回；打开时暂时接管快捷键 |
| `RotateAutoSave` | 248 + 13 | 旋转后自动写回：格式白名单跳过（RAW / 动图 / 多页 TIFF / PSD），同目录临时文件 → fsync → 替换，JPEG 保留 EXIF 且 Orientation 归 1 |

**解码与数据**

| 模块 | 行数 | 职责 |
|---|---|---|
| `DirectoryLoader` | 544 + 119 | 目录扫描、文件序列与排序 |
| `RawJpgGroup` | 143 + 44 | RAW+JPG（含 PSD+JPG）分组：目录列表构建层的纯函数，只做字符串与路径判断，可在后台线程调用 |
| `RawImageLoader` | 163 + 15 | RAW 解码（libraw，可选） |
| `HeifLoader` | 167 + 9 | HEIC / AVIF 解码（libheif，可选） |
| `FrameNavigator` | 170 + 42 | 动图 GIF / 多页 TIFF 逐帧 |
| `ExifParser` | 308 + 26 | 自研 EXIF / GPS 解析，无 GPL 依赖 |
| `ZipArchive` | 459 + 91 | ZIP / CBZ 只读解析：自研 EOCD / 中央目录 / 局部头，解压复用 in-tree miniz 的 `tinfl` 与 `mz_crc32`；只认 store / deflate，不落盘、不解密、不支持分卷 |
| `ArchiveStore` | 360 + 69 | 压缩包「虚拟目录」唯一入口：包注册表、虚拟路径拼装/拆分、条目解码；**包内条目路径 = 包路径 + `/` + 条目名** |
| `PsdLoader` | 309 + 55 | PSD/PSB 解析：`psd_sdk`（BSD-2，vendor）适配层，只覆盖 8bit RGB / PSD v1；整图用文件内原始合成预览，逐层重合成不在范围内 |
| `LoadError.h` | 13 | 解码失败类别枚举（`Corrupt` / `Unsupported` / `NoCompatPreview`），判定只在 loader 层，UI 只按类别取文案 |

**界面**

| 模块 | 行数 | 职责 |
|---|---|---|
| `SettingsDialog` | 6912 + 492 | **12 页**（一般 / 浏览 / 外观 / 图像处理 / 格式关联 / 键盘 / 鼠标 / 书签 / 上下文菜单 / 查看器右键菜单 / 杂项 / 语言）。**首屏只建「一般」，其余全部懒构建**，首次进入或首次搜索时才建；登记处 `SettingsDialog.cpp:1774-1794` |
| `MetadataPanel` | 798 + 120 | 图片信息 HUD：标题栏 + 键值栅格 + 可折叠直方图（`Tab` 呼出）；实装 Tab-HUD 2026-11-13 定稿 |
| `HudShell` | 360 + 81 | 画布浮层共用外壳（2026-11-14 浮层标准统一）：磨砂底板 `HudBackdrop`、缓存投影 `HudEffect`、28×28 标题栏按钮 `HudToolButton` 与 110/85ms 开合动效，信息浮层与图层面板共用 |
| `LayerPanel` | 387 + 63 | PSD 图层面板：右侧浮层，整图 / 图层预览 / 分组折叠 / 独立眼睛显隐；外壳走 `HudShell`，Esc 优先收起 |
| `ThumbnailBrowser` | 440 + 71 | 底部胶片栏，绘制并异步解码可见项 |
| `CtxMenu` / `MenuCatalog.h` / `MenuIcons` | 648 + 75 / 362 / 23 + 20 | 应用内右键菜单：三层收纳目录、全量可见性配置（`config/appmenu/*`）、顶部五图标快捷区。**图标自 2026-11-18 起不再自绘**：`MenuIcons` 只剩把历史 id 转成 `AppIcon` 语义 id 的转发层 |
| `AppMenuPreview` | 859 + 147 | 「查看器右键菜单」设置页的常驻一级预览 + 浮动二级预览：只呈现当前可见两级菜单与两级内交互，不改任何配置 |
| `AppIcon` | 270 + 33 | **功能图标唯一入口**：语义 id → Fluent 家族映射表（一份，94 条），按目标像素取 16/20/24 档，`QSvgRenderer` 渲染 + 按主题重新着色，`QPixmap` 按 id/px/style/dpr/ink 缓存；qrc 缺失时 `available()==false` 优雅回退成无图标 |
| `BatchConvertDialog` | 759 + 71 | 批量格式转换 |

**基础设施**

| 模块 | 行数 | 职责 |
|---|---|---|
| `Config` | 690 + 161 | QSettings INI、便携判定、快捷键与设置持久化 |
| `UpdateChecker` | 314 + 56 | 检查更新（默认关，24 小时频控） |
| `Wallpaper` | 23 + 4 | 设为壁纸 |
| `UiStyle.h` | 763 | **UI 令牌唯一来源**：颜色 / 间距 / 字号，79 条 `inline constexpr`（其中颜色 45 条） |
| `Theme.h` | 134 | 深 / 浅主题切换 |
| `FlipProbe.h` / `PerfProbe.h` / `ThemeProbe.h` | 66 / 96 / 68 | 验收计时探针（翻图 / 打开分段 / 主题切换），环境变量开启，未开启时首判即返回，零开销 |

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

## 「单一来源」硬规则

1. **版本号**：只有 `CMakeLists.txt` 的 `project(VERSION ...)`。它生成 `QUIKVIEW_VERSION` 宏，`setApplicationVersion()` 与「关于」对话框都读这个宏，不再各写各的字面量。文档侧（README / ROADMAP / CHANGELOG / 本文）的版本口径以 tag 与 `发布产物/README.md` 的发布状态为准，不自行推断。
2. **UI 令牌**：只有 `src/UiStyle.h`。QSS 里写 `@Token@` 占位符，运行时由 `withColorTokens()` 替换成常量——界面代码不出现裸色值。
3. **翻译**：只有 `translations/*.ts`，运行时编译成 `.qm` 由 `QTranslator` 装载；中文（简体）是源语言，不额外装载。
4. **图标**：只有 `src/AppIcon.cpp` 的映射表。`resources/icons/fluent/` 里的 SVG 由 `tools/install-fluent-icons.py` 依该表复制、`resources/ui.qrc` 的登记段也由它生成——不要在页面里各自拼路径，也不要手工往 qrc 里加图标，更不要把一个家族的多档尺寸全塞进来。
5. **压缩包虚拟路径**：只有 `src/ArchiveStore.cpp`。包内条目的路径一律是 **包文件的绝对路径 + `/` + 包内条目名**（例：`E:/comics/book.cbz/001.jpg`）——这样 `QFileInfo(path).fileName()` 就是条目名，胶片栏 / 标题 / 缩略图的既有代码不必为虚拟路径另写取名逻辑，`lastIndexOf('/')` 之前就是包路径。**禁止**各模块自行拼 `包 + 分隔符 + 条目`，或用 `:`、`::`、「包名 + 空格」之类自造写法（详见 `ArchiveStore.h` 顶部注释）。

## 依赖清单

- 构建：CMake ≥ 3.16 + Ninja；Qt 6（Widgets / Concurrent / PrintSupport / Network / Svg / LinguistTools）
- `Qt6::Svg`（2026-11-18 起）：功能图标全部由 `QSvgRenderer` 渲染，`find_package(Qt6 ... Svg)` 是硬依赖（构建机必须补上这一模块）。运行期若安装目录缺 `Qt6Svg.dll`，`AppIcon::available()` 变 false，图标整体缺席但界面照常可用、不黑块不崩溃；打包时 `tools/package.sh` 硬校验 `Qt6Svg.dll` 在 staging 里，缺了直接失败。
- 可选解码：libraw、libheif（都走 `pkg_check_modules`，缺失时降级而非编译失败）
- 内嵌第三方（`third_party/`，随源码编译、不额外装 DLL）：`psd_sdk`（BSD-2，PSD/PSB 二进制解析，`PsdLoader` 只做适配）、`nanosvg`（zlib，壳扩展的 SVG 缩略图软渲染）、以及 psd_sdk 自带的 miniz（public domain，只编 `tinfl` 与 `mz_crc32`，供 `ZipArchive` 解压与校验）
- Windows 专有：`winmm`（翻页音效，运行时合成 WAV 走 `PlaySoundW` 异步播放）
- 说明：Honeyview 用到的 libbpg 生态已停更，BPG 支持列为可选、不阻塞主线

## 冒烟入口

`--smoke <cmd[,cmd2]>` / `--screenshot` / `--screenshot-ui` 三个模式都会把配置重定向到临时 ini（环境变量 `QUIKVIEW_CONFIG_PATH`，优先级高于便携模式），**不污染用户 `config.ini`**。新增测试入口必须沿用这条路径。

## 许可证

本项目代码 MIT（见 LICENSE）。依赖库各自保留其许可证（libraw=LGPL/自定、libheif=LGPL、libwebp=BSD、psd_sdk=BSD-2、nanosvg=zlib、miniz=public domain）。壳扩展为独立 COM 实现，不链接 Qt。
