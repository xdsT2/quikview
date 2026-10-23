# QuikView 架构

## 模块划分

```
quikview/
├── src/
│   ├── main.cpp            # 入口 + 命令行解析
│   ├── MainWindow          # 主窗口：菜单/状态栏/拖放/全屏/快捷键
│   ├── ImageCanvas         # 图片画布：缩放/平移/适配（M1 换高性能自绘渲染）
│   ├── Config              # INI 配置：便携判定、语言、更新（遥测禁死）
│   └── (M1 起) ImageCache / Decoder / MetadataPanel / ThumbnailModel
├── config/config.ini       # 默认配置（零遥测）
├── resources/              # 图标/皮肤
└── translations/           # M3 起 Qt .ts
```

## 关键选型与取舍

| 决策点 | Honeyview 的做法（逆向） | QuikView 的做法 | 理由 |
|---|---|---|---|
| 解码层 | 自研 Ark + 内嵌 libbpg/libraw | Qt 图像插件 + libraw/libheif | 用成熟开源，不重复造轮子 |
| 遥测 | config 驱动，默认上报 ana.bandi.so | **默认关，opt-in** | 逆向揭示的坑，反着做 |
| 便携 | config.ini 里 is_portable 标志 | 配置与程序同目录即自动判定 | 少一个隐藏开关 |
| 推广 | 打包 coupang.exe 联盟推广器 | **无** | 产品底线 |
| 多语言 | 自研 ini 机制 | Qt Linguist .ts | 标准化，编辑器工具链现成 |
| 配置格式 | 自研 config.ini（[config] 节） | QSettings INI | 与 Honeyview 同思路，跨平台 |

## 依赖清单（M0 起）

- 构建：CMake ≥ 3.16 + Qt 6 Widgets/Concurrent
- 包管理：建议 vcpkg 统一拉 libraw / libheif / libwebp / libexiv2
- 说明：Honeyview 用到的 libbpg 生态已死（上游停更），BPG 支持列为可选、不阻塞主线

## 许可证

本项目代码 MIT（见 LICENSE）。依赖库各自保留其许可证（libraw=LGPL/自定、libheif=LGPL、libwebp=BSD、libexiv2=GPL v2——**注意 libexiv2 是 GPL，若介意可换 Exiv2 的 BSD 选项或自实现 EXIF 读取**）。
