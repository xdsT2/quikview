#include "SettingsDialog.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <shobjidl.h>
#include <QFile>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QUrl>
#include <QWidget>
#include <functional>

#include "Config.h"

namespace {

// ---------------------------------------------------------------------------
// 布局常量（对照原版截图：870x575 白色无边框，左页签列 + 右内容区）
// ---------------------------------------------------------------------------
constexpr int kDlgW = 870;
constexpr int kDlgH = 575;
constexpr int kTabX = 20, kTabY0 = 55, kTabW = 130, kTabH = 30, kTabGap = 8;
constexpr int kResetY = 494, kOkY = 532;
constexpr int kPageX = 168, kPageY = 50, kPageW = 682, kPageH = 505;

// 内容行
constexpr int kRowX = 24;          // 复选框起始 x
constexpr int kChkY0 = 52;         // 首行 y（页头线 y=36 之下）
constexpr int kChkPitch = 36;      // 一般页行距
constexpr int kChkPitchS = 33;     // 紧凑行距（浏览/杂项/上下文菜单页）
constexpr int kLabelRight = 310;   // 组合行标签右对齐位置
constexpr int kComboX = 318;       // 组合行下拉起点
constexpr int kComboW = 290;       // 组合行下拉宽度

QFont hvFont(int size = 9, bool bold = false)
{
    QFont f(QStringLiteral("Microsoft YaHei UI"), size);
    f.setBold(bold);
    return f;
}

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
// 页签贴图：setting_tab_0.png（未选中：帧0 常态 / 帧1 悬停）/ setting_tab_1.png
// （选中：帧0 常态 / 帧2 按下）。4 帧横排，帧宽 = w/4。函数内 static 惰性构造，避免 QApplication
// 之前初始化 QPixmap 导致 Qt6 fail-fast。
// ---------------------------------------------------------------------------
QPixmap &tabNormal()  { static QPixmap pm; return pm; }
QPixmap &tabHover()   { static QPixmap pm; return pm; }
QPixmap &tabSel()     { static QPixmap pm; return pm; }
QPixmap &tabSelDown() { static QPixmap pm; return pm; }

void loadTabSkin()
{
    const QString dirs[] = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/../resources/skin"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../resources/skin"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/resources/skin"),
        QStringLiteral("resources/skin"),
    };
    for (const QString &dir : dirs) {
        QPixmap t0(dir + QStringLiteral("/setting_tab_0.png"));
        QPixmap t1(dir + QStringLiteral("/setting_tab_1.png"));
        if (!t0.isNull() && !t1.isNull() && t0.width() >= 90 && t1.width() >= 90) {
            const int fw = t0.width() / 4;
            tabNormal() = t0.copy(0, 0, fw, t0.height());
            tabHover() = t0.copy(fw, 0, fw, t0.height());
            const int fw1 = t1.width() / 4;
            tabSel() = t1.copy(0, 0, fw1, t1.height());
            tabSelDown() = t1.copy(fw1 * 2, 0, fw1, t1.height());
            return;
        }
    }
    // 找不到贴图：留空，绘制退化为纯色圆角块
}

// ---------------------------------------------------------------------------
// 左侧页签按钮：贴图拉伸填充，文字 #333 9pt 居中
// ---------------------------------------------------------------------------
class HvTabButton : public QAbstractButton
{
public:
    explicit HvTabButton(const QString &text, QWidget *parent = nullptr)
        : QAbstractButton(parent)
    {
        setText(text);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFont(hvFont());
    }

    QSize sizeHint() const override { return QSize(kTabW, kTabH); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setFont(font());
        const QRect r = rect();
        if (isChecked()) {
            if (!tabSel().isNull()) {
                p.drawPixmap(r, (isDown() && !tabSelDown().isNull()) ? tabSelDown() : tabSel());
            } else {
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setBrush(QColor(isDown() ? "#cfddf3" : "#dbe7f7"));
                p.setPen(QPen(QColor("#a8c4e8"), 1));
                p.drawRoundedRect(r.adjusted(0, 0, -1, -1), 3, 3);
                p.setRenderHint(QPainter::Antialiasing, false);
            }
        } else {
            const QPixmap &pm = underMouse() ? tabHover() : tabNormal();
            if (!pm.isNull()) {
                p.drawPixmap(r, pm);
            } else {
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setBrush(QColor("#fcf5d5"));
                p.setPen(QPen(QColor("#e5d9a8"), 1));
                p.drawRoundedRect(r.adjusted(0, 0, -1, -1), 3, 3);
                p.setRenderHint(QPainter::Antialiasing, false);
            }
        }
        p.setPen(QColor("#333333"));
        p.drawText(rect(), Qt::AlignCenter, text());
    }

    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }
};

// ---------------------------------------------------------------------------
// 底部按钮。奶黄款（重置/浏览.../打开/删除/预设等）：
//   奶黄底 #fdf6d5 / 边框 #e5d9a8 / 圆角 3。
// accent 款（确定/经典方法——原版默认按钮）：白底 + 蓝边 #1a70d6。
// ---------------------------------------------------------------------------
class HvButton : public QAbstractButton
{
public:
    HvButton(const QString &text, bool accent, QWidget *parent = nullptr)
        : QAbstractButton(parent)
        , m_accent(accent)
    {
        setText(text);
        setCursor(Qt::PointingHandCursor);
        setFont(hvFont());
    }

    QSize sizeHint() const override { return QSize(kTabW, kTabH); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setFont(font());
        const QRect r = rect().adjusted(0, 0, -1, -1);
        if (m_accent) {
            p.setBrush(QColor(isDown() ? "#e8f1fb" : "#ffffff"));
            p.setPen(QPen(QColor("#1a70d6"), 1));
            p.drawRect(r);
        } else {
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setBrush(QColor(isDown() ? "#f3e9bd" : "#fdf6d5"));
            p.setPen(QPen(QColor("#e5d9a8"), 1));
            p.drawRoundedRect(r, 3, 3);
            p.setRenderHint(QPainter::Antialiasing, false);
        }
        if (hasFocus() && !m_accent) {
            p.setPen(QPen(QColor("#1a70d6"), 1, Qt::DotLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r.adjusted(2, 2, -2, -2));
        }
        p.setPen(QColor("#333333"));
        p.drawText(rect(), Qt::AlignCenter, text());
    }

private:
    bool m_accent;
};

// ---------------------------------------------------------------------------
// 复选框：15px 方框，勾选蓝底 #1a70d6 白勾，文字 #333（Windows 10 风格，与原版一致）
// ---------------------------------------------------------------------------
class HvCheckBox : public QCheckBox
{
public:
    explicit HvCheckBox(const QString &text, QWidget *parent = nullptr)
        : QCheckBox(text, parent)
    {
        setFont(hvFont());
        setCursor(Qt::ArrowCursor);
    }

    QSize sizeHint() const override
    {
        return QSize(23 + fontMetrics().horizontalAdvance(text()), 20);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setFont(font());
        const int h = height();
        const QRect box(0, (h - 15) / 2, 15, 15);
        if (isEnabled()) {
            if (isChecked()) {
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor("#1a70d6"));
                p.drawRoundedRect(box, 2, 2);
                QPen tick(QColor("#ffffff"), 2);
                tick.setCapStyle(Qt::RoundCap);
                tick.setJoinStyle(Qt::RoundJoin);
                p.setPen(tick);
                const qreal x = box.left(), y = box.top();
                p.drawLine(QPointF(x + 15 * 0.22, y + 15 * 0.52), QPointF(x + 15 * 0.42, y + 15 * 0.72));
                p.drawLine(QPointF(x + 15 * 0.42, y + 15 * 0.72), QPointF(x + 15 * 0.78, y + 15 * 0.30));
                p.setRenderHint(QPainter::Antialiasing, false);
            } else {
                p.setBrush(QColor("#ffffff"));
                p.setPen(QPen(QColor(underMouse() ? "#707070" : "#adadad"), 1));
                p.drawRect(box.adjusted(0, 0, -1, -1));
            }
        } else {
            p.setBrush(QColor("#ffffff"));
            p.setPen(QPen(QColor("#d4d4d4"), 1));
            p.drawRect(box.adjusted(0, 0, -1, -1));
        }
        p.setPen(QColor(isEnabled() ? "#333333" : "#a0a0a0"));
        p.drawText(QRect(23, 0, width() - 23, h), Qt::AlignVCenter | Qt::AlignLeft, text());
    }

    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }
};

// ---------------------------------------------------------------------------
// 下拉框：白底 24px、1px #adadad 边框、自绘箭头、白色弹层
// ---------------------------------------------------------------------------
class HvComboBox : public QComboBox
{
public:
    explicit HvComboBox(QWidget *parent = nullptr)
        : QComboBox(parent)
    {
        setFont(hvFont());
        setCursor(Qt::ArrowCursor);
        auto *view = new QListView(this);
        view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        QPalette pal = view->palette();
        pal.setColor(QPalette::Base, QColor("#ffffff"));
        pal.setColor(QPalette::Window, QColor("#ffffff"));
        pal.setColor(QPalette::Text, QColor("#333333"));
        pal.setColor(QPalette::Highlight, QColor("#cde8fa"));
        pal.setColor(QPalette::HighlightedText, QColor("#333333"));
        view->setPalette(pal);
        setView(view);
    }

    QSize sizeHint() const override { return QSize(120, 24); }
    QSize minimumSizeHint() const override { return QSize(60, 24); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setFont(font());
        const QRect r = rect().adjusted(0, 0, -1, -1);
        p.fillRect(r, QColor("#ffffff"));
        p.setPen(QPen(QColor(isEnabled() ? (underMouse() ? "#707070" : "#adadad") : "#d4d4d4"), 1));
        p.drawRect(r);
        p.setPen(QColor(isEnabled() ? "#333333" : "#a0a0a0"));
        p.drawText(QRect(6, 0, width() - 30, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, currentText());
        if (isEnabled()) {
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(QColor("#666666"), 1.4));
            const int cx = width() - 14, cy = height() / 2;
            p.drawLine(cx - 3, cy - 2, cx, cy + 1);
            p.drawLine(cx, cy + 1, cx + 3, cy - 2);
            p.setRenderHint(QPainter::Antialiasing, false);
        }
    }

    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }

    void showPopup() override
    {
        QComboBox::showPopup();
        QWidget *w = view()->window();
        QPalette pal = w->palette();
        pal.setColor(QPalette::Window, QColor("#ffffff"));
        pal.setColor(QPalette::Base, QColor("#ffffff"));
        w->setPalette(pal);
        w->setAutoFillBackground(true);
    }
};

// ---------------------------------------------------------------------------
// 数字框：白底无边按钮、1px 边框
// ---------------------------------------------------------------------------
class HvSpinBox : public QSpinBox
{
public:
    explicit HvSpinBox(QWidget *parent = nullptr)
        : QSpinBox(parent)
    {
        setFont(hvFont());
        setButtonSymbols(QAbstractSpinBox::NoButtons);
        setFixedHeight(24);
        setAlignment(Qt::AlignLeft);
        // 页面是浅色皮肤：显式白底深字，避免深色主题的白色前景在浅底上隐形
        setStyleSheet(QStringLiteral(
            "QSpinBox{background:#ffffff;color:#333333;}"
            "QSpinBox QLineEdit{background:#ffffff;color:#333333;}"));
        if (QLineEdit *le = findChild<QLineEdit *>()) {
            le->setFrame(false);
            le->setFont(hvFont());
        }
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QSpinBox::paintEvent(event);
        QPainter p(this);
        p.setPen(QPen(QColor(underMouse() ? "#707070" : "#adadad"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
};

// ---------------------------------------------------------------------------
// 编辑框：白底 1px #adadad 边框（原版路径输入样式）
// ---------------------------------------------------------------------------
class HvLineEdit : public QLineEdit
{
public:
    explicit HvLineEdit(QWidget *parent = nullptr)
        : QLineEdit(parent)
    {
        setFont(hvFont());
        setFrame(false);
        setFixedHeight(24);
        setCursor(Qt::IBeamCursor);
        setStyleSheet(QStringLiteral(
            "QLineEdit{background:#ffffff;color:#333333;padding:0 4px;"
            "border:1px solid #adadad;selection-background-color:#cde8fa;}"));
    }
};

// ---------------------------------------------------------------------------
// 蓝色链接文字：[下载更多语言...]。匿名命名空间内不能用 Q_OBJECT 自定义信号，
// 点击回调走 std::function。
// ---------------------------------------------------------------------------
class HvLink : public QWidget
{
public:
    HvLink(const QString &text, std::function<void()> onClick, QWidget *parent = nullptr)
        : QWidget(parent), m_text(text), m_onClick(std::move(onClick))
    {
        setFont(hvFont());
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(20);
        setFixedWidth(QFontMetrics(font()).horizontalAdvance(text) + 4);
        setMouseTracking(true);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setFont(font());
        p.setPen(QColor(underMouse() ? "#2a6be0" : "#0a4bd0"));
        p.drawText(rect(), Qt::AlignVCenter | Qt::AlignLeft, m_text);
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
// 颜色选择按钮：显示当前颜色（原版"选择..."按钮即当前色预览），点击取色器
// ---------------------------------------------------------------------------
class HvColorButton : public QAbstractButton
{
public:
    HvColorButton(const char *key, const QString &def, QWidget *parent = nullptr)
        : QAbstractButton(parent), m_key(key), m_def(def)
    {
        setCursor(Qt::PointingHandCursor);
        setFixedSize(64, 20);
        setFont(hvFont());
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QColor c = QColor(sGet(m_key, m_def));
        p.fillRect(rect().adjusted(0, 0, -1, -1), c);
        p.setPen(QPen(QColor(underMouse() ? "#707070" : "#5a5a5a"), 1));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        p.setPen(QColor(c.red() * 299 + c.green() * 587 + c.blue() * 114 < 128000
                            ? "#e8e8e8" : "#333333"));
        p.drawText(rect(), Qt::AlignCenter, tr("选择..."));
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        const QColor c = QColorDialog::getColor(
            QColor(sGet(m_key, m_def)), parentWidget(), tr("选择颜色"));
        if (c.isValid())
            sSet(m_key, c.name());
        update();
    }

private:
    const char *m_key;
    QString m_def;
};

// ---------------------------------------------------------------------------
// 固定色块：18x18，点击直接应用
// ---------------------------------------------------------------------------
class HvSwatch : public QAbstractButton
{
public:
    HvSwatch(const QColor &color, const char *key, QWidget *parent = nullptr)
        : QAbstractButton(parent)
        , m_color(color)
        , m_key(key)
    {
        setFixedSize(18, 18);
        setCursor(Qt::PointingHandCursor);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect().adjusted(0, 0, -1, -1), m_color);
        p.setPen(QPen(QColor(underMouse() ? "#1a70d6" : "#8a8a8a"), 1));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton)
            sSet(m_key, m_color.name());
    }

private:
    QColor m_color;
    const char *m_key;
};

// ---------------------------------------------------------------------------
// 热键编辑框右侧的上下微调箭头（原版装饰，静态绘制）
// ---------------------------------------------------------------------------
class HvStepperHint : public QWidget
{
public:
    explicit HvStepperHint(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(12, 24);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor("#f4f4f4"));
        p.setPen(QPen(QColor("#adadad"), 1));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        p.drawLine(width() / 2, 0, width() / 2, height());
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor("#666666"), 1.2));
        const int cx = width() / 2;
        p.drawLine(cx - 3, 8, cx, 5);
        p.drawLine(cx, 5, cx + 3, 8);
        p.drawLine(cx - 3, height() - 8, cx, height() - 5);
        p.drawLine(cx, height() - 5, cx + 3, height() - 8);
        p.setRenderHint(QPainter::Antialiasing, false);
    }
};

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

} // namespace

// ===========================================================================
// 构造：骨架 + 逐页构建
// ===========================================================================

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    takeSnapshot();

    setWindowTitle(tr("设置"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setModal(true);
    setFixedSize(kDlgW, kDlgH);
    setMouseTracking(true);

    // 白底黑字（含 label / 输入控件继承）
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor("#ffffff"));
    pal.setColor(QPalette::Base, QColor("#ffffff"));
    pal.setColor(QPalette::Button, QColor("#ffffff"));
    pal.setColor(QPalette::WindowText, QColor("#333333"));
    pal.setColor(QPalette::Text, QColor("#333333"));
    pal.setColor(QPalette::ButtonText, QColor("#333333"));
    setPalette(pal);

    m_closeRect = QRect(kDlgW - 34, 6, 26, 26);

    loadTabSkin();

    buildGeneralPage(addPage(tr("一般"), tr("一般设定")));
    buildViewPage(addPage(tr("浏览"), tr("浏览设置")));
    buildImageProcPage(addPage(tr("图像处理"), tr("图像处理")));
    buildAssocPage(addPage(tr("格式关联"), tr("格式关联")));
    buildKeyboardPage(addPage(tr("键盘"), QString()));   // 键盘页无粗体页头，直接列头
    buildMousePage(addPage(tr("鼠标"), QString()));
    buildBookmarkPage(addPage(tr("书签"), tr("书签设置")));
    buildContextMenuPage(addPage(tr("上下文菜单"), tr("上下文菜单设定")));
    buildEtcPage(addPage(tr("杂项"), tr("杂项设定")));
    buildLangPage(addPage(tr("语言"), tr("语言设定")));

    // ---- 底部：重置（奶黄）/ 确定（白底蓝边，默认焦点）----
    m_btnReset = new HvButton(tr("重置"), false, this);
    m_btnReset->setGeometry(kTabX, kResetY, kTabW, kTabH);
    connect(m_btnReset, &QAbstractButton::clicked, this, &SettingsDialog::onReset);

    m_btnOk = new HvButton(tr("确定"), true, this);
    m_btnOk->setGeometry(kTabX, kOkY, kTabW, kTabH);
    m_btnOk->setFocus();
    connect(m_btnOk, &QAbstractButton::clicked, this, [this]() {
        onAccepted();
        accept();
    });

    loadFromConfig();
    // 默认显示第一页
    for (int i = 0; i < m_pages.size(); ++i) {
        m_pages.at(i)->setVisible(i == 0);
        m_tabs.at(i)->setChecked(i == 0);
    }
}

// ===========================================================================
// 控件工厂
// ===========================================================================

QWidget *SettingsDialog::addPage(const QString &title, const QString &header)
{
    auto *page = new QWidget(this);
    page->setGeometry(kPageX, kPageY, kPageW, kPageH);
    page->hide();

    if (!header.isEmpty()) {
        auto *head = new QLabel(header, page);
        head->setFont(hvFont(9, true));
        QPalette hp = head->palette();
        hp.setColor(QPalette::WindowText, QColor("#1a1a1a"));
        head->setPalette(hp);
        head->setGeometry(16, 8, 400, 22);
    }
    // 页头分隔线（键盘/鼠标页没有粗体页头，但保留分隔线，列头位于其上方由页面自绘）
    auto *line = new QLabel(page);
    line->setGeometry(16, 36, kPageW - 32, 1);
    line->setStyleSheet(QStringLiteral("background:#dcdcdc;"));

    const int index = m_pages.size();
    m_pages.append(page);

    auto *tab = new HvTabButton(title, this);
    tab->setGeometry(kTabX, kTabY0 + index * (kTabH + kTabGap), kTabW, kTabH);
    connect(tab, &QAbstractButton::clicked, this, [this, index]() {
        for (int i = 0; i < m_pages.size(); ++i) {
            m_pages.at(i)->setVisible(i == index);
            if (i < m_tabs.size()) {
                m_tabs.at(i)->setChecked(i == index);
                m_tabs.at(i)->update();
            }
        }
    });
    m_tabs.append(tab);
    return page;
}

void SettingsDialog::addChk(QWidget *page, int y, const QString &text, const char *key,
                            bool inverted, bool def, int indent)
{
    auto *chk = new HvCheckBox(text, page);
    chk->setGeometry(kRowX + indent, y,
                     23 + chk->fontMetrics().horizontalAdvance(text) + 12, 24);
    m_chkBinds.append({ chk, key, inverted, def });
}

QComboBox *SettingsDialog::addComboRow(QWidget *page, int y, const QString &label,
                                       const char *key, const QString &def,
                                       const QList<QPair<QString, QString>> &options,
                                       int labelRight, int comboX, int comboWidth)
{
    auto *lab = new QLabel(label, page);
    lab->setFont(hvFont());
    QPalette lp = lab->palette();
    lp.setColor(QPalette::WindowText, QColor("#333333"));
    lab->setPalette(lp);
    lab->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lab->setGeometry(kRowX, y, labelRight - kRowX, 24);

    auto *combo = new HvComboBox(page);
    for (const auto &opt : options)
        combo->addItem(opt.second, opt.first);
    combo->setGeometry(comboX, y, comboWidth, 24);
    m_comboBinds.append({ combo, key, def });
    return combo;
}

QLabel *SettingsDialog::addSectionBold(QWidget *page, int y, const QString &text)
{
    auto *lab = new QLabel(text, page);
    lab->setFont(hvFont(9, true));
    QPalette lp = lab->palette();
    lp.setColor(QPalette::WindowText, QColor("#1a1a1a"));
    lab->setPalette(lp);
    lab->setGeometry(kRowX, y, 300, 20);
    return lab;
}

QLabel *SettingsDialog::addNote(QWidget *page, int y, const QString &text)
{
    auto *lab = new QLabel(text, page);
    lab->setFont(hvFont());
    QPalette lp = lab->palette();
    lp.setColor(QPalette::WindowText, QColor("#333333"));
    lab->setPalette(lp);
    lab->setGeometry(kRowX, y, kPageW - kRowX * 2, 20);
    return lab;
}

void SettingsDialog::addHLine(QWidget *page, int y, int x, int w)
{
    auto *line = new QLabel(page);
    line->setGeometry(x, y, w > 0 ? w : kPageW - x * 2, 1);
    line->setStyleSheet(QStringLiteral("background:#dcdcdc;"));
}

void SettingsDialog::addColumnHeader(QWidget *page, int y,
                                     const QList<QPair<QString, int>> &cols)
{
    for (const auto &col : cols) {
        auto *lab = new QLabel(col.first, page);
        lab->setFont(hvFont());
        QPalette lp = lab->palette();
        lp.setColor(QPalette::WindowText, QColor("#333333"));
        lab->setPalette(lp);
        lab->setGeometry(col.second, y, 200, 20);
    }
    addHLine(page, y + 24, 16, kPageW - 32);
}

// ===========================================================================
// 1. 一般：9 复选框（末项缩进）+ 2 个到达首尾行为下拉
// ===========================================================================

void SettingsDialog::buildGeneralPage(QWidget *page)
{
    int y = kChkY0;
    addChk(page, y, tr("捕捉窗口"), "capture_window", false, true);                      y += kChkPitch;
    addChk(page, y, tr("不保存浏览过的文件夹路径"), "remember_browsed_folders", true, true); y += kChkPitch;
    addChk(page, y, tr("不允许多个实例"), "allow_multiple_instances", true, false);        y += kChkPitch;
    addChk(page, y, tr("删除文件时确认"), "delete_confirm", false, true);                  y += kChkPitch;
    addChk(page, y, tr("在边角显示通知信息，而非中心"), "notify_corner", false, true);      y += kChkPitch;
    addChk(page, y, tr("总在最前(Ctrl+A)"), "always_on_top", false, false);               y += kChkPitch;
    addChk(page, y, tr("按 ESC 退出"), "esc_exit", false, true);                          y += kChkPitch;
    addChk(page, y, tr("检查更新"), "check_updates", false, false);                        y += kChkPitch;
    addChk(page, y, tr("检查小版本更新"), "check_minor_updates", false, false, 24);        y += kChkPitch + 14;

    const auto opts = lastFileOptions();
    addComboRow(page, y, tr("到达首尾图片时执行："), "edge_action",
                QStringLiteral("stop"), opts);
    addComboRow(page, y + 38, tr("到达压缩包内首尾图片时执行："), "edge_action_archive",
                QStringLiteral("ask"), opts);
}

// ===========================================================================
// 2. 浏览：7 + 2 复选框 + 背景图片行 + 背景/边框颜色行
// ===========================================================================

void SettingsDialog::buildViewPage(QWidget *page)
{
    // 浏览页内容多（10 复选框 + 背景图片行 + 2 颜色行），用 31px 紧凑行距收进页面
    const int p = 31;
    int y = kChkY0;
    addChk(page, y, tr("显示主窗口"), "view/show_main_window", false, true);              y += p;
    addChk(page, y, tr("顶端菜单隐藏时显示文件名（Shift+TAB）"), "view/overlay_filename", false, false); y += p;
    addChk(page, y, tr("保存缩放状态"), "view/save_zoom_state", false, false);            y += p;
    addChk(page, y, tr("不显示菜单栏"), "view/show_menu_bar", true, true);                y += p;
    addChk(page, y, tr("不显示控制栏"), "view/show_control_bar", true, true);             y += p;
    addChk(page, y, tr("不显示两侧箭头"), "view/show_side_arrow", true, true);            y += p;
    addChk(page, y, tr("不显示动画信息控制"), "view/show_anim_control", true, true);      y += p;
    addChk(page, y, tr("不显示缩放信息控制"), "view/show_zoom_control", true, true);      y += p;
    y += 12;
    addChk(page, y, tr("不显示鼠标悬浮工具提示"), "view/show_tooltip", true, true);       y += p;
    addChk(page, y, tr("用小字体显示 EXIF/文件信息"), "view/exif_small_font", false, false); y += p + 10;

    // 更改默认背景图片 + 浏览/打开文件夹按钮
    const QString bkgndText = tr("更改默认背景图片");
    auto *bkgndChk = new HvCheckBox(bkgndText, page);
    const int chkW = 23 + bkgndChk->fontMetrics().horizontalAdvance(bkgndText) + 12;
    bkgndChk->setGeometry(kRowX, y, chkW, 24);
    m_chkBinds.append({ bkgndChk, "view/bkgnd_image", false, false });

    auto *btnBrowse = new HvButton(tr("浏览..."), false, page);
    btnBrowse->setGeometry(kRowX + chkW + 24, y - 1, 76, 26);
    auto *btnOpen = new HvButton(tr("打开文件夹"), false, page);
    btnOpen->setGeometry(kRowX + chkW + 108, y - 1, 88, 26);
    y += p + 4;

    m_bkgndPath = new HvLineEdit(page);
    m_bkgndPath->setGeometry(90, y, 320, 24);
    connect(btnBrowse, &QAbstractButton::clicked, this, [this]() {
        const QString f = QFileDialog::getOpenFileName(
            this, tr("选择图片"), m_bkgndPath->text(),
            tr("图像文件 (*)"));
        if (!f.isEmpty()) {
            m_bkgndPath->setText(f);
        }
    });
    connect(btnOpen, &QAbstractButton::clicked, this, [this]() {
        const QString d = m_bkgndPath->text().trimmed();
        if (!d.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(d).absolutePath()));
    });
    y += 32;

    // 背景/边框颜色行：标签 + 当前色预览按钮 + 12 个快速色块
    struct ColorRow { const char *label; const char *key; const char *def;
                      QList<QColor> swatches; };
    const QList<QColor> bgColors = {
        QColor("#000000"), QColor("#2b6bd7"), QColor("#7fb3e8"), QColor("#f5d327"),
        QColor("#f0a30a"), QColor("#22a04a"), QColor("#7fd4a0"), QColor("#e04b3a"),
        QColor("#f08bb4"), QColor("#8b5cf6"), QColor("#c4b5fd"), QColor("#9aa0a6"),
    };
    const QList<QColor> bdColors = {
        QColor("#5b6672"), QColor("#2b3a4a"), QColor("#7fb3e8"), QColor("#f08bb4"),
        QColor("#22a04a"), QColor("#f0a30a"), QColor("#e04b3a"), QColor("#d4699e"),
        QColor("#2e8b57"), QColor("#4169e1"), QColor("#8b5cf6"), QColor("#b0b0b0"),
    };
    const ColorRow rows[] = {
        { "背景颜色：", "view/bkgnd_color", "#202020", bgColors },
        { "边框颜色：", "view/border_color", "#000000", bdColors },
    };
    for (const ColorRow &row : rows) {
        auto *lab = new QLabel(tr(row.label), page);
        lab->setFont(hvFont());
        lab->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lab->setGeometry(kRowX, y, 106, 24);

        auto *colorBtn = new HvColorButton(row.key, QLatin1String(row.def), page);
        colorBtn->setGeometry(140, y + 2, 64, 20);
        if (QLatin1String(row.key) == QLatin1String("view/bkgnd_color"))
            m_bkgndColorBtn = colorBtn;
        else
            m_borderColorBtn = colorBtn;

        int sx = 212;
        for (const QColor &c : row.swatches) {
            auto *sw = new HvSwatch(c, row.key, page);
            sw->setGeometry(sx, y + 3, 18, 18);
            sx += 20;
        }
        y += 32;
    }
}

// ===========================================================================
// 3. 图像处理：3 复选框 + 缓存大小 + 4 复选框 + 4 下拉
// ===========================================================================

void SettingsDialog::buildImageProcPage(QWidget *page)
{
    int y = kChkY0;
    addChk(page, y, tr("根据 EXIF 信息自动旋转图片"), "imageproc/exif_rotation", false, true); y += kChkPitch;
    addChk(page, y, tr("使用强锐化滤镜"), "imageproc/strong_sharpen", false, false);           y += kChkPitch;
    addChk(page, y, tr("缓存下一张图片"), "imageproc/cache_image", false, true);               y += kChkPitch;

    // 缓存大小：[300] MB
    auto *lab = new QLabel(tr("缓存大小："), page);
    lab->setFont(hvFont());
    {
        QPalette lp = lab->palette();
        lp.setColor(QPalette::WindowText, QColor("#333333"));
        lab->setPalette(lp);
    }
    lab->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lab->setGeometry(kRowX, y, 250 - kRowX, 24);
    m_cacheSize = new HvSpinBox(page);
    m_cacheSize->setRange(10, 100000);
    m_cacheSize->setGeometry(258, y, 76, 24);
    auto *mb = new QLabel(tr("MB"), page);
    mb->setFont(hvFont());
    {
        QPalette mp = mb->palette();
        mp.setColor(QPalette::WindowText, QColor("#333333"));
        mb->setPalette(mp);
    }
    mb->setGeometry(344, y + 3, 30, 20);
    y += kChkPitch + 6;

    addChk(page, y, tr("图片大于窗口时显示图片顶部"), "imageproc/align_top_when_large", false, true); y += kChkPitch;
    addChk(page, y, tr("先显示低品质图片以加速渲染"), "imageproc/preview_mode", false, true);         y += kChkPitch;
    addChk(page, y, tr("使用图片文件的 ICC 设置"), "imageproc/image_icc", false, true);               y += kChkPitch;
    addChk(page, y, tr("使用监视器的 ICC 设置"), "imageproc/monitor_icc", false, false);              y += kChkPitch + 8;

    addComboRow(page, y, tr("为 GIF 图片使用透明背景"), "imageproc/gif_transparent",
                QStringLiteral("0"),
                { { "0", tr("默认GIF调色板") }, { "1", tr("白色背景") },
                  { "2", tr("黑色背景") }, { "3", tr("自定义颜色") } }, 310, 318, 310);
    y += 36;
    addComboRow(page, y, tr("为32位图片使用透明背景"), "imageproc/img32_transparent",
                QStringLiteral("0"),
                { { "0", tr("网格背景") }, { "1", tr("白色背景") },
                  { "2", tr("黑色背景") }, { "3", tr("深色网格") } }, 310, 318, 310);
    y += 36;
    addComboRow(page, y, tr("插值算法（裁剪或减淡）"), "imageproc/interpolate",
                QStringLiteral("0"),
                { { "0", tr("Bilinear 插值 (快)") }, { "1", tr("Bicubic 插值") },
                  { "2", tr("Lanczos 插值") } }, 310, 318, 310);
    y += 36;
    addComboRow(page, y, tr("翻页效果"), "imageproc/pageturn_fx",
                QStringLiteral("0"),
                { { "0", tr("无") }, { "1", tr("淡入淡出") },
                  { "2", tr("Alpha 滑动") }, { "3", tr("滑动") } }, 310, 318, 310);
}

// ===========================================================================
// 4. 格式关联：说明 + 两个按钮
// ===========================================================================

void SettingsDialog::buildAssocPage(QWidget *page)
{
    int y = kChkY0;
    addNote(page, y, tr("Windows 10 或更新版本，可按以下方式关联文件格式：")); y += 26;
    addNote(page, y, tr("1、单击\"格式关联\"按钮"));                           y += 24;
    addNote(page, y, tr("2、打开\"设置\"→\"应用\"→\"默认应用\"→\"照片查看器\"")); y += 24;
    addNote(page, y, tr("3、选择\"QuikView\""));                              y += 40;

    auto *btnAssoc = new HvButton(tr("格式关联"), false, page);
    btnAssoc->setGeometry(kRowX - 8, y, 110, 30);
    auto *btnClassic = new HvButton(tr("经典方法"), true, page);
    btnClassic->setGeometry(kRowX + 112, y, 110, 30);
    connect(btnAssoc, &QAbstractButton::clicked, this, []() {
        // 直接打开 Windows 设置 → 应用 → 默认应用
        ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps", nullptr, nullptr,
                      SW_SHOWNORMAL);
    });
    connect(btnClassic, &QAbstractButton::clicked, this, []() {
        // 经典方法：Windows 7+ 高级文件关联界面（选择"打开方式"程序的传统对话框）
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
}

// ===========================================================================
// 5. 键盘：勾选 | 热键 | 操作 表格（14 行，勾选/动作存 key，热键只读展示）
// ===========================================================================

void SettingsDialog::buildKeyboardPage(QWidget *page)
{
    addColumnHeader(page, 52, { { tr("勾选"), 30 }, { tr("热键"), 82 }, { tr("操作"), 226 } });

    struct KeyDef { const char *action; const char *hotkey; const char *label; };
    const KeyDef rows[] = {
        { "open",        "Ctrl+O",  QT_TR_NOOP("打开") },
        { "openfolder",  "",        QT_TR_NOOP("打开文件夹") },
        { "next",        "\u2192",       QT_TR_NOOP("翻下一张") },
        { "prev",        "\u2190",       QT_TR_NOOP("翻上一张") },
        { "next_frame",  "Ctrl+\u2192",  QT_TR_NOOP("下一帧") },
        { "prev_frame",  "Ctrl+\u2190",  QT_TR_NOOP("上一帧") },
        { "zoom_in",     "+",       QT_TR_NOOP("放大") },
        { "zoom_out",    "-",       QT_TR_NOOP("缩小") },
        { "actual_size", "1",       QT_TR_NOOP("实际大小") },
        { "fit",         "0",       QT_TR_NOOP("适配窗口") },
        { "slideshow",   "Space",   QT_TR_NOOP("幻灯片") },
        { "crop",        "X",       QT_TR_NOOP("裁剪") },
        { "favorite",    "F2",      QT_TR_NOOP("收藏") },
        { "del",         "Del",     QT_TR_NOOP("删除") },
    };

    const auto opts = actionOptions();
    int y = 84;
    for (const KeyDef &row : rows) {
        // 勾选：启用该键位
        auto *chk = new HvCheckBox(QString(), page);
        chk->setGeometry(36, y + 5, 15, 15);

        // 热键（只读展示 + 原版微调箭头装饰）
        auto *edit = new QLineEdit(page);
        edit->setReadOnly(true);
        edit->setFont(hvFont());
        edit->setFixedSize(110, 24);
        edit->setStyleSheet(QStringLiteral(
            "QLineEdit{background:#ffffff;color:#333333;padding:0 4px;"
            "border:1px solid #adadad;}"));
        edit->move(82, y);
        auto *stepper = new HvStepperHint(page);
        stepper->move(192, y);

        // 操作下拉
        auto *combo = new HvComboBox(page);
        for (const auto &opt : opts)
            combo->addItem(opt.second, opt.first);
        combo->setGeometry(226, y, 260, 24);

        m_keyRows.append({ chk, edit, combo, row.action, row.hotkey });
        y += 30;
    }
}

// ===========================================================================
// 6. 鼠标：设定 | 操作 表格，7 个按键行，左键行下缩进 2 个子选项
// ===========================================================================

void SettingsDialog::buildMousePage(QWidget *page)
{
    addColumnHeader(page, 52, { { tr("设定"), 30 }, { tr("操作"), 210 } });

    struct BtnDef { const char *key; const char *label; const char *def; };
    static const BtnDef btns[] = {
        { "mouse/btn_left",   QT_TR_NOOP("鼠标左键"),  "none" },
        { "mouse/btn_middle", QT_TR_NOOP("鼠标中键"),  "pan" },
        { "mouse/btn_right",  QT_TR_NOOP("鼠标右键"),  "none" },
        { "mouse/btn_x",      QT_TR_NOOP("X 键"),     "none" },
        { "mouse/btn_y",      QT_TR_NOOP("Y 键"),     "none" },
        { "mouse/wheel_up",   QT_TR_NOOP("滚轮向上"),  "zoom_in" },
        { "mouse/wheel_down", QT_TR_NOOP("滚轮向下"),  "zoom_out" },
    };

    const auto opts = actionOptions();
    int y = 84;
    for (int i = 0; i < 7; ++i) {
        const BtnDef &b = btns[i];
        addChk(page, y, tr(b.label), b.key, false, true, 0);

        auto *combo = new HvComboBox(page);
        for (const auto &opt : opts)
            combo->addItem(opt.second, opt.first);
        combo->setGeometry(210, y, 200, 24);
        m_mouseRows.append({ combo, QString::fromUtf8(b.key), QString::fromUtf8(b.def) });

        y += kChkPitchS;
        if (i == 0) {
            // 左键行下的两个缩进子选项
            addChk(page, y, tr("双击最大化/还原"), "mouse/dblclick_maximize",
                   false, true, 24);
            y += kChkPitchS - 4;
            addChk(page, y, tr("单击屏幕两侧时翻页"), "mouse/click_side_pageturn",
                   false, false, 24);
            y += kChkPitchS;
        }
    }
}

// ===========================================================================
// 7. 书签：2 复选框 + 最大书签数 + 分隔线 + 压缩包路径记忆 + 删除按钮
// ===========================================================================

void SettingsDialog::buildBookmarkPage(QWidget *page)
{
    int y = kChkY0;
    addChk(page, y, tr("自动保存当前图片路径为书签"), "bookmark/auto_bookmark", false, false); y += kChkPitch;
    addChk(page, y, tr("多窗口共享设置/书签"), "bookmark/share_settings", false, true);        y += kChkPitch;

    auto *lab = new QLabel(tr("最大书签数："), page);
    lab->setFont(hvFont());
    lab->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lab->setGeometry(kRowX, y, 250 - kRowX, 24);
    m_maxBookmarks = new HvComboBox(page);
    for (int v : { 10, 20, 30, 50, 100 })
        m_maxBookmarks->addItem(QString::number(v), v);
    m_maxBookmarks->setGeometry(258, y, 64, 24);
    y += kChkPitch + 14;

    addHLine(page, y, 16, kPageW - 32);
    y += 20;
    addChk(page, y, tr("自动记忆当前图片在压缩包中的路径"), "bookmark/remember_archive_pos",
           false, true);
    y += kChkPitch;

    auto *btnDel = new HvButton(tr("删除"), false, page);
    btnDel->setGeometry(kRowX + 16, y, 100, 28);
    connect(btnDel, &QAbstractButton::clicked, this, []() {
        QMessageBox::information(nullptr, tr("书签"),
                                 tr("已清除压缩包内图片的路径记忆。"));
        sSet("bookmark/archive_pos_cleared", QStringLiteral("1"));
    });
}

// ===========================================================================
// 8. 上下文菜单：4 级缩进复选框 + Thumbnail preview in Explorer 节
// ===========================================================================

void SettingsDialog::buildContextMenuPage(QWidget *page)
{
    int y = kChkY0;
    addChk(page, y, tr("在资源管理器中使用上下文菜单"), "ctxmenu/use_shell_menu", false, true); y += kChkPitch;
    addChk(page, y, tr("用QuikView转换图片"), "ctxmenu/transform_dlg", false, true, 24);        y += kChkPitch;
    addChk(page, y, tr("用QuikView浏览图片"), "ctxmenu/browse_image", false, true, 24);         y += kChkPitch;
    addChk(page, y, tr("上下文菜单预览"), "ctxmenu/preview_image", false, false, 48);           y += kChkPitch;
    addChk(page, y, tr("显示图片信息"), "ctxmenu/show_image_info", false, true, 48);            y += kChkPitch;
    // 原版该项依赖预览功能，置灰
    m_chkBinds.last().cb->setEnabled(false);
    addChk(page, y, tr("显示软件图标"), "ctxmenu/show_icon", false, true, 24);                  y += kChkPitch + 20;

    auto *head = addSectionBold(page, y, tr("Thumbnail preview in Explorer"));
    Q_UNUSED(head);
    addHLine(page, y + 26, kRowX, kPageW - kRowX * 2);
    y += 40;
    addChk(page, y, tr("WebP"), "ctxmenu/thumb_webp", false, true, 24);   y += kChkPitchS;
    addChk(page, y, tr("PSD"), "ctxmenu/thumb_psd", false, false, 24);    y += kChkPitchS;
    addChk(page, y, tr("PSB"), "ctxmenu/thumb_psb", false, false, 24);
}

// ===========================================================================
// 9. 杂项：图片文件夹 ×2 + 图像编辑器 + 6 复选框
// ===========================================================================

void SettingsDialog::buildEtcPage(QWidget *page)
{
    int y = kChkY0;
    addSectionBold(page, y, tr("图片文件夹"));
    y += 28;
    auto makeFolderRow = [this, page](int rowY, const QString &label, QLineEdit **edit) {
        auto *lab = new QLabel(label, page);
        lab->setFont(hvFont());
        lab->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lab->setGeometry(kRowX, rowY, 136, 24);
        *edit = new HvLineEdit(page);
        (*edit)->setGeometry(170, rowY, 330, 24);
        auto *btnBrowse = new HvButton(tr("浏览..."), false, page);
        btnBrowse->setGeometry(508, rowY, 76, 24);
        auto *btnOpen = new HvButton(tr("打开"), false, page);
        btnOpen->setGeometry(592, rowY, 62, 24);
        connect(btnBrowse, &QAbstractButton::clicked, this, [this, edit]() {
            const QString d = QFileDialog::getExistingDirectory(
                this, tr("选择一个文件夹."), (*edit)->text());
            if (!d.isEmpty())
                (*edit)->setText(d);
        });
        connect(btnOpen, &QAbstractButton::clicked, this, [edit]() {
            const QString d = (*edit)->text().trimmed();
            if (!d.isEmpty())
                QDesktopServices::openUrl(QUrl::fromLocalFile(d));
        });
    };
    makeFolderRow(y, tr("图片文件夹 1:"), &m_photoFolder1);
    y += 32;
    makeFolderRow(y, tr("图片文件夹 2:"), &m_photoFolder2);
    y += 40;

    addSectionBold(page, y, tr("图像编辑器"));
    y += 28;
    m_editorPath = new HvLineEdit(page);
    m_editorPath->setGeometry(170, y, 330, 24);
    auto *btnBrowse = new HvButton(tr("浏览..."), false, page);
    btnBrowse->setGeometry(508, y, 76, 24);
    auto *btnPreset = new HvButton(tr("预设"), false, page);
    btnPreset->setGeometry(592, y, 62, 24);
    connect(btnBrowse, &QAbstractButton::clicked, this, [this]() {
        const QString f = QFileDialog::getOpenFileName(
            this, tr("选择图像编辑器"), m_editorPath->text(),
            tr("程序 (*.exe);;所有文件 (*)"));
        if (!f.isEmpty())
            m_editorPath->setText(f);
    });
    connect(btnPreset, &QAbstractButton::clicked, this, [this]() {
        m_editorPath->setText(QStringLiteral("C:/Windows/System32/mspaint.exe"));
    });
    y += 38;

    const int p = kChkPitchS + 1;
    addChk(page, y, tr("翻页时播放音效"), "etc/pageturn_wav", false, false);                            y += p;
    addChk(page, y, tr("不打开 RAW 图片文件"), "etc/no_raw_load", false, false);                        y += p;
    addChk(page, y, tr("打开 JPEG 文件时，不打开同名 RAW 文件"), "etc/skip_raw_when_jpg", false, true);  y += p;
    addChk(page, y, tr("打开 RAW 图片文件时，显示已有缩略图"), "etc/raw_thumbnail", false, true);        y += p;
    addChk(page, y, tr("根据图片大小调节窗口大小"), "etc/fit_window_to_image", false, true);             y += p;
    addChk(page, y, tr("运行QuikView时禁用Windows屏幕保护程序"), "etc/disable_screensaver", false, false);
}

// ===========================================================================
// 10. 语言：下拉 + Translators 列表 + 两个链接
// ===========================================================================

void SettingsDialog::buildLangPage(QWidget *page)
{
    m_language = new HvComboBox(page);
    populateLanguageCombo(m_language);
    m_language->setGeometry(kRowX - 8, kChkY0, 470, 24);

    auto *translators = new QPlainTextEdit(page);
    translators->setReadOnly(true);
    translators->setFont(hvFont());
    translators->setStyleSheet(QStringLiteral(
        "QPlainTextEdit{border:1px solid #d8d8d8; background:#fdfdfd; color:#333333;}"));
    translators->setGeometry(kRowX - 8, kChkY0 + 40, kPageW - 32, 366);
    translators->setPlainText(QStringLiteral(
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
        "Русский (ru_RU) ........................... QuikView\n"
        "\n"
        "想贡献翻译？把 .qm/.ts 文件放进 translations 文件夹\n"
        "并在下方链接中打开它。\n"));

    auto *link1 = new HvLink(tr("[下载更多语言...]"), []() {
        QMessageBox::information(nullptr, QObject::tr("语言"),
                                 QObject::tr("更多语言包请访问 QuikView 项目主页下载。"));
    }, page);
    link1->move(kRowX - 8, kChkY0 + 416);
    auto *link2 = new HvLink(tr("[打开语言文件夹...]"), []() {
        const QString dir = QCoreApplication::applicationDirPath()
                            + QStringLiteral("/translations");
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    }, page);
    link2->move(336, kChkY0 + 416);
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
    const QString interp = sGet("imageproc/interpolate", QStringLiteral("0"));
    sSet("interpolation", interp == QStringLiteral("0") ? QStringLiteral("fast")
                                                        : QStringLiteral("smooth"));
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
}

// ===========================================================================
// 窗口铬件：自绘标题/关闭/拖拽（保留）
// ===========================================================================

void SettingsDialog::setCloseHover(bool hover)
{
    if (hover != m_closeHover) {
        m_closeHover = hover;
        update(m_closeRect);
    }
}

void SettingsDialog::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor("#ffffff"));
    p.setPen(QPen(QColor("#b4b4b4"), 1));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    // 标题
    p.setFont(hvFont(10, true));
    p.setPen(QColor("#333333"));
    p.drawText(QRect(14, 8, 200, 24), Qt::AlignVCenter | Qt::AlignLeft, tr("设置"));

    // 自绘关闭 ×
    if (m_closeHover)
        p.fillRect(m_closeRect, QColor("#e5e5e5"));
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor("#444444"), 1.4));
    const int cx = m_closeRect.center().x();
    const int cy = m_closeRect.center().y();
    p.drawLine(cx - 5, cy - 5, cx + 5, cy + 5);
    p.drawLine(cx - 5, cy + 5, cx + 5, cy - 5);
    p.setRenderHint(QPainter::Antialiasing, false);
}

void SettingsDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QPoint pos = event->position().toPoint();
        if (m_closeRect.contains(pos)) {
            reject();
            return;
        }
        if (pos.y() < 44) {
            m_dragging = true;
            m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
            return;
        }
    }
    QDialog::mousePressEvent(event);
}

void SettingsDialog::mouseMoveEvent(QMouseEvent *event)
{
    setCloseHover(m_closeRect.contains(event->position().toPoint()));
    if (m_dragging && (event->buttons() & Qt::LeftButton))
        move(event->globalPosition().toPoint() - m_dragOffset);
    QDialog::mouseMoveEvent(event);
}

void SettingsDialog::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    QDialog::mouseReleaseEvent(event);
}

void SettingsDialog::leaveEvent(QEvent *event)
{
    setCloseHover(false);
    QDialog::leaveEvent(event);
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
