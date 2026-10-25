# QuikView

一个快速、干净的看图器。C++ / Qt6，MIT 开源。

> 想先知道**实现到什么程度、哪些还没做、怎么自测**？看 [STATUS-2026-10-25.md](STATUS-2026-10-25.md)。

本项目**从零重写**，不包含任何第三方专有代码或二进制。其功能范围与架构理解，来自对 Honeyview（Bandisoft 已停更的免费看图器）官方安装包的逆向分析——但这是两个独立的工程：QuikView 是原创实现。

## 功能

**看图核心**
- 缩放 / 平移 / 适配窗口 / 实际大小 / 全屏
- 旋转 90° / 水平垂直翻转 / 交互式裁剪（框选 + 回车确认）/ 按比例调整大小
- 幻灯片（间隔可配）、设壁纸

**浏览**
- 目录序列切换（方向键 / 翻页 / 空格）
- 左侧缩略图浏览器（异步缩略图）
- 动画 GIF / 多页 TIFF 逐帧浏览（Ctrl+←/→）
- 收藏夹 + 最近打开

**格式**（Qt6 插件 + 扩展库）
- 常规：PNG / JPEG / BMP / GIF / WebP / TIFF / ICO 等
- RAW：DNG / NEF / CR2 / ARW / RW2 等（libraw）
- 现代：HEIC / AVIF（libheif）

**元数据**
- EXIF / GPS 面板（自研解析器，无 GPL 依赖），GPS 一键跳转地图
- 直方图

**文件**
- 另存为（PNG/JPEG/BMP/WebP/TIFF）、批量格式转换、打印

**产品化**
- 深色 / 浅色主题、设置界面、多语言（中 / 英 / 韩）、便携模式
- 零遥测、零推广（对照逆向出的"坑清单"反向设计）

**系统集成**（`shell/` 子项目，纯 Win32 COM+WIC，不依赖 Qt）
- 资源管理器缩略图 + 预览窗格

## 构建

前置：CMake ≥ 3.16、Qt 6（Widgets / Concurrent / PrintSupport / LinguistTools）、可选 libraw / libheif（pkg-config）。

主程序：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build
```

壳扩展（仅 Windows）：

```bash
cd shell && cmake -S . -B build -G Ninja && cmake --build build
# 管理员注册：shell/register.bat
```

MSYS2 一键：

```bash
pacman -S --needed mingw-w64-x86_64-{toolchain,cmake,ninja,qt6-base,qt6-imageformats,qt6-tools,libraw,libheif}
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/mingw64
cmake --build build
```

## 目录结构

```
src/          主程序源码（13 个模块）
shell/        壳扩展（缩略图 + 预览窗格）
translations/ 语言文件（.ts）
resources/    图标与界面素材（全部原创绘制，见 docs/ATTRIBUTION.md）
config/       便携配置（config.ini，不入库）
```

## 素材与许可

界面素材（`resources/skin/`）与应用图标（`resources/quikview.ico`）全部为本项目程序化绘制，
不含任何第三方图像素材。每个文件的尺寸、字节数与 SHA-256 记在
`resources/skin/PROVENANCE.json`（`source` 字段区分脚本生成与烘焙两类）；
生成器与设计参数见 [docs/ATTRIBUTION.md](docs/ATTRIBUTION.md)。

第三方库与商标声明见 [NOTICE](NOTICE)。

## 免责声明

- 本项目为独立实现，与 Honeyview / Bandisoft 无任何从属、授权或背书关系。
- 不包含任何 Honeyview 的代码、二进制、图标、皮肤素材或语言文件；
  界面尺寸、帧序与热区布局参照其公开可观察行为自行实现。
- "Honeyview" 及相关商标归其权利人所有。

## 许可证

[MIT](LICENSE)
