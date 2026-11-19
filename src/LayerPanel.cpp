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

#include "AppIcon.h"
#include "HudShell.h"
#include "UiStyle.h"

namespace {

// 自绘眼睛按钮：图形走 AppIcon（Fluent），颜色全部走 UiStyle 令牌，跟随深浅主题。
// 可见/隐藏用 eye、eye_off 两个家族，不再手绘眼睛轮廓（图标派单 2026-11-18）。
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
        // 14×14 逻辑尺寸落在 16px 档资源上，等比缩放，不拉伸。
        // 可见是持续状态，用 filled；隐藏切 eye-off 线性（2026-11-19 定稿状态规则）。
        AppIcon::paint(p, isChecked() ? QStringLiteral("eye") : QStringLiteral("eye-off"),
                       QRectF(-7, -7, 14, 14), UiStyle::muted(),
                       isChecked() ? AppIcon::Style::Filled : AppIcon::Style::Regular);
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
        // 展开/折叠：chevron 家族，收起朝右、展开朝下；展开是持续状态用 filled。
        AppIcon::paint(p, m_collapsed ? QStringLiteral("chevron-right")
                                      : QStringLiteral("chevron-down"),
                       QRectF(-6, -6, 12, 12), UiStyle::muted(),
                       m_collapsed ? AppIcon::Style::Regular : AppIcon::Style::Filled);

        p.translate(24, 0);
        AppIcon::paint(p, QStringLiteral("folder"), QRectF(-7, -7, 14, 14), UiStyle::muted());
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
    setFixedWidth(248);
    setStyleSheet(UiStyle::panelQss());

    // 外壳与信息浮层同源（HudShell）：磨砂底板 + 缓存投影 + 12px 圆角 +
    // 1px 边界。底板由 HudBackdrop 自绘，本体透明，QSS 只管内容行与文字。
    m_backdrop = new HudBackdrop(this);
    m_backdrop->setGeometry(rect());
    m_backdrop->lower();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(UiStyle::hud::PadH, UiStyle::hud::PadTop,
                             UiStyle::hud::PadH, UiStyle::hud::PadBottom);
    root->setSpacing(4);

    // —— 标题栏（30px）：「图层」左，关闭 28×28 热区右（信息浮层同款）——
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("layerPanelHead"));
    header->setFixedHeight(UiStyle::hud::TitleH);
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(2);
    auto *title = new QLabel(tr("图层"), header);
    title->setObjectName(QStringLiteral("panelTitle"));
    QFont titleFont = UiStyle::font(14);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    hl->addWidget(title);
    hl->addStretch(1);
    auto *close = new HudToolButton(HudToolButton::Close, tr("关闭 (Tab)"), header);
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

    // 常驻特效：静息只画缓存投影（信息浮层同款，下 5 / 模糊 18 / 黑 33%）
    m_effect = new HudEffect(this);
    setGraphicsEffect(m_effect);
}

void LayerPanel::setBackdropSource(QWidget *canvasViewport)
{
    if (m_backdrop) {
        m_backdrop->setSource(canvasViewport);
    }
}

void LayerPanel::showHud()
{
    m_closing = false;
    setVisible(true);
    raise();
}

void LayerPanel::dismiss()
{
    if (m_closing || !isVisible()) {
        return;
    }
    m_closing = true;
    hudAnimateDismiss(this, m_effect, [this]() { m_closing = false; });
}

void LayerPanel::hideNow()
{
    m_closing = false;
    hudStopAnimations(this);
    if (m_effect) {
        m_effect->setScaleOn(false);
        m_effect->setProgress(1.0);
    }
    setVisible(false);
}

void LayerPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_backdrop) {
        m_backdrop->invalidate();   // 面板位置/画布内容都可能变了
    }
    hudAnimateShow(this, m_effect);
}

void LayerPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_backdrop) {
        m_backdrop->setGeometry(rect());
        m_backdrop->invalidate();
    }
}

QVector<int> LayerPanel::visibleLayerIndexes() const
{
    // 倒序遍历（psd_sdk 底层在前，面板顶层在上），用深度栈匹配嵌套分组。
    //
    // 关键点：不能只记「最近遇到的 groupStart」。遇到内层组时它会覆盖掉外层组，
    // 父组的折叠状态就丢了；groupEnd 也必须真的出栈，否则栈永远不平。
    // 判定「这一行要不要显示」的唯一依据是：栈里有没有一个已折叠的组。
    QVector<int> out;
    QVector<int> stack;   // 当前所处的组头层索引，栈底是外层
    out.reserve(m_doc.layers.size());
    for (int i = m_doc.layers.size() - 1; i >= 0; --i) {
        const PsdLayer &L = m_doc.layers.at(i);
        // 组尾：弹出对应组头，本身不是可见行
        if (L.groupEnd) {
            if (!stack.isEmpty()) {
                stack.pop_back();
            }
            continue;
        }
        // 祖先链里只要有一个组折叠着，本行就整行不显示（组头也一样，
        // 折叠父组时嵌套子组应该连带消失）
        bool hidden = false;
        for (int g : stack) {
            if (m_collapsed.value(g, false)) {
                hidden = true;
                break;
            }
        }
        if (L.groupStart) {
            if (!hidden) {
                out.append(i);
            }
            // 无论本行显示与否都要入栈：它的子层仍需按它的折叠状态判定
            stack.push_back(i);
            continue;
        }
        if (!hidden) {
            out.append(i);
        }
    }
    return out;
}

int LayerPanel::preferredHeight() const
{
    // 与 rebuild() 共用同一份可见行判定，不再复制一遍折叠逻辑。
    // 固定开销 = PadTop9 + 标题栏30 + 间距4 + 整图32 + 间距4 + PadBottom11 = 90
    //（外壳统一后与信息浮层同款内边距；简化提示行另加 ~44）。
    const int rows = visibleLayerIndexes().size();
    int h = 90 + rows * 34 + (m_simplified ? 44 : 0);
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
    // 可见行由 visibleLayerIndexes() 统一判定：嵌套分组靠深度栈，父组折叠时
    // 内层组及其子层一并隐藏。这里只负责把那些行造出来。
    for (int i : visibleLayerIndexes()) {
        const PsdLayer &L = m_doc.layers.at(i);
        if (L.groupStart) {
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
