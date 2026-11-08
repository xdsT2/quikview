#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

// 自研最小 PSD 解析：只覆盖 8bit RGB / PSD v1，不引入第三方库。
// 设计边界（派单「避免用近似结果冒充原文件效果」）：
//  - 默认整图用文件末尾 Image Data 段的**原始合成预览**（Photoshop 保存的结果），
//    绝不逐层重合成冒充。
//  - 图层树与单层像素单独解析，用于面板、单层预览。
//  - 用户切换眼睛后的整图用 compositeFromVisible() 做**简化合成**，调用方必须
//    在界面上标注「可见性预览（简化合成）」，不得当作原始合成图呈现。
//  - 调整图层 / 特殊混合 / 蒙版 / 智能对象的重合成能力不在此实现，
//    逐层在 limitation 里写明，由 UI 呈现。

struct PsdLayer
{
    QString name;                 // Pascal 名优先，回退 luni Unicode 名，再回退「图层 N」
    QRect bounds;                 // 相对画布的像素矩形（含偏移，可超出画布）
    bool visible = true;          // 文件保存的可见性（flags bit1）
    int opacity = 255;            // 0..255
    QString blendKey = QStringLiteral("norm");
    QImage image;                 // 单层像素，尺寸 = bounds.size()，带 alpha
    bool previewable = true;      // 是否有独立像素可预览
    QString limitation;           // 不可预览 / 重合成受限的原因
    bool groupStart = false;      // lsct 1/2：分组开始
    bool groupEnd = false;        // lsct 3：分组结束
    int groupDepth = 0;           // 面板用：所属分组层级
};

struct PsdDocument
{
    QImage composite;             // 原始合成预览（Image Data 段）
    QVector<PsdLayer> layers;
    QSize canvasSize;
    QString error;                // 解析失败原因
    QStringList limitations;      // 全文级别的能力限制说明

    bool valid() const { return !composite.isNull(); }
};

namespace PsdLoader {

bool isPsdPath(const QString &path);

// withLayerPixels=false 时只解析图层元信息（缩略图/预读场景省时）。
PsdDocument load(const QString &path, bool withLayerPixels = true);

// 按当前可见性做简化合成（normal blend + opacity + bounds 偏移）。
// 这是**临时可见性预览**，不是原始合成图；调用方必须显式标注。
QImage compositeFromVisible(const PsdDocument &doc, const QVector<bool> &visible);

// 单层预览：把某层按原画布坐标贴到透明画布，保留位置/比例/透明区域。
QImage layerOnCanvas(const PsdDocument &doc, int index);

} // namespace PsdLoader
