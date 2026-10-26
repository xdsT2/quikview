#include "SettingsDialog.h"

#include "DirectoryLoader.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyleOptionButton>
#include <QStylePainter>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include <cmath>
#include <shobjidl.h>

#include "Config.h"
#include "UiStyle.h"

namespace {

// ---------------------------------------------------------------------------
// 尺寸（对齐原型 .settings：960x720 / 标题 43 / 导航 204 / footer 62 / 内容内边距 25,28）
// ---------------------------------------------------------------------------
constexpr int kDlgW = 940;
constexpr int kDlgH = 720;
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
// 颜色选择按钮：原型「选择…  #202020」。点开取色器，选完立即写 config 并刷新文案。
// （取消设置时由 restoreSnapshot 统一回滚）
// ---------------------------------------------------------------------------
class ColorPickButton : public QPushButton
{
public:
    ColorPickButton(const char *key, const QString &def, QWidget *parent = nullptr)
        : QPushButton(parent), m_key(key), m_def(def)
    {
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(kComboH);
        setMinimumWidth(150);
        refresh();
        connect(this, &QPushButton::clicked, this, [this]() {
            const QColor c = QColorDialog::getColor(QColor(sGet(m_key, m_def)), this,
                                                    tr("选择颜色"));
            if (c.isValid()) {
                sSet(m_key, c.name());
                refresh();
            }
        });
    }

    void refresh()
    {
        setText(tr("选择…   %1").arg(QColor(sGet(m_key, m_def)).name().toUpper()));
    }

private:
    const char *m_key;
    QString m_def;
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
// 颜色跟随按钮状态（选中=强调青 / 悬停=白 / 常态 #bfc3c8），与文字间距 9px。
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
    NavItemButton(const QString &text, NavGlyph glyph, QWidget *parent = nullptr)
        : QPushButton(text, parent), m_glyph(glyph)
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

        QColor color(0xbf, 0xc3, 0xc8);
        if (isChecked())
            color = UiStyle::accent();
        else if (underMouse())
            color = QColor(0xff, 0xff, 0xff);

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

// 追加的几何/局部样式：只用 UiStyle 令牌与原型色，不引入新色板。
QString extraQss()
{
    return QStringLiteral(R"(
QDialog { background: transparent; }
QFrame#dlgTitleBar { border-top-left-radius: 10px; border-top-right-radius: 10px; }
QFrame#dlgFooter { border-bottom-left-radius: 10px; border-bottom-right-radius: 10px; }

QFrame#formatGroup { background: #222427; border: 1px solid rgba(255,255,255,0.085);
    border-radius: 7px; }
QFrame#keyTable { border: 1px solid rgba(255,255,255,0.085); border-radius: 7px; }
QWidget#keyHead { background: #292b2f; }
QLabel#keyHeadCell { color: #9ca2aa; font-size: 11px; }

QPushButton#miniBtn { min-height: 18px; max-height: 22px; padding: 0 9px; font-size: 10px;
    border-radius: 5px; }
QComboBox#flatCombo { background: transparent; border: 0; color: #d5d7da; font-size: 11px;
    min-height: 20px; padding: 0 4px 0 6px; }
QComboBox#flatCombo:hover { color: #ffffff; }
QComboBox#flatCombo::drop-down { width: 0; border: 0; }
QComboBox#flatCombo::down-arrow { width: 0; height: 0; }

QLabel#valueText { color: #d5d7da; font-size: 11px; }
QPlainTextEdit#credits { background: #1e2023; border: 1px solid rgba(255,255,255,0.085);
    border-radius: 6px; color: #b9bec5; font-size: 11px; padding: 6px; }

/* 置灰（尚未实现）项的统一不可用态：文字压暗、底座与边框退到背景层，
   与可交互项一眼可分。UiStyle::dialogQss() 只给了 indicator:disabled，
   没有文字色，直接 setEnabled(false) 时文字仍与正常项同亮度、看不出差别，
   所以这里补一套。明暗两套主题下设置页底色都由 paintEvent 固定为 #232427，
   灰字 #6f747a 对比度约 3.2:1，可读且明确「不可点」。 */
QCheckBox:disabled { color: #6f747a; }
QLabel:disabled { color: #6f747a; }
QComboBox:disabled { color: #6f747a; background: #26282b; border-color: #3a3c40; }
QSpinBox:disabled { color: #6f747a; background: #26282b; border-color: #3a3c40; }
QLineEdit:disabled { color: #6f747a; background: #26282b; border-color: #3a3c40; }
QPushButton:disabled { color: #6f747a; border-color: #3a3c40; background: #292b2f; }
)");
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
    buildGeneralPage(addPage(tr("一般"), tr("窗口行为、通知与到达首尾时的操作。")));
    buildViewPage(addPage(tr("浏览"), tr("背景颜色、边框颜色与侧边箭头等浏览显示设置。")));
    buildImageProcPage(addPage(tr("图像处理"), tr("插值算法与透明/翻页设置；多数项尚未实现，已置灰。")));
    buildAssocPage(addPage(tr("格式关联"), tr("选择由 QuikView 打开的图片格式。")));
    buildKeyboardPage(addPage(tr("键盘"), tr("自定义浏览、编辑与窗口操作快捷键。")));
    buildMousePage(addPage(tr("鼠标"), tr("设置鼠标按键、滚轮与双击行为。")));
    buildBookmarkPage(addPage(tr("书签"), tr("管理书签保存与共享。")));
    buildContextMenuPage(addPage(tr("上下文菜单"), tr("设置资源管理器中的 QuikView 功能入口。")));
    buildEtcPage(addPage(tr("杂项"), tr("图片文件夹、外部编辑器与 RAW 文件行为。")));
    buildLangPage(addPage(tr("语言"), tr("选择界面语言并管理语言文件。")));

    // 页面内容不足一屏时，多余空间落在末尾弹簧上（否则各 section 会被均匀拉开）
    for (QWidget *p : m_pages) {
        if (auto *pv = qobject_cast<QVBoxLayout *>(p->layout()))
            pv->addStretch(1);
    }

    // 导航底部：分隔 + 关于
    m_navLayout->addStretch(1);
    auto *navDivider = new QFrame(nav);
    navDivider->setObjectName(QStringLiteral("navDivider"));
    navDivider->setFixedHeight(1);
    m_navLayout->addSpacing(9);
    m_navLayout->addWidget(navDivider);
    m_navLayout->addSpacing(9);
    auto *btnAbout = new NavItemButton(tr("关于 QuikView"), NavGlyph::Info, nav);
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
    fl->setContentsMargins(18, 0, 18, 0);
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
    addCheck(sec, tr("捕捉窗口"), "capture_window", false, true);
    addCheck(sec, tr("不保存浏览过的文件夹路径"), "remember_browsed_folders", true, true);
    addCheck(sec, tr("不允许多个实例"), "allow_multiple_instances", true, false);
    addCheck(sec, tr("删除文件时确认"), "delete_confirm", false, true);
    addCheck(sec, tr("在边角显示通知信息，而非中心"), "notify_corner", false, true);
    addCheck(sec, tr("总在最前 (Ctrl+A)"), "always_on_top", false, false);
    addCheck(sec, tr("按 ESC 退出"), "esc_exit", false, true);
    // 「检查更新 / 检查小版本更新」已删：无联网能力（不依赖 Qt6Network），
    // Config::checkUpdates()/checkMinorUpdates() 除定义外零调用点，勾了也不会有任何事发生。

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
    // 本页除「不显示侧边箭头」外，其余键除本文件外全仓零消费（
    // grep -rn '"view/' src/ | grep -v SettingsDialog 无命中），按 ICC 模式灰显。
    // 值仍写回 config（向后兼容历史配置），但视觉上必须是不可用态。
    QWidget *sec = beginSection(page, tr("浏览设置"));
    addCheckUnavailable(sec, tr("显示主窗口"), "view/show_main_window", false, true);
    addCheckUnavailable(sec, tr("顶端菜单隐藏时显示文件名 (Shift+TAB)"),
                        "view/overlay_filename", false, false);
    addCheckUnavailable(sec, tr("保存缩放状态"), "view/save_zoom_state", false, false);
    addCheckUnavailable(sec, tr("不显示菜单栏"), "view/show_menu_bar", true, true);
    addCheckUnavailable(sec, tr("不显示控制栏"), "view/show_control_bar", true, true);
    // 唯一真开关：本轮接通的 ImageCanvas::setSideArrowsEnabled()。
    addCheck(sec, tr("不显示侧边箭头"), "view/show_side_arrow", true, true);
    addCheckUnavailable(sec, tr("不显示动画信息控制"), "view/show_anim_control", true, true);
    addCheckUnavailable(sec, tr("不显示缩放信息控制"), "view/show_zoom_control", true, true);
    addCheckUnavailable(sec, tr("不显示鼠标悬浮工具提示"), "view/show_tooltip", true, true);
    addCheckUnavailable(sec, tr("用小字体显示 EXIF / 文件信息"),
                        "view/exif_small_font", false, false);
    addHint(sec, unavailableTip());

    sec = beginSection(page, tr("背景"));
    // 背景图片（铺图）未实现：画布只画纯色，路径与两个按钮一并灰显。
    addCheckUnavailable(sec, tr("更改默认背景图片"), "view/bkgnd_image", false, false);

    QHBoxLayout *pathRow = addRow(sec, QString());
    m_bkgndPath = new QLineEdit;
    m_bkgndPath->setPlaceholderText(tr("未设置背景图片"));
    m_bkgndPath->setFixedHeight(kInputH);
    pathRow->addWidget(m_bkgndPath, 1);
    auto *btnBrowse = addButton(nullptr, tr("浏览…"));
    auto *btnOpenFolder = addButton(nullptr, tr("打开文件夹"));
    pathRow->addWidget(btnBrowse);
    pathRow->addWidget(btnOpenFolder);
    connect(btnBrowse, &QPushButton::clicked, this, [this]() {
        const QString f = QFileDialog::getOpenFileName(
            this, tr("选择图片"), m_bkgndPath->text(), DirectoryLoader::imageFilter());
        if (!f.isEmpty())
            m_bkgndPath->setText(f);
    });
    connect(btnOpenFolder, &QPushButton::clicked, this, [this]() {
        const QString d = m_bkgndPath->text().trimmed();
        if (!d.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(d).absolutePath()));
    });
    markUnavailable(pathRow->parentWidget());   // 整行：输入框 + 浏览… + 打开文件夹
    addHint(sec, unavailableTip());

    // 颜色两项是真开关：ImageCanvas::setBackgroundColor()/setBorderColor()，
    // 保存后 MainWindow::applyRuntimeSettings() 立即重刷当前画布。
    auto *bgBtn = new ColorPickButton("view/bkgnd_color", QStringLiteral("#202020"));
    m_bkgndColorBtn = bgBtn;
    QHBoxLayout *bgRow = addRow(sec, tr("背景颜色"));
    bgRow->addWidget(bgBtn);
    bgRow->addStretch(1);

    auto *bdBtn = new ColorPickButton("view/border_color", QStringLiteral("#000000"));
    m_borderColorBtn = bdBtn;
    QHBoxLayout *bdRow = addRow(sec, tr("边框颜色"));
    bdRow->addWidget(bdBtn);
    bdRow->addStretch(1);
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
    addCheckUnavailable(sec, tr("使用强锐化滤镜"), "imageproc/strong_sharpen", false, false);
    addCheckUnavailable(sec, tr("缓存下一张图片"), "imageproc/cache_image", false, true);

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
    // 缓存上限目前是 ImageCache 里的硬编码字节数，读不到 imageproc/cache_size_mb，
    // 所以「缓存下一张图片」与「缓存大小」一起灰显（接配置是下一步的事）。
    markUnavailable(cacheRow->parentWidget());

    addCheckUnavailable(sec, tr("图片大于窗口时显示图片顶部"),
                        "imageproc/align_top_when_large", false, true);
    addCheckUnavailable(sec, tr("先显示低品质图片以加速渲染"), "imageproc/preview_mode", false, true);
    addHint(sec, unavailableTip());

    // ICC 色彩管理尚未实现（无任何消费方），灰显占位，等解码层接入后再开放。
    auto *iccImage = addCheck(sec, tr("使用图片文件的 ICC 设置"), "imageproc/image_icc", false, true);
    auto *iccMonitor = addCheck(sec, tr("使用监视器的 ICC 设置"), "imageproc/monitor_icc", false, false);
    iccImage->setEnabled(false);
    iccMonitor->setEnabled(false);
    iccImage->setToolTip(tr("ICC 色彩管理尚未实现"));
    iccMonitor->setToolTip(tr("ICC 色彩管理尚未实现"));
    addHint(sec, tr("ICC 色彩管理尚未实现，以上两项暂不可用。"));

    sec = beginSection(page, tr("透明与翻页"));
    addComboRowUnavailable(sec, tr("为 GIF 图片使用透明背景"), "imageproc/gif_transparent",
                           QStringLiteral("0"),
                           { { "0", tr("默认GIF调色板") }, { "1", tr("白色背景") },
                             { "2", tr("黑色背景") }, { "3", tr("自定义颜色") } }, 160);
    addComboRowUnavailable(sec, tr("为32位图片使用透明背景"), "imageproc/img32_transparent",
                           QStringLiteral("0"),
                           { { "0", tr("网格背景") }, { "1", tr("白色背景") },
                             { "2", tr("黑色背景") }, { "3", tr("深色网格") } }, 160);
    // 标签按实测语义改名：Canvas 的 InterpBilinear 走 FastTransformation（最近邻），
    // 中间档是 GPU 平滑（双线性量级），第三档才是真 Lanczos3 重采样。
    addComboRow(sec, tr("插值算法（裁剪或减淡）"), "imageproc/interpolate", QStringLiteral("0"),
                { { "0", tr("最近邻 (快)") }, { "1", tr("平滑 (双线性)") },
                  { "2", tr("Lanczos (锐利)") } }, 160);
    addComboRowUnavailable(sec, tr("翻页效果"), "imageproc/pageturn_fx", QStringLiteral("0"),
                           { { "0", tr("无") }, { "1", tr("淡入淡出") },
                             { "2", tr("Alpha 滑动") }, { "3", tr("滑动") } }, 160);
    addHint(sec, unavailableTip());
}

// ===========================================================================
// 4. 格式关联：说明 + 两个系统按钮 + 四组格式勾选网格
// ===========================================================================

void SettingsDialog::buildAssocPage(QWidget *page)
{
    QWidget *sec = beginSection(page, tr("格式关联"));
    addHint(sec, tr("选择需要由 QuikView 打开的文件格式；系统关联方式可在下方设置。"));

    QHBoxLayout *btnRow = addRow(sec, QString());
    auto *btnAssoc = addButton(nullptr, tr("格式关联"));
    auto *btnClassic = addButton(nullptr, tr("经典方法"));
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

    // 2x2 组网格
    auto *gridHost = new QWidget(sec);
    auto *grid = new QGridLayout(gridHost);
    grid->setContentsMargins(0, 6, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);
    if (auto *v = qobject_cast<QVBoxLayout *>(sec->layout()))
        v->addWidget(gridHost);

    // 格式清单唯一来源：DirectoryLoader::imageGroups()。
    // 这里不再手写第三份——旧代码单独维护过一份，列了 jxl / zip / rar / 7z
    // 这些 QuikView 根本打不开的格式，用户勾选关联后双击直接失败。
    const QList<DirectoryLoader::Group> formatGroups = DirectoryLoader::imageGroups();

    for (int g = 0; g < formatGroups.size(); ++g) {
        auto *group = new QFrame(gridHost);
        group->setObjectName(QStringLiteral("formatGroup"));
        auto *gl = new QVBoxLayout(group);
        gl->setContentsMargins(11, 10, 11, 11);
        gl->setSpacing(7);

        auto *head = new QWidget(group);
        auto *hl = new QHBoxLayout(head);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(5);
        auto *name = new QLabel(formatGroups.at(g).label, head);
        name->setObjectName(QStringLiteral("sectionTitle"));
        hl->addWidget(name);
        hl->addStretch(1);
        auto *btnAll = addMiniButton(head, tr("全选"));
        auto *btnNone = addMiniButton(head, tr("取消"));
        hl->addWidget(btnAll);
        hl->addWidget(btnNone);
        gl->addWidget(head);

        auto *checksHost = new QWidget(group);
        auto *cg = new QGridLayout(checksHost);
        cg->setContentsMargins(0, 0, 0, 0);
        cg->setHorizontalSpacing(5);
        cg->setVerticalSpacing(0);
        QList<QCheckBox *> boxes;
        const QStringList exts = formatGroups.at(g).exts;
        for (int i = 0; i < exts.size(); ++i) {
            const QString ext = exts.at(i);
            auto *cb = new QCheckBox(ext, checksHost);
            cb->setCursor(Qt::PointingHandCursor);
            cb->setMinimumHeight(22);
            cb->setStyleSheet(QStringLiteral("QCheckBox{font-size:11px;}"));
            const QByteArray key = (QStringLiteral("assoc/ext_") + ext).toLatin1();
            cb->setChecked(bGet(key.constData(), true));
            connect(cb, &QCheckBox::toggled, cb, [key](bool on) {
                bSet(key.constData(), on);
            });
            cg->addWidget(cb, i / 4, i % 4);
            boxes.append(cb);
            m_assocChecks.append(cb);
        }
        gl->addWidget(checksHost);

        connect(btnAll, &QPushButton::clicked, group, [boxes]() {
            for (QCheckBox *cb : boxes)
                cb->setChecked(true);
        });
        connect(btnNone, &QPushButton::clicked, group, [boxes]() {
            for (QCheckBox *cb : boxes)
                cb->setChecked(false);
        });

        grid->addWidget(group, g / 2, g % 2);
    }
}

// ===========================================================================
// 5. 键盘：勾选 | 热键 | 操作 表格（14 行）
// ===========================================================================

void SettingsDialog::buildKeyboardPage(QWidget *page)
{
    auto *pageLay = qobject_cast<QVBoxLayout *>(page->layout());
    if (!pageLay)
        return;

    // 快捷键自定义尚未接通：整表灰掉并注明，避免「能改但不生效」的假象。
    // 勾选/热键/下拉仍保留为占位，等 QAction 统一注册表做完再开放。
    auto *hint = new QLabel(tr("暂不支持自定义：本轮快捷键为固定绑定，此处仅作展示。"), page);
    hint->setObjectName(QStringLiteral("pageHint"));
    hint->setStyleSheet(QStringLiteral("QLabel#pageHint{color:#9aa0a6;font-size:11px;}"));
    hint->setWordWrap(true);
    pageLay->addWidget(hint);

    auto *table = new QFrame(page);
    table->setObjectName(QStringLiteral("keyTable"));
    table->setEnabled(false);            // 灰显占位，不接受编辑
    table->setToolTip(tr("暂不支持自定义快捷键"));
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

    struct KeyDef { const char *action; const char *hotkey; const char *label; };
    static const KeyDef defs[] = {
        { "open",        "Ctrl+O",      QT_TR_NOOP("打开") },
        { "openfolder",  "",            QT_TR_NOOP("打开文件夹") },
        { "next",        "\u2192",      QT_TR_NOOP("翻下一张") },
        { "prev",        "\u2190",      QT_TR_NOOP("翻上一张") },
        { "next_frame",  "Ctrl+\u2192", QT_TR_NOOP("下一帧") },
        { "prev_frame",  "Ctrl+\u2190", QT_TR_NOOP("上一帧") },
        { "zoom_in",     "+",           QT_TR_NOOP("放大") },
        { "zoom_out",    "-",           QT_TR_NOOP("缩小") },
        { "actual_size", "1",           QT_TR_NOOP("实际大小") },
        { "fit",         "0",           QT_TR_NOOP("适配窗口") },
        { "slideshow",   "Space",       QT_TR_NOOP("幻灯片") },
        { "crop",        "X",           QT_TR_NOOP("裁剪") },
        { "favorite",    "F2",          QT_TR_NOOP("收藏") },
        { "del",         "Del",         QT_TR_NOOP("删除") },
    };

    const auto opts = actionOptions();
    for (int i = 0; i < int(sizeof(defs) / sizeof(defs[0])); ++i) {
        if (i > 0) {
            auto *line = new QFrame(table);
            line->setObjectName(QStringLiteral("hLine"));
            line->setFixedHeight(1);
            tl->addWidget(line);
        }

        const KeyDef &def = defs[i];
        auto *row = new QWidget(table);
        row->setFixedHeight(32);
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
        auto *edit = new QLineEdit(editHost);
        edit->setFixedHeight(26);
        edit->setPlaceholderText(tr("未设置"));
        edit->setStyleSheet(QStringLiteral("QLineEdit{font-size:11px;padding:0 6px;}"));
        el->addWidget(edit);
        rl->addWidget(editHost);

        auto *combo = new QComboBox(row);
        combo->setObjectName(QStringLiteral("flatCombo"));
        for (const auto &opt : opts)
            combo->addItem(opt.second, opt.first);
        combo->setCursor(Qt::PointingHandCursor);
        combo->setFixedHeight(26);
        rl->addWidget(combo, 1);

        m_keyRows.append({ chk, edit, combo, QString::fromUtf8(def.action),
                           QString::fromUtf8(def.hotkey) });
        tl->addWidget(row);
    }

    pageLay->addWidget(table);
}

// ===========================================================================
// 6. 鼠标：两条设定 + 两列「按键 → 动作」
// ===========================================================================

void SettingsDialog::buildMousePage(QWidget *page)
{
    QWidget *sec = beginSection(page, tr("鼠标设定"));
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

    struct BtnDef { const char *key; const char *label; const char *def; };
    static const BtnDef btns[] = {
        { "mouse/btn_left",   QT_TR_NOOP("鼠标左键"),  "none" },
        { "mouse/btn_middle", QT_TR_NOOP("鼠标中键"),  "pan" },
        { "mouse/btn_right",  QT_TR_NOOP("鼠标右键"),  "none" },
        { "mouse/btn_x",      QT_TR_NOOP("X 键"),       "none" },
        { "mouse/btn_y",      QT_TR_NOOP("Y 键"),       "none" },
        { "mouse/wheel_up",   QT_TR_NOOP("滚轮向上"),  "zoom_in" },
        { "mouse/wheel_down", QT_TR_NOOP("滚轮向下"),  "zoom_out" },
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

        m_mouseRows.append({ combo, QString::fromUtf8(b.key), QString::fromUtf8(b.def) });
        grid->addWidget(row, i % 4, i / 4);
    }
}

// ===========================================================================
// 7. 书签
// ===========================================================================

void SettingsDialog::buildBookmarkPage(QWidget *page)
{
    QWidget *sec = beginSection(page, tr("书签设置"));
    addCheck(sec, tr("自动保存当前图片路径为书签"), "bookmark/auto_bookmark", false, false);
    addCheck(sec, tr("多窗口共享设置 / 书签"), "bookmark/share_settings", false, true);
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
}

// ===========================================================================
// 8. 上下文菜单
// ===========================================================================

void SettingsDialog::buildContextMenuPage(QWidget *page)
{
    QWidget *sec = beginSection(page, tr("上下文菜单设定"));
    addCheck(sec, tr("在资源管理器中使用上下文菜单"), "ctxmenu/use_shell_menu", false, true);
    addCheck(sec, tr("用QuikView转换图片"), "ctxmenu/transform_dlg", false, true);
    addCheck(sec, tr("用QuikView浏览图片"), "ctxmenu/browse_image", false, true);
    addCheck(sec, tr("上下文菜单预览"), "ctxmenu/preview_image", false, false);
    // 该项依赖预览功能，原版置灰
    QCheckBox *infoChk = addCheck(sec, tr("显示图片信息"), "ctxmenu/show_image_info",
                                  false, true);
    infoChk->setEnabled(false);
    addCheck(sec, tr("显示软件图标"), "ctxmenu/show_icon", false, true);

    sec = beginSection(page, tr("Thumbnail preview in Explorer"));
    addCheck(sec, tr("WebP"), "ctxmenu/thumb_webp", false, true);
    addCheck(sec, tr("PSD"), "ctxmenu/thumb_psd", false, false);
    addCheck(sec, tr("PSB"), "ctxmenu/thumb_psb", false, false);

    sec = beginSection(page, tr("说明"));
    addHint(sec, tr("这些选项对应系统资源管理器中的 QuikView 菜单，"
                    "不是应用内图片右键菜单的功能开关。"));
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
    QHBoxLayout *row = addRow(sec, QString());
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
    addCheck(sec, tr("翻页时播放音效"), "etc/pageturn_wav", false, false);
    addCheck(sec, tr("不打开 RAW 图片文件"), "etc/no_raw_load", false, false);
    addCheck(sec, tr("打开 JPEG 文件时，不打开同名 RAW 文件"), "etc/skip_raw_when_jpg", false, true);
    addCheck(sec, tr("打开 RAW 图片文件时，显示已有缩略图"), "etc/raw_thumbnail", false, true);
    addCheck(sec, tr("根据图片大小调节窗口大小"), "etc/fit_window_to_image", false, true);
    addCheck(sec, tr("运行 QuikView 时禁用 Windows 屏幕保护程序"), "etc/disable_screensaver",
             false, false);
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

    auto *link1 = new LinkLabel(tr("[下载更多语言…]"), []() {
        QMessageBox::information(nullptr, QObject::tr("语言"),
                                 QObject::tr("更多语言包请访问 QuikView 项目主页下载。"));
    }, sec);
    auto *link2 = new LinkLabel(tr("[打开语言文件夹…]"), []() {
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

    sec = beginSection(page, tr("翻译"));
    auto *credits = new QPlainTextEdit(sec);
    credits->setObjectName(QStringLiteral("credits"));
    credits->setReadOnly(true);
    credits->setFixedHeight(196);
    credits->setPlainText(QStringLiteral(
        "QuikView translations\n"
        "--------------------------------------------------\n"
        "中文(简体) (zh_CN) ........................ QuikView\n"
        "中文(繁體) (zh_TW) ........................ QuikView\n"
        "English (en_US) ........................... QuikView\n"
        "한국어 (ko_KR) ............................. QuikView\n"
        "日本語 (ja_JP) ............................. QuikView\n"
        "Français (fr_FR) .......................... QuikView\n"
        "Deutsch (de_DE) ........................... QuikView\n"
        "Español (es_ES) ........................... QuikView\n"
        "Русский (ru_RU) ........................... QuikView\n"));
    if (auto *v = qobject_cast<QVBoxLayout *>(sec->layout()))
        v->addWidget(credits);
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
    if (m_bkgndPath)
        m_bkgndPath->setText(sGet("view/bkgnd_image_path"));
    for (const KeyRow &r : m_keyRows) {
        r.cb->setChecked(bGet(QStringLiteral("keyboard/%1_enabled").arg(r.action).toLatin1().constData(), true));
        r.edit->setText(sGet(QStringLiteral("keyboard/%1_key").arg(r.action).toLatin1().constData(),
                             r.defHotkey));
        const int idx = r.combo->findData(
            sGet(QStringLiteral("keyboard/%1_action").arg(r.action).toLatin1().constData(), r.action));
        r.combo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    for (const MouseRow &r : m_mouseRows) {
        const int idx = r.combo->findData(sGet(r.key.toLatin1().constData(), r.def));
        r.combo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    if (m_maxBookmarks) {
        const int idx = m_maxBookmarks->findData(iGet("bookmark/max_bookmarks", 30));
        m_maxBookmarks->setCurrentIndex(idx >= 0 ? idx : 2);
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
    for (const ChkBind &b : m_chkBinds) {
        const bool v = b.cb->isChecked();
        bSet(b.key, b.inverted ? !v : v);
    }
    for (const ComboBind &b : m_comboBinds)
        sSet(b.key, b.combo->currentData().toString());

    if (m_cacheSize)
        iSet("imageproc/cache_size_mb", m_cacheSize->value());
    if (m_bkgndPath)
        sSet("view/bkgnd_image_path", m_bkgndPath->text());
    for (const KeyRow &r : m_keyRows) {
        bSet(QStringLiteral("keyboard/%1_enabled").arg(r.action).toLatin1().constData(),
             r.cb->isChecked());
        sSet(QStringLiteral("keyboard/%1_key").arg(r.action).toLatin1().constData(),
             r.edit->text());
        sSet(QStringLiteral("keyboard/%1_action").arg(r.action).toLatin1().constData(),
             r.combo->currentData().toString());
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

    // 直接读写 config 的控件也要跟着回到默认值
    for (QCheckBox *cb : m_assocChecks) {
        const QByteArray key = (QStringLiteral("assoc/ext_")
                                + cb->text()).toLatin1();
        cb->setChecked(bGet(key.constData(), true));
    }
    if (auto *bg = static_cast<ColorPickButton *>(m_bkgndColorBtn))
        bg->refresh();
    if (auto *bd = static_cast<ColorPickButton *>(m_borderColorBtn))
        bd->refresh();
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
    p.fillPath(path, QColor(0x23, 0x24, 0x27));
    p.setPen(QPen(QColor(0x4a, 0x4c, 0x51), 1));
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
