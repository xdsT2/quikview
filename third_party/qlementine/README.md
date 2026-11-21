Qlementine — 参考的开源控件实现
================================

上游仓库：https://github.com/oclero/qlementine
许可：MIT（版权 Olivier Cléro <oclero@hotmail.com>，全文见同目录 LICENSE.txt）
抓取日期：2026-11-22
抓取分支：dev（上游未提供可直接引用的 release tag，且 GitHub commits API
         在本机不可达，因此按 dev 分支原文核对，未编造版本号）

本项目参考的文件
----------------
lib/src/widgets/Switch.cpp        （开关的轨道/滑块几何与状态划分）
lib/src/style/Theme.hpp           （controlHeightLarge / controlHeightSmall、
                                   borderWidth 等尺寸字段含义，仅作理解用）

使用范围与方式
--------------
仅参考「圆角轨道 + 圆形滑块」的绘制结构与状态划分（常态/悬停/按下/禁用/焦点
各自取色、滑块位置比例驱动、OutCubic 位置动画），在 QuikView 的
src/PillToggle.cpp 中按本项目尺寸与色板重写，未整文件拷贝上游源码，也**没有**
引入 Qlementine 库、没有替换应用 QStyle。

上游几何为 controlHeightLarge × controlHeightSmall = 28×16（滑块 12、内边距 2）。
本项目按派单要求取可见轨道 40×22、滑块 16、内边距 3（22-3×2=16，滑块/轨道高
0.727，接近上游 0.75 的比例），并把焦点环内联进自绘（上游为独立的
RoundedFocusFrame 子控件）。

上游的「滑块内勾/叉辅助符号」未采用：本体滑块 16px，1.5px 描边符号会在设置页
密度下显得脏；开关语义已由位置 + 填色 + 旁边的 QLabel 文案表达。
