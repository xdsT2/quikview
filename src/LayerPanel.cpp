#include "LayerPanel.h"

#include <QAbstractButton>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

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
        const QRectF button = QRectF(rect()).adjusted(1, 1, -1, -1);
        if (underMouse() || isDown()) {
            p.setPen(Qt::NoPen);
            p.setBrush(isDown() ? UiStyle::accentSoft()
                                : UiStyle::color(UiStyle::setRowHover()));
            p.drawRoundedRect(button, 5, 5);
        }
        if (hasFocus()) {
            p.setPen(QPen(UiStyle::accent(), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(button, 5, 5);
        }

        p.translate(QRectF(rect()).center());
        p.setPen(QPen(UiStyle::muted(), 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        QPainterPath eye;
        if (isChecked()) {
            eye.moveTo(-7, 0);
            eye.cubicTo(-3, -5.5, 3, -5.5, 7, 0);
            eye.cubicTo(3, 5.5, -3, 5.5, -7, 0);
            eye.closeSubpath();
            p.drawPath(eye);
            p.drawEllipse(QPointF(0, 0), 1.7, 1.7);
        } else {
            eye.moveTo(-7, 0);
            eye.cubicTo(-5, -2.7, -3, -4, 0, -4);
            eye.cubicTo(3, -4, 5, -2.7, 7, 0);
            eye.cubicTo(6, 1.4, 4.5, 2.5, 3, 3.2);
            eye.moveTo(-7, 0);
            eye.cubicTo(-5.5, 2, -4, 3, -2, 3.6);
            p.drawPath(eye);
            p.drawLine(QPointF(-5.5, -5.5), QPointF(5.5, 5.5));
        }
    }
};

class GroupButton : public QPushButton
{
public:
    GroupButton(const QString &name, bool collapsed, QWidget *parent)
        : QPushButton(name, parent), m_collapsed(collapsed)
    {
        setObjectName(QStringLiteral("layerFold"));
        setFixedHeight(32);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPushButton::paintEvent(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(UiStyle::muted(), 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.translate(12, height() / 2.0);
        if (m_collapsed)
            p.drawPolyline(QPolygonF{QPointF(-2, -3), QPointF(1, 0), QPointF(-2, 3)});
        else
            p.drawPolyline(QPolygonF{QPointF(-3, -1), QPointF(0, 2), QPointF(3, -1)});

        p.translate(24, 0);
        QPainterPath folder;
        folder.moveTo(-7, 5);
        folder.lineTo(-7, -5);
        folder.lineTo(-2, -5);
        folder.lineTo(0, -3);
        folder.lineTo(7, -3);
        folder.lineTo(7, 5);
        folder.closeSubpath();
        p.drawPath(folder);
    }

private:
    bool m_collapsed;
};

QLabel *makeThumb(const QImage &image)
{
    auto *lab = new QLabel;
    lab->setFixedSize(36, 26);
    lab->setScaledContents(false);
    lab->setObjectName(QStringLiteral("layerThumb"));
    if (!image.isNull()) {
        // 透明区域用浅/深棋盘垫底，避免透明层和背景同色看不出来
        QImage bg(36, 26, QImage::Format_ARGB32);
        for (int y = 0; y < 26; ++y) {
            for (int x = 0; x < 36; ++x) {
                const bool a = ((x / 6) + (y / 6)) % 2 == 0;
                bg.setPixel(x, y, a ? qRgb(190, 190, 190) : qRgb(230, 230, 230));
            }
        }
        QPainter p(&bg);
        p.drawImage(QRect(0, 0, 36, 26), image);
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
    row->setFixedHeight(32);
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
    root->setContentsMargins(8, 6, 8, 8);
    root->setSpacing(4);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("layerPanelHead"));
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(6, 0, 0, 2);
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
    composite->setFixedHeight(32);
    connect(composite, &QPushButton::clicked, this, &LayerPanel::compositeSelected);
    root->addWidget(composite);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    m_rows = new QVBoxLayout(content);
    m_rows->setContentsMargins(0, 2, 0, 2);
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
    int h = 84 + rows * 34 + (m_simplified ? 44 : 0);
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
            const bool collapsed = m_collapsed.value(i, false);
            auto *fold = new GroupButton(L.name, collapsed, row);
            fold->setAccessibleName(tr("%1，%2").arg(
                L.name, collapsed ? tr("已折叠") : tr("已展开")));
            fold->setToolTip(collapsed ? tr("展开分组") : tr("折叠分组"));
            connect(fold, &QPushButton::clicked, this, [this, i]() { toggleGroup(i); });
            rl->addWidget(fold);
            m_rows->insertWidget(m_rows->count() - 1, row);
            continue;
        }
        if (currentGroup >= 0 && L.groupDepth > 0 && m_collapsed.value(currentGroup, false))
            continue;

        auto *row = new QWidget(this);
        row->setObjectName(QStringLiteral("layerItem"));
        row->setAttribute(Qt::WA_StyledBackground);
        row->setProperty("selected", m_selected == i);
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
