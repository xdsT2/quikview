#include "SettingsDialog.h"

#include "DirectoryLoader.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QShowEvent>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStylePainter>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include <cmath>
#include <shobjidl.h>

#include "Config.h"
#include "MainWindow.h"
#include "Theme.h"
#include "UpdateChecker.h"
#include "UiStyle.h"

#include <QOperatingSystemVersion>

namespace {

// ---------------------------------------------------------------------------
// 尺寸（对齐原型 .settings：宽 940 / 标题 43 / 导航 204 / footer 62 / 内容内边距 25,28）
// 高度见下方 kDlgH 的说明：720 放不下键盘页 14 行，2026-11-04 起为 800。
// ---------------------------------------------------------------------------
constexpr int kDlgW = 940;
// 2026-11-04：720 时键盘页 14 行只能塞下 11 行，右侧逼出滚动条、表格下方空一大片，
// 用户以原型图（940x900、14 行一屏全显、无滚动条）为基准判定「不对」。
// 14 行 = 14*(32+1)=462，加表头 31、提示行约 44、底部 12 与内容区内边距，
// 需要约 800 逻辑高；800 刚好填满，不会像 900 那样又留出 100px 空白。
constexpr int kDlgH = 800;
constexpr int kDlgMinW = 900;
constexpr int kDlgMinH = 660;
constexpr int kTitleBarH = 43;
constexpr int kNavW = 204;
constexpr int kFooterH = 62;
constexpr int kComboH = 30;
constexpr int kInputH = 30;

// ---------------------------------------------------------------------------
// QSettings 便捷读写（统一 config/ 前缀）
// ---------------------------------------------------------------------------
QString sGet(const char *key, const QString &def = QString())
{
    return Config::instance().settings().value(QStringLiteral("config/") + key, def).toString();
}
bool bGet(const char *key, bool def)
{
    return Config::instance().settings().value(QStringLiteral("config/") + key, def).toBool();
}
int iGet(const char *key, int def)
{
    return Config::instance().settings().value(QStringLiteral("config/") + key, def).toInt();
}
void sSet(const char *key, const QString &v)
{
    Config::instance().settings().setValue(QStringLiteral("config/") + key, v);
}
void bSet(const char *key, bool v)
{
    Config::instance().settings().setValue(QStringLiteral("config/") + key, v);
}
void iSet(const char *key, int v)
{
    Config::instance().settings().setValue(QStringLiteral("config/") + key, v);
}

// ---------------------------------------------------------------------------
// 键盘页行区：高度按整行吸附（行 32 + 分隔线 1），自己滚。
// 起因（2026-11-04 09:44 用户截图）：表格直接放页面里时，内容区底边会把最后
// 一行「删除」切成半行，看上去像被裁坏。改成整行数高度后，要么完整显示某一行，
// 要么整行藏起来等滚动，不会再出现半行。
// ---------------------------------------------------------------------------
class KeyRowsArea : public QScrollArea
{
public:
    explicit KeyRowsArea(QWidget *parent = nullptr) : QScrollArea(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setWidgetResizable(true);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        // sizeHint 跟随内容：行区不再吃 stretch（M-05），若仍用 QScrollArea 的默认
        // sizeHint（与内容无关的预置值），表格只按 360 高分配、14 行被逼出滚动条。
        setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    }

    // hint / head 是行区之外、同样占据纵向空间的固定部件。
    void snapTo(QWidget *hint, QWidget *head, int rowStep)
    {
        m_hint = hint;
        m_head = head;
        m_step = rowStep;
        QWidget *w = parentWidget();
        while (w && !qobject_cast<QScrollArea *>(w)) {
            w = w->parentWidget();
        }
        if (auto *outer = qobject_cast<QScrollArea *>(w)) {
            m_outer = outer;
            outer->viewport()->installEventFilter(this);
        }
        apply();
        // showEvent 时布局还没激活，viewport 高度常是旧值；等一轮事件循环再算一次。
        QTimer::singleShot(0, this, [this]() { apply(); });
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (m_outer && o == m_outer->viewport() && e->type() == QEvent::Resize) {
            apply();
        }
        return QScrollArea::eventFilter(o, e);
    }

    void showEvent(QShowEvent *e) override
    {
        QScrollArea::showEvent(e);
        apply();
    }

private:
    void apply()
    {
        if (!m_hint || !m_head || !m_outer) {
            return;
        }
        // 内容自然高 = 14 行 + 13 条分隔线 + 底部 12px 安全留白（在 rowsBox margin 里）
        const int natural = widget() ? qMax(1, widget()->sizeHint().height())
                                     : m_step * 14;
        // 可用高 = 外层可视高 − 提示行 − 表头 − 表格上下 1px 边框。
        // 页面 layout 在这三段之外已无其它纵向占用（margins/spacing 均 0，
        // 表格外 spacing 已随 M-05 删除），不再重复扣减。
        constexpr int kTableBorder = 2;
        const int avail = m_outer->viewport()->height() - m_hint->height()
                          - m_head->height() - kTableBorder;
        if (avail >= natural) {
            // 窗口够高：行区按内容自然高展开（14 行 + 12px 留白），无滚动条。
            // 不设 QWIDGETSIZE_MAX 让 maxHeight 生效也行，但显式放开便于 resize 收回。
            if (maximumHeight() != QWIDGETSIZE_MAX) {
                setMaximumHeight(QWIDGETSIZE_MAX);
                updateGeometry();
            }
        } else {
            // 空间不足：视口高取「k 整行」——k 行 + (k−1) 条线 = k*33−1。
            // 底部 12px 留白由内容 margin 保证，滚动到底自然保留（不在视口高度里重复加，
            // 否则会顶出半行）。k 下限 1，避免极矮窗口算出 0。
            const int k = qBound(1, avail / m_step, 14);
            const int h = k * m_step - 1;
            if (maximumHeight() != h) {
                setMaximumHeight(h);
                updateGeometry();
            }
        }
    }

    QWidget *m_hint = nullptr;
    QWidget *m_head = nullptr;
    QScrollArea *m_outer = nullptr;
    int m_step = 33;
};

// ---------------------------------------------------------------------------
// 蓝色链接文字：匿名命名空间内不能用 Q_OBJECT，点击回调走 std::function。
// ---------------------------------------------------------------------------
class LinkLabel : public QWidget
{
public:
    LinkLabel(const QString &text, std::function<void()> onClick, QWidget *parent = nullptr)
        : QWidget(parent), m_text(text), m_onClick(std::move(onClick))
    {
        setFont(UiStyle::font(12));
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(22);
        setMouseTracking(true);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setFont(font());
        p.setPen(QColor(UiStyle::Accent));
        p.drawText(rect(), Qt::AlignVCenter | Qt::AlignLeft, m_text);
        if (underMouse()) {
            const int w = fontMetrics().horizontalAdvance(m_text);
            p.drawLine(0, height() - 5, w, height() - 5);
        }
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && m_onClick)
            m_onClick();
    }

private:
    QString m_text;
    std::function<void()> m_onClick;
};

// ---------------------------------------------------------------------------
// 导航图标：不依赖 Qt6::Svg（本机无 qsvg 插件），用 QPainter 画 16px 线性几何图形，
// 颜色跟随按钮状态（选中=强调青 / 悬停=白 / 常态 NavText），与文字间距 9px。
// ---------------------------------------------------------------------------
enum class NavGlyph { Gear, Eye, Image, Doc, Keyboard, Mouse, Bookmark, Menu, More, Globe, Info };

void paintNavGlyph(QPainter &p, NavGlyph glyph, const QRect &box, const QColor &color)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const qreal x = box.x() + 0.8;
    const qreal y = box.y() + 0.8;
    const qreal w = box.width() - 1.6;
    const qreal h = box.height() - 1.6;
    const QPointF c = QRectF(box).center();

    switch (glyph) {
    case NavGlyph::Gear: {
        p.drawEllipse(c, 4.4, 4.4);
        p.drawEllipse(c, 1.5, 1.5);
        constexpr qreal kPi = 3.14159265358979323846;
        for (int i = 0; i < 8; ++i) {
            const qreal a = i * kPi / 4.0;
            const QPointF d(std::cos(a), std::sin(a));
            p.drawLine(c + d * 4.6, c + d * 6.8);
        }
        break;
    }
    case NavGlyph::Eye: {
        QPainterPath path;
        path.moveTo(x + 0.6, y + h / 2);
        path.quadTo(x + w / 2, y - 1.2, x + w - 0.6, y + h / 2);
        path.quadTo(x + w / 2, y + h + 1.2, x + 0.6, y + h / 2);
        p.drawPath(path);
        p.setBrush(color);
        p.drawEllipse(c, 1.7, 1.7);
        break;
    }
    case NavGlyph::Image: {
        p.drawRoundedRect(QRectF(x + 0.4, y + 1.6, w - 0.8, h - 3.2), 2.2, 2.2);
        p.drawEllipse(QPointF(x + 4.4, y + 5.4), 1.0, 1.0);
        QPainterPath m;
        m.moveTo(x + 1.6, y + h - 2.4);
        m.lineTo(x + 5.8, y + 7.4);
        m.lineTo(x + 8.6, y + 10.1);
        m.lineTo(x + 10.8, y + 8.6);
        m.lineTo(x + w - 1.6, y + h - 2.4);
        p.drawPath(m);
        break;
    }
    case NavGlyph::Doc: {
        QPainterPath d;
        d.moveTo(x + 2.8, y + 0.8);
        d.lineTo(x + 9.0, y + 0.8);
        d.lineTo(x + 12.8, y + 4.6);
        d.lineTo(x + 12.8, y + h - 0.8);
        d.lineTo(x + 2.8, y + h - 0.8);
        d.closeSubpath();
        p.drawPath(d);
        p.drawLine(QPointF(x + 9.0, y + 0.8), QPointF(x + 9.0, y + 4.6));
        p.drawLine(QPointF(x + 9.0, y + 4.6), QPointF(x + 12.8, y + 4.6));
        p.drawLine(QPointF(x + 5.0, y + 8.0), QPointF(x + 10.6, y + 8.0));
        p.drawLine(QPointF(x + 5.0, y + 11.0), QPointF(x + 10.6, y + 11.0));
        break;
    }
    case NavGlyph::Keyboard: {
        p.drawRoundedRect(QRectF(x + 0.6, y + 4.0, w - 1.2, h - 8.0), 1.8, 1.8);
        for (int i = 0; i < 4; ++i)
            p.drawPoint(QPointF(x + 4.0 + i * 2.6, y + 7.4));
        p.drawLine(QPointF(x + 4.6, y + 10.6), QPointF(x + 11.0, y + 10.6));
        break;
    }
    case NavGlyph::Mouse: {
        p.drawRoundedRect(QRectF(c.x() - 3.8, y + 0.6, 7.6, h - 1.2), 3.8, 3.8);
        p.drawLine(QPointF(c.x(), y + 0.6), QPointF(c.x(), y + 6.4));
        break;
    }
    case NavGlyph::Bookmark: {
        QPainterPath b;
        b.moveTo(x + 3.4, y + 0.8);
        b.lineTo(x + 12.6, y + 0.8);
        b.lineTo(x + 12.6, y + h - 0.6);
        b.lineTo(x + 8.0, y + h - 4.2);
        b.lineTo(x + 3.4, y + h - 0.6);
        b.closeSubpath();
        p.drawPath(b);
        break;
    }
    case NavGlyph::Menu: {
        p.drawLine(QPointF(x + 1.4, y + 4.0), QPointF(x + w - 1.4, y + 4.0));
        p.drawLine(QPointF(x + 1.4, y + h / 2), QPointF(x + w - 1.4, y + h / 2));
        p.drawLine(QPointF(x + 1.4, y + h - 4.0), QPointF(x + w - 1.4, y + h - 4.0));
        break;
    }
    case NavGlyph::More: {
        p.setBrush(color);
        for (int i = 0; i < 3; ++i)
            p.drawEllipse(QPointF(x + 3.0 + i * 4.8, c.y()), 1.2, 1.2);
        break;
    }
    case NavGlyph::Globe: {
        p.drawEllipse(QRectF(x + 0.4, y + 0.4, w - 0.8, h - 0.8));
        p.drawLine(QPointF(x + 0.6, c.y()), QPointF(x + w - 0.6, c.y()));
        p.drawEllipse(QRectF(x + 3.6, y + 0.4, w - 7.2, h - 0.8));
        break;
    }
    case NavGlyph::Info: {
        p.drawEllipse(QRectF(x + 0.4, y + 0.4, w - 0.8, h - 0.8));
        p.drawLine(QPointF(c.x(), y + 7.4), QPointF(c.x(), y + 11.6));
        p.setBrush(color);
        p.drawEllipse(QPointF(c.x(), y + 5.0), 0.9, 0.9);
        break;
    }
    }
    p.restore();
}

// navItem：QSS 负责背景/边框，图标与文字自绘（图标 16px + 间距 9px + 左内边距 11px）
class NavItemButton : public QPushButton
{
public:
    NavItemButton(const QString &text, NavGlyph glyph, QWidget *parent = nullptr,
                  bool muted = false)
        : QPushButton(text, parent), m_glyph(glyph), m_muted(muted)
    {
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QStyleOptionButton opt;
        initStyleOption(&opt);
        const QString label = opt.text;
        opt.text.clear();

        QStylePainter p(this);
        p.drawControl(QStyle::CE_PushButton, opt);

        QColor color = m_muted ? UiStyle::muted() : UiStyle::color(UiStyle::NavText);
        if (isChecked())
            color = UiStyle::accent();
        else if (underMouse())
            color = UiStyle::color(UiStyle::PureWhite);

        const int pad = 11;
        const int glyphSize = 16;
        const int gap = 9;
        paintNavGlyph(p, m_glyph, QRect(pad, (height() - glyphSize) / 2, glyphSize, glyphSize),
                      color);

        p.setPen(color);
        p.setFont(font());
        p.drawText(QRect(pad + glyphSize + gap, 0, width() - pad - glyphSize - gap - 8, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, label);
    }

    void enterEvent(QEnterEvent *e) override
    {
        QPushButton::enterEvent(e);
        update();
    }
    void leaveEvent(QEvent *e) override
    {
        QPushButton::leaveEvent(e);
        update();
    }

private:
    NavGlyph m_glyph;
    bool m_muted = false;
};

// 空热键占位修正：QKeySequenceEdit 内部的 QLineEdit 在构造后才挂上，
// 构造期 findChild 拿不到；在 Polish/Show 时再把占位文案换成中文「未设置」（Muted 色）。
class KeySeqPlaceholderFixer : public QObject
{
public:
    explicit KeySeqPlaceholderFixer(QKeySequenceEdit *edit) : QObject(edit)
    {
        edit->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject *, QEvent *ev) override
    {
        if (ev->type() == QEvent::Polish || ev->type() == QEvent::Show) {
            if (auto *le = static_cast<QKeySequenceEdit *>(parent())->findChild<QLineEdit *>()) {
                le->setPlaceholderText(QCoreApplication::translate("SettingsDialog", "未设置"));
                QPalette pal = le->palette();
                pal.setBrush(QPalette::PlaceholderText, UiStyle::muted());
                le->setPalette(pal);
            }
        }
        return false;
    }
};

// 10 个页签的图标（顺序与 addPage 调用顺序一致）
NavGlyph navGlyphFor(int index)
{
    static const NavGlyph glyphs[] = { NavGlyph::Gear,  NavGlyph::Eye,    NavGlyph::Image,
                                       NavGlyph::Doc,   NavGlyph::Keyboard, NavGlyph::Mouse,
                                       NavGlyph::Bookmark, NavGlyph::Menu, NavGlyph::More,
                                       NavGlyph::Globe };
    return (index >= 0 && index < 10) ? glyphs[index] : NavGlyph::Menu;
}

// ---------------------------------------------------------------------------
// 动作选项总表（键盘页 / 鼠标页共用）。value 存 config key。
// ---------------------------------------------------------------------------
QList<QPair<QString, QString>> actionOptions()
{
    return {
        { QStringLiteral("none"),          QObject::tr("无操作") },
        { QStringLiteral("next"),          QObject::tr("翻下一张") },
        { QStringLiteral("prev"),          QObject::tr("翻上一张") },
        { QStringLiteral("next_frame"),    QObject::tr("下一帧") },
        { QStringLiteral("prev_frame"),    QObject::tr("上一帧") },
        { QStringLiteral("zoom_in"),       QObject::tr("放大") },
        { QStringLiteral("zoom_out"),      QObject::tr("缩小") },
        { QStringLiteral("pan"),           QObject::tr("拖拽平移") },
        { QStringLiteral("actual_size"),   QObject::tr("实际大小") },
        { QStringLiteral("fit"),           QObject::tr("适配窗口") },
        { QStringLiteral("slideshow"),     QObject::tr("幻灯片") },
        { QStringLiteral("fullscreen"),    QObject::tr("全屏切换") },
        { QStringLiteral("close"),         QObject::tr("关闭窗口") },
        { QStringLiteral("context_menu"),  QObject::tr("上下文菜单") },
        { QStringLiteral("open"),          QObject::tr("打开") },
        { QStringLiteral("openfolder"),    QObject::tr("打开文件夹") },
        { QStringLiteral("crop"),          QObject::tr("裁剪") },
        { QStringLiteral("favorite"),      QObject::tr("收藏") },
        { QStringLiteral("del"),           QObject::tr("删除") },
    };
}

// 到达首尾行为选项
QList<QPair<QString, QString>> lastFileOptions()
{
    return {
        { QStringLiteral("first"), QObject::tr("转到首个文件") },
        { QStringLiteral("stop"),  QObject::tr("停止") },
        { QStringLiteral("ask"),   QObject::tr("询问") },
    };
}

// 语言下拉：跟随系统 + 9 语言
void populateLanguageCombo(QComboBox *combo)
{
    combo->addItem(QObject::tr("跟随系统"), QStringLiteral("system"));
    combo->addItem(QStringLiteral("中文(简体)"), QStringLiteral("zh_CN"));
    combo->addItem(QStringLiteral("中文(繁體)"), QStringLiteral("zh_TW"));
    combo->addItem(QStringLiteral("English"), QStringLiteral("en_US"));
    combo->addItem(QStringLiteral("한국어"), QStringLiteral("ko_KR"));
    combo->addItem(QStringLiteral("日本語"), QStringLiteral("ja_JP"));
    combo->addItem(QStringLiteral("Français"), QStringLiteral("fr_FR"));
    combo->addItem(QStringLiteral("Deutsch"), QStringLiteral("de_DE"));
    combo->addItem(QStringLiteral("Español"), QStringLiteral("es_ES"));
    combo->addItem(QStringLiteral("Русский"), QStringLiteral("ru_RU"));
}

// 把 QSS 里的 @Token@ 占位符换成 UiStyle 常量：界面代码里不再出现裸色值。
QString withColorTokens(QString qss)
{
    // 令牌值按当前主题出深/浅两套（UiStyle::pick）；浅色值 2026-11-04 首版，
    // 以 Win11 浅色控件为基准，后续按实截图微调。
    static const struct {
        const char *key;
        const char *(*fn)();
    } tokens[] = {
        { "@DialogBg@",        []{ return UiStyle::pick("#232427", "#f2f3f5"); } },
        { "@GroupBg@",         []{ return UiStyle::pick("#222427", "#ffffff"); } },
        { "@Line@",            []{ return UiStyle::pick("rgba(255,255,255,0.085)", "rgba(0,0,0,0.085)"); } },
        { "@HeadBg@",          []{ return UiStyle::pick("#292b2f", "#e8e9eb"); } },
        { "@HeadText@",        []{ return UiStyle::pick("#9ca2aa", "#5f6368"); } },
        { "@Body@",            []{ return UiStyle::pick("#d5d7da", "#202124"); } },
        { "@Field@",           []{ return UiStyle::pick("#2d2f34", "#ffffff"); } },
        { "@FieldText@",       []{ return UiStyle::pick("#e0e2e4", "#1b1b1b"); } },
        { "@Border@",          []{ return UiStyle::pick("#787878", "#8a8a8a"); } },
        { "@BorderHover@",     []{ return UiStyle::pick("#686d75", "#6b6b6b"); } },
        { "@Accent@",          []{ return UiStyle::pick("#55c0f0", "#1689bd"); } },
        { "@DisabledTextSoft@",[]{ return UiStyle::pick("#6b7078", "#a0a0a0"); } },
        { "@DisabledBorder@",  []{ return UiStyle::pick("#3a3c40", "#d0d0d0"); } },
        { "@DisabledBg@",      []{ return UiStyle::pick("#26282b", "#f0f0f0"); } },
        { "@HintText@",        []{ return UiStyle::pick("#9aa0a6", "#6b7075"); } },
        { "@DisabledText@",    []{ return UiStyle::pick("#6f747a", "#a0a0a0"); } },
        { "@PureWhite@",       []{ return UiStyle::pick("#ffffff", "#101113"); } },
        { "@DangerBg@",        []{ return UiStyle::pick("rgba(196,43,28,0.20)", "rgba(196,43,28,0.12)"); } },
    };
    for (const auto &t : tokens)
        qss.replace(QLatin1String(t.key), QLatin1String(t.fn()));
    return qss;
}

// 追加的几何/局部样式：只用 UiStyle 令牌，不引入新色板。
QString extraQss()
{
    return withColorTokens(QStringLiteral(R"(
QDialog { background: transparent; }
QFrame#dlgTitleBar { border-top-left-radius: 10px; border-top-right-radius: 10px; }
QFrame#dlgFooter { border-bottom-left-radius: 10px; border-bottom-right-radius: 10px; }

QFrame#formatGroup { background: @GroupBg@; border: 1px solid @Line@;
    border-radius: 7px; }
QFrame#keyTable { border: 1px solid @Line@; border-radius: 7px; }
QWidget#keyHead { background: @HeadBg@; }
QLabel#keyHeadCell { color: @HeadText@; font-size: 11px; }
QLabel#pageHint { color: @HintText@; font-size: 11px; }

QPushButton#miniBtn { min-height: 18px; max-height: 22px; padding: 0 9px; font-size: 10px;
    border-radius: 5px; }
/* 颜色按钮（ColorPickButton）已随第 18 期「背景段下线」删除，QSS 一并清掉 */
/* 键盘页「操作」列：只读动作名（原 flatCombo 可改绑，2026-11-04 锁死为标签） */
QLabel#keyAction { color: @Body@; font-size: 12px; }

QLabel#valueText { color: @Body@; font-size: 11px; }

/* 键盘页热键编辑器（批次 D1）：QKeySequenceEdit 自绘边框，与 QLineEdit 同一
   底座/描边/焦点色，避免「能用但长得不像这套 UI」的原生外观。 */
QKeySequenceEdit { background: @Field@; color: @FieldText@; border: 1px solid @Border@;
    border-radius: 5px; font-size: 11px; padding: 0 6px; min-height: 24px; }
QKeySequenceEdit:hover { border-color: @BorderHover@; }
QKeySequenceEdit:focus { border-color: @Accent@; }
QKeySequenceEdit:disabled { color: @DisabledTextSoft@; border-color: @DisabledBorder@;
    background: @DisabledBg@; }

/* 键盘页冲突行（保存被拒时整行高亮，动态属性 conflict） */
QWidget[conflict="true"] { background: @DangerBg@; border-radius: 4px; }

/* 置灰（尚未实现）项的统一不可用态：文字压暗、底座与边框退到背景层，
   与可交互项一眼可分。UiStyle::dialogQss() 只给了 indicator:disabled，
   没有文字色，直接 setEnabled(false) 时文字仍与正常项同亮度、看不出差别，
   所以这里补一套。设置页底色由 paintEvent 固定为 DialogBg，DisabledText
   对比度约 3.3:1（满足禁用态 ≥3:1），可读且明确「不可点」。 */
QCheckBox:disabled { color: @DisabledText@; }
QLabel:disabled { color: @DisabledText@; }
QComboBox:disabled { color: @DisabledText@; background: @DisabledBg@; border-color: @DisabledBorder@; }
QSpinBox:disabled { color: @DisabledText@; background: @DisabledBg@; border-color: @DisabledBorder@; }
QLineEdit:disabled { color: @DisabledText@; background: @DisabledBg@; border-color: @DisabledBorder@; }
QPushButton:disabled { color: @DisabledText@; border-color: @DisabledBorder@; background: @HeadBg@; }
)"));
}

} // namespace

// ===========================================================================
// 构造：深色三栏骨架 + 逐页构建
// ===========================================================================

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    takeSnapshot();

    setWindowTitle(tr("设置"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setModal(true);
    resize(kDlgW, kDlgH);
    setMinimumSize(kDlgMinW, kDlgMinH);
    setFont(UiStyle::font(12));
    setStyleSheet(UiStyle::dialogQss() + extraQss());

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);

    // —— 标题栏：⚙ 设置 … × ——
    auto *titleBar = new QFrame(this);
    titleBar->setObjectName(QStringLiteral("dlgTitleBar"));
    titleBar->setFixedHeight(kTitleBarH);
    auto *tl = new QHBoxLayout(titleBar);
    tl->setContentsMargins(18, 0, 14, 0);
    tl->setSpacing(9);
    auto *title = new QLabel(tr("⚙  设置"), titleBar);
    title->setObjectName(QStringLiteral("dlgTitle"));
    tl->addWidget(title);
    tl->addStretch(1);
    auto *btnClose = new QPushButton(QStringLiteral("\u2715"), titleBar);
    btnClose->setObjectName(QStringLiteral("closeBtn"));
    btnClose->setCursor(Qt::PointingHandCursor);
    btnClose->setFixedSize(32, 30);
    tl->addWidget(btnClose);
    connect(btnClose, &QPushButton::clicked, this, [this]() { reject(); });
    root->addWidget(titleBar);

    // —— 主体：导航 | 竖分隔 | 内容 ——
    auto *body = new QWidget(this);
    auto *bl = new QHBoxLayout(body);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(0);

    auto *nav = new QFrame(body);
    nav->setObjectName(QStringLiteral("navRail"));
    nav->setFixedWidth(kNavW);
    m_navLayout = new QVBoxLayout(nav);
    m_navLayout->setContentsMargins(10, 16, 10, 16);
    m_navLayout->setSpacing(2);
    auto *caption = new QLabel(tr("偏好设置"), nav);
    caption->setObjectName(QStringLiteral("navCaption"));
    m_navLayout->addWidget(caption);
    m_navLayout->addSpacing(10);
    bl->addWidget(nav);

    auto *rail = new QFrame(body);
    rail->setObjectName(QStringLiteral("navDivider"));
    rail->setFixedWidth(1);
    bl->addWidget(rail);

    auto *content = new QFrame(body);
    content->setObjectName(QStringLiteral("contentPane"));
    auto *cl = new QVBoxLayout(content);
    cl->setContentsMargins(28, 25, 28, 20);
    cl->setSpacing(0);
    m_pageTitle = new QLabel(tr("浏览"), content);
    m_pageTitle->setObjectName(QStringLiteral("pageTitle"));
    m_pageDesc = new QLabel(content);
    m_pageDesc->setObjectName(QStringLiteral("pageDesc"));
    m_pageDesc->setWordWrap(true);
    auto *headBox = new QWidget(content);
    auto *hb = new QVBoxLayout(headBox);
    hb->setContentsMargins(0, 0, 0, 0);
    hb->setSpacing(6);
    hb->addWidget(m_pageTitle);
    hb->addWidget(m_pageDesc);
    cl->addWidget(headBox);
    cl->addSpacing(16);

    m_stack = new QStackedWidget(content);
    cl->addWidget(m_stack, 1);
    bl->addWidget(content, 1);

    root->addWidget(body, 1);

    // —— 页面（每页一个 QScrollArea，内容 widget 存 m_pages）——
    buildGeneralPage(addPage(tr("一般"), tr("窗口行为、检查更新与到达首尾时的操作。")));
    buildViewPage(addPage(tr("浏览"), tr("缩放状态、侧边箭头与悬浮提示等浏览显示设置；画布底色已固定。")));
    buildAppearancePage(addPage(tr("外观"), tr("主题与材质。跟随系统时由 Windows 的深浅色设置决定。")));
    buildImageProcPage(addPage(tr("图像处理"), tr("透明背景、插值算法、翻页效果与 ICC 色彩管理；监视器 ICC 受 Qt6 限制暂不可用。")));
    buildAssocPage(addPage(tr("格式关联"), tr("通过系统入口设置默认打开方式（Windows 10+ 不允许程序静默改默认）。")));
    buildKeyboardPage(addPage(tr("键盘"), tr("14 个动作可自定义热键与启用状态；「操作」列固定不可改，保存后立即生效。")));
    buildMousePage(addPage(tr("鼠标"), tr("左/右/X/Y 与滚轮的按键映射可自定义；中键平移、双击最大化可用。")));
    buildBookmarkPage(addPage(tr("书签"), tr("自动保存开关与书签数量上限。")));
    buildContextMenuPage(addPage(tr("上下文菜单"), tr("右键菜单与资源管理器缩略图随安装包注册、卸载时移除；本页仅作状态说明。")));
    buildEtcPage(addPage(tr("杂项"), tr("图片文件夹、外部编辑器与 RAW 打开策略。")));
    buildLangPage(addPage(tr("语言"), tr("选择界面语言并管理语言文件。")));

    // 页面内容不足一屏时，多余空间落在末尾弹簧上（否则各 section 会被均匀拉开）。
    // 键盘页例外：它要让表格吃掉剩余高度才能一屏放下 14 行，末尾再加弹簧
    // 会把新高度全变成表格下方的大片空白（2026-11-04 实测）。
    for (QWidget *p : m_pages) {
        if (auto *pv = qobject_cast<QVBoxLayout *>(p->layout())) {
            if (!p->property("noTailStretch").toBool())
                pv->addStretch(1);
        }
    }

    // 导航底部：分隔 + 关于
    m_navLayout->addStretch(1);
    auto *navDivider = new QFrame(nav);
    navDivider->setObjectName(QStringLiteral("navDivider"));
    navDivider->setFixedHeight(1);
    m_navLayout->addSpacing(9);
    m_navLayout->addWidget(navDivider);
    m_navLayout->addSpacing(9);
    auto *btnAbout = new NavItemButton(tr("关于 QuikView"), NavGlyph::Info, nav, true);
    btnAbout->setObjectName(QStringLiteral("navItem"));
    btnAbout->setCursor(Qt::PointingHandCursor);
    btnAbout->setFixedHeight(36);
    m_navLayout->addWidget(btnAbout);
    connect(btnAbout, &QPushButton::clicked, this, [this]() {
        QMessageBox::about(this, tr("关于 QuikView"),
                           tr("<b>QuikView</b> %1<br>轻量图片浏览器。")
                               .arg(QCoreApplication::applicationVersion()));
    });

    // —— footer：提示 … 恢复默认 / 取消 / 保存设置 ——
    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("dlgFooter"));
    footer->setFixedHeight(kFooterH);
    auto *fl = new QHBoxLayout(footer);
    // 右缘与内容区右缘对齐（内容 pane 右内边距 28px；原 18px 会让保存按钮外凸 9px）。
    fl->setContentsMargins(18, 0, 28, 0);
    fl->setSpacing(9);
    auto *note = new QLabel(tr("更改会在保存后生效"), footer);
    note->setObjectName(QStringLiteral("footerNote"));
    fl->addWidget(note);
    fl->addStretch(1);

    m_btnReset = new QPushButton(tr("恢复默认"), footer);
    m_btnReset->setObjectName(QStringLiteral("btnSubtle"));
    m_btnReset->setCursor(Qt::PointingHandCursor);
    m_btnReset->setFixedHeight(32);
    connect(m_btnReset, &QPushButton::clicked, this, &SettingsDialog::onReset);
    fl->addWidget(m_btnReset);

    auto *btnCancel = new QPushButton(tr("取消"), footer);
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setFixedHeight(32);
    connect(btnCancel, &QPushButton::clicked, this, [this]() { reject(); });
    fl->addWidget(btnCancel);

    m_btnOk = new QPushButton(tr("保存设置"), footer);
    m_btnOk->setObjectName(QStringLiteral("btnPrimary"));
    m_btnOk->setCursor(Qt::PointingHandCursor);
    m_btnOk->setFixedHeight(32);
    m_btnOk->setMinimumWidth(86);
    m_btnOk->setDefault(true);
    connect(m_btnOk, &QPushButton::clicked, this, [this]() {
        onAccepted();
        accept();
    });
    fl->addWidget(m_btnOk);

    root->addWidget(footer);

    loadFromConfig();
    selectPage(0);
}

// ===========================================================================
// 页面骨架
// ===========================================================================

QWidget *SettingsDialog::addPage(const QString &title, const QString &desc)
{
    const int index = m_pages.size();

    auto *scroll = new QScrollArea(m_stack);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    scroll->setWidget(page);
    m_stack->addWidget(scroll);

    m_pages.append(page);
    m_pageTitles.append(title);
    m_pageDescs.append(desc);

    auto *btn = new NavItemButton(title, navGlyphFor(index), m_navLayout->parentWidget());
    btn->setObjectName(QStringLiteral("navItem"));
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(36);
    connect(btn, &QPushButton::clicked, this, [this, index]() { selectPage(index); });
    m_navLayout->addWidget(btn);
    m_tabs.append(btn);

    return page;
}

void SettingsDialog::selectPage(int index)
{
    if (index < 0 || index >= m_pages.size())
        return;
    m_stack->setCurrentIndex(index);
    m_pageTitle->setText(m_pageTitles.at(index));
    m_pageDesc->setText(m_pageDescs.at(index));
    for (int i = 0; i < m_tabs.size(); ++i)
        m_tabs.at(i)->setChecked(i == index);
}

// ===========================================================================
// 控件工厂
// ===========================================================================

QWidget *SettingsDialog::beginSection(QWidget *page, const QString &title)
{
    auto *pageLay = qobject_cast<QVBoxLayout *>(page->layout());
    if (!pageLay)
        return page;

    // 组间 1px 分隔（原型 .section 的 border-top）
    if (pageLay->count() > 0) {
        auto *line = new QFrame(page);
        line->setObjectName(QStringLiteral("hLine"));
        line->setFixedHeight(1);
        pageLay->addWidget(line);
    }

    auto *sec = new QWidget(page);
    auto *v = new QVBoxLayout(sec);
    v->setContentsMargins(0, 13, 0, 13);
    v->setSpacing(0);

    if (!title.isEmpty()) {
        auto *t = new QLabel(title, sec);
        t->setObjectName(QStringLiteral("sectionTitle"));
        v->addWidget(t);
        v->addSpacing(7);
    }

    pageLay->addWidget(sec);
    return sec;
}

QWidget *SettingsDialog::addRowWidget(QWidget *section, const QString &label, int labelWidth)
{
    auto *row = new QWidget(section);
    row->setMinimumHeight(34);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(12);

    if (!label.isEmpty()) {
        auto *lab = new QLabel(label, row);
        lab->setObjectName(QStringLiteral("rowLabel"));
        lab->setFixedWidth(labelWidth);
        h->addWidget(lab);
    }

    if (auto *v = qobject_cast<QVBoxLayout *>(section->layout())) {
        v->addSpacing(5);
        v->addWidget(row);
    }
    return row;
}

QHBoxLayout *SettingsDialog::addRow(QWidget *section, const QString &label, int labelWidth)
{
    return qobject_cast<QHBoxLayout *>(addRowWidget(section, label, labelWidth)->layout());
}

QCheckBox *SettingsDialog::addCheckRaw(QWidget *section, const QString &text)
{
    auto *cb = new QCheckBox(text, section);
    cb->setCursor(Qt::PointingHandCursor);
    cb->setMinimumHeight(23);
    if (auto *v = qobject_cast<QVBoxLayout *>(section->layout()))
        v->addWidget(cb);
    return cb;
}

QCheckBox *SettingsDialog::addCheck(QWidget *section, const QString &text, const char *key,
                                    bool inverted, bool def)
{
    auto *cb = addCheckRaw(section, text);
    m_chkBinds.append({ cb, key, inverted, def });
    return cb;
}

QComboBox *SettingsDialog::addComboRow(QWidget *section, const QString &label, const char *key,
                                       const QString &def,
                                       const QList<QPair<QString, QString>> &options,
                                       int comboWidth)
{
    QHBoxLayout *row = addRow(section, label);

    auto *combo = new QComboBox;
    for (const auto &opt : options)
        combo->addItem(opt.second, opt.first);
    combo->setCursor(Qt::PointingHandCursor);
    combo->setFixedHeight(kComboH);
    combo->setMinimumWidth(comboWidth);
    combo->setMaximumWidth(comboWidth);
    row->addWidget(combo);
    row->addStretch(1);

    m_comboBinds.append({ combo, key, def });
    return combo;
}

QLabel *SettingsDialog::addHint(QWidget *section, const QString &text)
{
    auto *lab = new QLabel(text, section);
    lab->setObjectName(QStringLiteral("hint"));
    lab->setWordWrap(true);
    if (auto *v = qobject_cast<QVBoxLayout *>(section->layout())) {
        v->addSpacing(4);
        v->addWidget(lab);
    }
    return lab;
}

QPushButton *SettingsDialog::addButton(QWidget *parent, const QString &text)
{
    auto *btn = new QPushButton(text, parent);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(kComboH);
    return btn;
}

QPushButton *SettingsDialog::addMiniButton(QWidget *parent, const QString &text)
{
    auto *btn = new QPushButton(text, parent);
    btn->setObjectName(QStringLiteral("miniBtn"));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(22);
    return btn;
}

// ===========================================================================
// 诚实化：零消费项灰显
// ===========================================================================

namespace {

// 单一文案：所有"尚未实现"项共用一句。文案散落在十几处，将来改动/翻译必走样。
QString unavailableTip()
{
    return QObject::tr("该选项暂未实现，将在后续版本提供。");
}

} // namespace

void SettingsDialog::markUnavailable(QWidget *w)
{
    if (!w)
        return;
    w->setEnabled(false);           // 不可交互：点击 / Tab / 键盘都不响应
    w->setCursor(Qt::ArrowCursor);  // 收掉手型，别"看起来能点"
    w->setToolTip(unavailableTip());
}

QCheckBox *SettingsDialog::addCheckUnavailable(QWidget *section, const QString &text,
                                               const char *key, bool inverted, bool def)
{
    QCheckBox *cb = addCheck(section, text, key, inverted, def);
    markUnavailable(cb);
    return cb;
}

QComboBox *SettingsDialog::addComboRowUnavailable(QWidget *section, const QString &label,
                                                  const char *key, const QString &def,
                                                  const QList<QPair<QString, QString>> &options,
                                                  int comboWidth)
{
    QComboBox *combo = addComboRow(section, label, key, def, options, comboWidth);
    markUnavailable(combo->parentWidget());   // 整行（行标签 + 下拉）一起灰
    return combo;
}

// ===========================================================================
// 1. 一般
// ===========================================================================

void SettingsDialog::buildGeneralPage(QWidget *page)
{
    QWidget *sec = beginSection(page, tr("一般设定"));
    // 「捕捉窗口」原为灰显假开关（勾着 + 禁用 + 零消费点），2026-11-04 按视觉基线
    // P0 删除：Config::captureWindow() 一并下线，本段已无灰显项。
    addCheck(sec, tr("不保存浏览过的文件夹路径"), "remember_browsed_folders", true, true);
    addCheck(sec, tr("不允许多个实例"), "allow_multiple_instances", true, false);
    addCheck(sec, tr("删除文件时确认"), "delete_confirm", false, true);
    addCheck(sec, tr("在边角显示通知信息，而非中心"), "notify_corner", false, true);
    addCheck(sec, tr("总在最前 (Ctrl+A)"), "always_on_top", false, false);
    addCheck(sec, tr("按 ESC 退出"), "esc_exit", false, true);
    // 「检查更新 / 检查小版本更新」旧开关见下方「检查更新」组：
    // 现在有真实的联网实现（UpdateChecker），不再是零消费假开关。

    // —— 检查更新 ——
    // 手动检查不受频控；自动检查默认关（不擅自联网），勾上后启动 5s 静默查一次。
    sec = beginSection(page, tr("检查更新"));
    QHBoxLayout *updateRow = addRow(sec, tr("手动检查"));
    m_checkUpdateBtn = addButton(updateRow->parentWidget(), tr("检查更新"));
    updateRow->addWidget(m_checkUpdateBtn);
    updateRow->addStretch(1);
    addCheck(sec, tr("启动时检查更新"), "check_updates", false, false);
    addHint(sec, tr("自动检查每 24 小时最多一次；手动检查不受限制。"));
    connect(m_checkUpdateBtn, &QPushButton::clicked, this, [this]() {
        m_checkUpdateBtn->setEnabled(false);
        m_checkUpdateBtn->setText(tr("检查中…"));
        if (!UpdateChecker::instance().check(UpdateChecker::Mode::Manual)) {
            // 已有请求在飞（比如刚触发的自动检查）→ 立即恢复，不排队、不重试。
            m_checkUpdateBtn->setEnabled(true);
            m_checkUpdateBtn->setText(tr("检查更新"));
        }
    });
    connect(&UpdateChecker::instance(), &UpdateChecker::finished, this,
            [this](bool manual) {
                if (manual)
                    m_checkUpdateBtn->setEnabled(true);
                m_checkUpdateBtn->setText(tr("检查更新"));
            });

    sec = beginSection(page, tr("到达首尾图片时"));
    const auto opts = lastFileOptions();
    addComboRow(sec, tr("到达首尾图片时执行"), "edge_action", QStringLiteral("stop"), opts, 160);
    // 「到达压缩包内首尾图片时执行」已删：压缩包浏览未实现，该场景不存在。
}

// ===========================================================================
// 2. 浏览
// ===========================================================================

void SettingsDialog::buildViewPage(QWidget *page)
{
    // 已接通：保存缩放状态、侧边箭头、动画帧信息、悬浮提示（B 批）。
    // 其余仍灰显的键在仓储侧无消费点，保持不可用态。
    QWidget *sec = beginSection(page, tr("浏览设置"));
    // 「显示主窗口」原为灰显假开关：语义待确认（启动时显示？托盘模式？），
    // 无法给出可验收的行为，按交办单要求删除该项，理由写进交接文档。
    addCheck(sec, tr("顶端菜单隐藏时显示文件名 (Shift+TAB)"),
             "view/overlay_filename", false, false);
    addCheck(sec, tr("保存缩放状态"), "view/save_zoom_state", false, false);
    // 「不显示菜单栏 / 控制栏」两项同样零消费，2026-11-04 按视觉基线 P0 删除；
    // 本页已无灰显项，unavailableTip 提示一并去掉。
    // 侧边箭头是真开关：ImageCanvas::setSideArrowsEnabled()。
    addCheck(sec, tr("不显示侧边箭头"), "view/show_side_arrow", true, true);
    addCheck(sec, tr("不显示动画信息控制"), "view/show_anim_control", true, true);
    addCheck(sec, tr("不显示缩放信息控制"), "view/show_zoom_control", true, true);
    addCheck(sec, tr("不显示鼠标悬浮工具提示"), "view/show_tooltip", true, true);
    addCheck(sec, tr("用小字体显示 EXIF / 文件信息"),
             "view/exif_small_font", false, false);

    // 「背景」段（背景图 + 路径 + 背景色 + 边框色）已按用户 2026-11-03 决定整段
    // 移除（第 18 期）：画布外观固定，见 MainWindow::applyRuntimeSettings 的注释。
}

// ===========================================================================
// 2.5 外观（第 18 期：主题三档 + 磨砂材质）
// ===========================================================================

void SettingsDialog::buildAppearancePage(QWidget *page)
{
    // —— 主题 ——
    // 三档：跟随系统 / 深色 / 浅色。落盘形态沿用 main.cpp 第 18 期前的取值约定：
    // theme=dark | light | system，默认 dark。UI 上以「跟随系统」为主开关，
    // 关闭后才能手选深浅——避免三选一 radio 里 system 与 dark/light 语义重叠。
    QWidget *sec = beginSection(page, tr("主题"));
    m_followSystem = addCheckRaw(sec, tr("跟随系统"));
    m_darkToggle = addCheckRaw(sec, tr("深色模式"));

    const bool follow = sGet("theme", QStringLiteral("dark")) == QStringLiteral("system");
    const bool dark = Theme::resolveMode(sGet("theme", QStringLiteral("dark")))
                      == Theme::Mode::Dark;
    m_followSystem->setChecked(follow);
    m_darkToggle->setChecked(dark);
    // 跟随系统时深浅由 Windows 决定，手选开关必须灰掉，否则两者写同一键互相打架。
    m_darkToggle->setEnabled(!follow);
    if (follow)
        m_darkToggle->setToolTip(tr("跟随系统开启时由系统决定"));

    connect(m_followSystem, &QCheckBox::toggled, this, [this](bool on) {
        m_darkToggle->setEnabled(!on);
        if (on) {
            m_darkToggle->setToolTip(tr("跟随系统开启时由系统决定"));
            // 开启跟随的一刻把勾选态亮成系统当前解析值，让用户知道现在是什么模式。
            const QSignalBlocker b(m_darkToggle);
            m_darkToggle->setChecked(Theme::resolveMode(QStringLiteral("system"))
                                     == Theme::Mode::Dark);
        } else {
            m_darkToggle->setToolTip(QString());
        }
    });
    addHint(sec, tr("跟随系统关闭后，可用上面的开关手动选择深色或浅色。"));

    // —— 材质 ——
    // 磨砂只做右键菜单与信息浮层（第 18 期④），独立键 ui/blur_material，
    // 默认关、不参与「恢复默认」清空（在 config 组外）。Win10 上 DWM 不支持
    // SystemBackdrop，勾了也只会是半透明纯色——直接灰掉，别给人坏的期待。
    sec = beginSection(page, tr("材质"));
    m_blurMaterial = addCheckRaw(sec, tr("磨砂材质（右键菜单与信息浮层）"));
    m_blurMaterial->setChecked(bGet("ui/blur_material", false));
#if defined(Q_OS_WIN)
    if (QOperatingSystemVersion::current() < QOperatingSystemVersion::Windows11)
        m_blurMaterial->setEnabled(false);   // 不写回、不落盘，保留用户已有取值
#endif
    addHint(sec, tr("仅影响右键菜单与信息浮层。Windows 11 效果最佳，Windows 10 降级为半透明。"));
}

void SettingsDialog::saveAppearance()
{
    // 外观页唯一落盘点：主题三选一 + 材质独立键。跟随时只写 system，
    // 显式选择才写 dark/light；disabled 的材质钮绝不回写（no writeback）。
    if (m_followSystem && m_darkToggle)
        sSet("theme", m_followSystem->isChecked()
                          ? QStringLiteral("system")
                          : (m_darkToggle->isChecked() ? QStringLiteral("dark")
                                                       : QStringLiteral("light")));
    if (m_blurMaterial && m_blurMaterial->isEnabled())
        bSet("ui/blur_material", m_blurMaterial->isChecked());
}

// ===========================================================================
// 3. 图像处理
// ===========================================================================

void SettingsDialog::buildImageProcPage(QWidget *page)
{
    QWidget *sec = beginSection(page, tr("图像处理"));
    // 「根据 EXIF 信息自动旋转图片」勾选框已移除：功能恒开且无开关消费方——
    // 解码链路无条件 setAutoTransform(true)（FrameNavigator / ImageCache /
    // MainWindow 各处），留个勾选框只会让人以为能关。实现处见
    // FrameNavigator.cpp 的同名注释。
    // 强锐化（C13）：lanczos 缩图路径叠加固定强度 unsharp mask，见 ImageCanvas。
    addCheck(sec, tr("使用强锐化滤镜"), "imageproc/strong_sharpen", false, false);
    addCheck(sec, tr("缓存下一张图片"), "imageproc/cache_image", false, true);

    QHBoxLayout *cacheRow = addRow(sec, tr("缓存大小"));
    m_cacheSize = new QSpinBox;
    m_cacheSize->setRange(10, 100000);
    m_cacheSize->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_cacheSize->setFixedHeight(kComboH);
    m_cacheSize->setFixedWidth(78);
    m_cacheSize->setAlignment(Qt::AlignCenter);
    cacheRow->addWidget(m_cacheSize);
    auto *mb = new QLabel(tr("MB"));
    mb->setObjectName(QStringLiteral("hint"));
    cacheRow->addWidget(mb);
    cacheRow->addStretch(1);
    // 缓存上限已接配置：保存时 applyRuntimeSettings 调 ImageCache::setFullCacheLimit
    // 即时生效（构造时也读同一键设初值），无需重启。

    addCheck(sec, tr("图片大于窗口时显示图片顶部"),
             "imageproc/align_top_when_large", false, true);
    // 快帧预览（C14）：ImageCache::startFullDecode 里的 DCT 缩放先出帧一级。
    addCheck(sec, tr("先显示低品质图片以加速渲染"), "imageproc/preview_mode", false, true);

    // 图片内嵌 ICC 已接通（C1）；监视器 ICC 判定为暂不可行（C2）：Win32 可用
    // GetICMProfile / WcsGetDefaultColorProfile 取到 profile，但 Qt6 的 raster
    // 后端没有显示器色彩管理，转换后的像素仍按 sRGB 上屏，等于错误映射。
    addCheck(sec, tr("使用图片文件的 ICC 设置"), "imageproc/image_icc", false, true);
    auto *iccMonitor = addCheck(sec, tr("使用监视器的 ICC 设置"), "imageproc/monitor_icc", false, false);
    iccMonitor->setEnabled(false);
    iccMonitor->setToolTip(tr("Qt6 光栅后端不支持显示器色彩管理，暂不可用"));
    addHint(sec, tr("使用监视器的 ICC 设置需要显示器级色彩管理，Qt6 光栅后端不支持，暂不可用。"));

    sec = beginSection(page, tr("透明与翻页"));
    addComboRow(sec, tr("为 GIF 图片使用透明背景"), "imageproc/gif_transparent",
                QStringLiteral("0"),
                { { "0", tr("默认GIF调色板") }, { "1", tr("白色背景") },
                  { "2", tr("黑色背景") }, { "3", tr("自定义颜色") } }, 160);
    addComboRow(sec, tr("为32位图片使用透明背景"), "imageproc/img32_transparent",
                QStringLiteral("0"),
                { { "0", tr("网格背景") }, { "1", tr("白色背景") },
                  { "2", tr("黑色背景") }, { "3", tr("深色网格") } }, 160);
    // 标签按实测语义改名：Canvas 的 InterpBilinear 走 FastTransformation（最近邻），
    // 中间档是 GPU 平滑（双线性量级），第三档才是真 Lanczos3 重采样。
    addComboRow(sec, tr("插值算法（裁剪或减淡）"), "imageproc/interpolate", QStringLiteral("0"),
                { { "0", tr("最近邻 (快)") }, { "1", tr("平滑 (双线性)") },
                  { "2", tr("Lanczos (锐利)") } }, 160);
    addComboRow(sec, tr("翻页效果"), "imageproc/pageturn_fx", QStringLiteral("0"),
                { { "0", tr("无") }, { "1", tr("淡入淡出") },
                  { "2", tr("Alpha 滑动") }, { "3", tr("滑动") } }, 160);
}

// ===========================================================================
// 4. 格式关联：说明 + 两个系统按钮 + 四组格式勾选网格
// ===========================================================================

void SettingsDialog::buildAssocPage(QWidget *page)
{
    QWidget *sec = beginSection(page, tr("格式关联"));
    addHint(sec, tr("两个入口都用于修改系统默认打开方式：前者打开 Windows「默认应用」，"
                    "后者打开旧式文件关联面板。"));

    QHBoxLayout *btnRow = addRow(sec, QString());
    auto *btnAssoc = addButton(nullptr, tr("设为默认打开方式…"));
    auto *btnClassic = addButton(nullptr, tr("打开系统默认应用"));
    btnRow->addWidget(btnAssoc);
    btnRow->addWidget(btnClassic);
    btnRow->addStretch(1);
    connect(btnAssoc, &QPushButton::clicked, this, []() {
        // 直接打开 Windows 设置 → 应用 → 默认应用
        ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps", nullptr, nullptr,
                      SW_SHOWNORMAL);
    });
    connect(btnClassic, &QPushButton::clicked, this, []() {
        // 经典方法：Windows 7+ 高级文件关联界面
        const HRESULT hrInit = CoInitialize(nullptr);
        IApplicationAssociationRegistrationUI *ui = nullptr;
        const HRESULT hr = CoCreateInstance(CLSID_ApplicationAssociationRegistrationUI,
                                            nullptr, CLSCTX_INPROC_SERVER,
                                            IID_PPV_ARGS(&ui));
        if (SUCCEEDED(hr) && ui) {
            ui->LaunchAdvancedAssociationUI(L"QuikView");
            ui->Release();
        }
        if (SUCCEEDED(hrInit)) {
            CoUninitialize();
        }
    });

    // 逐格式勾选组已删除（第 13 期）：
    // Windows 10/11 不允许程序静默改默认关联（UserChoice 带哈希，写完无效），
    // 而原先这一组勾选框只是把 assoc/ext_* 写进 config，没有任何消费点 ——
    // 就是「勾了也没事发生」的假开关。留一句只读清单，把格式范围讲清楚，
    // 真正的关联动作只有上面两个系统入口。
    {
        const QList<DirectoryLoader::Group> groups = DirectoryLoader::imageGroups();
        QStringList parts;
        for (const DirectoryLoader::Group &g : groups) {
            parts << QStringLiteral("%1（%2）").arg(g.label, g.exts.join(QStringLiteral(" / ")));
        }
        auto *fmt = new QLabel(parts.join(QStringLiteral("、")), sec);
        fmt->setWordWrap(true);
        fmt->setObjectName(QStringLiteral("hintText"));
        fmt->setContentsMargins(0, 6, 0, 0);
        if (auto *v = qobject_cast<QVBoxLayout *>(sec->layout()))
            v->addWidget(fmt);
    }

    addHint(sec, tr("安装程序已把 QuikView 登记为上述格式的候选打开方式；"
                    "Windows 10 起不允许程序静默抢占默认关联，"
                    "要改默认请在系统「默认应用」里选择 QuikView。"));
}

// ===========================================================================
// 5. 键盘：勾选 | 热键 | 操作 表格（14 行）
// ===========================================================================

void SettingsDialog::buildKeyboardPage(QWidget *page)
{
    auto *pageLay = qobject_cast<QVBoxLayout *>(page->layout());
    if (!pageLay)
        return;
    // 本页不要末尾弹簧：表格要吃掉剩余高度，14 行才能一屏全显。
    page->setProperty("noTailStretch", true);

    // 14 行全部可配置（config/keyboard/<action>_{enabled,_key,_action}），
    // MainWindow::applyShortcutConfig 在保存后立即给 QAction 重装键位。
    auto *hint = new QLabel(tr("双击「热键」栏录入新键位（Esc 清空），保存后立即生效；"
                               "取消勾选可停用该动作。"
                               "两个启用的动作占用同一键位时保存会被拒绝。"), page);
    hint->setObjectName(QStringLiteral("pageHint"));
    hint->setWordWrap(true);
    pageLay->addWidget(hint);

    auto *table = new QFrame(page);
    table->setObjectName(QStringLiteral("keyTable"));
    auto *tl = new QVBoxLayout(table);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(0);

    // 表头
    auto *head = new QWidget(table);
    head->setObjectName(QStringLiteral("keyHead"));
    head->setFixedHeight(31);
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);
    auto makeHeadCell = [head](const QString &text, int width) {
        auto *lab = new QLabel(text, head);
        lab->setObjectName(QStringLiteral("keyHeadCell"));
        lab->setContentsMargins(8, 0, 8, 0);
        if (width > 0)
            lab->setFixedWidth(width);
        return lab;
    };
    hl->addWidget(makeHeadCell(tr("勾选"), 42));
    hl->addWidget(makeHeadCell(tr("热键"), 122));
    hl->addWidget(makeHeadCell(tr("操作"), 0), 1);
    tl->addWidget(head);

    // 行区：独立滚动、高度按整行吸附（见 KeyRowsArea 注释）
    auto *rowsArea = new KeyRowsArea(table);
    rowsArea->setObjectName(QStringLiteral("keyRowsArea"));
    auto *rowsBox = new QWidget;
    rowsBox->setObjectName(QStringLiteral("keyRowsBox"));
    auto *rl0 = new QVBoxLayout(rowsBox);
    // 底部 12px 安全留白属于行区内容：14 行全显时末行与表格底边之间有明确间距，
    // 滚动到底时同样保留（M-05 返工，2026-11-05）。旧实现把留白放在表格外的
    // pageLay->addSpacing(12)，表格内末行直接贴底——视觉上等于没有。
    rl0->setContentsMargins(0, 0, 0, 12);
    rl0->setSpacing(0);

    // 默认键位 = MainWindow 出厂硬编码值（PortableText；幻灯片/裁剪出厂无单键，
    // 放映由 Ctrl+1..9/Ctrl+0 承担、裁剪走菜单）。改动须同步
    // MainWindow::applyShortcutConfig 的槽位表。
    struct KeyDef { const char *action; const char *hotkey; const char *label; };
    static const KeyDef defs[] = {
        { "open",        "Ctrl+O",     QT_TR_NOOP("打开") },
        { "openfolder",  "F",          QT_TR_NOOP("打开文件夹") },
        { "next",        "Right",      QT_TR_NOOP("翻下一张") },
        { "prev",        "Left",       QT_TR_NOOP("翻上一张") },
        { "next_frame",  "Ctrl+Right", QT_TR_NOOP("下一帧") },
        { "prev_frame",  "Ctrl+Left",  QT_TR_NOOP("上一帧") },
        { "zoom_in",     "Ctrl++",     QT_TR_NOOP("放大") },
        { "zoom_out",    "Ctrl+-",     QT_TR_NOOP("缩小") },
        { "actual_size", "0",          QT_TR_NOOP("实际大小") },
        { "fit",         "9",          QT_TR_NOOP("适配窗口") },
        { "slideshow",   "",           QT_TR_NOOP("幻灯片") },
        { "crop",        "",           QT_TR_NOOP("裁剪") },
        { "favorite",    "B",          QT_TR_NOOP("收藏") },
        { "del",         "Del",        QT_TR_NOOP("删除") },
    };

    // 「操作」列只读：动作由行本身决定，用户只能改勾选与热键（2026-11-04 拍板）。
    for (int i = 0; i < int(sizeof(defs) / sizeof(defs[0])); ++i) {
        if (i > 0) {
            auto *line = new QFrame(table);
            line->setObjectName(QStringLiteral("hLine"));
            line->setFixedHeight(1);
            rl0->addWidget(line);
        }

        const KeyDef &def = defs[i];
        auto *row = new QWidget(table);
        row->setFixedHeight(32);
        row->setAttribute(Qt::WA_StyledBackground, true);   // 冲突行高亮走 QSS 背景色
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(0);

        auto *chkHost = new QWidget(row);
        chkHost->setFixedWidth(42);
        auto *cl2 = new QHBoxLayout(chkHost);
        cl2->setContentsMargins(0, 0, 0, 0);
        auto *chk = new QCheckBox(chkHost);
        chk->setCursor(Qt::PointingHandCursor);
        cl2->addWidget(chk, 0, Qt::AlignCenter);
        rl->addWidget(chkHost);

        auto *editHost = new QWidget(row);
        editHost->setFixedWidth(122);
        auto *el = new QHBoxLayout(editHost);
        el->setContentsMargins(8, 3, 8, 3);
        auto *edit = new QKeySequenceEdit(editHost);
        edit->setFixedHeight(26);
        edit->setStyleSheet(QStringLiteral("QKeySequenceEdit{font-size:11px;padding:0 6px;}"));
        // 空热键：Qt 内置占位是英文 "Press shortcut"，且 windeployqt 没带 Qt 翻译，
        // 直接显示裸英文。Polish/Show 时把内部 QLineEdit 占位换成中文「未设置」（Muted 色）。
        new KeySeqPlaceholderFixer(edit);
        el->addWidget(edit);
        rl->addWidget(editHost);

        // 「操作」列只读：动作由行本身决定，不给下拉、不给编辑（用户 2026-11-04 拍板）
        auto *actionLab = new QLabel(tr(def.label), row);
        actionLab->setObjectName(QStringLiteral("keyAction"));
        actionLab->setContentsMargins(8, 0, 8, 0);
        rl->addWidget(actionLab, 1);
        // 「操作」列右缘与行区滚动条留 6px，避免文字/箭头贴着滚动条（P-17 / D3）
        rl->addSpacing(6);

        // 任一字段改动即撤掉上次保存被拒的冲突高亮（重新保存时再检测）
        connect(chk, &QCheckBox::toggled, this, [this]() { clearKeyConflicts(); });
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this,
                [this]() { clearKeyConflicts(); });

        m_keyRows.append({ chk, edit, actionLab, row, QString::fromUtf8(def.action),
                           QString::fromUtf8(def.hotkey), tr(def.label) });
        rl0->addWidget(row);
    }

    rowsArea->setWidget(rowsBox);
    // 行区不吃 stretch：widgetResizable 会把 rowsBox 拉到视口高，QVBoxLayout 再把
    // 多出来的高度均摊到 27 个 item 之间（行距被打散、末行贴底）。改为行区高度=内容
    // 自然高，表格内剩余高度交给下面的弹簧——留白稳定、行距恒为 33（M-05 根因）。
    tl->addWidget(rowsArea, 0);
    tl->addStretch(1);

    pageLay->addWidget(table, 1);   // 表格吃掉页面剩余高度，14 行一屏全显
    // 旧的表格外 12px spacing 已删除：留白改由 rowsBox 内容底部 margin 承担
    // （表格外留白不算数，单子 §4.1）。

    // 整行吸附：高度按外层页面可视高算出「能放下几整行」，多出来的行交给行区
    // 自己的滚动条（行高 32 + 分隔线 1 = 33）。
    rowsArea->snapTo(hint, head, 33);
}

// ===========================================================================
// 6. 鼠标：两条设定 + 两列「按键 → 动作」
// ===========================================================================

void SettingsDialog::buildMousePage(QWidget *page)
{
    QWidget *sec = beginSection(page, tr("鼠标设定"));
    // 两项均已接线：双击最大化/还原走 ImageCanvas::doubleClickRequested →
    // MainWindow 切换窗口；两侧单击翻页走 edgeTurnRequested，且老配置默认值
    // 做过一次性迁移（main.cpp 的 etc/migrated_side_click）。
    addCheck(sec, tr("双击最大化 / 还原"), "mouse/dblclick_maximize", false, true);
    addCheck(sec, tr("单击屏幕两侧时翻页"), "mouse/click_side_pageturn", false, false);

    sec = beginSection(page, tr("自定义操作"));

    auto *host = new QWidget(sec);
    auto *grid = new QGridLayout(host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    if (auto *v = qobject_cast<QVBoxLayout *>(sec->layout()))
        v->addWidget(host);

    // configurable：该行是否真有消费点。中键与滚轮的「平移/缩放」经
    // onAccepted 桥接写回 config/middle_pan、config/wheel_zoom 生效；
    // 左右/X/Y 键由 ImageCanvas 判定点击后发 mouseActionRequested → 
    // MainWindow::onMouseAction 派发，none 保留内置行为（右键菜单等）。
    struct BtnDef { const char *key; const char *label; const char *def; bool configurable; };
    static const BtnDef btns[] = {
        { "mouse/btn_left",   QT_TR_NOOP("鼠标左键"),  "none",     true  },
        { "mouse/btn_middle", QT_TR_NOOP("鼠标中键"),  "pan",      true  },
        { "mouse/btn_right",  QT_TR_NOOP("鼠标右键"),  "none",     true  },
        { "mouse/btn_x",      QT_TR_NOOP("X 键"),      "none",     true  },
        { "mouse/btn_y",      QT_TR_NOOP("Y 键"),      "none",     true  },
        { "mouse/wheel_up",   QT_TR_NOOP("滚轮向上"),  "zoom_in",  true  },
        { "mouse/wheel_down", QT_TR_NOOP("滚轮向下"),  "zoom_out", true  },
    };

    const auto opts = actionOptions();
    for (int i = 0; i < 7; ++i) {
        const BtnDef &b = btns[i];
        auto *row = new QWidget(host);
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(12);

        auto *lab = new QLabel(tr(b.label), row);
        lab->setObjectName(QStringLiteral("rowLabel"));
        lab->setFixedWidth(88);
        rl->addWidget(lab);

        auto *combo = new QComboBox(row);
        for (const auto &opt : opts)
            combo->addItem(opt.second, opt.first);
        combo->setCursor(Qt::PointingHandCursor);
        combo->setFixedHeight(kComboH);
        combo->setMinimumWidth(160);
        combo->setMaximumWidth(170);
        rl->addWidget(combo, 1);

        if (!b.configurable)
            markUnavailable(combo->parentWidget());   // 整行（行标签 + 下拉）一起灰

        m_mouseRows.append({ combo, QString::fromUtf8(b.key), QString::fromUtf8(b.def) });
        grid->addWidget(row, i % 4, i / 4);
    }

    addHint(sec, tr("左/右/X/Y 键可映射成动作；「无」保留内置行为（左键拖动平移与"
                    "边缘单击翻页、右键上下文菜单、中键切全屏、滚轮缩放）。"));
}

// ===========================================================================
// 7. 书签
// ===========================================================================

void SettingsDialog::buildBookmarkPage(QWidget *page)
{
    QWidget *sec = beginSection(page, tr("书签设置"));
    // 自动书签与最大书签数已接通；「多窗口共享」在单实例语义下无实现，
    // 2026-11-04 按视觉基线 P0 删除（原为勾着 + 禁用 + 零写回）。
    addCheck(sec, tr("自动保存当前图片路径为书签"), "bookmark/auto_bookmark", false, false);
    // 「自动记忆当前图片在压缩包中的路径」与「删除已记忆位置」已删：
    // 压缩包浏览未实现，压缩包路径记忆无从产生，两项勾/点了也不会有任何事发生。

    sec = beginSection(page, tr("书签容量"));
    QHBoxLayout *row = addRow(sec, tr("最大书签数"));
    m_maxBookmarks = new QComboBox;
    for (int v : { 10, 20, 30, 50, 100 })
        m_maxBookmarks->addItem(QString::number(v), v);
    m_maxBookmarks->setCursor(Qt::PointingHandCursor);
    m_maxBookmarks->setFixedHeight(kComboH);
    m_maxBookmarks->setFixedWidth(110);
    row->addWidget(m_maxBookmarks);
    row->addStretch(1);
    // 已接配置：Config::toggleFavorite 超限时从尾部淘汰最早的书签
    // （键 config/bookmark/max_bookmarks，默认 50，clamp 10..100）。

    // P2 / E2：补一段真实说明，减少「内容只占上半屏」的空白占比。
    sec = beginSection(page, tr("说明"));
    addHint(sec, tr("书签记录图片的完整路径，文件移动或重命名后原条目会失效。"
                    "自动保存只在打开图片时追加一条；数量超过上限时从最早的书签开始淘汰。"
                    "书签数据保存在本机配置中，不随安装包或账户同步。"));
}

// ===========================================================================
// 8. 上下文菜单
// ===========================================================================

void SettingsDialog::buildContextMenuPage(QWidget *page)
{
    // 壳扩展随安装包落盘：64 位 quikthumb.dll 与 exe 同目录（packaging/quikview.nsi）。
    // 本页只做只读状态说明，不再放会写 config 的勾选框——ctxmenu/* 全仓零消费，
    // 真正生效的是安装器注册的 Shell 扩展。
    const bool shellReady = QFileInfo::exists(
        QCoreApplication::applicationDirPath() + QStringLiteral("/quikthumb.dll"));
    const QString shellState = shellReady ? tr("已随安装包注册 ✓")
                                          : tr("未检测到壳扩展");

    auto addStatusRow = [](QWidget *section, const QString &label,
                           const QString &state, const char *color) {
        auto *row = new QWidget(section);
        row->setMinimumHeight(30);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(12);

        auto *name = new QLabel(label, row);
        name->setObjectName(QStringLiteral("rowLabel"));
        name->setFixedWidth(230);
        h->addWidget(name);

        auto *value = new QLabel(state, row);
        value->setWordWrap(true);
        value->setStyleSheet(QStringLiteral("color:%1;font-size:11px;")
                                 .arg(QLatin1String(color)));
        h->addWidget(value, 1);

        if (auto *v = qobject_cast<QVBoxLayout *>(section->layout())) {
            v->addSpacing(5);
            v->addWidget(row);
        }
    };
    const char *kOk = UiStyle::StatusOk;      // 已注册
    const char *kWarn = UiStyle::StatusWarn;   // 缺文件
    const char *kMuted = UiStyle::Muted;      // 本版未提供 / 暂不支持

    QWidget *sec = beginSection(page, tr("上下文菜单设定"));
    // 本版壳扩展真实提供两个菜单项（浏览 / 转换），其余三项是规划项，不能谎报已注册。
    addStatusRow(sec, tr("在资源管理器中使用上下文菜单"), shellState,
                 shellReady ? kOk : kWarn);
    addStatusRow(sec, tr("用QuikView转换图片"), shellState, shellReady ? kOk : kWarn);
    addStatusRow(sec, tr("用QuikView浏览图片"), shellState, shellReady ? kOk : kWarn);
    addStatusRow(sec, tr("上下文菜单预览"), tr("已实现自绘缩略图（≤256px），待资源管理器验收"), kMuted);
    addStatusRow(sec, tr("显示图片信息"), tr("已支持（宽×高 · 文件大小）"), kOk);
    addStatusRow(sec, tr("显示软件图标"), tr("已支持（壳扩展内嵌资源图标）"), kOk);
    if (!shellReady)
        addHint(sec, tr("未检测到壳扩展，重新运行安装包可修复。"));

    sec = beginSection(page, tr("资源管理器缩略图"));
    addStatusRow(sec, tr("WebP"), shellState, shellReady ? kOk : kWarn);
    addStatusRow(sec, tr("PSD"), tr("部分支持（内嵌 JPEG 缩略图，无内嵌回退默认图标）"), kWarn);
    addStatusRow(sec, tr("PSB"), tr("部分支持（内嵌 JPEG 缩略图，无内嵌回退默认图标）"), kWarn);

    sec = beginSection(page, tr("说明"));
    addHint(sec, tr("这些入口由 QuikView 壳扩展在资源管理器中提供，"
                    "不是应用内图片右键菜单的功能开关；"
                    "安装/卸载安装包时自动注册/移除。"));
}

// ===========================================================================
// 9. 杂项
// ===========================================================================

void SettingsDialog::buildEtcPage(QWidget *page)
{
    QWidget *sec = beginSection(page, tr("图片文件夹"));

    auto makeFolderRow = [this, sec](const QString &label, QLineEdit **edit) {
        QHBoxLayout *row = addRow(sec, label);
        *edit = new QLineEdit;
        (*edit)->setFixedHeight(kInputH);
        row->addWidget(*edit, 1);
        auto *btnBrowse = addButton(nullptr, tr("浏览…"));
        auto *btnOpen = addButton(nullptr, tr("打开"));
        row->addWidget(btnBrowse);
        row->addWidget(btnOpen);
        connect(btnBrowse, &QPushButton::clicked, this, [this, edit]() {
            const QString d = QFileDialog::getExistingDirectory(
                this, tr("选择一个文件夹."), (*edit)->text());
            if (!d.isEmpty())
                (*edit)->setText(d);
        });
        connect(btnOpen, &QPushButton::clicked, this, [edit]() {
            const QString d = (*edit)->text().trimmed();
            if (!d.isEmpty())
                QDesktopServices::openUrl(QUrl::fromLocalFile(d));
        });
    };
    makeFolderRow(tr("图片文件夹 1"), &m_photoFolder1);
    makeFolderRow(tr("图片文件夹 2"), &m_photoFolder2);

    sec = beginSection(page, tr("图像编辑器"));
    QHBoxLayout *row = addRow(sec, tr("程序路径"));
    m_editorPath = new QLineEdit;
    m_editorPath->setFixedHeight(kInputH);
    row->addWidget(m_editorPath, 1);
    auto *btnBrowse = addButton(nullptr, tr("浏览…"));
    auto *btnPreset = addButton(nullptr, tr("预设"));
    row->addWidget(btnBrowse);
    row->addWidget(btnPreset);
    connect(btnBrowse, &QPushButton::clicked, this, [this]() {
        const QString f = QFileDialog::getOpenFileName(
            this, tr("选择图像编辑器"), m_editorPath->text(),
            tr("程序 (*.exe);;所有文件 (*)"));
        if (!f.isEmpty())
            m_editorPath->setText(f);
    });
    connect(btnPreset, &QPushButton::clicked, this, [this]() {
        m_editorPath->setText(QStringLiteral("C:/Windows/System32/mspaint.exe"));
    });

    sec = beginSection(page, tr("其他"));
    // 本批已接线：翻页音效 / 按图调窗 / 禁用屏保 / 不打开 RAW / JPEG 同名过滤 / RAW 缩略图。
    addCheck(sec, tr("翻页时播放音效"), "etc/pageturn_wav", false, false);
    addCheck(sec, tr("不打开 RAW 图片文件"), "etc/no_raw_load", false, false);
    addCheck(sec, tr("打开 JPEG 文件时，不打开同名 RAW 文件"), "etc/skip_raw_when_jpg", false, true);
    addCheck(sec, tr("打开 RAW 图片文件时，显示已有缩略图"), "etc/raw_thumbnail", false, true);
    addCheck(sec, tr("根据图片大小调节窗口大小"), "etc/fit_window_to_image", false, true);
    addCheck(sec, tr("运行 QuikView 时禁用 Windows 屏幕保护程序"), "etc/disable_screensaver",
             false, false);

    // P2 / G1-2：补一段 RAW / 编辑器说明，减少内容下方的大块空白。
    addHint(sec, tr("RAW 相关选项只影响 RAW 文件的打开方式；勾选「不打开 RAW 图片文件」时，"
                    "JPEG 旁的 RAW 也不会被自动带出。外部编辑器留空时使用系统默认程序。"));
}

// ===========================================================================
// 10. 语言
// ===========================================================================

void SettingsDialog::buildLangPage(QWidget *page)
{
    QWidget *sec = beginSection(page, tr("语言设定"));

    QHBoxLayout *row = addRow(sec, tr("界面语言"));
    m_language = new QComboBox;
    populateLanguageCombo(m_language);
    m_language->setCursor(Qt::PointingHandCursor);
    m_language->setFixedHeight(kComboH);
    m_language->setMinimumWidth(160);
    m_language->setMaximumWidth(200);
    row->addWidget(m_language);
    row->addStretch(1);

    auto *link1 = new LinkLabel(tr("下载更多语言…"), []() {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://github.com/xdsT2/quikview")));
    }, sec);
    auto *link2 = new LinkLabel(tr("打开语言文件夹…"), []() {
        const QString dir = QCoreApplication::applicationDirPath()
                            + QStringLiteral("/translations");
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    }, sec);
    if (auto *v = qobject_cast<QVBoxLayout *>(sec->layout())) {
        v->addSpacing(4);
        v->addWidget(link1);
        v->addWidget(link2);
    }

    // 「翻译」区原是硬编码的等宽文本清单，与上方语言下拉重复且像调试输出；
    // 2026-11-04 按视觉基线 H2 直接删除（已安装语言以下拉为准）。
}

// ===========================================================================
// 配置读写
// ===========================================================================

void SettingsDialog::loadFromConfig()
{
    for (const ChkBind &b : m_chkBinds) {
        const bool v = bGet(b.key, b.def);
        b.cb->setChecked(b.inverted ? !v : v);
    }
    for (const ComboBind &b : m_comboBinds) {
        const int idx = b.combo->findData(sGet(b.key, b.def));
        b.combo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    if (m_cacheSize)
        m_cacheSize->setValue(iGet("imageproc/cache_size_mb", 300));
    for (const KeyRow &r : m_keyRows) {
        // 出厂无单键的动作（幻灯片 / 裁剪）默认不启用，避免「勾着但热键为空」；
        // 已有配置仍以用户落盘值为准（P2 / D4）。
        const bool enabledByDefault = !r.defHotkey.isEmpty();
        r.cb->setChecked(bGet(QStringLiteral("keyboard/%1_enabled").arg(r.action).toLatin1().constData(),
                              enabledByDefault));
        const QString key = sGet(QStringLiteral("keyboard/%1_key").arg(r.action).toLatin1().constData(),
                                 r.defHotkey);
        r.edit->setKeySequence(QKeySequence::fromString(key, QKeySequence::PortableText));
        // 「操作」列只读：不再回读 keyboard/<action>_action（历史上被改绑过的配置
        // 会在下次保存时按固定动作覆写回去）。
    }
    for (const MouseRow &r : m_mouseRows) {
        const int idx = r.combo->findData(sGet(r.key.toLatin1().constData(), r.def));
        r.combo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    if (m_maxBookmarks) {
        const int idx = m_maxBookmarks->findData(iGet("bookmark/max_bookmarks", 50));
        m_maxBookmarks->setCurrentIndex(idx >= 0 ? idx : 3);
    }
    if (m_photoFolder1)
        m_photoFolder1->setText(sGet("etc/photo_folder_1"));
    if (m_photoFolder2)
        m_photoFolder2->setText(sGet("etc/photo_folder_2"));
    if (m_editorPath)
        m_editorPath->setText(sGet("etc/image_editor"));
    if (m_language) {
        const int idx = m_language->findData(sGet("language", QStringLiteral("system")));
        m_language->setCurrentIndex(idx >= 0 ? idx : 0);
    }
}

void SettingsDialog::takeSnapshot()
{
    QSettings &s = Config::instance().settings();
    s.beginGroup(QStringLiteral("config"));
    const QStringList keys = s.allKeys();
    for (const QString &key : keys)
        m_snapshot.insert(key, s.value(key));
    s.endGroup();
}

void SettingsDialog::restoreSnapshot()
{
    QSettings &s = Config::instance().settings();
    s.beginGroup(QStringLiteral("config"));
    s.remove(QStringLiteral(""));
    for (auto it = m_snapshot.constBegin(); it != m_snapshot.constEnd(); ++it)
        s.setValue(it.key(), it.value());
    s.endGroup();
}

void SettingsDialog::onAccepted()
{
    // —— 键盘冲突检测先于一切写盘：同一键位被两个启用的动作占用（或撞上
    // 固定动作、同一动作被重复指派）→ 高亮冲突行、提示占用者、整次保存拒绝 ——
    clearKeyConflicts();
    const QString conflictMsg = keyboardConflicts();
    if (!conflictMsg.isEmpty()) {
        QMessageBox::warning(this, tr("设置"), conflictMsg);
        return;
    }

    for (const ChkBind &b : m_chkBinds) {
        const bool v = b.cb->isChecked();
        bSet(b.key, b.inverted ? !v : v);
    }
    for (const ComboBind &b : m_comboBinds)
        sSet(b.key, b.combo->currentData().toString());

    if (m_cacheSize)
        iSet("imageproc/cache_size_mb", m_cacheSize->value());
    for (const KeyRow &r : m_keyRows) {
        bSet(QStringLiteral("keyboard/%1_enabled").arg(r.action).toLatin1().constData(),
             r.cb->isChecked());
        // _key 存 PortableText（"Ctrl+P"、"Right"…），MainWindow 按
        // QKeySequence::fromString(…, PortableText) 读回。
        sSet(QStringLiteral("keyboard/%1_key").arg(r.action).toLatin1().constData(),
             r.edit->keySequence().toString(QKeySequence::PortableText));
        // 动作固定，仍写回同名字段：把历史改绑留下的旧值清掉。
        sSet(QStringLiteral("keyboard/%1_action").arg(r.action).toLatin1().constData(),
             r.action);
    }
    for (const MouseRow &r : m_mouseRows)
        sSet(r.key.toLatin1().constData(), r.combo->currentData().toString());
    if (m_maxBookmarks)
        iSet("bookmark/max_bookmarks", m_maxBookmarks->currentData().toInt());
    if (m_photoFolder1)
        sSet("etc/photo_folder_1", m_photoFolder1->text());
    if (m_photoFolder2)
        sSet("etc/photo_folder_2", m_photoFolder2->text());
    if (m_editorPath)
        sSet("etc/image_editor", m_editorPath->text());
    if (m_language)
        sSet("language", m_language->currentData().toString());

    // —— 兼容桥：运行逻辑沿用的旧 key 由新设置项推导 ——
    // 三档必须一一对应，之前只写了 fast/smooth 两支，选 Lanczos("2") 被吞成
    // smooth，Lanczos 永远选不中。Config::interpolation() 会把 fast→bilinear、
    // smooth→bicubic 翻译成画布认识的模式，lanczos 原样透传。
    const QString interp = sGet("imageproc/interpolate", QStringLiteral("0"));
    sSet("interpolation", interp == QStringLiteral("0") ? QStringLiteral("fast")
                          : interp == QStringLiteral("1") ? QStringLiteral("smooth")
                                                          : QStringLiteral("lanczos"));
    bSet("wheel_zoom", sGet("mouse/wheel_up", QStringLiteral("zoom_in")) == QStringLiteral("zoom_in")
                       && sGet("mouse/wheel_down", QStringLiteral("zoom_out")) == QStringLiteral("zoom_out"));
    bSet("middle_pan", sGet("mouse/btn_middle", QStringLiteral("pan")) == QStringLiteral("pan"));

    // 外观页（第 18 期）：三态主题与磨砂键在这里统一落盘。
    saveAppearance();
}

void SettingsDialog::onReset()
{
    const int r = QMessageBox::question(this, tr("设置"), tr("是否要初始化设置？"),
                                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (r != QMessageBox::Yes)
        return;
    QSettings &s = Config::instance().settings();
    s.beginGroup(QStringLiteral("config"));
    s.remove(QStringLiteral(""));
    s.endGroup();
    loadFromConfig();

    // 「背景」段的两个颜色钮已随第 18 期下线；其余直接读写 config 的控件
    // 均为绑定表/专项控件，loadFromConfig() 已覆盖。
}

// ===========================================================================
// 键盘自定义（批次 D1）：冲突检测 + 验收探针
// ===========================================================================

// 高亮以整行为单位（QSS QWidget[conflict="true"]，行 widget 已开 WA_StyledBackground）
void SettingsDialog::setKeyRowConflict(int index, bool on)
{
    if (index < 0 || index >= m_keyRows.size())
        return;
    QWidget *row = m_keyRows[index].row;
    if (row->property("conflict").toBool() == on)
        return;
    row->setProperty("conflict", on);
    row->style()->unpolish(row);
    row->style()->polish(row);
    row->update();
}

void SettingsDialog::clearKeyConflicts()
{
    for (int i = 0; i < m_keyRows.size(); ++i)
        setKeyRowConflict(i, false);
}

// 键盘页冲突检测（保存前调用）。生效语义与 MainWindow::applyShortcutConfig
// 一一对应，判定基准是「保存后每个动作实际会装什么键」：
//   出厂态行（启用 + 绑自身 + 键为空或等于出厂主键）= 基线，自身不参与冲突，
//   其动作按出厂完整键位表生效；其余为自定义行（将顶掉出厂键位）。
// 多个启用行指向同一动作是合法用法（第 15 期：open 行可改绑 openfolder，
// 两个槽位的键聚合到同一动作）；同一动作内部重复的键只算一次，不报冲突。
// 两类冲突（只由自定义行触发，且目标动作不同）：
//   1) 两个自定义行键位相同；
//   2) 自定义行键位撞上「仍处于出厂态的其他动作」的任何键（含 PageUp/Backspace
//      等副键）或固定动作（打印 Ctrl+P 等 14 个之外的动作）的任何键。
// 有冲突：高亮全部冲突行并返回第一条提示文案；无冲突返回空串。
QString SettingsDialog::keyboardConflicts()
{
    struct RowState {
        bool enabled = false;
        QString target;
        QKeySequence key;
        bool factory = false;
    };
    QList<RowState> st;
    for (const KeyRow &r : m_keyRows) {
        RowState s;
        s.enabled = r.cb->isChecked();
        s.target = r.action;   // 动作只读，目标恒等于行自身动作
        s.key = r.edit->keySequence();
        const QKeySequence defSeq =
            QKeySequence::fromString(r.defHotkey, QKeySequence::PortableText);
        s.factory = s.enabled && s.target == r.action
                    && (s.key.isEmpty() || s.key == defSeq);
        st.append(s);
    }

    // 动作 id → 显示名（「操作」列已改只读标签，直接从行里取）
    QHash<QString, QString> labelOf;
    for (const KeyRow &r : m_keyRows) {
        labelOf.insert(r.action, r.label);
    }
    auto label = [&labelOf](const QString &id) { return labelOf.value(id, id); };
    auto rowIndexOfAction = [this](const QString &action) -> int {
        for (int i = 0; i < m_keyRows.size(); ++i) {
            if (m_keyRows[i].action == action)
                return i;
        }
        return -1;
    };

    // 占用表（显示名 → 键位列表）：仍处于出厂态的可配置动作（含副键）+ 固定动作
    QList<QPair<QString, QList<QKeySequence>>> occupied;
    QList<QString> occupiedIds;   // 与 occupied 平行；空串 = 固定动作
    if (MainWindow *mw = qobject_cast<MainWindow *>(parent())) {
        const auto factory = mw->factoryShortcutLists();
        for (auto it = factory.constBegin(); it != factory.constEnd(); ++it) {
            // 只有当该动作自己的行处于出厂态时，它的出厂键位才仍然在岗
            const int own = rowIndexOfAction(it.key());
            if (own >= 0 && st[own].factory) {
                occupied.append({ label(it.key()), it.value() });
                occupiedIds.append(it.key());
            }
        }
        QHash<QString, QList<QKeySequence>> merged;
        const auto fixedEntries = mw->fixedShortcutActions();
        for (const auto &f : fixedEntries)
            merged[f.first].append(f.second);
        for (auto it = merged.constBegin(); it != merged.constEnd(); ++it) {
            occupied.append({ it.key(), it.value() });
            occupiedIds.append(QString());
        }
    }

    QSet<int> bad;
    QString msg;
    auto markBad = [&bad, &msg](QList<int> rows, const QString &m) {
        for (int r : rows)
            bad.insert(r);
        if (msg.isEmpty())
            msg = m;
    };
    auto keyText = [](const QKeySequence &s) {
        return s.toString(QKeySequence::NativeText);
    };

    // 1) 两个自定义行键位相同（同目标动作内部重复由 MainWindow 聚合去重，不算冲突）
    for (int i = 0; i < st.size(); ++i) {
        if (st[i].factory || st[i].key.isEmpty())
            continue;
        for (int j = i + 1; j < st.size(); ++j) {
            if (st[j].factory || st[j].key.isEmpty())
                continue;
            if (st[i].target == st[j].target)
                continue;
            if (st[i].key == st[j].key)
                markBad({ i, j }, tr("%1 已被『%2』占用")
                                      .arg(keyText(st[i].key), label(st[j].target)));
        }
    }
    // 2) 自定义行 vs 出厂/固定键位（目标动作自己的出厂表不参与：改绑后原表即让位）
    for (int i = 0; i < st.size(); ++i) {
        if (st[i].factory || st[i].key.isEmpty())
            continue;
        for (int k = 0; k < occupied.size(); ++k) {
            if (!occupiedIds[k].isEmpty() && occupiedIds[k] == st[i].target)
                continue;
            if (occupied[k].second.contains(st[i].key))
                markBad({ i }, tr("%1 已被『%2』占用")
                                   .arg(keyText(st[i].key), occupied[k].first));
        }
    }

    for (int i = 0; i < st.size(); ++i)
        setKeyRowConflict(i, bad.contains(i));
    return msg;
}

// —— 验收探针（--smoke keyconflict）——
void SettingsDialog::probeKeyConflict()
{
    // 切到键盘页（导航第 5 项），让冲突行高亮进截图
    selectPage(4);
    // 把「删除」行热键改成 Ctrl+P（固定动作「打印」的主键），走真实保存链路：
    // 应弹「Ctrl+P 已被『打印』占用」、冲突行高亮、config 不落盘。
    for (const KeyRow &r : m_keyRows) {
        if (r.action == QLatin1String("del")) {
            r.edit->setKeySequence(QKeySequence(QStringLiteral("Ctrl+P")));
            break;
        }
    }
    // 冲突提示框是 exec 阻塞的：先定时在嵌套循环里截提示框证据再关掉它
    QTimer::singleShot(600, this, [this]() {
        if (auto *mb = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            mb->grab().save(QStringLiteral("Temp/keys-conflict-msg.png"));
            mb->close();   // exec 随之返回，保存已被阻止
        }
    });
    onAccepted();
    grab().save(QStringLiteral("Temp/keys-conflict-row.png"));   // 冲突行高亮态
    close();
}

// —— 验收探针（--smoke keyreset）——
void SettingsDialog::probeResetDefaults()
{
    // footer「恢复默认」→「是否要初始化设置？」自动答「是」→ 回默认值；
    // 再按真实「确定」链路写盘并 accept（accept 不走快照回滚）。
    QTimer::singleShot(400, this, [this]() {
        if (auto *mb = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            if (auto *yes = mb->button(QMessageBox::Yes))
                yes->click();
        }
    });
    if (m_btnReset)
        m_btnReset->click();
    onAccepted();
    accept();
}

// ===========================================================================
// 窗口铬件：圆角底 + 边框；标题栏拖拽
// ===========================================================================

void SettingsDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (m_centered)
        return;
    m_centered = true;
    QWidget *host = parentWidget() ? parentWidget()->window() : nullptr;
    if (!host)
        return;
    const QRect g = host->frameGeometry();
    move(g.center() - QPoint(width() / 2, height() / 2));
}

void SettingsDialog::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, UiStyle::RadiusDialog, UiStyle::RadiusDialog);
    p.fillPath(path, UiStyle::dialogBg());
    p.setPen(QPen(UiStyle::color(UiStyle::pick("#4a4c51", "#c8c9cb")), 1));
    p.drawPath(path);
}

void SettingsDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && event->position().y() < kTitleBarH) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        return;
    }
    QDialog::mousePressEvent(event);
}

void SettingsDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton))
        move(event->globalPosition().toPoint() - m_dragOffset);
    QDialog::mouseMoveEvent(event);
}

void SettingsDialog::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    QDialog::mouseReleaseEvent(event);
}

void SettingsDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        onAccepted();
        accept();
        return;
    }
    QDialog::keyPressEvent(event); // ESC -> reject() -> 回滚
}

void SettingsDialog::reject()
{
    restoreSnapshot();
    QDialog::reject();
}
