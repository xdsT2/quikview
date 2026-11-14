// 菜单偏好：设置程序（Qt 写 config.ini）与壳扩展（本 DLL 读）共用的
// 按用户隔离偏好。存储 = QuikView 的 config.ini（QSettings IniFormat）：
//   [config]
//   ctxmenu\use_shell_menu=true
// QSettings 把键里的 '/' 转义成 '\' 落盘；读取器两种写法都兼容。
// 缺文件 / 缺键一律视为默认（全开 = 0.5.0 已有体验），因此旧配置、
// 便携绿色目录、全新安装都无需迁移。
#pragma once

struct QuikMenuPrefs {
    bool master;    // use_shell_menu：总开关，关 = 右键完全不注入 QuikView 项
    bool browse;    // show_browse：「用 QuikView 浏览」
    bool convert;   // show_convert：「用 QuikView 转换」
    bool preview;   // show_preview：菜单内自绘缩略图
    bool info;      // show_info：浏览项右侧「宽×高 · 大小」
    bool icon;      // show_icon：菜单项软件图标
};

// 每次调用都现读配置（不做缓存）：设置页改动在资源管理器下一次弹菜单即生效，
// 无需重装、无需管理员权限，也无需重启 Explorer。
QuikMenuPrefs QuikLoadMenuPrefs();
