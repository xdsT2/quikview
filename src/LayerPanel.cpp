#include "LayerPanel.h"

#include <QAbstractButton>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>

#include "UiStyle.h"

namespace {

// 自绘眼睛按钮：颜色全部走 UiStyle 令牌，跟随深浅主题（不硬编码纯白）。
class EyeButton : public QAbstractButton
{
public:
    explicit EyeButton(QWidget *parent = nullptr) : QAbstractButton(parent)
    {
        setCheckable(true);
        setFixedSize(24, 24);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QColor ink = isChecked() ? UiStyle::text() : UiStyle::muted();
        QPen pen(ink, 1.4);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const QRectF eye(2.5, 7.5, 19.0, 10.0);
        p.drawEllipse(eye.adjusted(0, 1, 0, -1));
        if (isChecked()) {
            p.setBrush(ink);
            p.drawEllipse(QPointF(12, 12), 2.6, 2.6);
        } else {
            p.drawLine(QLineF(3, 21, 21, 3));
        }
    }
};

QLabel *makeThumb(const QImage &image)
{
    auto *lab = new QLabel;
    lab->setFixedSize(44, 32);
    lab->setScaledContents(false);
    lab->setObjectName(QStringLiteral("layerThumb"));
    if (!image.isNull()) {
        // 透明区域用浅/深棋盘垫底，避免透明层和背景同色看不出来
        QImage bg(44, 32, QImage::Format_ARGB32);
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 44; ++x) {
                const bool a = ((x / 6) + (y / 6)) % 2 == 0;
                bg.setPixel(x, y, a ? qRgb(190, 190, 190) : qRgb(230, 230, 230));
            }
        }
        QPainter p(&bg);
        p.drawImage(QRect(0, 0, 44, 32), image);
        p.end();
        lab->setPixmap(QPixmap::fromImage(bg));
    }
    return lab;
}

QWidget *makeRow(QWidget *host, const QString &text, bool selected)
{
    auto *row = new QPushButton(text, host);
    row->setObjectName(QStringLiteral("layerRow"));
    row->setCheckable(true);
    row->setChecked(selected);
    row->setCursor(Qt::PointingHandCursor);
    row->setMinimumHeight(28);
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return row;
}

} // namespace

LayerPanel::LayerPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("layerPanel"));
    setAttribute(Qt::WA_StyledBackground, true);   // 否则 QWidget 不按 QSS 绘制底板
    setFixedWidth(248);
    setStyleSheet(UiStyle::panelQss());

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("layerPanelHead"));
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(10, 8, 6, 8);
    auto *title = new QLabel(tr("图层"), header);
    title->setObjectName(QStringLiteral("panelTitle"));
    hl->addWidget(title);
    hl->addStretch(1);
    auto *close = new QPushButton(QStringLiteral("✕"), header);
    close->setObjectName(QStringLiteral("layerPanelClose"));
    close->setFixedSize(24, 24);
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QPushButton::clicked, this, &LayerPanel::closeRequested);
    hl->addWidget(close);
    root->addWidget(header);

    auto *composite = new QPushButton(tr("整图"), this);
    composite->setObjectName(QStringLiteral("layerComposite"));
    composite->setCheckable(true);
    composite->setMinimumHeight(30);
    connect(composite, &QPushButton::clicked, this, &LayerPanel::compositeSelected);
    root->addWidget(composite);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    m_rows = new QVBoxLayout(content);
    m_rows->setContentsMargins(4, 4, 4, 4);
    m_rows->setSpacing(2);
    m_rows->addStretch(1);
    m_scroll->setWidget(content);
    root->addWidget(m_scroll, 1);

    m_hint = new QLabel(this);
    m_hint->setObjectName(QStringLiteral("layerHint"));
    m_hint->setWordWrap(true);
    m_hint->setContentsMargins(8, 6, 8, 6);
    m_hint->hide();
    root->addWidget(m_hint);
}

int LayerPanel::preferredHeight() const
{
    // 与 rebuild() 的可见性口径一致：折叠组内的子层不占高度。
    int rows = 0;
    int currentGroup = -1;
    for (int i = m_doc.layers.size() - 1; i >= 0; --i) {
        const PsdLayer &L = m_doc.layers.at(i);
        if (L.groupEnd) continue;
        if (L.groupStart) { currentGroup = i; ++rows; continue; }
        if (currentGroup >= 0 && L.groupDepth > 0
            && m_collapsed.value(currentGroup, false))
            continue;
        ++rows;
    }
    int h = 40 + 32 + 8 + rows * 38 + (m_simplified ? 44 : 0) + 12;
    return qBound(180, h, 620);
}

void LayerPanel::toggleGroup(int index)
{
    if (index < 0 || index >= m_doc.layers.size()) return;
    m_collapsed[index] = !m_collapsed.value(index, false);
    rebuild();
}

void LayerPanel::setDocument(const PsdDocument &doc, const QVector<bool> &visible,
                             int selectedIndex, bool simplified)
{
    m_doc = doc;
    m_visible = visible;
    m_selected = selectedIndex;
    m_simplified = simplified;
    rebuild();
}

void LayerPanel::rebuild()
{
    while (m_rows->count() > 1) {
        QLayoutItem *it = m_rows->takeAt(0);
        if (it->widget()) delete it->widget();
        delete it;
    }
    // 复合按钮（第 0 项之外的整图行）选中态
    if (auto *comp = findChild<QPushButton *>(QStringLiteral("layerComposite")))
        comp->setChecked(m_selected < 0);

    if (m_simplified) {
        m_hint->setText(tr("可见性预览为简化合成，不还原混合模式 / 蒙版；原始合成图未改动。"));
        m_hint->show();
    } else {
        m_hint->hide();
    }

    // psd_sdk 层顺序为「底层在前」，面板按文档顺序倒序显示（顶层在上）。
    // groupStart 是组头，groupEnd 只是边界标记不显示。
    int currentGroup = -1;
    for (int i = m_doc.layers.size() - 1; i >= 0; --i) {
        const PsdLayer &L = m_doc.layers.at(i);
        if (L.groupEnd)
            continue;
        if (L.groupStart) {
            currentGroup = i;
            auto *row = new QWidget(this);
            auto *rl = new QHBoxLayout(row);
            rl->setContentsMargins(4 + L.groupDepth * 12, 0, 4, 0);
            rl->setSpacing(6);
            auto *fold = new QPushButton(m_collapsed.value(i, false) ? QStringLiteral("▸")
                                                                     : QStringLiteral("▾"), row);
            fold->setObjectName(QStringLiteral("layerFold"));
            fold->setFixedSize(20, 24);
            fold->setCursor(Qt::PointingHandCursor);
            connect(fold, &QPushButton::clicked, this, [this, i, fold]() {
                toggleGroup(i);
            });
            rl->addWidget(fold);
            auto *name = new QLabel(L.name, row);
            name->setObjectName(QStringLiteral("layerName"));
            rl->addWidget(name, 1);
            row->setToolTip(tr("分组（%1 个子层）").arg(
                std::count_if(m_doc.layers.begin(), m_doc.layers.end(),
                              [](const PsdLayer &x) { return x.groupDepth > 0; })));
            m_rows->insertWidget(m_rows->count() - 1, row);
            continue;
        }
        if (currentGroup >= 0 && L.groupDepth > 0 && m_collapsed.value(currentGroup, false))
            continue;

        auto *row = new QWidget(this);
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(4 + L.groupDepth * 12, 0, 4, 0);
        rl->setSpacing(6);

        auto *eye = new EyeButton(row);
        eye->setChecked(i < m_visible.size() ? m_visible.at(i) : L.visible);
        eye->setAccessibleName(tr("%1，%2").arg(L.name, eye->isChecked() ? tr("可见") : tr("隐藏")));
        eye->setToolTip(eye->isChecked() ? tr("隐藏该图层") : tr("显示该图层"));
        connect(eye, &QAbstractButton::clicked, this, [this, i]() { emit eyeToggled(i); });
        rl->addWidget(eye);

        rl->addWidget(makeThumb(L.image));

        auto *name = makeRow(row, L.name, m_selected == i);
        name->setAccessibleName(tr("%1，%2").arg(L.name,
            (i < m_visible.size() ? m_visible.at(i) : L.visible) ? tr("可见") : tr("隐藏")));
        if (!L.previewable)
            name->setToolTip(L.limitation.isEmpty() ? tr("该图层无法单独预览") : L.limitation);
        connect(static_cast<QPushButton *>(name), &QPushButton::clicked,
                this, [this, i]() { emit layerSelected(i); });
        rl->addWidget(name, 1);
        m_rows->insertWidget(m_rows->count() - 1, row);
    }
    emit contentChanged();   // 折叠/展开后让主窗口按内容重排面板高度
}
