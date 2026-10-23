# QuikView 路线图

> 目标：重建一个「Honeyview 同级」的快速看图器，但站在逆向得到的结构图谱与坑清单之上，从零写、更干净。
> 逆向产物（结构、组件、行为、坑）见本仓库文档与 `docs/`，本文档只负责把它翻译成工程里程碑。

## 功能矩阵（逆向 Honeyview 得到的功能清单 → 本项目的实现与优先级）

| # | 功能（Honeyview 实测） | 逆向依据 | 里程碑 | 本实现 |
|---|---|---|---|---|
| 1 | 打开/缩放/平移/旋转/翻转 | 通用看图 | M0 | QGraphicsView |
| 2 | 全屏 / 幻灯片 / 设壁纸 | 通用看图 | M1 | Qt |
| 3 | 多格式解码（常规+WebP+AVIF+HEIC） | dll\libbpg、libraw、Ark | M1 | Qt 图像插件 + libheif |
| 4 | RAW 解码 | dll\libraw32/64.dll | M1 | libraw |
| 5 | BPG 解码 | dll\libbpg32/64.dll | M1 | libbpg（可选，生态弱） |
| 6 | 异步解码 + 缩略图/原图二级缓存 | 性能卖点 | M1 | QtConcurrent + QCache |
| 7 | EXIF / 元数据面板 | 未深挖，标准能力 | M2 | libexiv2 |
| 8 | GPS → 地图链接 | `maps.google.com/maps?q=%f,%f`、kakao 模板 | M2 | 模板化 URL |
| 9 | 多语言（37 语言 ini） | langs\ 37 个 ini | M3 | Qt Linguist .ts |
| 10 | 便携模式 | config.ini is_portable | M3 | 配置同目录自动判定 |
| 11 | 皮肤 / 主题 | data\skin.data | M3 | Qt 样式表 |
| 12 | 更新检查 | ver.bandi.so（config 驱动） | M3 | **默认关**，opt-in |
| 13 | 右键菜单预览/缩略图 | HVShell32/64.dll + RegDll | M4 | COM 壳扩展（独立工程） |
| 14 | 信息面板 | 内嵌网页控件 | M4 | 可选：Qt WebEngine 或自绘 |

## 刻意「不做」的清单（逆向揭示的坑）

- ❌ 静默推广（coupang.exe 那种桌面快捷方式投放）—— 永不
- ❌ 诱导式勾选 / 纯韩文硬编码弹窗
- ❌ 默认开启遥测（ana.bandi.so 计数器）—— 遥测默认关，要做就显式 opt-in
- ❌ 「无广告」却埋广告点击过滤器 —— 本项目直接无广告逻辑
- ❌ 隐藏的开发路径 / 调试后门（`FindWindowW("#32770","XRecv")`）

## 里程碑

- **M0（骨架，本阶段）**：CMake + Qt6 工程；主窗口 + 打开/缩放/平移 + INI 配置（便携判定、遥测禁死）。
- **M1（解码与性能）**：libraw / libheif / WebP 接入；异步解码；二级缓存；目录浏览（左右键切图）；幻灯片；设壁纸。
- **M2（元数据）**：EXIF/GPS 面板（libexiv2），GPS→地图链接模板化。
- **M3（产品化）**：多语言 .ts、便携模式完整、主题、opt-in 更新检查。
- **M4（系统集成）**：Windows 右键缩略图/预览壳扩展（COM DLL 独立工程）。

## 前置依赖（当前机器缺失，需先装）

- CMake ≥ 3.16
- Qt 6.x（Widgets + Concurrent；M2 起 + ImageFormats 插件；M4 起 + 对应编译器）
- 编译器：MSVC 2019+ 或 MinGW-w64（推荐 MSVC，与 Qt 官方二进制一致）
- M1 起：libraw、libheif、libwebp 的 Windows 开发包（vcpkg 统一管理最省事）
