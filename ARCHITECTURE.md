# QuikView 架构

> 当前版本 **0.6.1**（2026-11-26 发布，tag `v0.6.1`）。`0.5.1` 从未发布、已跳过，其变更并入 0.5.2。本文按 `src/` 实际代码写，改动后请同步。
> 视觉与交互规范另见 [docs/design/QUIKVIEW-DESIGN.md](docs/design/QUIKVIEW-DESIGN.md)。

## 模块划分

`src/` 共 43 个 `.cpp` + 56 个 `.h`（不含 `src/pano/`），合计 50,971 行（`.cpp` 43,013 + `.h` 7,958），其中 `src/pano/` 的 3+2 个文件属于独立可执行目标（见「子项目」）。按职责分五层（表内行数为模块规模快照，2026-11-27 实测，以文件实际内容为准）。

**入口与外壳**

| 模块 | 行数 | 职责 |
|---|---|---|
| `main.cpp` | 371 | 入口、命令行解析（`--smoke` / `--screenshot` / `--screenshot-ui`）、翻译装载 |
| `MainWindow` | 8658 + 583 | 主窗口：菜单、状态栏、拖放、全屏、快捷键、翻页调度、打印、设壁纸入口；会话 / 窗口几何恢复、目录外部变更刷新也在这里收口 |
| `Chrome` | 2702 + 440 | 界面外壳：顶部条、底部胶囊控制栏、翻页滑块、两侧渐显箭头、信息浮层；图层按钮仅 PSD / PSB 显示 |
| `PillToggle` | 254 + 61 | 胶囊式开关控件 |
| `PanoChip` | 102 + 28 | 主窗「360° 查看」胶囊：高 30 / 半径 15 / 12px DemiBold、`Qt::NoFocus`、悬停提亮；只在**完整球形全景图**上出现，居中贴底岛上沿 10px |

**画布与图像**

| 模块 | 行数 | 职责 |
|---|---|---|
| `ImageCanvas` | 2006 + 293 | 画布：缩放 / 平移 / 适配 / 裁剪框 / 自绘渲染；手动缩放 0.01~64，长图纵向溢出时横向轴锁 |
| `ImageCache` | 648 + 118 | 二级缓存，按字节计成本，空闲预读；显式跳转可抢占旧解码（代次隔离），全图解码走专用线程池（上限 2） |
| `ImageEffects` | 102 + 18 | 像素处理：反色、裁剪、旋转等 |
| `ToneMap` | 166 + 27 | HDR 色调映射 |
| `GammaDialog` | 180 + 33 | Gamma 调节对话框 |
| `OverviewPanel` | 447 + 83 | 底栏「图片总览」：全窗网格、按原始宽高比排布、单击选中 / 双击或 Enter 打开 / `Esc` 返回；打开时暂时接管快捷键 |
| `RotateAutoSave` | 265 + 17 | 旋转后自动写回：格式白名单跳过（RAW / 动图 / 多页 TIFF / PSD），同目录临时文件 → fsync → 替换，JPEG 保留 EXIF 且 Orientation 归 1 |

**解码与数据**

| 模块 | 行数 | 职责 |
|---|---|---|
| `DirectoryLoader` | 587 + 140 | 目录扫描、文件序列与排序；`QFileSystemWatcher` 800ms 防抖重扫，同名重写作废缓存 |
| `RawJpgGroup` | 162 + 53 | RAW+JPG（含 PSD+JPG）分组：目录列表构建层的纯函数，只做字符串与路径判断，可在后台线程调用 |
| `RawImageLoader` | 181 + 19 | RAW 解码（libraw，可选） |
| `HeifLoader` | 189 + 11 | HEIC / AVIF 解码（libheif，可选） |
| `FrameNavigator` | 182 + 51 | 动图 GIF / 多页 TIFF 逐帧 |
| `ExifParser` | 335 + 30 | 自研 EXIF / GPS 解析，无 GPL 依赖；提供打印尺寸用的 DPI |
| `ImageFacts` | 201 + 26 | 只读文件头取像素尺寸与 EXIF 拍摄日期，供图片总览按日期 / 大小分组；读不到即「未知」，不拿文件修改时间冒充 |
| `SvgLoader` | 179 + 36 | SVG 打开：头部嗅探 + `QSvgRenderer` 栅格化，8MB 读入上限、单边 4096px / 800 万像素栅格上限，不依赖 `imageformats/qsvg` 插件 |
| `HdrImageLoader` | 305 + 45 | HDR / EXR 产品侧入口：扩展名识别、流式逐行 + 盒式降采样编排、默认 EV（环境变量 `QUIKVIEW_HDR_EV`，夹在 −8 ~ +8）、内核错误 → `LoadErrorCategory` 映射 |
| `ExrStream` | 423 + 99 | OpenEXR 单帧 scanline 流式解析：头 / 通道 / 行数据逐行读取，ZIP / ZIPS 压缩复用 in-tree miniz 的 `tinfl`；一次只驻留当前行缓冲，不整帧常驻 |
| `HdrRadiance` | 193 + 55 | Radiance RGBE（.hdr / .rgbe）自研解码，逐行返回线性 RGB float |
| `HdrError` | 81 + 79 | HDR / EXR 错误状态与类别（`Ok` / `Corrupt` / `Unsupported` / `Io`），上层映射到 `LoadErrorCategory` |
| `ZipArchive` | 495 + 102 | ZIP / CBZ 只读解析：自研 EOCD / 中央目录 / 局部头，解压复用 in-tree miniz 的 `tinfl` 与 `mz_crc32`；只认 store / deflate，不落盘、不解密、不支持分卷 |
| `ArchiveStore` | 392 + 78 | 压缩包「虚拟目录」唯一入口：包注册表、虚拟路径拼装/拆分、条目解码；**包内条目路径 = 包路径 + `/` + 条目名** |
| `PsdLoader` | 331 + 67 | PSD/PSB 解析：`psd_sdk`（BSD-2，vendor）适配层，只覆盖 8bit RGB / PSD v1；整图用文件内原始合成预览，逐层重合成不在范围内 |
| `LoadError.h` | 16 | 解码失败类别枚举（`Corrupt` / `Unsupported` / `NoCompatPreview`），判定只在 loader 层，UI 只按类别取文案 |
| `PanoMetadata` | 320 + 49 | **全景识别**：只扫 JPEG 段头（SOI→SOS 按段长 seek，不整文件读），取 APP1 里 xap/1.0 包，`QXmlStreamReader` 读 GPano 字段，失败退回已知字段名字节扫描；输出 `Complete` / `Suppressed` / `AspectOnly` / `Unsupported` / `NotFound` 与初始视角（heading / pitch / roll / fov）。纯逻辑，可在后台线程调，主窗用代次令牌丢弃过期结果 |

**界面**

| 模块 | 行数 | 职责 |
|---|---|---|
| `SettingsDialog` | 7480 + 525 | **10 页**（通用 / 浏览 / 外观与显示 / 图像处理 / 键盘 / 鼠标与滚轮 / 查看器右键菜单 / Windows 集成 / 扩展 / 关于）。**首屏只建「通用」，其余全部懒构建**，首次进入或首次搜索时才建；登记处 `SettingsDialog.cpp:1686-1706` |
| `MetadataPanel` | 880 + 143 | 图片信息 HUD：标题栏 + 键值栅格 + 可折叠直方图（`Tab` 呼出）；打印尺寸按 EXIF DPI 计算，无 DPI 时按 96 估算并标 `@96` |
| `HudShell` | 397 + 107 | 画布浮层共用外壳：磨砂底板 `HudBackdrop`、缓存投影 `HudEffect`、28×28 标题栏按钮 `HudToolButton` 与 110/85ms 开合动效；底栏 / 信息面板 / 图层面板共用同一套柔和投影 |
| `LayerPanel` | 428 + 75 | PSD 图层面板：右侧浮层，整图 / 图层预览 / 分组折叠 / 独立眼睛显隐；外壳走 `HudShell`，Esc 优先收起；高度 92 去掉常驻 2px 滚动条，长名省略 + tooltip |
| `ThumbnailBrowser` | 469 + 79 | 底部胶片栏，绘制并异步解码可见项 |
| `CtxMenu` / `MenuCatalog.h` / `MenuIcons` | 697 + 85 / 381 / 29 + 26 | 应用内右键菜单：三层收纳目录、全量可见性配置（`config/appmenu/*`）、顶部五图标快捷区；五个根组全关时不再弹空面板。**图标自 2026-11-18 起不再自绘**：`MenuIcons` 只剩把历史 id 转成 `AppIcon` 语义 id 的转发层 |
| `AppMenuPreview` | 936 + 166 | 「查看器右键菜单」设置页的常驻一级预览 + 浮动二级预览：只呈现当前可见两级菜单与两级内交互，不改任何配置 |
| `AppIcon` | 292 + 44 | **功能图标唯一入口**：语义 id → Fluent 家族映射表（一份，94 条），按目标像素取 16/20/24 档，`QSvgRenderer` 渲染 + 按主题重新着色，`QPixmap` 按 id/px/style/dpr/ink 缓存；qrc 缺失时 `available()==false` 优雅回退成无图标 |
| `BatchConvertDialog` | 836 + 85 | 批量格式转换 |

**基础设施**

| 模块 | 行数 | 职责 |
|---|---|---|
| `Config` | 799 + 187 | QSettings INI、便携判定、快捷键与设置持久化；窗口几何 / 会话文件 / 导出导入与设置十页共用同一份键定义；旧槽位键一次性迁移到命令 ID |
| `CommandCatalog.h` | 438 | 命令注册表唯一来源：稳定 `CMD_*` ID、旧槽位与别名映射、出厂键位与可配置标记，供快捷键 / 设置页 / 冒烟探针 / CLI 共用 |
| `LanguageRuntime.h` | 318 | 语言运行态：记录型 `QTranslator` + 管理器，支持启动装载、运行期切换、整窗重译与测试目录覆盖 |
| `UpdateChecker` | 357 + 69 | 检查更新（默认关，24 小时频控） |
| `Wallpaper` | 25 + 6 | 设为壁纸 |
| `UiStyle.h` | 826 | **UI 令牌唯一来源**：颜色 / 间距 / 字号，79 条 `inline constexpr`（其中颜色 45 条） |
| `Theme.h` | 148 | 深 / 浅主题切换 |
| `FlipProbe.h` / `PerfProbe.h` / `ThemeProbe.h` | 77 / 111 / 77 | 验收计时探针（翻图 / 打开分段 / 主题切换），环境变量开启，未开启时首判即返回，零开销 |
| `PanoExtension` | 482 + 72 | **可选组件管理单例**：状态只认文件事实（`applicationDirPath()/quikview-pano.exe` + `pano-version.txt` 首行版本），兼容性 = major.minor 与主程序版本一致；下载源 `QUIKVIEW_PANO_SOURCE` → `config/ext/pano_source` → 安装包自带；网络下载取同址 `.sha256`（缺摘要即拒装）→ SHA-256 比对 → 解包 → 校验 MZ 头与版本 → 先写 `.new` 再替换；取消回 `NotInstalled` 且无错误，失败保留原状态并给中文原因 |

## 子项目

| 目录 | 说明 |
|---|---|
| `shell-ext/` | 资源管理器壳扩展：缩略图提供程序（`IThumbnailProvider`）+ 右键菜单（`IContextMenu`），纯 Win32 裸 COM，不依赖 Qt。64 位 MinGW 静态构建为主交付物 |
| `src/pano/` | **360° 全景查看器**：独立可执行目标 `quikview-pano.exe`（`PanoGlobe` 每像素反投视线求经纬 + 双线性采样、横向环绕、纵向夹极；`PanoWindow` 拖动 / 滚轮 / 双击复位 / Esc 关闭 + 底部 HUD）。只链 `Qt6::Widgets`，**不引第三方全景库、不用 OpenGL**：软件光栅，交互期半分辨率、停手 120ms 补全分辨率。`main.cpp` 解析 `--image/--heading/--pitch/--fov/--ev/--bench/--bench-out/--bench-shot`，参数逐个传递不拼引号，退出码 2 表示参数或解码失败 |
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
| 360° 全景查看 | 内置球面视图 | **自研独立进程**（软件光栅，Qt Widgets 自绘） | 不引第三方全景库 / OpenGL：可选功能不该给主程序绑图形栈依赖，组件 `quikview-pano.exe` 344.3 KiB（352,559 B，Release 构建、未 strip，内嵌 DWARF 调试段）；代价是 8K 全分辨率约 66ms/帧，交互期走半分辨率 |

## 「单一来源」硬规则

1. **版本号**：只有 `CMakeLists.txt` 的 `project(VERSION ...)`。它生成 `QUIKVIEW_VERSION` 宏，`setApplicationVersion()` 与「关于」对话框都读这个宏，不再各写各的字面量。文档侧（README / ROADMAP / CHANGELOG / 本文）的版本口径以 tag 与 `发布产物/README.md` 的发布状态为准，不自行推断。
2. **UI 令牌**：只有 `src/UiStyle.h`。QSS 里写 `@Token@` 占位符，运行时由 `withColorTokens()` 替换成常量——界面代码不出现裸色值。
3. **翻译**：只有 `translations/*.ts`，运行时编译成 `.qm` 由 `QTranslator` 装载；中文（简体）是源语言，不额外装载。
4. **图标**：只有 `src/AppIcon.cpp` 的映射表。`resources/icons/fluent/` 里的 SVG 由 `tools/install-fluent-icons.py` 依该表复制、`resources/ui.qrc` 的登记段也由它生成——不要在页面里各自拼路径，也不要手工往 qrc 里加图标，更不要把一个家族的多档尺寸全塞进来。
5. **压缩包虚拟路径**：只有 `src/ArchiveStore.cpp`。包内条目的路径一律是 **包文件的绝对路径 + `/` + 包内条目名**（例：`E:/comics/book.cbz/001.jpg`）——这样 `QFileInfo(path).fileName()` 就是条目名，胶片栏 / 标题 / 缩略图的既有代码不必为虚拟路径另写取名逻辑，`lastIndexOf('/')` 之前就是包路径。**禁止**各模块自行拼 `包 + 分隔符 + 条目`，或用 `:`、`::`、「包名 + 空格」之类自造写法（详见 `ArchiveStore.h` 顶部注释）。
6. **组件状态**：只有 `src/PanoExtension.cpp`。装没装只看文件事实（`quikview-pano.exe` + `pano-version.txt` 首行），「装上了」不看配置、不看注册表；版本口径与主程序同一个 `project(VERSION)`，安装向导写版本文件也用它。设置页、主窗入口、右键菜单都向 `Manager::instance().status()` 取状态，不各自拼判断。

## 依赖清单

- 构建：CMake ≥ 3.16 + Ninja；Qt 6（Widgets / Concurrent / PrintSupport / Network / Svg / LinguistTools）
- `Qt6::Svg`（2026-11-18 起）：功能图标全部由 `QSvgRenderer` 渲染，`find_package(Qt6 ... Svg)` 是硬依赖（构建机必须补上这一模块）。运行期若安装目录缺 `Qt6Svg.dll`，`AppIcon::available()` 变 false，图标整体缺席但界面照常可用、不黑块不崩溃；打包时 `tools/package.sh` 硬校验 `Qt6Svg.dll` 在 staging 里，缺了直接失败。
- 可选解码：libraw、libheif（都走 `pkg_check_modules`，缺失时降级而非编译失败）
- 内嵌第三方（`third_party/`，随源码编译、不额外装 DLL）：`psd_sdk`（BSD-2，PSD/PSB 二进制解析，`PsdLoader` 只做适配）、`nanosvg`（zlib，壳扩展的 SVG 缩略图软渲染）、以及 psd_sdk 自带的 miniz（public domain，只编 `tinfl` 与 `mz_crc32`，供 `ZipArchive` 解压与校验；HDR/EXR 轮的 EXR ZIP / ZIPS 解压同样复用这份 miniz，未新增第三方库或 DLL）
- Windows 专有：`winmm`（翻页音效，运行时合成 WAV 走 `PlaySoundW` 异步播放）
- 打包期约定（0.5.6 起）：360° 组件**不进 staging**——`tools/package.sh` 把它放在输出目录的 `_pano-payload/`，`tools/package-installer.sh` 用 `/DEXTSRC` 指给 NSIS 的可选节；主安装节递归 `staging\*` 时目录里没有它，「不勾选就不装」由目录布局保证（早期 `/x "extensions\*.*"` 实测拦不住，已弃用）。
- 说明：Honeyview 用到的 libbpg 生态已停更，BPG 支持列为可选、不阻塞主线

## HDR / EXR 解码与色彩管线

### 解码链

- **Radiance RGBE**（`.hdr` / `.rgbe`）：`HdrRadiance` 自研解码，逐行返回线性 RGB float，无第三方依赖。
- **OpenEXR**（`.exr`）：`ExrStream` 按 scanline 分块流式读取；只支持单帧、单部件、ZIP / ZIPS（scanline deflate）压缩，像素类型仅 HALF / FLOAT（UINT 不支持）。ZIP / ZIPS 解压调用 in-tree miniz 的 `tinfl_decompress_mem_to_mem`，CRC / adler 用 miniz 既有能力；一次只驻留当前 chunk 与当前行，不做整帧 float 缓冲。
- **降采样**：盒式（box）累加降采样器只驻留「一行源行缓冲 + 一份目标累加帧」；超上限先压到显示上限，再向视口 / 缩略图盒收缩，只缩不放。
- **错误分类**：内核状态 `Ok` / `Corrupt` / `Unsupported` / `Io`，经 `HdrImageLoader` 映射到产品 `LoadErrorCategory`（`Corrupt` / `Unsupported`）；截断、读不出归 `Corrupt`，格式 / 压缩 / 部件不在白名单归 `Unsupported`，UI 按类别取文案，不把「打不开」一律说成格式不认识。

### 色彩管线

- 顺序：线性浮点帧 → 曝光增益 `2^EV`（负值压暗、正值提亮）→ 下限截零 → Reinhard 压缩 `v/(1+v)` → sRGB 查找表编码 → RGB888（色彩空间标记 sRGB）。
- HDR 内核在出帧前已压到 SDR；既有 `ToneMap` / ICC 链路只作用在这份 SDR 图像上，不做第二次 HDR 映射，避免双重映射。
- 主窗默认 EV 取环境变量 `QUIKVIEW_HDR_EV`（浮点，夹在 −8 ~ +8，缺省 0）；全景查看器 `--ev` 与键位 `+` / `=`、`-` / `_` 调档（−5 ~ +5），`0` 复位曝光，`R` 复位视角。

### 依赖与许可证

- 本轮**零新增第三方库、零新增 DLL**：EXR ZIP / ZIPS 复用仓库既有 miniz（Unlicense / 公有领域，vendored `third_party/psd_sdk/Psdminiz.cpp`，miniz v1.15, Rich Geldreich；上游 <https://github.com/richgel999/miniz>）；Radiance 为自研解码。未接入 OpenEXR（ASWF, BSD-3）/ tinyexr（BSD-3）/ stb_image：前者带来动态库与传递依赖，tinyexr V1 为整帧解码模型，stb 不支持 EXR。

### 上限与内存口径

- 主窗平面显示纹理上限 8192×4096（`QUIKVIEW_HDR_TEXW` / `QUIKVIEW_HDR_TEXH` 仅内存受限复测开关，不改默认）。本机六样本实测峰值（`PeakWorkingSetSize`，含上屏帧）：4K（4096×2048）约 231 MiB、首帧 0.675–0.842s；8K（8192×4096）约 593–599 MiB、首帧 2.201–2.786s；16K（源 16384×8192，压到 8192×4096 纹理）约 595–606 MiB、首帧 7.653–9.523s（同机 32GB 环境，更低配机器未测）。
- 全景查看器浮点纹理上限 4096×2048（`QUIKVIEW_PANO_TEXW` / `QUIKVIEW_PANO_TEXH` 复测开关），RGB float 纹理约 96 MiB，进程峰值约 145 MiB。
- 超上限先盒式降采样，不在内存里常驻整帧线性浮点；内存口径以实测样本为准。

### 支持范围与不支持清单

- 支持：Radiance RGBE（`.hdr` / `.rgbe`）；OpenEXR 单帧 scanline ZIP / ZIPS（deflate），像素类型 HALF / FLOAT。
- 不支持（本轮）：tiled 分块、多部件（multi-part）、DEEP 数据、UINT 像素类型、PIZ / RLE / PXR24 / B44 / DWA 等非 ZIP / ZIPS 压缩、非 scanline 布局；不支持的类型在错误提示里与文件损坏区分。
- 导出侧：现有另存 / 批量转换仍是 SDR 编码，不输出真 HDR（浮点或 HDR 编码）文件。

## 冒烟入口

`--smoke <cmd[,cmd2]>` / `--screenshot` / `--screenshot-ui` 三个模式都会把配置重定向到临时 ini（环境变量 `QUIKVIEW_CONFIG_PATH`，优先级高于便携模式），**不污染用户 `config.ini`**。新增测试入口必须沿用这条路径。

全景相关入口：`panoinfo:<path>`（同步跑一次识别并追加写 `Temp/pano-probe.txt`）、`panoopen`（走主窗真实入口链路）、`panochip[:out]`（当前图入口可见性与几何）、`extpage`（定位「设置 → 扩展」组件行并写 `Temp/ext-row.txt`）、`extstatus[:out]`、`extinstall` / `extcancel` / `extuninstall`。查看器自身另有 `--bench` / `--bench-out` / `--bench-shot` 三个验收参数。

## 许可证

本项目代码 MIT（见 LICENSE）。依赖库各自保留其许可证（libraw=LGPL/自定、libheif=LGPL、libwebp=BSD、psd_sdk=BSD-2、nanosvg=zlib、miniz=public domain）。壳扩展为独立 COM 实现，不链接 Qt。
