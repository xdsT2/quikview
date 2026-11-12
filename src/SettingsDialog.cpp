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
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QTextStream>
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
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QAbstractItemView>
#include <QFont>
#include <QStyle>
#include <QStyleHints>
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
#include "PillToggle.h"
#include "Theme.h"
#include "UpdateChecker.h"
#include "UiStyle.h"

#include <QOperatingSystemVersion>

// ---------------------------------------------------------------------------
// 输入来源追踪（v2 焦点可见性，2026-11-11 修订规格）
// 鼠标点击控件时不得出现蓝色焦点框，再次点击已聚焦控件也要隐藏；只有
// Tab / Shift+Tab 等键盘导航显示 2px #1688F8 环并留 2px 外距。这里不改任何
// 控件的 FocusPolicy（保留 StrongFocus、Tab 顺序与高对比模式），只用一条应用级
// 过滤器记录最近一次输入来源，把结果写成每个控件的 kbFocus 动态属性；
// QSS 与 PillToggle 自绘都只在「有焦点 且 kbFocus」时画环。
// 菜单 Esc 返回是 KeyPress，自然延续键盘模式；程序性恢复焦点由 Qt 的
// focusChanged 触发 refresh，继承同一份模式状态。
// ---------------------------------------------------------------------------
class InputModeWatcher : public QObject
{
public:
    explicit InputModeWatcher(SettingsDialog *dialog)
        : QObject(dialog), m_dialog(dialog)
    {
    }

    bool keyboardMode() const { return m_keyboard; }

    // 焦点变化 / 切页 / 打开菜单后按当前焦点重刷一次属性。
    void refresh()
    {
        if (!m_dialog)
            return;
        QWidget *host = m_dialog->window();
        if (!host)
            return;
        QWidget *focus = host->focusWidget();
        // focusProxy 链：QKeySequenceEdit / 可编辑 QComboBox 的实际焦点在内层
        // QLineEdit 上，环要画在真正可见的那个控件上。
        QSet<QWidget *> ringTargets;
        for (QWidget *f = focus; f; f = f->focusProxy())
            ringTargets.insert(f);
        const QList<QWidget *> widgets = host->findChildren<QWidget *>();
        for (QWidget *w : widgets) {
            const bool on = m_keyboard && ringTargets.contains(w);
            if (w->property("kbFocus").toBool() != on) {
                w->setProperty("kbFocus", on);
                w->update();
            }
        }
        // focusWidget 可能尚未进入 findChildren（首次显示 / 刚切页）。
        for (QWidget *f : ringTargets) {
            if (f->property("kbFocus").toBool() != m_keyboard) {
                f->setProperty("kbFocus", m_keyboard);
                f->update();
            }
        }
    }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::KeyPress: {
            auto *ke = static_cast<QKeyEvent *>(event);
            // 单独按修饰键不算导航，避免 Ctrl 起手的组合键先闪一下环。
            if (ke->key() != Qt::Key_Shift && ke->key() != Qt::Key_Control
                && ke->key() != Qt::Key_Alt && ke->key() != Qt::Key_Meta) {
                setKeyboardMode(true);
            }
            break;
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::Wheel:
        case QEvent::TouchBegin:
            setKeyboardMode(false);
            break;
        default:
            break;
        }
        return QObject::eventFilter(obj, event);
    }

private:
    void setKeyboardMode(bool keyboard)
    {
        if (m_keyboard == keyboard)
            return;
        m_keyboard = keyboard;
        refresh();
    }

    SettingsDialog *m_dialog = nullptr;
    bool m_keyboard = false;
};

namespace {

// ---------------------------------------------------------------------------
// 尺寸：所有页共用 920 × 784（窄窗 900 × 660）。
// 其余十页沿用 2026-11-11 的 kPage* 节奏，取值不变。
// ---------------------------------------------------------------------------
// 2026-11-12 用户纠偏：窗口随内容收紧（参考 macOS 设置窗口），默认逻辑宽
// 920（原 1068 让卡片两侧留出大片无用空白），侧栏 208，卡片最大 640，
// 超出继续拉宽时额外空间均分两侧、卡片不再变宽。实测：卡片 638、左右留白
// 36/37；125% 下物理 1150 × 980。
constexpr int kDlgW = 920;
constexpr int kDlgH = 784;
// 键盘页只加高到 822（宽同 920）：14 行 + 四分组 + 冲突红条要一屏审阅
// （规格 §审图索引）。宽度不再单独放宽。
constexpr int kDlgKeyboardH = 822;
constexpr int kDlgMinW = 900;
constexpr int kDlgMinH = 660;
constexpr int kTitleBarH = 41;     // v2：标题栏 41（窄窗 40）
constexpr int kNavW = 208;         // 收紧后左栏 208（窄窗 180），11 项不挤
constexpr int kFooterH = 44;       // v2：底栏 44（窄窗 43）
constexpr int kComboH = 30;
constexpr int kInputH = 30;
// 其余十页的设置行节奏（既有实现，不变）
constexpr int kRowMinH = 40;
constexpr int kRowPadX = 11;
constexpr int kRowPadY = 5;
constexpr int kGroupTop = 16;
// v2 修订（2026-11-11）：组间 14；行高不再统一加高，按行型由内容撑开——
// 单行设置 38、标题+说明 44（说明换行继续按文字增长），控件列 154。
constexpr int kPageFirstGap = 16;
constexpr int kPageGroupGap = 14;
constexpr int kPageRowH = 44;        // 标题+说明
constexpr int kPageRowHSingle = 38;  // 单行设置
constexpr int kPageCtlW = 190;

// —— 「设置 › 一般」v2 母版专用尺寸 ——
// 标准 920×784：卡片列居中最大 640（实测 638）、两侧留白 36/37；组间距 12、
// 行高按内容撑开、导航行 36 / 行距 4、图标底板 22、控件高 30、下拉宽 190。
// 窄窗 900×660：左栏 180、导航行 34 / 行距 2、组间距 8、行高约 38、
// 标题栏 40、底栏 43；卡片仍 638、左右各 40/41（实测）。
constexpr int kGenContentMaxW = 640;   // 卡片上限 640（620–640 区间取上沿）
constexpr int kGenColumnPadX = 28;
constexpr int kGenCompactPadX = 24;
constexpr int kGenPaneTop = 20;
constexpr int kGenFirstGap = 14;
constexpr int kGenGroupGap = 12;
constexpr int kGenRowH = 47;
constexpr int kGenRowPadX = 14;
constexpr int kGenTextGap = 16;
constexpr int kGenCtlW = 190;
constexpr int kGenNavRowH = 36;
constexpr int kGenCtrlH = 30;
constexpr int kGenNavSpacing = 4;
constexpr int kGenComboW = 190;
// 紧凑档（窗口宽 < 1000 时启用）
constexpr int kGenCompactNavW = 180;
constexpr int kGenCompactNavRowH = 34;
constexpr int kGenCompactNavSpacing = 2;
constexpr int kGenCompactTitleH = 40;
constexpr int kGenCompactFooterH = 43;
constexpr int kGenCompactGroupGap = 8;
constexpr int kGenCompactRowH = 38;
constexpr int kGenCompactFirstGap = 8;
constexpr int kGenCompactPaneTop = 12;
constexpr int kGenCompactHeadGap = 4;
constexpr int kGenCompactThreshold = 920;   // 小于默认宽度才进紧凑档

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

// ---------------------------------------------------------------------------
// 壳扩展状态（2026-11-11 修订）：只有 CLSID 的 InprocServer32 真指向本机
// quikthumb.dll，才允许在设置页写「已注册」；仅文件存在时只能写「已检测到
// 壳扩展文件」。避免仅凭 DLL 落盘就谎报注册成功。
// ---------------------------------------------------------------------------
bool shellExtDllPresent()
{
    return QFileInfo::exists(QCoreApplication::applicationDirPath()
                             + QStringLiteral("/quikthumb.dll"));
}

bool shellExtRegistered()
{
    if (!shellExtDllPresent())
        return false;
    // 两个 COM 类：缩略图提供程序与上下文菜单（shell-ext/src/guids.h）。
    static const char *kGuids[] = {
        "{D8A1C4E2-7B35-4F69-92A4-5C6D7E8F1A2B}",   // CLSID_QuikThumbProvider
        "{C9FFAFAD-7C1C-4CCC-A37C-A075ED1F27AF}",   // CLSID_QuikContextMenu
    };
    for (const char *guid : kGuids) {
        QSettings reg(QStringLiteral("HKEY_CLASSES_ROOT\\CLSID\\%1\\InprocServer32")
                          .arg(QLatin1String(guid)),
                      QSettings::NativeFormat);
        const QString path = reg.value(QStringLiteral(".")).toString();
        if (path.isEmpty())
            return false;
        if (!path.endsWith(QStringLiteral("quikthumb.dll"), Qt::CaseInsensitive))
            return false;
    }
    return true;
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
    // extra：行区之外新增的部件（键盘页状态图例），也要从可用高度里扣掉，
    // 否则 14 行会算出过高、表格底部越出页面。其余页不传，行为不变。
    void snapTo(QWidget *hint, QWidget *head, int rowStep, QWidget *extra = nullptr,
                int fixedContent = 0)
    {
        m_hint = hint;
        m_head = head;
        m_extra = extra;
        m_step = rowStep;
        m_fixedContent = fixedContent;
        QWidget *w = parentWidget();
        while (w && !qobject_cast<QScrollArea *>(w)) {
            w = w->parentWidget();
        }
        if (auto *outer = qobject_cast<QScrollArea *>(w)) {
            m_outer = outer;
            m_page = outer->widget();
            outer->viewport()->installEventFilter(this);
            outer->installEventFilter(this);
            if (m_page)
                m_page->installEventFilter(this);
        }
        apply();
        // showEvent 时布局还没激活，viewport 高度常是旧值；等一轮事件循环再算一次。
        QTimer::singleShot(0, this, [this]() { apply(); });
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        // 页面在 QStackedWidget 里时，隐藏页的 geometry 会先被清空、显示时才重排；
        // 只监听 viewport 的 Resize 会读到旧高度。这里把外层滚动区、viewport、页面
        // 本体一起监听，统一走 queued 重算，等布局激活后再取最终几何。
        if (e->type() == QEvent::Resize || e->type() == QEvent::Show) {
            if (m_outer && (o == m_outer || o == m_outer->viewport() || o == m_page))
                scheduleApply();
        }
        return QScrollArea::eventFilter(o, e);
    }

    void showEvent(QShowEvent *e) override
    {
        QScrollArea::showEvent(e);
        scheduleApply();
    }

    // 高度直接由内容 sizeHint 决定：QScrollArea 默认 sizeHint 是固定值（360），
    // 与 AdjustToContents 无关——stretch=0 时布局按 sizeHint 给高度，所以必须自己重写。
    // NoFrame 时 frameWidth()=0，高度 = 内容 sizeHint 高度。宽度沿用父类（跟随外层表格）。
    // stretch 模式（键盘页 2026-11-11 起）：表格吃满页面剩余高度，行区可压缩，
    // sizeHint 必须给一个小值；否则 QBoxLayout 先按内容自然高分配，页面被撑出
    // 外层滚动条。滚动范围仍由 widget 内容决定，不受这里影响。
    QSize sizeHint() const override
    {
        return QSize(QScrollArea::sizeHint().width(), 120);
    }

private:
    void scheduleApply()
    {
        // 重算放到下一轮事件循环：等页面/视口布局激活完，读到的才是最终高度。
        if (m_applyQueued)
            return;
        m_applyQueued = true;
        QTimer::singleShot(0, this, [this]() {
            m_applyQueued = false;
            apply();
        });
    }

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
        // 分组头等固定内容也占纵向空间：先扣掉，再按整行数封顶。
        const int avail = m_outer->viewport()->height() - m_hint->height()
                          - m_head->height() - (m_extra ? m_extra->height() : 0)
                          - kTableBorder - m_fixedContent;

        // 高度两步走：
        // 1) 窗口够高 → 就取内容自然高，表格不再吃满页面，剩余纵向空间由页面底部弹簧吸收；
        // 2) 窗口不够高 → 封顶到「k 整行」= k*33−1（k 行 + (k−1) 条线），仅行区内部滚动，
        //    页面不出现外层滚动条；k 下限 1，避免极矮窗口算出 0。
        // 统一用固定高度而不是只设 maximumHeight：表格已不再是 stretch 项，
        // 只设 max 时布局仍按 sizeHint 撑开，封顶不会真正生效。
        int target = natural;
        if (avail < natural) {
            const int k = qBound(1, avail / m_step, 14);
            target = k * m_step - 1;
        }
        if (maximumHeight() != target) {
            setFixedHeight(target);
            updateGeometry();
        }
    }

    QWidget *m_hint = nullptr;
    QWidget *m_head = nullptr;
    QWidget *m_extra = nullptr;
    QWidget *m_page = nullptr;
    QScrollArea *m_outer = nullptr;
    int m_step = 33;
    int m_fixedContent = 0;   // 分组头等不参与整行吸附的固定内容高
    bool m_applyQueued = false;
};

// ---------------------------------------------------------------------------
// v2 checkbox（2026-11-11 修订）：表格内逐项启用用它，16×16 外框、勾选为
// 中性填充 + 对比色勾（不是蓝色）。自绘而不用 QSS ::indicator，原因有二：
//   1) 深/浅主题的勾色需要跟随填充色反转，QSS 只能靠两张位图；
//   2) 焦点环只在键盘导航时出现（kbFocus），和 PillToggle 用同一套规则。
// 命中区 24×24：16 外框 + 2px 环 + 2px 外距，正好满足规格的外距要求。
// ---------------------------------------------------------------------------
class V2CheckBox : public QCheckBox
{
public:
    explicit V2CheckBox(QWidget *parent = nullptr) : QCheckBox(parent)
    {
        setText(QString());
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setFixedSize(24, 24);
    }

    QSize sizeHint() const override { return QSize(24, 24); }
    QSize minimumSizeHint() const override { return QSize(24, 24); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF box((width() - 16) / 2.0, (height() - 16) / 2.0, 16, 16);
        const bool on = isChecked();
        QColor fill = on ? UiStyle::fluent::switchOffColor() : QColor(0, 0, 0, 0);
        QColor line = on ? UiStyle::fluent::switchOffColor()
                         : (underMouse() ? UiStyle::fluent::descColor()
                                         : UiStyle::fluent::borderColor());
        if (!isEnabled())
            p.setOpacity(0.45);

        p.setPen(QPen(line, 1));
        p.setBrush(fill);
        p.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);

        if (on) {
            const QColor mark(Qt::white);
            QPainterPath path;
            path.moveTo(box.left() + 3.6, box.center().y() + 0.2);
            path.lineTo(box.left() + 6.4, box.bottom() - 3.8);
            path.lineTo(box.right() - 3.2, box.top() + 4.0);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(mark, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(path);
        }

        if (isEnabled())
            p.setOpacity(1.0);
        if (hasFocus() && property("kbFocus").toBool()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(UiStyle::fluent::accentColor(), 2));
            p.drawRoundedRect(box.adjusted(-2, -2, 2, 2), 5, 5);
        }
    }

    bool hitButton(const QPoint &pos) const override { return rect().contains(pos); }
};

// ---------------------------------------------------------------------------
// v2 pop-up（2026-11-11 修订）：持续显示互斥值的字段。
// 规格：行内左文右箭头、当前值、选中项有勾 + 中性底、禁用项灰显并把原因写在
// 次行、菜单宽度按最长值扩展（上限 290）、方向键跳过禁用项、Enter 提交、
// Esc 取消并恢复触发器焦点。QComboBox 本身已提供键盘语义与焦点恢复，
// 这里只替换弹出项绘制与几何。
// ---------------------------------------------------------------------------
class PopupDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    int currentRow = -1;

    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        const QString reason = idx.data(Qt::UserRole + 1).toString();
        return QSize(opt.rect.width(), reason.isEmpty() ? 30 : 46);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);

        const bool enabled = idx.flags() & Qt::ItemIsEnabled;
        const bool hot = opt.state & QStyle::State_Selected;
        const bool isCurrent = idx.row() == currentRow;
        const QString reason = idx.data(Qt::UserRole + 1).toString();

        QRect r = opt.rect.adjusted(2, 1, -2, -1);
        if (hot) {
            p->setPen(Qt::NoPen);
            p->setBrush(UiStyle::fluent::hoverColor());
            p->drawRoundedRect(r, 6, 6);
        }
        if (!enabled)
            p->setOpacity(0.45);

        // 勾：当前值左侧 18px 区域，用两条线段画，不依赖位图。
        if (isCurrent) {
            QPen pen(UiStyle::fluent::textColor(), 1.6, Qt::SolidLine,
                     Qt::RoundCap, Qt::RoundJoin);
            p->setPen(pen);
            const qreal cx = r.left() + 12;
            const qreal cy = r.top() + (reason.isEmpty() ? r.height() / 2.0
                                                          : 14.0);
            p->drawLine(QPointF(cx - 3, cy), QPointF(cx - 0.5, cy + 3));
            p->drawLine(QPointF(cx - 0.5, cy + 3), QPointF(cx + 4, cy - 3));
        }

        QFont f = opt.font;
        f.setPixelSize(13);
        f.setWeight(QFont::Normal);
        p->setFont(f);
        p->setPen(enabled ? UiStyle::fluent::textColor()
                          : UiStyle::fluent::descColor());
        const QRect textRect = reason.isEmpty()
            ? r.adjusted(24, 0, -10, 0)
            : QRect(r.left() + 24, r.top() + 3, r.width() - 34, 20);
        const QString text = p->fontMetrics().elidedText(
            idx.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width());
        p->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);

        // 禁用原因：次行小字，始终可见，不用悬停才看到。
        if (!reason.isEmpty()) {
            QFont rf = opt.font;
            rf.setPixelSize(11);
            p->setFont(rf);
            p->setPen(UiStyle::fluent::descColor());
            const QRect rr(r.left() + 24, r.top() + 23, r.width() - 34, 18);
            p->drawText(rr, Qt::AlignLeft | Qt::AlignVCenter,
                        p->fontMetrics().elidedText(reason, Qt::ElideRight, rr.width()));
        }

        p->restore();
    }
};

class V2ComboBox : public QComboBox
{
public:
    explicit V2ComboBox(QWidget *parent = nullptr) : QComboBox(parent)
    {
        auto *d = new PopupDelegate(this);
        m_delegate = d;
        view()->setItemDelegate(d);
        view()->setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(kComboH);
    }

    // 弹出前按最长值 + 原因算宽度：最小与按钮同宽，最大 290，屏幕边缘交给 Qt。
    void showPopup() override
    {
        int need = width();
        const QFontMetrics fm(font());
        for (int i = 0; i < count(); ++i) {
            int w = fm.horizontalAdvance(itemText(i)) + 44;
            const QString reason = itemData(i, Qt::UserRole + 1).toString();
            if (!reason.isEmpty())
                w = qMax(w, fm.horizontalAdvance(reason) + 44);
            need = qMax(need, w);
        }
        view()->setMinimumWidth(qMin(need, 290));
        m_delegate->currentRow = currentIndex();
        QComboBox::showPopup();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QComboBox::paintEvent(event);
        // 右侧 12–14px 下箭头自绘（QSS 的 image 在小尺寸下不可控）。
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(UiStyle::fluent::descColor(), 1.5, Qt::SolidLine,
                      Qt::RoundCap, Qt::RoundJoin));
        const qreal cx = width() - 15;
        const qreal cy = height() / 2.0;
        p.drawLine(QPointF(cx - 3.5, cy - 1.5), QPointF(cx, cy + 2.0));
        p.drawLine(QPointF(cx, cy + 2.0), QPointF(cx + 3.5, cy - 1.5));
    }

private:
    PopupDelegate *m_delegate = nullptr;
};

// ---------------------------------------------------------------------------
// 导航图标：不依赖 Qt6::Svg（本机无 qsvg 插件），用 QPainter 画 16px 线性几何图形，
// 颜色跟随按钮状态（选中=强调青 / 悬停=白 / 常态 NavText），与文字间距 9px。
// ---------------------------------------------------------------------------
enum class NavGlyph { Gear, Eye, Image, Doc, Link, Keyboard, Mouse, Bookmark, Menu, More, Globe, Info };

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
    case NavGlyph::Link: {
        // 格式关联：两节相扣的链环（16px 下横排两段胶囊，重叠区自然形成扣接）
        p.drawRoundedRect(QRectF(x + 0.4, y + 5.2, 8.6, 5.6), 2.8, 2.8);
        p.drawRoundedRect(QRectF(x + 7.0, y + 5.2, 8.6, 5.6), 2.8, 2.8);
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
                  bool muted = false, int categoryIndex = -1)
        : QPushButton(text, parent), m_glyph(glyph), m_muted(muted),
          m_category(categoryIndex)
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
        // 整行选中 / 悬停底色由 QSS 的 #navItem:checked / :hover 绘制。
        p.drawControl(QStyle::CE_PushButton, opt);

        // 定稿：20×20 实色圆角底板（半径 5）+ 16×16 网格白色线性图形。
        // 主题切换保留类别色，选中只用整行底色与 600 字重表达。
        const int plateSize = 22;   // v2：底板 22×22、圆角 6
        const int pad = 10;
        const QRect plate(pad, (height() - plateSize) / 2, plateSize, plateSize);
        QColor plateColor = m_muted ? UiStyle::color(UiStyle::fluent::neutralIcon())
                                    : UiStyle::fluent::categoryColor(m_category);
        if (!isEnabled())
            plateColor.setAlpha(140);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(plateColor);
        p.drawRoundedRect(plate, 6, 6);

        const int glyphSize = 16;
        paintNavGlyph(p, m_glyph,
                      QRect(plate.x() + (plateSize - glyphSize) / 2,
                            plate.y() + (plateSize - glyphSize) / 2,
                            glyphSize, glyphSize),
                      QColor(255, 255, 255));

        QFont f = font();
        if (isChecked())
            f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(isEnabled() ? UiStyle::fluent::textColor()
                             : UiStyle::fluent::descColor());
        const int gap = 9;
        const int textX = plate.right() + gap;
        p.drawText(QRect(textX, 0, width() - textX - 8, height()),
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
    int m_category = -1;
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
                apply(le);
                // QKeySequenceEdit::clear()/resetState() 会把占位重置回英文
                // "Press shortcut"；文本重新变空时（含 Esc 清空）补回中文占位。
                if (!m_watchingEmpty) {
                    m_watchingEmpty = true;
                    // 延迟到事件循环：resetState() 里 setText 之后紧接着会写回英文占位，
                    // 必须等它写完再覆盖，否则又被冲掉。编辑结束（焦点离开也走
                    // finishEditing→resetState）同样要补一次。
                    auto *edit = static_cast<QKeySequenceEdit *>(parent());
                    auto reapply = [le]() {
                        QTimer::singleShot(0, le, [le]() {
                            if (le->text().isEmpty())
                                apply(le);
                        });
                    };
                    connect(le, &QLineEdit::textChanged, this, [reapply](const QString &t) {
                        if (t.isEmpty())
                            reapply();
                    });
                    connect(edit, &QKeySequenceEdit::editingFinished, this, reapply);
                }
            }
        }
        return false;
    }
private:
    bool m_watchingEmpty = false;
    static void apply(QLineEdit *le)
    {
        // QKeySequenceEdit 不会替内部编辑框保留完整高度；明确固定到键帽的
        // 26px 外形，避免全局 QLineEdit 最小高度把它压成一条矮槽。
        le->setFixedHeight(26);
        const bool editing = le->parentWidget()
            && le->parentWidget()->property("state").toString()
                   == QLatin1String("editing");
        le->setPlaceholderText(editing
            ? QCoreApplication::translate("SettingsDialog", "按下组合键…")
            : QCoreApplication::translate("SettingsDialog", "未设置"));
        QPalette pal = le->palette();
        pal.setBrush(QPalette::PlaceholderText, UiStyle::muted());
        le->setPalette(pal);
    }
};

// Qt 6.11 的 QKeySequenceEdit 会把 Esc 本身记成键位（只有「全选文本后按 Backspace」
// 才清空），而键盘页提示承诺「Esc 清空」。这里在编辑框上补一个过滤器：焦点在热键
// 编辑框内按 Esc → 清空为「未设置」，不再把 Esc 存成快捷键；其余情况 Esc 仍照常
// 交给对话框（关闭设置）。
class KeySeqEscClearFilter : public QObject
{
public:
    explicit KeySeqEscClearFilter(QKeySequenceEdit *edit) : QObject(edit), m_edit(edit)
    {
        edit->installEventFilter(this);
        hookInner();
        // 内部 QLineEdit 在构造后才由 QKeySequenceEdit 挂上；补一次延迟安装，
        // 否则 Esc 会漏到 QDialog::reject 把整个设置窗口关掉（2026-11-11 实测）。
        QTimer::singleShot(0, this, [this]() { hookInner(); });
    }

protected:
    bool eventFilter(QObject *, QEvent *ev) override
    {
        if (ev->type() == QEvent::Show || ev->type() == QEvent::Polish)
            hookInner();
        if (ev->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(ev);
            if (ke->key() == Qt::Key_Escape && !ke->isAutoRepeat()) {
                m_edit->clear();
                return true;   // 吃掉 Esc：不再向上冒泡关闭对话框
            }
        }
        return false;
    }

private:
    void hookInner()
    {
        if (m_hooked)
            return;
        // 焦点实际落在内部 QLineEdit（QKeySequenceEdit 设了 focusProxy），键事件先到它。
        if (auto *le = m_edit->findChild<QLineEdit *>()) {
            le->installEventFilter(this);
            m_hooked = true;
        }
    }

    QKeySequenceEdit *m_edit;
    bool m_hooked = false;
};

// 键盘页热键框状态（2026-11-09 样板）：把「已设置 / 未设置 / 正在录入」写成动态
// 属性 state，交给 QSS 属性选择器；冲突态由整行 [conflict="true"] 级联，不在这里判。
// 属性变化后必须对容器和内部 QLineEdit 都重新 polish，否则后代选择器不会重算。
class KeySeqStateFilter : public QObject
{
public:
    explicit KeySeqStateFilter(QKeySequenceEdit *edit,
                               std::function<void()> onStateChanged = {})
        : QObject(edit), m_edit(edit), m_onStateChanged(onStateChanged)
    {
        edit->installEventFilter(this);
        if (auto *le = edit->findChild<QLineEdit *>())
            le->installEventFilter(this);
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this,
                [this]() { schedule(); });
        schedule();
    }

    void schedule()
    {
        QTimer::singleShot(0, m_edit, [this]() { refresh(); });
    }

    void refresh()
    {
        const bool editing = m_edit->hasFocus()
            || (m_edit->focusProxy() && m_edit->focusProxy()->hasFocus());
        const QString state = editing ? QStringLiteral("editing")
            : (m_edit->keySequence().isEmpty() ? QStringLiteral("empty")
                                               : QStringLiteral("set"));
        if (m_edit->property("state").toString() != state) {
            m_edit->setProperty("state", state);
            m_edit->style()->unpolish(m_edit);
            m_edit->style()->polish(m_edit);
            if (auto *le = m_edit->findChild<QLineEdit *>()) {
                le->style()->unpolish(le);
                le->style()->polish(le);
            }
            m_edit->update();
        }
        // 录入时把旧值从显示上拿掉，键帽直接写「按下组合键…」（设计图 05）。
        // Qt 内部仍保留原序列：直接点走会走 finishEditing→resetState 恢复旧值，
        // 不会把「没按键就离开」误判成清空。
        if (auto *le = m_edit->findChild<QLineEdit *>()) {
            if (editing && !m_wasEditing && !le->text().isEmpty())
                le->clear();
            const QString want = editing ? tr("按下组合键…")
                : (m_edit->keySequence().isEmpty() ? tr("未设置") : QString());
            if (le->placeholderText() != want)
                le->setPlaceholderText(want);
            QPalette pal = le->palette();
            pal.setBrush(QPalette::PlaceholderText, UiStyle::muted());
            le->setPalette(pal);
            if (!editing && !m_edit->keySequence().isEmpty()) {
                QString shown = m_edit->keySequence().toString(QKeySequence::PortableText);
                shown.replace(QStringLiteral("Ctrl+"), QStringLiteral("Ctrl + "));
                shown.replace(QStringLiteral("Shift+"), QStringLiteral("Shift + "));
                shown.replace(QStringLiteral("Alt+"), QStringLiteral("Alt + "));
                shown.replace(QStringLiteral("Meta+"), QStringLiteral("Meta + "));
                shown.replace(QStringLiteral("Right"), QStringLiteral("→"));
                shown.replace(QStringLiteral("Left"), QStringLiteral("←"));
                if (le->text() != shown)
                    le->setText(shown);
            }
        }
        m_wasEditing = editing;

        // 行内「正在录入」与页首说明都靠这一次回调同步（含焦点进出）。
        if (m_onStateChanged)
            m_onStateChanged();
    }

protected:
    bool eventFilter(QObject *, QEvent *ev) override
    {
        const QEvent::Type t = ev->type();
        if (t == QEvent::FocusIn || t == QEvent::FocusOut
            || t == QEvent::Show || t == QEvent::Polish)
            schedule();
        return false;
    }

private:
    QKeySequenceEdit *m_edit;
    std::function<void()> m_onStateChanged;
    bool m_wasEditing = false;
};

// 11 个页签的图标（顺序必须与 addPage 调用顺序一致；漏项会让其后全部错一格）
NavGlyph navGlyphFor(int index)
{
    static const NavGlyph glyphs[] = { NavGlyph::Gear,  NavGlyph::Eye,    NavGlyph::Image,
                                       NavGlyph::Doc,   NavGlyph::Link,   NavGlyph::Keyboard,
                                       NavGlyph::Mouse, NavGlyph::Bookmark, NavGlyph::Menu,
                                       NavGlyph::More,  NavGlyph::Globe };
    return (index >= 0 && index < int(sizeof(glyphs) / sizeof(glyphs[0])))
               ? glyphs[index]
               : NavGlyph::Menu;
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
        { "@SetGroupBg@",      []{ return UiStyle::setGroupBg(); } },
        { "@SetGroupLine@",    []{ return UiStyle::setGroupLine(); } },
        { "@RowHover@",        []{ return UiStyle::setRowHover(); } },
        { "@RowTag@",          []{ return UiStyle::setRowTag(); } },
        // 「一般」v2 母版定稿的语义角色，全部映射到 UiStyle::fluent 令牌，
        // 设置页内部不另存色值。旧版蓝灰色值已全部作废。
        { "@GenTitleBg@",      []{ return UiStyle::fluent::titleBarBg(); } },
        { "@GenNavBg@",        []{ return UiStyle::fluent::navBg(); } },
        { "@GenContentBg@",    []{ return UiStyle::fluent::contentBg(); } },
        { "@GenCardBg@",       []{ return UiStyle::fluent::cardBg(); } },
        { "@GenCardLine@",     []{ return UiStyle::fluent::cardLine(); } },
        { "@GenRowLine@",      []{ return UiStyle::fluent::rowLine(); } },
        { "@GenFrameLine@",    []{ return UiStyle::fluent::frameLine(); } },
        { "@GenText@",         []{ return UiStyle::fluent::text(); } },
        { "@GenDesc@",         []{ return UiStyle::fluent::desc(); } },
        { "@GenAccent@",       []{ return UiStyle::fluent::focus(); } },
        { "@GenOnAccent@",     []{ return UiStyle::fluent::navSelText(); } },
        { "@GenNavSel@",       []{ return UiStyle::fluent::navSelected(); } },
        { "@GenHover@",        []{ return UiStyle::fluent::hover(); } },
        { "@GenCtrlBg@",       []{ return UiStyle::fluent::controlBg(); } },
        { "@GenCtrlLine@",     []{ return UiStyle::fluent::controlLine(); } },
        { "@GenDisabled@",     []{ return UiStyle::fluent::disabled(); } },
        { "@GenStatusOk@",     []{ return UiStyle::fluent::statusOk(); } },
        { "@GenStatusWarn@",   []{ return UiStyle::fluent::statusWarn(); } },
        { "@GenError@",        []{ return UiStyle::fluent::error(); } },
        { "@GenDangerBg@",     []{ return UiStyle::fluent::dangerBg(); } },
        { "@GenDangerLine@",   []{ return UiStyle::fluent::dangerLine(); } },
        { "@GenDangerIconText@", []{ return UiStyle::fluent::dangerIconText(); } },
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
/* 设置页分组卡片（2026-11-07 排版统一）：低对比组底 + 1px 描边，行与行之间
   用同色 1px 分隔；整行悬停只提亮背景，不改控件尺寸。 */
QFrame#setGroup { background: @SetGroupBg@; border: 1px solid @SetGroupLine@;
    border-radius: 7px; }
QWidget#settingRow { background: transparent; }
QWidget#settingRow:hover { background: @RowHover@; }
QWidget#rowControls { background: transparent; }
QFrame#rowLine { background: @SetGroupLine@; border: 0; max-height: 1px; }
QLabel#rowDesc { color: @HintText@; font-size: 11px; }
QLabel#rowTag { color: @RowTag@; font-size: 11px; }
QLabel#rowUnit { color: @HintText@; font-size: 11px; }
QLabel#setNote { color: @HintText@; font-size: 11px; }
QLabel#navEmpty { color: @HintText@; font-size: 11px; padding: 2px 2px 6px 2px; }
QFrame#keyTable { border: 1px solid @Line@; border-radius: 7px; }
QWidget#keyHead { background: @HeadBg@; }
QLabel#keyHeadCell { color: @HeadText@; font-size: 11px; }
QLabel#pageHint { color: @HintText@; font-size: 11px; }

QPushButton#miniBtn { min-height: 18px; max-height: 22px; padding: 0 9px; font-size: 10px;
    border-radius: 5px; }
/* 颜色按钮（ColorPickButton）已随第 18 期「背景段下线」删除，QSS 一并清掉 */
/* 键盘页「操作」列：只读动作名（原 flatCombo 可改绑，2026-11-04 锁死为标签） */
QLabel#keyAction { color: @Body@; font-size: 12px; }
/* 键盘页整行悬停（2026-11-09）：与一般页 settingRow 对齐，只提亮行底，
   不改行高/内边距。冲突行必须保持危险底色——`#keyRow:hover` 与
   `#keyRow[conflict="true"]` 特异性相同（id+伪类 / id+属性），冲突规则必须排在其后。 */
QWidget#keyRow { background: transparent; }
QWidget#keyRow:hover { background: @RowHover@; }
QWidget#keyRow[conflict="true"], QWidget#keyRow[conflict="true"]:hover {
    background: transparent; }

QLabel#valueText { color: @Body@; font-size: 11px; }

/* 键盘页热键编辑器（批次 D1 / 2026-11-06 下沿修复）：
   QKeySequenceEdit 是纯 QWidget 容器，自身不绘制 QSS 背景与边框——实际可见的
   底座/描边来自内部 QLineEdit。内部编辑框继承全局 QLineEdit 的 min-height:26px，
   在 26px 高的容器里被裁掉底部 1px 描边（用户图里从 Ctrl+O 到 Del 下边框全消失）。
   改法：容器透明无边框，底座/描边/悬停/焦点全部落到内部 QLineEdit，并解除
   min-height，让四边框完整落在容器内。焦点态由内部 QLineEdit:focus 提供。 */
QKeySequenceEdit { background: transparent; border: 0; padding: 0; }
QKeySequenceEdit QLineEdit { background: @Field@; color: @FieldText@;
    border: 1px solid @Border@; border-radius: 5px; font-size: 11px;
    padding: 0 6px; min-height: 0; }
QKeySequenceEdit QLineEdit:hover { border-color: @BorderHover@; }
QKeySequenceEdit QLineEdit:disabled { color: @DisabledTextSoft@;
    border-color: @DisabledBorder@; background: @DisabledBg@; }

/* 键盘页冲突态由 v2 段按键帽/文字表达，不再整行染色（2026-11-11 修订） */

/* 置灰（尚未实现）项的统一不可用态：文字压暗、底座与边框退到背景层，
   与可交互项一眼可分。UiStyle::dialogQss() 只给了 indicator:disabled，
   没有文字色，直接 setEnabled(false) 时文字仍与正常项同亮度、看不出差别，
   所以这里补一套。设置页底色由 paintEvent 固定为 DialogBg，DisabledText
   对比度约 3.3:1（满足禁用态 ≥3:1），可读且明确「不可点」。 */
QCheckBox:disabled { color: @DisabledText@; }
QLabel:disabled { color: @DisabledText@; }
/* 主题区「浅色模式」整行灰显：rowLabel / hint 是 id 选择器，特异性高于
   QLabel:disabled，必须显式补 id+pseudo，否则 setEnabled(false) 后文字亮度不变。 */
QLabel#rowLabel:disabled, QLabel#rowDesc:disabled,
QLabel#hint:disabled { color: @DisabledText@; }
QComboBox:disabled { color: @DisabledText@; background: @DisabledBg@; border-color: @DisabledBorder@; }
/* 下拉六态（SET-04 阶段 C）：底座/悬停/按下/焦点/展开列表全部走主题令牌。
   QComboBox:pressed 在部分平台不触发，另用 :open 覆盖弹层打开时的按下观感；
   焦点态用强调色描边，键盘 Tab 进入时可见，不加额外光晕避免改变控件尺寸。 */
QComboBox { background: @Field@; color: @FieldText@; border: 1px solid @Border@; }
QComboBox:hover { border-color: @BorderHover@; }
QComboBox:pressed, QComboBox:open { background: @HeadBg@; border-color: @Accent@; }
QComboBox:focus { border-color: @Accent@; }
QComboBox QAbstractItemView { background: @GroupBg@; color: @FieldText@;
    border: 1px solid @Line@; border-radius: 6px; padding: 4px;
    outline: 0; selection-background-color: @HeadBg@; selection-color: @FieldText@; }
QComboBox QAbstractItemView::item { min-height: 24px; padding: 0 8px; }
QComboBox QAbstractItemView::item:hover { background: @HeadBg@; }
QComboBox QAbstractItemView::item:selected { background: @HeadBg@; color: @FieldText@; }
QComboBox QAbstractItemView::item:disabled { color: @DisabledText@; }
/* 数值框（2026-11-07）：与下拉框同一套深浅主题边界与焦点样式。浅色主题
   此前只有全局 appQss（无 QSpinBox 规则）导致「只剩一条底线」，这里补齐。 */
QSpinBox { background: @Field@; color: @FieldText@; border: 1px solid @Border@;
    border-radius: 5px; padding: 0 6px; font-size: 12px; min-height: 28px; }
QSpinBox:hover { border-color: @BorderHover@; }
QSpinBox:focus { border-color: @Accent@; }
QSpinBox:disabled { color: @DisabledText@; background: @DisabledBg@; border-color: @DisabledBorder@; }
QLineEdit:disabled { color: @DisabledText@; background: @DisabledBg@; border-color: @DisabledBorder@; }
QPushButton:disabled { color: @DisabledText@; border-color: @DisabledBorder@; background: @HeadBg@; }

/* ===== 一般 / 键盘 两页样板（2026-11-09）=================================
   用属性选择器与 #keyTable / #keyLegend 后代限定，只作用于这两页；
   其余九页、菜单和弹窗不套用。色值一律走上面的 @Token@，不在此另造。 */
/* 一般页：分组标题提级为独立层级（页标题 21px → 说明 12px → 分组标题 13px/600） */
QLabel#sectionTitle[variant="page"] { font-size: 13px; font-weight: 600;
    color: @Body@; padding-bottom: 2px; }
/* 键盘页：表头与行区分层；三列表头加重，下边线用组卡片同色 1px */
QWidget#keyHead { border-bottom: 1px solid @SetGroupLine@; }
QLabel#keyHeadCell { font-size: 11px; font-weight: 600; color: @HeadText@; }
/* 状态图例：默认（鼠标移开）画面里就能读出热键框四种状态的含义 */
QWidget#keyLegend { background: transparent; }
QLabel#keyLegendChip { font-size: 10px; padding: 2px 8px; border-radius: 4px;
    background: @Field@; color: @FieldText@; border: 1px solid @Border@; }
QLabel#keyLegendChip[state="empty"] { background: transparent;
    color: @HintText@; border-style: dashed; }
QLabel#keyLegendChip[state="editing"] { color: @Accent@; border-color: @Accent@; }
QLabel#keyLegendChip[state="conflict"] { background: @DangerBg@;
    border-color: @DangerBg@; color: @Body@; }
/* 热键输入框：未设置用虚线 + 提示色；正在录入用强调色文字（不改边框宽度，
   避免 26px 容器里再次出现裁边）；冲突由整行 [conflict] 级联，压过前两态。 */
QKeySequenceEdit[state="empty"] QLineEdit { border-style: dashed; color: @HintText@; }
QKeySequenceEdit[state="editing"] QLineEdit { color: @Accent@; border-color: @Accent@; }
QWidget#keyRow[conflict="true"] QKeySequenceEdit QLineEdit {
    background: @Field@; border-color: @DangerBg@; color: @FieldText@; }

/* ===== 设置 › 一般 v2 母版定稿（2026-11-11）============================
   唯一基准：QuikView-一般页-母版定稿/{dark,light,compact}-v2.png +
   颜色与尺寸标注.md。上一版蓝灰色板（#24262B 系 / #83BBFF / 蓝色开关）
   已作废，这里不保留任何旧值。外壳规则只覆盖设置对话框内具名对象；
   一般页规则一律带 [fluent="v2"]。色值全部走 @Gen*@ 令牌。 */
/* —— 外壳：中性石墨黑三档表面，禁止蓝灰 / 蓝紫 —— */
QWidget#dlgTitleBar { background: @GenTitleBg@; border-bottom: 1px solid @GenCardLine@; }
QLabel#dlgTitle { color: @GenDesc@; font-size: 11px; }
QWidget#navRail { background: @GenNavBg@; }
QFrame#navDivider { background: @GenCardLine@; border: 0; }
QWidget#contentPane { background: @GenContentBg@; }
QWidget#dlgFooter { background: @GenTitleBg@; border-top: 1px solid @GenCardLine@; }
QLabel#footerNote { color: @GenDesc@; font-size: 11px; }
QLabel#navCaption { color: @GenDesc@; font-size: 11px; }
QLineEdit#navSearch { background: @GenCtrlBg@; color: @GenText@;
    border: 1px solid @GenCtrlLine@; border-radius: 7px;
    min-height: 30px; max-height: 30px; padding: 0 10px; font-size: 11px; }
QLineEdit#navSearch[kbFocus="true"]:focus { border: 2px solid @GenAccent@; padding: 0 9px; }
QFrame#hLine { background: @GenCardLine@; }
QPushButton#closeBtn { color: @GenDesc@; }
QPushButton#closeBtn:hover { background: @GenHover@; color: @GenText@; }

/* 11 项导航：整行选中 #1688F8（圆角 8），彩色图标底板见自绘 */
QPushButton#navItem { background: transparent; border: 1px solid transparent;
    border-radius: 8px; color: @GenText@; font-size: 13px; text-align: left;
    padding: 0 10px; min-height: 36px; }
QPushButton#navItem:hover { background: @GenHover@; }
QPushButton#navItem:checked { background: @GenNavSel@; color: @GenOnAccent@;
    font-weight: 600; }
QPushButton#navItem[kbFocus="true"]:focus { border: 2px solid @GenAccent@; padding: 0 9px; }
QPushButton#navItem:disabled { color: @GenDisabled@; }

/* 页标题 23/600，说明 11 */
QLabel#pageTitle { font-size: 23px; font-weight: 600; color: @GenText@; }
QLabel#pageDesc { font-size: 11px; color: @GenDesc@; }

/* 底栏全部中性：恢复默认 / 取消为文字钮，保存设置为中性描边钮 */
QPushButton#btnSubtle, QPushButton#genNeutral {
    background: transparent; color: @GenText@; border: 1px solid transparent;
    border-radius: 7px; min-height: 30px; padding: 0 12px; font-size: 13px; }
QPushButton#btnSubtle:hover, QPushButton#genNeutral:hover { background: @GenHover@; }
QPushButton#btnPrimary { background: @GenCtrlBg@; color: @GenText@;
    border: 1px solid @GenCtrlLine@; border-radius: 7px; min-height: 30px;
    padding: 0 12px; font-size: 13px; font-weight: 600; }
QPushButton#btnPrimary:hover { background: @GenHover@; }
QPushButton#btnPrimary[kbFocus="true"]:focus, QPushButton#btnSubtle[kbFocus="true"]:focus,
QPushButton#genNeutral[kbFocus="true"]:focus { border: 2px solid @GenAccent@; padding: 0 9px; }

/* —— 一般页：中性灰卡片，极弱描边，靠明暗与间距分组 —— */
QFrame#setGroup[fluent="v2"] { background: @GenCardBg@;
    border: 1px solid @GenCardLine@; border-radius: 11px; }
QFrame#rowLine[fluent="v2"] { background: @GenRowLine@;
    margin-left: 14px; margin-right: 14px; }
QWidget#settingRow[fluent="v2"] { background: transparent; }
QWidget#settingRow[fluent="v2"]:hover { background: @GenHover@; }
QLabel#sectionTitle[fluent="v2"] { font-size: 13px; font-weight: 600;
    color: @GenText@; }
QLabel#rowLabel[fluent="v2"] { font-size: 13px; color: @GenText@; }
QLabel#rowDesc[fluent="v2"] { font-size: 11px; color: @GenDesc@; }
QLabel#setNote[fluent="v2"] { font-size: 11px; color: @GenDesc@; }

QPushButton[fluent="v2"] { background: @GenCtrlBg@; color: @GenText@;
    border: 1px solid @GenCtrlLine@; border-radius: 7px;
    min-height: 30px; max-height: 30px; padding: 0 12px; font-size: 13px; }
QPushButton[fluent="v2"]:hover, QPushButton[fluent="v2"]:pressed {
    background: @GenHover@; }
QPushButton[fluent="v2"][kbFocus="true"]:focus { border: 2px solid @GenAccent@;
    padding: 0 9px; }
QPushButton[fluent="v2"]:disabled { color: @GenDisabled@;
    border-color: @GenCtrlLine@; background: @GenCtrlBg@; }

QComboBox[fluent="v2"] { background: @GenCtrlBg@; color: @GenText@;
    border: 1px solid @GenCtrlLine@; border-radius: 7px;
    min-height: 30px; max-height: 30px; font-size: 13px;
    padding: 0 26px 0 9px; }
QComboBox[fluent="v2"]:hover { border-color: @GenDesc@; }
QComboBox[fluent="v2"]:focus { border-color: @GenCtrlLine@; }
QComboBox[fluent="v2"][kbFocus="true"]:focus {
    border: 2px solid @GenAccent@; padding: 0 25px 0 8px; }
/* 菜单展开不画焦点环：字段保持普通描边，环只在键盘导航进入时出现。 */
QComboBox[fluent="v2"]:open { border-color: @GenControlLine@; }
/* 箭头由 V2ComboBox::paintEvent 自绘，QSS 不再画一份 */
QComboBox[fluent="v2"]::drop-down { border: 0; width: 26px; }
QComboBox[fluent="v2"]::down-arrow { image: none; width: 0; height: 0; }
QComboBox[fluent="v2"]:disabled { color: @GenDisabled@;
    border-color: @GenCtrlLine@; background: @GenCtrlBg@; }
QComboBox[fluent="v2"] QAbstractItemView { background: @GenCardBg@;
    color: @GenText@; border: 1px solid @GenCardLine@; border-radius: 8px;
    padding: 4px; outline: 0; selection-background-color: @GenNavSel@;
    selection-color: @GenOnAccent@; }
QComboBox[fluent="v2"] QAbstractItemView::item { min-height: 26px;
    padding: 0 8px; }
QComboBox[fluent="v2"] QAbstractItemView::item:selected {
    background: @GenNavSel@; color: @GenOnAccent@; }
QComboBox[fluent="v2"] QAbstractItemView::item:disabled { color: @GenDisabled@; }

/* v2：普通勾选框（格式关联等）与键盘表格，同一套令牌，不退回蓝灰 */
QCheckBox { color: @GenText@; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid @GenCtrlLine@;
    border-radius: 4px; background: @GenCtrlBg@; }
QCheckBox::indicator:hover { border-color: @GenDesc@; }
QCheckBox::indicator:checked { background: @GenAccent@; border-color: @GenAccent@;
    image: url(:/ui/check-light.png); }
QCheckBox::indicator:disabled { border-color: @GenDisabled@; background: @GenCtrlBg@; }
QFrame#keyTable { background: @GenCardBg@; border: 1px solid @GenCardLine@;
    border-radius: 11px; }
/* 表头与行同底色，只靠下分隔线与 600 字重区分；分组头压到内容色，形成
   「表头 → 组 → 行」三级层次（设计图 04/07 实测 #303133 / #252628 / #303133）。 */
QWidget#keyHead { background: @GenCardBg@; border-bottom: 1px solid @GenRowLine@; }
QLabel#keyHeadCell { color: @GenDesc@; font-size: 11px; font-weight: 600; }
QLabel#keyAction { color: @GenText@; font-size: 13px; }
QWidget#keyRow { background: transparent; }
QWidget#keyRow:hover { background: @GenHover@; }
QWidget#keyRow[editing="true"]:hover,
QWidget#keyRow[conflict="true"], QWidget#keyRow[conflict="true"]:hover {
    background: transparent; }
QFrame#keyRowLine { background: @GenRowLine@; }
QWidget#keyGroupHead { background: @GenContentBg@; }
QLabel#keyGroupTitle { color: @GenDesc@; font-size: 11px; font-weight: 600; }
/* 行内状态：录入态蓝字、冲突态红字，和键帽边框一起表达 */
QLabel#keyRowState { font-size: 11px; }
QLabel#keyRowState[state="editing"] { color: @GenAccent@; }
QLabel#keyRowState[state="conflict"] { color: @GenError@; }

/* 冲突红条（设计图 06）：深色 #382C2D / 浅色 #FDECEC，文字用状态红 */
QWidget#keyBanner { background: @GenDangerBg@; border: 1px solid @GenDangerLine@;
    border-radius: 8px; }
QLabel#keyBannerIcon { background: @GenError@; color: @GenDangerIconText@;
    border-radius: 8px; font-size: 11px; font-weight: 600; }
QLabel#keyBannerText { color: @GenError@; font-size: 12px; }

/* 冲突行：键帽描边转红 + 行内「冲突」红字（设计图 06 行底不变，不做整行染色） */
QWidget#keyRow[conflict="true"] QKeySequenceEdit QLineEdit {
    background: @GenCtrlBg@; border: 1px solid @GenError@; color: @GenError@; }

QKeySequenceEdit QLineEdit { background: @GenCtrlBg@; color: @GenText@;
    border: 1px solid @GenCtrlLine@; border-radius: 6px; font-size: 12px;
    min-height: 0; max-height: 26px; padding: 0 8px; }
QKeySequenceEdit QLineEdit:hover { border-color: @GenDesc@; }
/* 全局 QLineEdit:focus 会把内部编辑框染成浅蓝；v2 只允许键盘导航画环，
   指针模式下焦点保持普通描边（2026-11-11 焦点规则）。 */
QKeySequenceEdit QLineEdit:focus { border-color: @GenCtrlLine@; }
QKeySequenceEdit QLineEdit[kbFocus="true"]:focus { border: 2px solid @GenAccent@;
    padding: 1px 7px; }
QKeySequenceEdit QLineEdit:disabled { color: @GenDisabled@; border-color: @GenCtrlLine@; }

/* 启用列 checkbox 由 V2CheckBox 自绘（16×16 中性填充 + 对比勾 + 键盘焦点环） */

/* 保存按钮在冲突时禁用：中性底 + 压暗文字，不保留强调描边 */
QPushButton#btnPrimary:disabled { color: @GenDisabled@; border-color: @GenCtrlLine@;
    background: @GenCtrlBg@; }
)"));
}

} // namespace

// ===========================================================================
// 构造：深色三栏骨架 + 逐页构建
// ===========================================================================

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("设置"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    // 非模态（2026-11-09）：原来 setModal(true) + exec() 会把主窗口在 Win32 层
    // disable，主窗口收不到鼠标消息、标题栏拖不动——需求池「设置打开时主窗口
    // 无法拖动」的成因。设置页是即时写盘，不需要模态来保证一致性；关闭收尾由
    // MainWindow::finishSettings() 负责。设置窗口自身拖动见 mousePressEvent。
    setModal(false);
    resize(kDlgW, kDlgH);
    setMinimumSize(kDlgMinW, kDlgMinH);
    setFont(UiStyle::font(12));
    setStyleSheet(UiStyle::dialogQss() + extraQss());

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);
    // 允许窗口缩到 900x660：布局最小高（约 716）不得反向撑住窗口，
    // 超出部分由右侧内容区滚动承担（v2 窄窗口径）。
    root->setSizeConstraint(QLayout::SetNoConstraint);

    // —— 标题栏：⚙ 设置 … × ——
    auto *titleBar = new QFrame(this);
    titleBar->setObjectName(QStringLiteral("dlgTitleBar"));
    m_titleBar = titleBar;
    titleBar->setFixedHeight(kTitleBarH);
    auto *tl = new QHBoxLayout(titleBar);
    tl->setContentsMargins(18, 0, 14, 0);
    tl->setSpacing(9);
    auto *title = new QLabel(tr("QuikView — 设置"), titleBar);
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
    m_navRail = nav;
    m_navLayout = new QVBoxLayout(nav);
    m_navLayout->setContentsMargins(10, 16, 10, 16);
    m_navLayout->setSpacing(kGenNavSpacing);   // v2：行距 4（窄窗 2）
    // 侧栏搜索（2026-11-07 排版统一）：按设置名匹配，命中页签保留并跳转；
    // 清空恢复全部导航，无匹配给明确反馈。
    m_search = new QLineEdit(nav);
    m_search->setObjectName(QStringLiteral("navSearch"));
    m_search->setPlaceholderText(tr("搜索设置…"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedHeight(kGenCtrlH);
    m_search->setAccessibleName(tr("搜索设置"));
    m_navLayout->addWidget(m_search);
    m_navLayout->addSpacing(10);
    auto *caption = new QLabel(tr("偏好设置"), nav);
    caption->setObjectName(QStringLiteral("navCaption"));
    m_navLayout->addWidget(caption);
    m_navLayout->addSpacing(10);
    m_navLayout->addWidget(m_searchEmpty = new QLabel(tr("没有匹配的设置"), nav));
    m_searchEmpty->setObjectName(QStringLiteral("navEmpty"));
    m_searchEmpty->setVisible(false);
    bl->addWidget(nav);

    auto *rail = new QFrame(body);
    rail->setObjectName(QStringLiteral("navDivider"));
    rail->setFixedWidth(1);
    bl->addWidget(rail);

    auto *content = new QFrame(body);
    content->setObjectName(QStringLiteral("contentPane"));
    auto *cl = new QVBoxLayout(content);
    // 外壳：内容列由 updateContentColumnWidth() 居中限宽到 kGenContentMaxW
    // （2026-11-12 收窗：默认 920 下卡片 638、两侧各留 36/37）。
    cl->setContentsMargins(0, 25, 0, 20);
    cl->setSpacing(0);
    m_contentLay = cl;
    auto *column = new QWidget(content);
    column->setObjectName(QStringLiteral("contentColumn"));
    column->setMaximumWidth(kGenContentMaxW);
    auto *cvl = new QVBoxLayout(column);
    // 左右留白由 updateContentColumnWidth() 统一算（含 pane 内边距），
    // 这里不再叠一层，否则卡片会被吃掉 56px（2026-11-12 收窗实测）。
    cvl->setContentsMargins(0, 0, 0, 0);
    cvl->setSpacing(0);
    m_contentColumn = column;

    m_pageTitle = new QLabel(tr("浏览"), column);
    m_pageTitle->setObjectName(QStringLiteral("pageTitle"));
    m_pageDesc = new QLabel(column);
    m_pageDesc->setObjectName(QStringLiteral("pageDesc"));
    m_pageDesc->setWordWrap(true);
    // 页标题 / 分组标题 / 行文案共享同一条左边线：卡片内行有 11px 内边距，
    // 标题同样左缩进 11px。一般页由 selectPage 把缩进归零，让标题与卡片左缘对齐。
    m_pageTitle->setContentsMargins(11, 0, 0, 0);
    m_pageDesc->setContentsMargins(11, 0, 0, 0);
    auto *headBox = new QWidget(column);
    auto *hb = new QVBoxLayout(headBox);
    hb->setContentsMargins(0, 0, 0, 0);
    hb->setSpacing(5);
    hb->addWidget(m_pageTitle);
    hb->addWidget(m_pageDesc);
    cvl->addWidget(headBox);
    // 页头分隔线（原型 .qv-page-head 的 border-bottom）。定稿的一般页无此线，
    // 由 selectPage 只在该页隐藏；其余页保持原样。
    auto *headLine = new QFrame(column);
    headLine->setObjectName(QStringLiteral("hLine"));
    headLine->setFixedHeight(1);
    m_headLine = headLine;
    m_headGapTop = new QSpacerItem(0, 13);
    m_headGapBottom = new QSpacerItem(0, 15);
    cvl->addSpacerItem(m_headGapTop);
    cvl->addWidget(headLine);
    cvl->addSpacerItem(m_headGapBottom);

    m_stack = new QStackedWidget(column);
    cvl->addWidget(m_stack, 1);
    // 内容列居中：宽由 updateContentColumnWidth() 以 min=max 固定到
    // kGenContentMaxW，AlignHCenter 把剩余空间均分两侧。窗口继续拉宽时
    // 卡片不变宽，只有两侧留白变大（2026-11-12 收窗口径）。
    cl->addWidget(column, 1, Qt::AlignHCenter);
    bl->addWidget(content, 1);

    root->addWidget(body, 1);

    // —— 页面（每页一个 QScrollArea，内容 widget 存 m_pages）——
    buildGeneralPage(addPage(tr("一般"), tr("管理窗口行为、操作反馈、更新和首尾翻页。")));
    buildViewPage(addPage(tr("浏览"), tr("缩放状态、侧边箭头与悬浮提示等浏览显示设置；画布底色已固定。")));
    buildAppearancePage(addPage(tr("外观"), tr("主题与材质。跟随系统时由 Windows 的深浅色设置决定。")));
    buildImageProcPage(addPage(tr("图像处理"), tr("透明背景、插值算法、翻页效果与 ICC 色彩管理；监视器 ICC 受 Qt6 限制暂不可用。")));
    buildAssocPage(addPage(tr("格式关联"),
                           tr("查看 QuikView 可打开的格式，并在 Windows 中选择默认应用。")));
    // 键盘页不显示 pageDesc：页首说明由 m_keyHint 承担（默认/录入两态切换），
    // 避免标题下出现两条近似文案（设计图 04/05 只有一行）。
    buildKeyboardPage(addPage(tr("键盘"), QString()));
    buildMousePage(addPage(tr("鼠标"), tr("左/右/X/Y 与滚轮的按键映射可自定义；中键平移、双击最大化可用。")));
    buildBookmarkPage(addPage(tr("书签"), tr("自动保存开关与书签数量上限。")));
    buildContextMenuPage(addPage(tr("上下文菜单"), tr("右键菜单与资源管理器缩略图随安装包注册、卸载时移除；本页仅作状态说明。")));
    buildEtcPage(addPage(tr("杂项"), tr("图片文件夹、外部编辑器与 RAW 打开策略。")));
    buildLangPage(addPage(tr("语言"), tr("选择界面语言并管理语言文件。")));

    // 页面内容不足一屏时，多余空间落在末尾弹簧上（否则各 section 会被均匀拉开）。
    // 键盘页例外：它用单一表格滚动区，表格本身 stretch 吃满剩余高度，再加弹簧
    // 会把表格压成半屏（2026-11-11 修订）。
    for (int i = 0; i < m_pages.size(); ++i) {
        if (i == 5)
            continue;
        if (auto *pv = qobject_cast<QVBoxLayout *>(m_pages[i]->layout()))
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
    auto *btnAbout = new NavItemButton(tr("关于 QuikView"), NavGlyph::Info, nav, true);
    m_btnAbout = btnAbout;
    btnAbout->setObjectName(QStringLiteral("navItem"));
    btnAbout->setCursor(Qt::PointingHandCursor);
    btnAbout->setFixedHeight(kGenNavRowH);
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
    m_footer = footer;
    auto *fl = new QHBoxLayout(footer);
    // 定稿页脚：恢复默认贴左栏下方、提示与内容左缘对齐、取消/保存右对齐，
    // 按钮间距 8。148 的间隔按 980 评审画布算（10 + 恢复默认约 76 + 8 + 148
    // ≈ 内容列左缘 242）；窗口变化时提示仍在右栏内、不压按钮。
    fl->setContentsMargins(10, 0, 20, 0);
    fl->setSpacing(8);

    m_btnReset = new QPushButton(tr("恢复默认"), footer);
    m_btnReset->setObjectName(QStringLiteral("btnSubtle"));
    m_btnReset->setCursor(Qt::PointingHandCursor);
    m_btnReset->setFixedHeight(kGenCtrlH);
    connect(m_btnReset, &QPushButton::clicked, this, &SettingsDialog::onReset);
    fl->addWidget(m_btnReset);
    // 提示与卡片左缘对齐：间隔由 updateContentColumnWidth() 按内容列实测左缘
    // 逐帧计算（窗口收窄到 920 后固定 240 会把提示留在 1068 时代的位置）。
    m_footerGap = new QSpacerItem(240, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    fl->addSpacerItem(m_footerGap);

    auto *note = new QLabel(tr("普通设置即时生效；热键改绑与界面语言在保存后生效"), footer);
    note->setObjectName(QStringLiteral("footerNote"));
    m_footerNote = note;
    fl->addWidget(note);
    fl->addStretch(1);

    auto *btnCancel = new QPushButton(tr("取消"), footer);
    btnCancel->setObjectName(QStringLiteral("genNeutral"));
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setFixedHeight(kGenCtrlH);
    connect(btnCancel, &QPushButton::clicked, this, [this]() { reject(); });
    fl->addWidget(btnCancel);

    m_btnOk = new QPushButton(tr("保存设置"), footer);
    m_btnOk->setObjectName(QStringLiteral("btnPrimary"));
    m_btnOk->setCursor(Qt::PointingHandCursor);
    m_btnOk->setFixedHeight(kGenCtrlH);
    m_btnOk->setMinimumWidth(92);
    m_btnOk->setDefault(true);
    // 保存按钮只走 accept()：校验与写盘都在 accept()/onAccepted() 里统一处理，
    // 冲突时不关闭窗口（P0-1）。
    connect(m_btnOk, &QPushButton::clicked, this, [this]() { accept(); });
    fl->addWidget(m_btnOk);

    root->addWidget(footer);

    // 同页下拉统一宽度（按最长选项）+ 搜索索引；再回填配置与待保存基线。
    for (QWidget *p : m_pages)
        levelComboWidths(p);
    buildSearchIndex();
    connect(m_search, &QLineEdit::textChanged, this,
            [this](const QString &text) { filterNavigation(text); });

    loadFromConfig();
    selectPage(0);
    capturePendingBaseline();
    updatePendingNote();
    // 即时项在加载完控件后再接信号：避免开窗时的 setChecked 触发一次写盘。
    connectLiveChecks();
    connectInstantSignals();
    // 全部控件建完后再放开一次：QDialog 默认会把布局最小高写成窗口最小高，
    // 导致 900×660 窄窗被撑到 716。放最后确保覆盖默认约束。
    layout()->setSizeConstraint(QLayout::SetNoConstraint);

    // v2 焦点可见性：应用级追踪最近一次输入来源。窗口首次打开没有主动键盘
    // 输入，全部控件保持无环；Tab 之后才由 kbFocus 属性点亮当前焦点控件。
    m_inputMode = new InputModeWatcher(this);
    qApp->installEventFilter(m_inputMode);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *) {
        if (m_inputMode)
            m_inputMode->refresh();
    });
}

SettingsDialog::~SettingsDialog()
{
    if (m_inputMode) {
        qApp->removeEventFilter(m_inputMode);
        // 关闭时清理追踪状态，避免下一次打开沿用上一次的键盘模式。
        m_inputMode = nullptr;
    }
}

bool SettingsDialog::keyboardInputMode() const
{
    return m_inputMode && m_inputMode->keyboardMode();
}

void SettingsDialog::refreshFocusVisibility()
{
    if (m_inputMode)
        m_inputMode->refresh();
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
    // 所有页面共用 v2 外壳：卡片 / 行 / 字号 / 控件列都由这枚标记命中。
    page->setProperty("fluent", QStringLiteral("v2"));
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    scroll->setWidget(page);
    m_stack->addWidget(scroll);

    m_pages.append(page);
    m_pageTitles.append(title);
    m_pageDescs.append(desc);

    auto *btn = new NavItemButton(title, navGlyphFor(index), m_navLayout->parentWidget(),
                                  false, index);
    btn->setObjectName(QStringLiteral("navItem"));
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(kGenNavRowH);
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
    const QString desc = m_pageDescs.at(index);
    m_pageDesc->setText(desc);
    m_pageDesc->setVisible(!desc.isEmpty());
    for (int i = 0; i < m_tabs.size(); ++i)
        m_tabs.at(i)->setChecked(i == index);

    // v2 母版：所有页统一无页头分隔线、标题与卡片左缘对齐、内容列 640 居中。
    if (m_headLine)
        m_headLine->setVisible(false);
    updateGeneralHeaderSpacing();
    m_pageTitle->setContentsMargins(0, 0, 0, 0);
    m_pageDesc->setContentsMargins(0, 0, 0, 0);
    updateContentColumnWidth();

    // 键盘页标准画布比其他页高 38px：14 行 + 四分组要一屏审阅。窄窗（≤660）
    // 不干预，交给表格内单一滚动区；用户手动放大过的高度也不强行压回。
    if (height() > kDlgMinH + 8) {
        const int target = (index == 5) ? kDlgKeyboardH : kDlgH;
        if (height() != target)
            resize(width(), target);
    }
}

void SettingsDialog::updateGeneralHeaderSpacing()
{
    // v2：所有页都无页头分隔线；标准档 8+8，紧凑档 6+6。
    const int gap = m_compact ? kGenCompactHeadGap : 8;
    if (m_headGapTop)
        m_headGapTop->changeSize(0, gap);
    if (m_headGapBottom)
        m_headGapBottom->changeSize(0, gap);
    if (m_contentColumn && m_contentColumn->layout())
        m_contentColumn->layout()->invalidate();
}

void SettingsDialog::updateContentColumnWidth()
{
    if (!m_contentLay)
        return;
    // 内容列：宽度封顶到 kGenContentMaxW，靠 QBoxLayout 的居中把多出来的
    // 空间均分到两侧（align=AlignHCenter 已在外壳上设好）。窗口继续拉宽时
    // 卡片不变宽，只有两侧留白变大；窗口变窄时列跟着收，不再叠第二层内边距。
    const int navW = m_compact ? kGenCompactNavW : kNavW;
    const int padX = m_compact ? kGenCompactPadX : kGenColumnPadX;
    const int paneW = qMax(0, width() - navW - 1 - 2 * padX);
    const int colW = qMin(kGenContentMaxW, paneW);
    if (m_contentColumn) {
        m_contentColumn->setMinimumWidth(colW);
        m_contentColumn->setMaximumWidth(colW);
    }
    m_contentLay->setContentsMargins(0, m_compact ? kGenCompactPaneTop : kGenPaneTop, 0, 12);

    // 底栏提示对齐到内容列左缘：footer 左边距 + 恢复默认按钮宽 + 布局间距
    // 是固定量（实测合计 20），其余交给弹性间隔，提示文字起点 = 卡片左缘。
    if (m_footerGap && m_btnReset && m_footer && m_footer->layout()) {
        const int columnLeft = navW + 1 + qMax(0, width() - navW - 1 - colW) / 2;
        const int resetW = m_btnReset->width() > 0 ? m_btnReset->width()
                                                   : m_btnReset->sizeHint().width();
        const int gap = qMax(0, columnLeft - 20 - resetW);
        m_footerGap->changeSize(gap, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        m_footer->layout()->invalidate();
    }
}

void SettingsDialog::applyCompactLayout(bool compact)
{
    if (m_compact == compact)
        return;
    m_compact = compact;

    if (m_navRail)
        m_navRail->setFixedWidth(compact ? kGenCompactNavW : kNavW);
    if (m_titleBar)
        m_titleBar->setFixedHeight(compact ? kGenCompactTitleH : kTitleBarH);
    if (m_footer)
        m_footer->setFixedHeight(compact ? kGenCompactFooterH : kFooterH);
    if (m_navLayout)
        m_navLayout->setSpacing(compact ? kGenCompactNavSpacing : kGenNavSpacing);
    const int navRowH = compact ? kGenCompactNavRowH : kGenNavRowH;
    for (QAbstractButton *b : m_tabs) {
        if (auto *w = qobject_cast<QWidget *>(b))
            w->setFixedHeight(navRowH);
    }
    if (m_btnAbout)
        m_btnAbout->setFixedHeight(navRowH);

    // 所有页：组间距与行高按档位收放（字号与控件尺寸不变）。
    for (QWidget *page : m_pages) {
        int index = 0;
        if (auto *lay = page->layout()) {
            for (int i = 0; i < lay->count(); ++i) {
                QWidget *sec = lay->itemAt(i) ? lay->itemAt(i)->widget() : nullptr;
                if (!sec || sec->objectName() != QLatin1String("pageSection"))
                    continue;
                if (auto *v = qobject_cast<QVBoxLayout *>(sec->layout())) {
                    const int gap = index == 0
                        ? (compact ? kGenCompactFirstGap : kGenFirstGap)
                        : (compact ? kGenCompactGroupGap : kGenGroupGap);
                    v->setContentsMargins(0, gap, 0, 0);
                    for (int k = 0; k < v->count(); ++k) {
                        if (auto *sp = v->itemAt(k)->spacerItem())
                            sp->changeSize(0, compact ? 4 : 6);
                    }
                }
                ++index;
            }
        }
        for (QWidget *row : page->findChildren<QWidget *>(QStringLiteral("settingRow"))) {
            row->setMinimumHeight(compact ? kGenCompactRowH : kGenRowH);
            if (auto *h = qobject_cast<QHBoxLayout *>(row->layout())) {
                const int padY = compact ? 2 : kRowPadY;
                h->setContentsMargins(kGenRowPadX, padY, kGenRowPadX, padY);
                if (auto *left = qobject_cast<QVBoxLayout *>(h->itemAt(0)->layout()))
                    left->setSpacing(compact ? 1 : 2);
            }
        }
    }
    updateGeneralHeaderSpacing();
    updateContentColumnWidth();
}

void SettingsDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    // 窗口大小变化时即时重排：先切标准/紧凑档，再算内容列宽度。
    applyCompactLayout(width() < kGenCompactThreshold);
    updateContentColumnWidth();
}

// ===========================================================================
// 控件工厂
// ===========================================================================

QWidget *SettingsDialog::beginSection(QWidget *page, const QString &title, int topGap)
{
    auto *pageLay = qobject_cast<QVBoxLayout *>(page->layout());
    if (!pageLay)
        return page;

    auto *sec = new QWidget(page);
    // 标记分组壳：applyPageRhythm() 按页面顺序统一首组 / 组间距，不再逐页传参。
    sec->setObjectName(QStringLiteral("pageSection"));
    auto *v = new QVBoxLayout(sec);
    v->setContentsMargins(0, topGap >= 0 ? topGap : kGroupTop, 0, 0);
    v->setSpacing(0);

    if (!title.isEmpty()) {
        auto *t = new QLabel(title, sec);
        t->setObjectName(QStringLiteral("sectionTitle"));
        // 与行文案共用左边线（卡片内行左内边距 11px）。
        t->setContentsMargins(kRowPadX, 0, 0, 0);
        v->addWidget(t);
        v->addSpacing(6);
    }

    auto *card = new QFrame(sec);
    card->setObjectName(QStringLiteral("setGroup"));
    auto *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(0, 0, 0, 0);
    cardLay->setSpacing(0);
    v->addWidget(card);

    pageLay->addWidget(sec);
    return sec;
}

QWidget *SettingsDialog::groupCard(QWidget *section) const
{
    return section ? section->findChild<QFrame *>(QStringLiteral("setGroup")) : nullptr;
}

void SettingsDialog::applyPageRhythm(QWidget *page, int firstGap, int groupGap,
                                    int rowH, int ctlW)
{
    if (!page)
        return;

    // v2 母版：所有设置页共用同一套卡片 / 行 / 字号 / 控件列。每个页面在
    // addPage 时已打上 fluent=v2，这里再把本页生成的控件逐类打标，供
    // extraQss 的属性选择器命中。参数留空即用 v2 默认 16/12/47/154。
    const int fg = firstGap >= 0 ? firstGap : kPageFirstGap;
    const int gg = groupGap >= 0 ? groupGap : kPageGroupGap;
    const int rh = rowH >= 0 ? rowH : kPageRowH;
    const int rowWithDesc = rowH >= 0 ? rowH : kPageRowH;
    const int rowSingle = qMax(28, rowWithDesc - 6);
    const int cw = ctlW >= 0 ? ctlW : kPageCtlW;
    const bool isFirstPage = !m_pages.isEmpty() && m_pages.at(0) == page;

    const char *marks[] = { "setGroup", "rowLine", "settingRow", "rowLabel",
                            "rowDesc", "sectionTitle", "setNote" };
    for (const char *name : marks) {
        for (QWidget *w : page->findChildren<QWidget *>(QLatin1String(name)))
            w->setProperty("fluent", QStringLiteral("v2"));
    }
    for (QPushButton *b : page->findChildren<QPushButton *>()) {
        if (b->objectName() == QLatin1String("miniBtn"))
            continue;   // 迷你按钮保持 22 高，不套 30 高控件规格
        b->setProperty("fluent", QStringLiteral("v2"));
    }
    for (QComboBox *c : page->findChildren<QComboBox *>())
        c->setProperty("fluent", QStringLiteral("v2"));
    for (QCheckBox *cb : page->findChildren<QCheckBox *>())
        cb->setProperty("fluent", QStringLiteral("v2"));

    // 分组顶部间距：首组 fg，其余 gg。
    int index = 0;
    if (auto *lay = page->layout()) {
        for (int i = 0; i < lay->count(); ++i) {
            QLayoutItem *item = lay->itemAt(i);
            QWidget *sec = item ? item->widget() : nullptr;
            if (!sec || sec->objectName() != QLatin1String("pageSection"))
                continue;
            if (auto *v = qobject_cast<QVBoxLayout *>(sec->layout()))
                v->setContentsMargins(0, index == 0 ? fg : gg, 0, 0);
            ++index;
        }
    }

    // 行高、卡片内边距 14、文案与控件相隔 16；控件列整页同宽并右对齐。
    // 一般页（首块验收页）控件列固定 154；其他页先量后设，避免杂项路径输入
    // 或上下文菜单长状态被 154 挤扁。
    QList<QWidget *> rows;
    int ctlWidth = cw;
    for (QWidget *row : page->findChildren<QWidget *>(QStringLiteral("settingRow"))) {
        rows.append(row);
        if (isFirstPage)
            continue;
        row->ensurePolished();
        if (auto *ctl = row->findChild<QWidget *>(QStringLiteral("rowControls"))) {
            ctl->ensurePolished();
            ctlWidth = qMax(ctlWidth, ctl->sizeHint().width() + 8);
        }
    }
    for (QWidget *row : rows) {
        // 三类行型（规格 §布局与文字）：单行设置 38 / 标题+说明 44 /
        // 表格与映射行走自己的密度（不在这一支）。minimumHeight 只是下限，
        // 说明换行或多行内容仍由文字自然撑高，不做封顶、不删说明。
        const bool hasDesc = row->findChild<QLabel *>(QStringLiteral("rowDesc")) != nullptr;
        row->setMinimumHeight(hasDesc ? rowWithDesc : rowSingle);
        if (auto *h = qobject_cast<QHBoxLayout *>(row->layout())) {
            h->setContentsMargins(kGenRowPadX, kRowPadY, kGenRowPadX, kRowPadY);
            h->setSpacing(kGenTextGap);
        }
        if (auto *ctl = row->findChild<QWidget *>(QStringLiteral("rowControls"))) {
            ctl->setFixedWidth(ctlWidth);
            if (auto *hl = qobject_cast<QHBoxLayout *>(ctl->layout()))
                hl->insertStretch(0, 1);
        }
    }

    for (QLabel *t : page->findChildren<QLabel *>(QStringLiteral("sectionTitle"))) {
        t->setProperty("variant", QStringLiteral("page"));
        t->setContentsMargins(kGenRowPadX, 0, 0, 0);
    }
    for (QLabel *n : page->findChildren<QLabel *>(QStringLiteral("setNote")))
        n->setContentsMargins(kGenRowPadX, 8, kGenRowPadX, 9);
}

SettingsDialog::SettingRowParts SettingsDialog::addSettingRow(QWidget *section,
                                                              const QString &label,
                                                              const QString &desc)
{
    SettingRowParts out;
    auto *card = groupCard(section);
    if (!card)
        return out;

    // 行间 1px 分隔：组卡片里已有内容才插，避免出现首行顶线。
    if (auto *cardLay = qobject_cast<QVBoxLayout *>(card->layout())) {
        if (cardLay->count() > 0) {
            auto *line = new QFrame(card);
            line->setObjectName(QStringLiteral("rowLine"));
            line->setFixedHeight(1);
            cardLay->addWidget(line);
        }
    }

    auto *row = new QWidget(card);
    row->setObjectName(QStringLiteral("settingRow"));
    // 整行悬停：WA_Hover 让行（含子控件的祖先链）收到 HoverEnter/Leave，
    // QSS `#settingRow:hover` 只提亮行底，不改变任何控件尺寸。
    row->setAttribute(Qt::WA_Hover, true);
    row->setMinimumHeight(kRowMinH);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(kGenRowPadX, kRowPadY, kGenRowPadX, kRowPadY);
    h->setSpacing(kGenTextGap);

    auto *left = new QVBoxLayout;
    left->setContentsMargins(0, 0, 0, 0);
    // 规格：第一行 18px，第二行 15px，间隔 1px。
    left->setSpacing(1);
    out.label = new QLabel(label, row);
    out.label->setObjectName(QStringLiteral("rowLabel"));
    out.label->setWordWrap(true);
    left->addStretch(1);
    left->addWidget(out.label);
    if (!desc.isEmpty()) {
        out.desc = new QLabel(desc, row);
        out.desc->setObjectName(QStringLiteral("rowDesc"));
        out.desc->setWordWrap(true);
        left->addWidget(out.desc);
    }
    left->addStretch(1);
    h->addLayout(left, 1);

    auto *ctlHost = new QWidget(row);
    ctlHost->setObjectName(QStringLiteral("rowControls"));
    auto *ctl = new QHBoxLayout(ctlHost);
    ctl->setContentsMargins(0, 0, 0, 0);
    ctl->setSpacing(8);
    out.controls = ctl;
    h->addWidget(ctlHost, 0, Qt::AlignVCenter);

    if (auto *cardLay = qobject_cast<QVBoxLayout *>(card->layout()))
        cardLay->addWidget(row);
    out.row = row;
    return out;
}

QHBoxLayout *SettingsDialog::addRow(QWidget *section, const QString &label,
                                    const QString &desc)
{
    return addSettingRow(section, label, desc).controls;
}

QCheckBox *SettingsDialog::addCheck(QWidget *section, const QString &text, const char *key,
                                    bool inverted, bool def)
{
    return addSwitchRow(section, text, QString(), key, inverted, def);
}

PillToggle *SettingsDialog::addSwitchRow(QWidget *section, const QString &label,
                                         const QString &desc, const char *key,
                                         bool inverted, bool def)
{
    SettingRowParts parts = addSettingRow(section, label, desc);
    if (!parts.row)
        return nullptr;

    auto *cb = new PillToggle(parts.row);
    cb->setAccessibleName(label);
    if (!desc.isEmpty())
        cb->setToolTip(desc);
    parts.controls->addWidget(cb, 0, Qt::AlignVCenter);
    if (key)
        m_chkBinds.append({ cb, key, inverted, def });
    return cb;
}

QLabel *SettingsDialog::makeToggleLabel(const QString &text, const QString &objectName)
{
    auto *lab = new QLabel(text);
    lab->setObjectName(objectName);
    lab->setWordWrap(true);
    return lab;
}

namespace {

// 主题来源值 → 运行态实际配色（"system" 按 Windows 当前深浅解析）。
QString themeMode(const QString &setting)
{
    return Theme::resolveMode(setting) == Theme::Mode::Light ? QStringLiteral("light")
                                                            : QStringLiteral("dark");
}

// 整行命中：标题 / 说明 / 行内空白都走同一次开关激活。轨道本身由 PillToggle
// 自己处理，不装过滤器，避免一次点击激活两次。按下后移出再松开按取消处理；
// 禁用行只吞事件、不激活。
class ToggleRowHitFilter final : public QObject
{
public:
    ToggleRowHitFilter(PillToggle *toggle, QWidget *row, QObject *parent)
        : QObject(parent)
        , m_toggle(toggle)
        , m_row(row)
    {
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() != Qt::LeftButton)
                return false;
            m_pressed = m_toggle->isEnabled();
            return true;   // 吃掉按下：标签不抢焦点，松手仍按整行判定
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() != Qt::LeftButton)
                return false;
            const bool pressed = m_pressed;
            m_pressed = false;
            if (!pressed || !m_toggle->isEnabled())
                return true;   // 禁用行点任何位置都无效
            const QPoint globalPos = me->globalPosition().toPoint();
            if (!QRect(m_row->mapToGlobal(QPoint(0, 0)), m_row->size()).contains(globalPos))
                return true;   // 按下后移出再松开：取消激活
            m_toggle->click();
            return true;
        }
        return false;
    }

private:
    PillToggle *m_toggle = nullptr;
    QWidget *m_row = nullptr;
    bool m_pressed = false;
};

} // namespace

SettingsDialog::ToggleRow SettingsDialog::addToggleRow(QWidget *section, const QString &label,
                                                       const QString &desc,
                                                       const QString &accessibleName)
{
    ToggleRow out;
    SettingRowParts parts = addSettingRow(section, label, desc);
    if (!parts.row)
        return out;

    out.label = parts.label;
    out.desc = parts.desc;
    out.row = parts.row;
    out.toggle = new PillToggle(parts.row);
    out.toggle->setAccessibleName(accessibleName);
    out.toggle->setToolTip(desc);
    parts.controls->addWidget(out.toggle, 0, Qt::AlignVCenter);

    // 启用行的标题、说明和行内空白都是开关命中区；焦点仍只在轨道上。
    out.label->setCursor(Qt::PointingHandCursor);
    if (out.desc)
        out.desc->setCursor(Qt::PointingHandCursor);
    parts.row->setCursor(Qt::PointingHandCursor);
    auto *hit = new ToggleRowHitFilter(out.toggle, parts.row, parts.row);
    out.label->installEventFilter(hit);
    if (out.desc)
        out.desc->installEventFilter(hit);
    parts.row->installEventFilter(hit);
    return out;
}

PillToggle *SettingsDialog::addPillCheck(QWidget *section, const QString &label,
                                         const char *key, bool inverted, bool def,
                                         const QString &timing)
{
    ToggleRow row = addToggleRow(section, label, timing, label);
    // 与 addCheck 同一张绑定表：loadFromConfig 回填、onAccepted 写盘都复用，
    // 保证胶囊只是换了外壳，配置键与保存路径不变。
    if (row.toggle)
        m_chkBinds.append({ row.toggle, key, inverted, def });
    return row.toggle;
}

QComboBox *SettingsDialog::makePopupCombo(QWidget *parent)
{
    auto *combo = new V2ComboBox(parent);
    combo->setProperty("fluent", QStringLiteral("v2"));
    return combo;
}

QComboBox *SettingsDialog::addComboRow(QWidget *section, const QString &label,
                                       const QString &desc, const char *key,
                                       const QString &def,
                                       const QList<QPair<QString, QString>> &options,
                                       int minWidth)
{
    SettingRowParts parts = addSettingRow(section, label, desc);
    if (!parts.row)
        return nullptr;

    auto *combo = new V2ComboBox(parts.row);
    combo->setProperty("fluent", QStringLiteral("v2"));
    for (const auto &opt : options)
        combo->addItem(opt.second, opt.first);
    combo->setCursor(Qt::PointingHandCursor);
    combo->setFixedHeight(kComboH);
    if (!desc.isEmpty())
        combo->setToolTip(desc);

    // 宽度按最长选项计算（含箭头与左右内边距），同页再由 levelComboWidths
    // 统一到最大需求；上下限避免超长选项把控件列撑爆。
    int req = minWidth;
    if (req <= 0) {
        const QFontMetrics fm(combo->font());
        for (const auto &opt : options)
            req = qMax(req, fm.horizontalAdvance(opt.second));
        req += 44;
        req = qBound(160, req, 260);
    }
    combo->setProperty("reqW", req);
    combo->setFixedWidth(req);
    combo->setMinimumWidth(req);
    combo->setMaximumWidth(req);
    parts.controls->addWidget(combo, 0, Qt::AlignVCenter);

    if (key)
        m_comboBinds.append({ combo, key, def });
    // 把禁用项的 tooltip 复制到 UserRole+1：pop-up 委托在次行显示禁用原因，
    // 不用悬停才看得到（规格 §Pop-up button）。
    for (int i = 0; i < combo->count(); ++i) {
        const QString tip = combo->itemData(i, Qt::ToolTipRole).toString();
        if (!tip.isEmpty())
            combo->setItemData(i, tip, Qt::UserRole + 1);
    }
    return combo;
}

void SettingsDialog::levelComboWidths(QWidget *page)
{
    if (!page)
        return;
    int widest = 0;
    for (QComboBox *combo : page->findChildren<QComboBox *>()) {
        // 键盘页没有下拉；语言页单行自然保持自身需求。
        widest = qMax(widest, combo->property("reqW").toInt());
    }
    if (widest <= 0)
        return;
    for (QComboBox *combo : page->findChildren<QComboBox *>()) {
        if (combo->property("reqW").toInt() <= 0)
            continue;   // 专项控件（书签上限等）保留既有宽度
        combo->setFixedWidth(widest);
        combo->setMinimumWidth(widest);
        combo->setMaximumWidth(widest);
    }
}

QLabel *SettingsDialog::addHint(QWidget *section, const QString &text)
{
    auto *card = groupCard(section);
    if (!card)
        return nullptr;
    auto *lab = new QLabel(text, card);
    lab->setObjectName(QStringLiteral("setNote"));
    lab->setWordWrap(true);
    lab->setContentsMargins(kRowPadX, 8, kRowPadX, 9);
    if (auto *cardLay = qobject_cast<QVBoxLayout *>(card->layout()))
        cardLay->addWidget(lab);
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
                                                  const QString &desc, const char *key,
                                                  const QString &def,
                                                  const QList<QPair<QString, QString>> &options,
                                                  int minWidth)
{
    QComboBox *combo = addComboRow(section, label, desc, key, def, options, minWidth);
    markUnavailable(combo->parentWidget()->parentWidget());   // 整行（行标签 + 下拉）一起灰
    return combo;
}

// ===========================================================================
// 1. 一般
// ===========================================================================

void SettingsDialog::buildGeneralPage(QWidget *page)
{
    // 2026-11-11 v2 母版：页面标记由 addPage 统一打，本页不再单独设置。
    // 样板（2026-11-09）：原来 6 项挤在「窗口与操作」一组里，按用途拆成
    // 「窗口行为」与「操作反馈」两组；页标题 / 说明 / 分组标题成为三级层次。
    // 只调分组与版式，选项、key、生效时机、即时生效绑定一律不动。
    // 2026-11-11：版式部分收进 applyPageRhythm()，九页共用同一套节奏。
    QWidget *sec = beginSection(page, tr("窗口行为"));
    // 说明贴在行下，生效时机用短句；统一规则只在页脚说一次。
    addSwitchRow(sec, tr("不保存浏览过的文件夹路径"),
                 tr("勾选＝不记录最近打开的文件夹；下次打开文件时生效"),
                 "remember_browsed_folders", true, true);
    addSwitchRow(sec, tr("不允许多个实例"), tr("重启 QuikView 后生效"),
                 "allow_multiple_instances", true, true);
    auto *alwaysOnTop = addSwitchRow(sec, tr("总在最前 (Ctrl+A)"),
                                     QString(),
                                     "always_on_top", false, false);
    // SET-03 小批 A：置顶是二态且运行态可逆（原生 SetWindowPos 切带）。
    m_liveChecks.append({ alwaysOnTop, "always_on_top", false, false });
    addSwitchRow(sec, tr("按 ESC 退出"), tr("下次按 Esc 时生效"),
                 "esc_exit", false, true);

    sec = beginSection(page, tr("操作反馈"));
    addSwitchRow(sec, tr("删除文件时确认"), tr("下次删除文件时生效"),
                 "delete_confirm", false, true);
    addSwitchRow(sec, tr("在边角显示通知信息，而非中心"), tr("下一次通知时生效"),
                 "notify_corner", false, true);

    // —— 检查更新 ——
    // 手动检查不受频控；自动检查默认开（2026-11-12 用户定稿），启动 5s 静默查一次、24h 限频。
    sec = beginSection(page, tr("检查更新"));
    QHBoxLayout *updateRow = addRow(sec, tr("手动检查"),
                                    tr("立即连接发布源检查更新"));
    m_checkUpdateBtn = addButton(updateRow->parentWidget(), tr("检查更新"));
    updateRow->addWidget(m_checkUpdateBtn, 0, Qt::AlignVCenter);
    addSwitchRow(sec, tr("启动时检查更新"), tr("重启后生效；每 24 小时最多一次"),
                 "check_updates", false, true);
    // v2 母版卡片内不放长说明（避免旧版密集表单感）；原文案保留在分组
    // 提示里，信息不丢失，鼠标悬停即可读到。
    sec->setToolTip(tr("手动检查不受频率限制；检查更新会联网访问发布源，"
                       "失败时提示「检查失败，请检查网络」。"));
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

    sec = beginSection(page, tr("首尾翻页"));
    const auto opts = lastFileOptions();
    auto *edge = addComboRow(sec, tr("到达首尾图片时执行"),
                             tr("下次到达首尾图片时按所选行为处理"),
                             "edge_action", QStringLiteral("first"), opts, kGenComboW);
    // 档位诚实化：DirectoryLoader 只实现 first / stop；ask 既没有询问对话框，
    // 在 next/prev 与 advance 两条路径上的回绕行为还不一致，按缺口灰显。
    if (auto *model = qobject_cast<QStandardItemModel *>(edge->model())) {
        for (int i = 0; i < edge->count(); ++i) {
            if (edge->itemData(i).toString() == QLatin1String("ask")) {
                if (auto *item = model->item(i)) {
                    item->setEnabled(false);
                    item->setToolTip(tr("「询问」档暂无实现（无确认对话框），灰显不可选"));
                }
            }
        }
    }
    // v2 母版：长说明收进分组提示（内容不丢），卡片只留设置行。
    sec->setToolTip(tr("「转到首个文件」在末尾继续时回绕到首个；「停止」停在当前图片。"
                       "「询问」档当前无询问对话框实现，已灰显，不按生效计。"));

    // —— 一般页 v2 母版定稿（2026-11-11）：只改视觉，不动任何业务 ——
    // 控件高 30 与工厂一致；首尾下拉宽 154（v2 标注）。按钮按文字自然宽，
    // 不再强制 92 最小宽。
    m_checkUpdateBtn->setFixedHeight(kGenCtrlH);
    m_checkUpdateBtn->setProperty("fluent", QStringLiteral("v2"));
    edge->setFixedHeight(kGenCtrlH);
    edge->setFixedWidth(kGenComboW);
    edge->setProperty("fluent", QStringLiteral("v2"));

    // 2) 开关：30×16 轨道、38×24 命中区、定稿配色与焦点环。
    // PillToggle 没有 Q_OBJECT，findChildren<PillToggle*> / qobject_cast 都会
    // 触发 Qt 静态断言；改用动态属性，控件内部 fluentMode() 自行读取。
    for (QCheckBox *cb : page->findChildren<QCheckBox *>())
        cb->setProperty("fluent", QStringLiteral("v2"));

    // 3) 逐类打标，让 extraQss 的 [fluent="v2"] 规则只作用于本页。
    const char *marks[] = { "setGroup", "rowLine", "settingRow", "rowLabel",
                            "rowDesc", "sectionTitle", "setNote" };
    for (const char *name : marks) {
        for (QWidget *w : page->findChildren<QWidget *>(QLatin1String(name)))
            w->setProperty("fluent", QStringLiteral("v2"));
    }

    // 4) 定稿节奏：首组 22 / 组间 14、行高 44、控件列 160；其余十页仍走
    //    applyPageRhythm(page) 的默认 12/20/42/176。
    applyPageRhythm(page, kGenFirstGap, kGenGroupGap, kGenRowH, kGenCtlW);
}

// ===========================================================================
// 2. 浏览
// ===========================================================================

void SettingsDialog::buildViewPage(QWidget *page)
{
    // 已接通：保存缩放状态、侧边箭头、动画帧信息、悬浮提示（B 批）。
    // 其余仍灰显的键在仓储侧无消费点，保持不可用态。
    QWidget *sec = beginSection(page, tr("导航"));
    // 侧边箭头是真开关：ImageCanvas::setSideArrowsEnabled()。
    auto *sideArrows = addSwitchRow(sec, tr("显示侧边箭头"),
                                    tr("鼠标移到画布左右边缘时显示翻页箭头"),
                                    "view/show_side_arrow", false, true);
    addSwitchRow(sec, tr("记住缩放状态"),
                 tr("下次打开图片或重启时恢复；当前画面不变"),
                 "view/save_zoom_state", false, false);

    sec = beginSection(page, tr("画面信息"));
    addSwitchRow(sec, tr("显示文件名浮层"),
                 tr("隐藏顶端菜单时，按 Shift+Tab 显隐文件名浮层"),
                 "view/overlay_filename", false, false);
    // SET-04：帧号行由 updateInfoOverlay() 直接重排，模态设置页内也能看到信息行变化。
    auto *animInfo = addSwitchRow(sec, tr("显示动画帧号"),
                                  tr("GIF / MNG 底部信息行显示「第 n/N 帧」"),
                                  "view/show_anim_control", false, true);
    auto *zoomInfo = addSwitchRow(sec, tr("显示缩放浮标"),
                                  tr("缩放时在窗口内显示当前比例"),
                                  "view/show_zoom_control", false, true);
    addSwitchRow(sec, tr("显示鼠标悬浮提示"),
                 tr("鼠标停在图片边缘或栏位按钮上时显示说明"),
                 "view/show_tooltip", false, true);
    auto *smallFont = addSwitchRow(sec, tr("用小字体显示 EXIF / 文件信息"),
                                   tr("信息面板字号更小，可容纳更多字段"),
                                   "view/exif_small_font", false, false);
    // SET-02 样板 + SET-03 小批 A + SET-04 浏览页批次（同一页面，均为二态运行态开关）：
    // addSwitchRow 已把它们加入 m_chkBinds，保存路径照常写同名键。
    m_liveChecks.append({ smallFont, "view/exif_small_font", false, false });
    m_liveChecks.append({ sideArrows, "view/show_side_arrow", false, false });
    m_liveChecks.append({ zoomInfo, "view/show_zoom_control", false, false });
    m_liveChecks.append({ animInfo, "view/show_anim_control", false, false });
    addHint(sec, tr("以上显示项切换后立即重排信息面板；关闭设置页后按当前值保留。"));

    // 「背景」段（背景图 + 路径 + 背景色 + 边框色）已按用户 2026-11-03 决定整段
    // 移除（第 18 期）：画布外观固定，见 MainWindow::applyRuntimeSettings 的注释。
    applyPageRhythm(page);
}

// ===========================================================================
// 2.5 外观（第 18 期：主题三档 + 磨砂材质）
// ===========================================================================

void SettingsDialog::buildAppearancePage(QWidget *page)
{
    // —— 主题 ——
    // 2026-11-11 修订：原来的「跟随系统 / 浅色模式」两个联动开关合成一个
    // 互斥值字段（pop-up）。语义不变：theme = system | light | dark，默认 dark。
    // 选择成功即应用并写盘；失败回退到上一次实际生效值（applyPendingTheme）。
    // 快速连续切换只提交最后一次（m_themeSerial + 延迟器）。
    QWidget *sec = beginSection(page, tr("主题"));
    {
        SettingRowParts parts = addSettingRow(
            sec, tr("界面主题"),
            tr("跟随系统时，随 Windows 深浅色设置切换"));
        m_themeCombo = makePopupCombo(parts.row);
        m_themeCombo->addItem(tr("跟随系统"), QStringLiteral("system"));
        m_themeCombo->addItem(tr("浅色"), QStringLiteral("light"));
        m_themeCombo->addItem(tr("深色"), QStringLiteral("dark"));
        m_themeCombo->setAccessibleName(tr("界面主题"));
        m_themeCombo->setFixedWidth(kGenComboW);
        parts.controls->addWidget(m_themeCombo, 0, Qt::AlignVCenter);
        connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
            if (!m_themeApplying)
                applyThemePreview();
        });
    }

    const QString theme = sGet("theme", QStringLiteral("dark"));
    syncThemeControls(theme);
    m_themeAppliedSetting = theme;
    m_themeAppliedMode = themeMode(theme);
    updateThemeControlStates();

    // 跟随系统时，Windows 深浅色变化要刷新设置页自身 QSS 与主窗口表面；
    // 字段显示值保持「跟随系统」，不跟着系统色来回跳。
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme cs) {
                Q_UNUSED(cs);
                if (m_themeApplying || m_themePendingActive || !m_themeCombo)
                    return;
                if (m_themeCombo->currentData().toString() != QLatin1String("system"))
                    return;
                Theme::currentSetting() = QStringLiteral("system");
                refreshDialogStyle();
                if (auto *host = qobject_cast<MainWindow *>(parentWidget()))
                    host->refreshThemeSurfaces();
            });
    // —— 材质 ——
    // 磨砂只做右键菜单与信息浮层（第 18 期④），独立键 ui/blur_material，
    // 默认关、不参与「恢复默认」清空（在 config 组外）。Win10 上 DWM 不支持
    // SystemBackdrop，勾了也只会是半透明纯色——直接灰掉，别给人坏的期待。
    // 控件形态与主题区统一为胶囊拨动开关；功能与键值不动，关闭设置页后保留。
    sec = beginSection(page, tr("材质"));
    ToggleRow blurRow = addToggleRow(
        sec, tr("磨砂材质（右键菜单与信息浮层）"),
        tr("影响右键菜单与信息浮层；Windows 11 效果最佳"),
        tr("磨砂材质"));
    m_blurMaterial = blurRow.toggle;
    m_blurMaterial->setChecked(bGet("ui/blur_material", false));
#if defined(Q_OS_WIN)
    if (QOperatingSystemVersion::current() < QOperatingSystemVersion::Windows11) {
        m_blurMaterial->setEnabled(false);   // 不写回、不落盘，保留用户已有取值
        if (blurRow.desc)
            blurRow.desc->setText(tr("当前系统不支持系统级磨砂，已停用"));
        blurRow.row->setToolTip(tr("当前系统不支持系统级磨砂，已停用"));
    }
#endif
    addHint(sec, tr("磨砂材质在 Windows 10 上降级为半透明。"));
    applyPageRhythm(page);
}

QString SettingsDialog::themeFromControls() const
{
    if (m_themeCombo) {
        const QString v = m_themeCombo->currentData().toString();
        if (v == QLatin1String("system") || v == QLatin1String("light")
            || v == QLatin1String("dark"))
            return v;
    }
    return QStringLiteral("dark");
}

void SettingsDialog::syncThemeControls(const QString &theme)
{
    if (!m_themeCombo)
        return;
    const QString v = (theme == QLatin1String("system") || theme == QLatin1String("light"))
        ? theme : QStringLiteral("dark");
    const int idx = m_themeCombo->findData(v);
    const QSignalBlocker blocker(m_themeCombo);
    m_themeCombo->setCurrentIndex(idx >= 0 ? idx : 2);
    m_themeLast = theme;
}

void SettingsDialog::updateThemeControlStates()
{
    // 单一 pop-up 字段没有互斥禁用项；三个值始终都可选。
    // 保留该入口供既有调用点（加载配置 / 失败回退）复用。
}

void SettingsDialog::refreshDialogStyle()
{
    // 设置页构造时把 dialogQss() 编译进了局部表；主题切换后必须按新令牌重装，
    // 否则设置页（用户正在看的这一面）会停在旧主题。
    setStyleSheet(UiStyle::dialogQss() + extraQss());
    update();
}

void SettingsDialog::applyThemePreview()
{
    if (!m_themeCombo)
        return;

    const QString want = themeFromControls();
    // 来源状态立即生效：跟随系统的监听条件与禁用态不依赖是否换肤。
    Theme::currentSetting() = want;
    updateThemeControlStates();

    if (themeMode(want) == m_themeAppliedMode) {
        // 等色：手选深色 ↔ 跟随系统(系统当前为深色) 等场景，运行态配色没变。
        // 只同步来源状态并作废在途请求，不重装全局调色板 / 全局样式。
        ++m_themeSerial;
        m_themePendingActive = false;
        m_themePending.clear();
        m_themeLast = want;
        m_themeAppliedSetting = want;
        // 等色也写盘：来源从「手选深色」变成「跟随系统」，配色虽未变，
        // 但配置必须保存，否则关窗后来源又跳回去。
        applyInstantValue(QStringLiteral("theme"), want);
        return;
    }

    // 真正变色：点击回调只排队，重任务等开关圆点过渡走完再执行。整页换肤是
    // 一次全量重抛光（实测 ~230ms），排在过渡中间会把圆点动画拦腰截断。
    m_themePending = want;
    m_themePendingActive = true;
    ++m_themeSerial;
    if (!m_themeDefer) {
        m_themeDefer = new QTimer(this);
        m_themeDefer->setSingleShot(true);
        connect(m_themeDefer, &QTimer::timeout, this,
                [this]() { applyPendingTheme(m_themeSerial); });
    }
    m_themeDefer->start(PillToggle::motionDurationMs() + 30);
}

void SettingsDialog::applyPendingTheme(int serial)
{
    if (!m_themePendingActive || serial != m_themeSerial)
        return;   // 已被更新的点击或回滚取代，不能覆盖最终选择
    const QString want = m_themePending;
    m_themePendingActive = false;
    m_themePending.clear();
    if (!m_themeCombo)
        return;
    auto *host = qobject_cast<MainWindow *>(parentWidget());
    if (!host)
        return;

    m_themeApplying = true;
    QString error;
    const bool ok = host->applyLiveSetting(QStringLiteral("theme"), want, &error);
    if (ok) {
        m_themeAppliedSetting = want;
        m_themeAppliedMode = themeMode(want);
        m_themeLast = want;
        refreshDialogStyle();
        // 主题是即时项：切换成功即写盘，关闭窗口（含「不保存」）也保留。
        applyInstantValue(QStringLiteral("theme"), want);
    } else {
        // 运行态没变（失败发生在下发入口）；回退到运行态实际值，而不是
        // 用户最后一次点选值，避免「控件显示浅色、界面还是深色」。
        Theme::currentSetting() = m_themeAppliedSetting;
        syncThemeControls(m_themeAppliedSetting);
        updateThemeControlStates();
        QMessageBox::warning(this, tr("主题未应用"),
                             error.isEmpty() ? tr("本次主题切换失败，已恢复原设置。")
                                             : error);
    }
    m_themeApplying = false;
}

void SettingsDialog::flushPendingTheme()
{
    // 保存前把排队中的真实变色同步做完，避免「配置写浅色、运行态还没换」。
    if (m_themeDefer)
        m_themeDefer->stop();
    if (m_themePendingActive)
        applyPendingTheme(m_themeSerial);
}

// ===========================================================================
// 3. 图像处理
// ===========================================================================

void SettingsDialog::buildImageProcPage(QWidget *page)
{
    // 「根据 EXIF 信息自动旋转图片」勾选框已移除：功能恒开且无开关消费方——
    // 解码链路无条件 setAutoTransform(true)，留个勾选框只会让人以为能关。
    QWidget *sec = beginSection(page, tr("渲染"));
    // 标签按实测语义：Canvas 的 InterpBilinear 走 FastTransformation（最近邻），
    // 中间档是 GPU 平滑（双线性量级），第三档才是真 Lanczos3 重采样。
    addComboRow(sec, tr("插值算法"), tr("缩放与减淡重采样时使用"),
                "imageproc/interpolate", QStringLiteral("0"),
                { { "0", tr("最近邻 (快)") }, { "1", tr("平滑 (双线性)") },
                  { "2", tr("Lanczos (锐利)") } });
    // 强锐化（C13）：lanczos 缩图路径叠加固定强度 unsharp mask，见 ImageCanvas。
    addSwitchRow(sec, tr("使用强锐化滤镜"),
                 tr("Lanczos 插值下缩小显示（小于 100%）时叠加锐化"),
                 "imageproc/strong_sharpen", false, false);
    addSwitchRow(sec, tr("图片大于窗口时显示图片顶部"),
                 tr("按「适配宽度」或「实际大小」打开且图片高于窗口时显示顶部"),
                 "imageproc/align_top_when_large", false, true);

    sec = beginSection(page, tr("缓存与载入"));
    addSwitchRow(sec, tr("缓存下一张图片"), tr("提前解码下一张，翻页更快"),
                 "imageproc/cache_image", false, true);
    {
        SettingRowParts parts = addSettingRow(sec, tr("缓存大小"),
                                              tr("全尺寸解码缓存的占用上限，范围 64–4096 MB，越界按边界值生效"));
        m_cacheSize = new QSpinBox(parts.row);
        // 与 Config::cacheSizeMb() 的读回夹取同范围：输入、旧配置加载、实际
        // 上限共用 64–4096，设置页不再显示一个超出实际生效范围的数值。
        m_cacheSize->setRange(64, 4096);
        m_cacheSize->setButtonSymbols(QAbstractSpinBox::NoButtons);
        m_cacheSize->setFixedHeight(kComboH);
        m_cacheSize->setFixedWidth(88);
        m_cacheSize->setAlignment(Qt::AlignCenter);
        m_cacheSize->setAccessibleName(tr("缓存大小"));
        parts.controls->addWidget(m_cacheSize, 0, Qt::AlignVCenter);
        // 单位紧随数值，不单独占一列。
        auto *mb = new QLabel(tr("MB"), parts.row);
        mb->setObjectName(QStringLiteral("rowUnit"));
        parts.controls->addWidget(mb, 0, Qt::AlignVCenter);
    }
    addSwitchRow(sec, tr("先显示低品质图片以加速渲染"),
                 tr("大图先出低清预览，再补全清晰度"),
                 "imageproc/preview_mode", false, true);

    sec = beginSection(page, tr("透明背景"));
    addComboRow(sec, tr("为 GIF 图片使用透明背景"), tr("打开 GIF 时按所选方式铺底"),
                "imageproc/gif_transparent", QStringLiteral("0"),
                { { "0", tr("默认 GIF 调色板") }, { "1", tr("白色背景") },
                  { "2", tr("黑色背景") },
                  { "3", tr("中灰背景（自定义取色待实现）") } });
    addComboRow(sec, tr("为 32 位图片使用透明背景"),
                tr("带透明通道的图片按所选方式铺底"),
                "imageproc/img32_transparent", QStringLiteral("0"),
                { { "0", tr("网格背景") }, { "1", tr("白色背景") },
                  { "2", tr("黑色背景") }, { "3", tr("深色网格") } });

    sec = beginSection(page, tr("翻页效果"));
    addComboRow(sec, tr("翻页效果"), tr("下一次翻页时播放所选过渡"),
                "imageproc/pageturn_fx", QStringLiteral("1"),
                { { "0", tr("无") }, { "1", tr("淡入淡出") },
                  { "2", tr("Alpha 滑动") }, { "3", tr("滑动") } });

    sec = beginSection(page, tr("色彩管理"));
    addSwitchRow(sec, tr("使用图片文件的 ICC 设置"),
                 tr("打开带非 sRGB ICC 的图片时按配置转换上屏"),
                 "imageproc/image_icc", false, true);
    auto *iccMonitor = addSwitchRow(sec, tr("使用监视器的 ICC 设置"),
                                    tr("Qt6 光栅后端不支持显示器色彩管理，暂不可用"),
                                    "imageproc/monitor_icc", false, false);
    // 整行灰显（标题 + 说明 + 开关一起压暗），默认态就能读出「不可用」；
    // 原因写在说明与整行 tooltip 里，不只藏在开关上。
    iccMonitor->setEnabled(false);
    if (QWidget *row = iccMonitor->parentWidget()) {
        row->setEnabled(false);
        row->setCursor(Qt::ArrowCursor);
        row->setToolTip(tr("Qt6 光栅后端不支持显示器色彩管理，暂不可用"));
    }
    addHint(sec, tr("「自定义颜色」档暂无取色器，实际按中灰铺底；"
                    "透明背景与翻页效果在下次打开图片 / 翻页时生效。"));
    applyPageRhythm(page);
}

// ===========================================================================
// 4. 格式关联：说明 + 两个系统按钮 + 四组格式勾选网格
// ===========================================================================

void SettingsDialog::buildAssocPage(QWidget *page)
{
    // 2026-11-11 修订：删掉假的扩展名勾选、全选/反选、计数与「应用」——
    // 那些勾选没有任何消费点（默认打开方式只能由 Windows 确认）。改为
    // DirectoryLoader::imageGroups() 的只读支持列表 + 真实系统入口。
    QWidget *sec = beginSection(page, tr("Windows 默认应用"));
    auto *btnDefault = addButton(nullptr, tr("打开默认应用…"));
    btnDefault->setToolTip(tr("打开 Windows 设置，再按扩展名选择默认应用"));
    auto parts = addSettingRow(sec, tr("在 Windows 中设置文件关联"),
                               tr("打开系统设置，再按扩展名选择默认应用"));
    parts.controls->addWidget(btnDefault, 0, Qt::AlignVCenter);
    connect(btnDefault, &QPushButton::clicked, this, [this]() {
        const HINSTANCE r = ShellExecuteW(nullptr, L"open",
                                          L"ms-settings:defaultapps?registeredAppMachine=QuikView",
                                          nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(r) <= 32) {
            QMessageBox::warning(this, tr("格式关联"),
                                 tr("无法打开 Windows 默认应用，请从 Windows 设置手动打开。"));
        }
    });
    addHint(sec, tr("默认打开方式由 Windows 管理，QuikView 不能绕过系统直接改写；"
                    "若系统里找不到 QuikView，请先运行安装包注册。"));

    // 支持的格式：只读文本，分组与顺序全部来自解码白名单唯一事实来源，
    // 不在设置页另写第二份扩展名列表。
    sec = beginSection(page, tr("支持的格式"));
    const QList<DirectoryLoader::Group> groups = DirectoryLoader::imageGroups();
    for (const DirectoryLoader::Group &g : groups) {
        QStringList exts;
        exts.reserve(g.exts.size());
        for (const QString &e : g.exts)
            exts.append(e.toUpper());
        addSettingRow(sec, g.label, exts.join(QStringLiteral(" · ")));
    }
    addHint(sec, tr("以上为 QuikView 能直接打开的全部格式；列表只读，"
                    "新增格式请随程序版本更新。"));
    applyPageRhythm(page);
}

// ===========================================================================
// 5. 键盘：勾选 | 热键 | 操作 表格（14 行）
// ===========================================================================

void SettingsDialog::buildKeyboardPage(QWidget *page)
{
    auto *pageLay = qobject_cast<QVBoxLayout *>(page->layout());
    if (!pageLay)
        return;

    // 页首说明：默认态 / 录入态两种文案（规格 §焦点与键盘状态）。冲突时由
    // 红条顶替，不叠两条提示（设计图 06）。
    m_keyHint = new QLabel(page);
    m_keyHint->setObjectName(QStringLiteral("pageHint"));
    m_keyHint->setWordWrap(true);
    m_keyHint->setContentsMargins(kGenRowPadX, 0, kGenRowPadX, 6);
    m_keyHint->setText(tr("双击热键录入，Esc 清空。勾选即时生效，改绑需保存。"));
    pageLay->addWidget(m_keyHint);

    // 冲突红条：指出涉及的动作与键位，修正后自动消失（可被辅助技术读到）。
    m_keyBanner = new QWidget(page);
    m_keyBanner->setObjectName(QStringLiteral("keyBanner"));
    m_keyBanner->setAttribute(Qt::WA_StyledBackground, true);
    auto *bannerLay = new QHBoxLayout(m_keyBanner);
    bannerLay->setContentsMargins(10, 7, 10, 7);
    bannerLay->setSpacing(8);
    auto *bannerIcon = new QLabel(QStringLiteral("!"), m_keyBanner);
    bannerIcon->setObjectName(QStringLiteral("keyBannerIcon"));
    bannerIcon->setAlignment(Qt::AlignCenter);
    bannerIcon->setFixedSize(16, 16);
    bannerLay->addWidget(bannerIcon, 0, Qt::AlignVCenter);
    m_keyBannerText = new QLabel(m_keyBanner);
    m_keyBannerText->setObjectName(QStringLiteral("keyBannerText"));
    m_keyBannerText->setWordWrap(true);
    m_keyBannerText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bannerLay->addWidget(m_keyBannerText, 1);
    m_keyBanner->hide();
    pageLay->addWidget(m_keyBanner);

    auto *table = new QFrame(page);
    table->setObjectName(QStringLiteral("keyTable"));
    auto *tl = new QVBoxLayout(table);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(0);

    // 表头 30px：操作 / 热键 / 启用（规格 §布局与文字）。
    auto *head = new QWidget(table);
    head->setObjectName(QStringLiteral("keyHead"));
    head->setFixedHeight(30);
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);
    auto makeHeadCell = [head](const QString &text, int width, int leftPad = 8) {
        auto *lab = new QLabel(text, head);
        lab->setObjectName(QStringLiteral("keyHeadCell"));
        lab->setContentsMargins(leftPad, 0, 8, 0);
        if (width > 0)
            lab->setFixedWidth(width);
        return lab;
    };
    hl->addWidget(makeHeadCell(tr("操作"), 0, kGenRowPadX), 1);
    hl->addWidget(makeHeadCell(tr("热键"), 160, 0));
    hl->addWidget(makeHeadCell(tr("启用"), 52, 0));
    hl->addSpacing(8);
    tl->addWidget(head);

    // 单一滚动区：表头固定，分组头与行一起滚（窄窗 900×660 用同一支）。
    auto *rowsArea = new KeyRowsArea(table);
    rowsArea->setObjectName(QStringLiteral("keyRowsArea"));
    auto *rowsBox = new QWidget;
    rowsBox->setObjectName(QStringLiteral("keyRowsBox"));
    auto *rl0 = new QVBoxLayout(rowsBox);
    rl0->setContentsMargins(0, 0, 0, 6);
    rl0->setSpacing(0);

    // 默认键位 = MainWindow 出厂硬编码值（PortableText；幻灯片/裁剪出厂无单键）。
    // 四个分组按文件、浏览、显示与缩放、编辑与其他排列（规格 §焦点与键盘状态）。
    struct KeyDef {
        const char *group;
        const char *action;
        const char *hotkey;
        const char *label;
    };
    static const KeyDef defs[] = {
        { "file", "open",        "Ctrl+O",     QT_TR_NOOP("打开") },
        { "file", "openfolder",  "F",          QT_TR_NOOP("打开文件夹") },
        { "nav",  "next",        "Right",      QT_TR_NOOP("翻下一张") },
        { "nav",  "prev",        "Left",       QT_TR_NOOP("翻上一张") },
        { "nav",  "next_frame",  "Ctrl+Right", QT_TR_NOOP("下一帧") },
        { "nav",  "prev_frame",  "Ctrl+Left",  QT_TR_NOOP("上一帧") },
        { "nav",  "slideshow",   "",           QT_TR_NOOP("幻灯片") },
        { "view", "zoom_in",     "Ctrl++",     QT_TR_NOOP("放大") },
        { "view", "zoom_out",    "Ctrl+-",     QT_TR_NOOP("缩小") },
        { "view", "actual_size", "0",          QT_TR_NOOP("实际大小") },
        { "view", "fit",         "9",          QT_TR_NOOP("适配窗口") },
        { "edit", "crop",        "",           QT_TR_NOOP("裁剪") },
        { "edit", "favorite",    "B",          QT_TR_NOOP("收藏") },
        { "edit", "del",         "Del",        QT_TR_NOOP("删除") },
    };
    static const struct { const char *id; const char *title; } groups[] = {
        { "file", QT_TR_NOOP("文件") },
        { "nav",  QT_TR_NOOP("浏览") },
        { "view", QT_TR_NOOP("显示与缩放") },
        { "edit", QT_TR_NOOP("编辑与其他") },
    };

    QString currentGroup;
    int groupCount = 0;
    int groupHeadTotal = 0;   // 交给行区：分组头也占纵向空间
    constexpr int kKeyRowH = 36;
    constexpr int kKeyGroupHeadH = 22;

    for (int i = 0; i < int(sizeof(defs) / sizeof(defs[0])); ++i) {
        const KeyDef &def = defs[i];
        const QString groupId = QString::fromLatin1(def.group);
        if (groupId != currentGroup) {
            currentGroup = groupId;
            auto *gh = new QWidget(table);
            gh->setObjectName(QStringLiteral("keyGroupHead"));
            gh->setAttribute(Qt::WA_StyledBackground, true);
            gh->setFixedHeight(kKeyGroupHeadH);
            auto *gl = new QHBoxLayout(gh);
            gl->setContentsMargins(kGenRowPadX, 0, 8, 0);
            auto *gt = new QLabel(gh);
            gt->setObjectName(QStringLiteral("keyGroupTitle"));
            for (const auto &g : groups) {
                if (groupId == QLatin1String(g.id)) {
                    gt->setText(tr(g.title));
                    break;
                }
            }
            gl->addWidget(gt);
            gl->addStretch(1);
            rl0->addWidget(gh);
            groupHeadTotal += kKeyGroupHeadH;
            ++groupCount;
        } else {
            auto *line = new QFrame(table);
            line->setObjectName(QStringLiteral("keyRowLine"));
            line->setFixedHeight(1);
            rl0->addWidget(line);
        }

        auto *row = new QWidget(table);
        row->setObjectName(QStringLiteral("keyRow"));
        row->setFixedHeight(kKeyRowH - 1);
        row->setAttribute(Qt::WA_StyledBackground, true);
        row->setAttribute(Qt::WA_Hover, true);
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(0);

        auto *actionLab = new QLabel(tr(def.label), row);
        actionLab->setObjectName(QStringLiteral("keyAction"));
        actionLab->setContentsMargins(kGenRowPadX, 0, 8, 0);
        rl->addWidget(actionLab, 1);

        // 热键列：键帽固定 88 宽、左对齐；右侧留出行内状态文字
        // （「正在录入」/「冲突」），不挤压键帽本身。
        auto *editHost = new QWidget(row);
        editHost->setFixedWidth(160);
        auto *el = new QHBoxLayout(editHost);
        el->setContentsMargins(0, 0, 0, 0);
        el->setSpacing(6);
        auto *edit = new QKeySequenceEdit(editHost);
        edit->setFixedSize(88, 26);
        new KeySeqPlaceholderFixer(edit);
        new KeySeqEscClearFilter(edit);
        new KeySeqStateFilter(edit, [this]() { refreshKeyEditingState(); });
        el->addWidget(edit, 0, Qt::AlignVCenter);
        auto *stateLab = new QLabel(editHost);
        stateLab->setObjectName(QStringLiteral("keyRowState"));
        stateLab->hide();
        el->addWidget(stateLab, 0, Qt::AlignVCenter);
        el->addStretch(1);
        rl->addWidget(editHost);

        auto *chkHost = new QWidget(row);
        chkHost->setFixedWidth(52);
        auto *cl2 = new QHBoxLayout(chkHost);
        cl2->setContentsMargins(0, 0, 0, 0);
        // 启用列统一用 checkbox（表格内逐项启用），不再是 mini switch。
        auto *chk = new V2CheckBox(chkHost);
        chk->setObjectName(QStringLiteral("keyEnable"));
        chk->setAccessibleName(tr("%1 启用").arg(tr(def.label)));
        cl2->addWidget(chk, 0, Qt::AlignCenter);
        rl->addWidget(chkHost);
        rl->addSpacing(8);

        // 任一字段改动即重算冲突：红条、行内标记与保存按钮一起变。
        connect(chk, &QCheckBox::toggled, this, [this]() { refreshKeyConflictBanner(); });
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this,
                [this]() { refreshKeyConflictBanner(); });

        KeyRow kr;
        kr.cb = chk;
        kr.edit = edit;
        kr.actionLab = actionLab;
        kr.row = row;
        kr.action = QString::fromUtf8(def.action);
        kr.defHotkey = QString::fromUtf8(def.hotkey);
        kr.label = tr(def.label);
        kr.stateLab = stateLab;
        m_keyRows.append(kr);
        rl0->addWidget(row);
    }

    rowsArea->setWidget(rowsBox);
    // 单一表格滚动区（规格 §审图索引）：表格吃满页面剩余高度，表头在滚动区
    // 之外固定，分组头与 14 行一起滚；900×660 与 1068×822 用同一支布局，
    // 只由可用高度决定露出多少行。不再做整行吸附——设计图 08 本身就允许
    // 末行被视口边缘自然截断。
    tl->addWidget(rowsArea, 1);
    pageLay->addWidget(table, 1);

    applyPageRhythm(page);
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
    addSwitchRow(sec, tr("双击最大化 / 还原"),
                 tr("下次双击画布时按新值最大化 / 还原窗口"),
                 "mouse/dblclick_maximize", false, true);
    addSwitchRow(sec, tr("单击屏幕两侧时翻页"),
                 tr("下次单击画布两侧箭头区域时按新值翻页"),
                 "mouse/click_side_pageturn", false, false);

    sec = beginSection(page, tr("按键映射"));

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
        SettingRowParts parts = addSettingRow(sec, tr(b.label), QString());
        auto *combo = new QComboBox(parts.row);
        for (const auto &opt : opts)
            combo->addItem(opt.second, opt.first);
        // 档位诚实化（SET-04 阶段 C）：右键 / X / Y 没有 "pan" 的
        // onMouseAction 分支，拖拽平移只对左键内置拖拽有效，因此把该档灰显，
        // 不写成「选了就生效」。其余 17 个动作都走同一个派发器。
        const QString key = QString::fromUtf8(b.key);
        const bool panFake = (key == QLatin1String("mouse/btn_right")
                              || key == QLatin1String("mouse/btn_x")
                              || key == QLatin1String("mouse/btn_y"));
        if (panFake) {
            for (int j = 0; j < opts.size(); ++j) {
                if (opts[j].first == QLatin1String("pan")) {
                    if (auto *model = qobject_cast<QStandardItemModel *>(combo->model())) {
                        if (auto *item = model->item(j)) {
                            const QString why = tr("该按键没有拖拽平移实现，灰显不可选");
                            item->setEnabled(false);
                            item->setToolTip(why);
                            item->setData(why, Qt::UserRole + 1);
                        }
                    }
                }
            }
        }
        // 中键 / 滚轮的真实桥接档位（SET-04 批次 8）：中键只写 config/middle_pan
        // （拖拽平移 / 无），滚轮只写 config/wheel_zoom（上=放大、下=缩小；任一
        // 为「无」即关闭滚轮缩放）。其余档位没有运行时消费点 → 灰显并提示。
        QStringList allowed;
        QString denyTip;
        if (key == QLatin1String("mouse/btn_middle")) {
            allowed << QStringLiteral("none") << QStringLiteral("pan");
            denyTip = tr("中键暂无该动作实现；仅「无操作 / 拖拽平移」生效（「无」会连现有的单击全屏一起停用）");
        } else if (key == QLatin1String("mouse/wheel_up")) {
            allowed << QStringLiteral("none") << QStringLiteral("zoom_in");
            denyTip = tr("滚轮向上暂无该动作实现；仅「无操作 / 放大」生效（「无」会关闭滚轮缩放）");
        } else if (key == QLatin1String("mouse/wheel_down")) {
            allowed << QStringLiteral("none") << QStringLiteral("zoom_out");
            denyTip = tr("滚轮向下暂无该动作实现；仅「无操作 / 缩小」生效（「无」会关闭滚轮缩放）");
        }
        if (!allowed.isEmpty()) {
            for (int j = 0; j < opts.size(); ++j) {
                if (allowed.contains(opts[j].first))
                    continue;
                if (auto *model = qobject_cast<QStandardItemModel *>(combo->model())) {
                    if (auto *item = model->item(j)) {
                        item->setEnabled(false);
                        item->setToolTip(denyTip);
                        item->setData(denyTip, Qt::UserRole + 1);
                    }
                }
            }
        }
        combo->setToolTip(tr("下次触发该按键时按新映射派发"));
        combo->setCursor(Qt::PointingHandCursor);
        combo->setFixedHeight(kComboH);
        combo->setProperty("reqW", 170);
        combo->setFixedWidth(170);
        parts.controls->addWidget(combo, 0, Qt::AlignVCenter);

        if (!b.configurable)
            markUnavailable(parts.row);   // 整行（行标签 + 下拉）一起灰

        m_mouseRows.append({ combo, QString::fromUtf8(b.key), QString::fromUtf8(b.def) });
    }

    addHint(sec, tr("「无」保留内置行为：左键拖动平移与边缘单击翻页、右键上下文菜单。"
                    "滚轮上下共用一个缩放开关，任一方向选「无」会同时停掉双向缩放；"
                    "中键选「无」会同时停掉拖拽平移与现有的单击全屏。"
                    "中键只支持「无操作 / 拖拽平移」，滚轮只支持「无操作 / 放大 / 缩小」；"
                    "其余档位已灰显，属实现缺口。所有映射在下次触发按键时生效。"));
    applyPageRhythm(page);
}

// ===========================================================================
// 7. 书签
// ===========================================================================

void SettingsDialog::buildBookmarkPage(QWidget *page)
{
    QWidget *sec = beginSection(page, tr("书签行为"));
    // 自动书签与最大书签数已接通；「多窗口共享」在单实例语义下无实现，
    // 2026-11-04 按视觉基线 P0 删除（原为勾着 + 禁用 + 零写回）。
    addSwitchRow(sec, tr("自动保存当前图片路径为书签"),
                 tr("打开图片时自动追加一条；翻页不追加"),
                 "bookmark/auto_bookmark", false, false);
    // 「自动记忆当前图片在压缩包中的路径」与「删除已记忆位置」已删：
    // 压缩包浏览未实现，压缩包路径记忆无从产生，两项勾/点了也不会有任何事发生。

    sec = beginSection(page, tr("书签容量"));
    {
        SettingRowParts parts = addSettingRow(
            sec, tr("最大书签数"),
            tr("新增书签超过上限时，从最早的书签开始淘汰"));
        m_maxBookmarks = new QComboBox(parts.row);
        for (int v : { 10, 20, 30, 50, 100 })
            m_maxBookmarks->addItem(QString::number(v), v);
        m_maxBookmarks->setCursor(Qt::PointingHandCursor);
        m_maxBookmarks->setFixedHeight(kComboH);
        m_maxBookmarks->setFixedWidth(110);
        m_maxBookmarks->setAccessibleName(tr("最大书签数"));
        parts.controls->addWidget(m_maxBookmarks, 0, Qt::AlignVCenter);
    }
    // 已接配置：Config::toggleFavorite 超限时从尾部淘汰最早的书签
    // （键 config/bookmark/max_bookmarks，默认 50，clamp 10..100）。
    addHint(sec, tr("上限在下次新增书签时读取，已有书签不会在保存瞬间被裁剪。"
                    "书签记录完整路径，文件移动或重命名后原条目失效；"
                    "数据保存在本机配置中，不随安装包或账户同步。"));
    applyPageRhythm(page);
}

// ===========================================================================
// 8. 上下文菜单
// ===========================================================================

void SettingsDialog::buildContextMenuPage(QWidget *page)
{
    // 状态真实性（2026-11-11 修订）：DLL 存在只说「已检测到壳扩展文件」；
    // 只有 CLSID 的 InProcServer32 指向本机 DLL 才写「已注册」。PSD/PSB 的
    // 缩略图能力按实际解码器显示为部分支持，并说明无内嵌 JPEG 时的回退。
    const bool dllPresent = shellExtDllPresent();
    const bool registered = shellExtRegistered();

    auto addStatusRow = [this](QWidget *section, const QString &label,
                               const QString &detail, const QString &state,
                               const char *color) {
        SettingRowParts parts = addSettingRow(section, label, detail);
        auto *value = new QLabel(state, parts.row);
        value->setObjectName(QStringLiteral("rowTag"));
        value->setWordWrap(false);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setStyleSheet(QStringLiteral("color:%1;font-size:11px;")
                                 .arg(QLatin1String(color)));
        value->setToolTip(detail.isEmpty() ? state : detail);
        parts.controls->addWidget(value, 0, Qt::AlignVCenter);
    };
    const char *kOk = UiStyle::fluent::statusOk();
    const char *kWarn = UiStyle::fluent::statusWarn();
    const char *kMuted = UiStyle::fluent::desc();

    // 注册态：已注册 → 绿色「已注册」；仅文件在 → 提醒色「已检测到壳扩展文件」。
    const QString menuState = registered ? tr("已注册")
                                         : (dllPresent ? tr("已检测到壳扩展文件")
                                                       : tr("未检测到壳扩展"));
    const char *menuColor = registered ? kOk : kWarn;

    QWidget *sec = beginSection(page, tr("上下文菜单设定"));
    // 本版壳扩展真实提供两个菜单项（浏览 / 转换），其余为规划或自绘能力，
    // 不能因为 DLL 在就写成已注册。
    addStatusRow(sec, tr("在资源管理器中使用上下文菜单"), QString(), menuState, menuColor);
    addStatusRow(sec, tr("用 QuikView 转换图片"), QString(), menuState, menuColor);
    addStatusRow(sec, tr("用 QuikView 浏览图片"), QString(), menuState, menuColor);
    addStatusRow(sec, tr("上下文菜单预览"),
                 tr("已实现自绘缩略图（≤256 px）"), tr("待资源管理器验收"), kMuted);
    addStatusRow(sec, tr("显示图片信息"),
                 tr("宽 × 高 · 文件大小"), tr("已支持"), kOk);
    addStatusRow(sec, tr("显示软件图标"),
                 tr("壳扩展内嵌资源图标"), tr("已支持"), kOk);
    if (!dllPresent)
        addHint(sec, tr("未检测到壳扩展文件，重新运行安装包可修复。"));
    else if (!registered)
        addHint(sec, tr("已检测到壳扩展文件，但系统里还没有注册记录。"
                        "运行安装包或以管理员身份注册后，这里会显示「已注册」。"));

    sec = beginSection(page, tr("资源管理器缩略图"));
    addStatusRow(sec, tr("WebP"), tr("Qt 解码器直接生成缩略图"),
                 tr("壳扩展提供"), kOk);
    addStatusRow(sec, tr("PSD"), tr("缺少内嵌 JPEG 时显示默认图标"),
                 tr("部分支持"), kWarn);
    addStatusRow(sec, tr("PSB"), tr("缺少内嵌 JPEG 时显示默认图标"),
                 tr("部分支持"), kWarn);

    addHint(sec, tr("这些入口由 QuikView 壳扩展在资源管理器中提供，"
                    "不是应用内图片右键菜单的功能开关；"
                    "安装 / 卸载安装包时自动注册 / 移除。"));
    applyPageRhythm(page);
}

// ===========================================================================
// 9. 杂项
// ===========================================================================

void SettingsDialog::buildEtcPage(QWidget *page)
{
    // 路径输入组：输入框与「浏览 / 打开」按钮保持同一组、统一右对齐。
    QWidget *sec = beginSection(page, tr("常用路径"));
    auto makeFolderRow = [this, sec](const QString &label, const QString &cfgKey,
                                     QLineEdit **edit) {
        SettingRowParts parts = addSettingRow(sec, label, tr("留空表示不使用该文件夹"));
        *edit = new QLineEdit(parts.row);
        (*edit)->setFixedHeight(kInputH);
        (*edit)->setFixedWidth(210);
        (*edit)->setAccessibleName(label);
        parts.controls->addWidget(*edit, 0, Qt::AlignVCenter);
        auto *btnBrowse = addButton(nullptr, tr("浏览…"));
        auto *btnOpen = addButton(nullptr, tr("打开"));
        parts.controls->addWidget(btnBrowse, 0, Qt::AlignVCenter);
        parts.controls->addWidget(btnOpen, 0, Qt::AlignVCenter);
        connect(btnBrowse, &QPushButton::clicked, this, [this, edit, cfgKey]() {
            const QString d = QFileDialog::getExistingDirectory(
                this, tr("选择一个文件夹."), (*edit)->text());
            if (!d.isEmpty()) {
                (*edit)->setText(d);
                applyInstantValue(cfgKey, d);
            }
        });
        connect(btnOpen, &QPushButton::clicked, this, [this, edit]() {
            const QString d = (*edit)->text().trimmed();
            if (d.isEmpty()) {
                QMessageBox::warning(this, tr("打开文件夹"), tr("请先填写文件夹路径。"));
                return;
            }
            if (!QFileInfo::exists(d)) {
                QMessageBox::warning(this, tr("打开文件夹"),
                                     tr("文件夹不存在：\n%1").arg(d));
                return;
            }
            if (!QDesktopServices::openUrl(QUrl::fromLocalFile(d))) {
                QMessageBox::warning(this, tr("打开文件夹"),
                                     tr("无法调用系统文件管理器打开：\n%1").arg(d));
            }
        });
    };
    makeFolderRow(tr("图片文件夹 1"), QStringLiteral("etc/photo_folder_1"), &m_photoFolder1);
    makeFolderRow(tr("图片文件夹 2"), QStringLiteral("etc/photo_folder_2"), &m_photoFolder2);
    {
        SettingRowParts parts = addSettingRow(
            sec, tr("图像编辑器程序路径"), tr("留空时使用系统默认程序"));
        m_editorPath = new QLineEdit(parts.row);
        m_editorPath->setFixedHeight(kInputH);
        m_editorPath->setFixedWidth(210);
        m_editorPath->setAccessibleName(tr("图像编辑器程序路径"));
        parts.controls->addWidget(m_editorPath, 0, Qt::AlignVCenter);
        auto *btnBrowse = addButton(nullptr, tr("浏览…"));
        auto *btnPreset = addButton(nullptr, tr("预设"));
        parts.controls->addWidget(btnBrowse, 0, Qt::AlignVCenter);
        parts.controls->addWidget(btnPreset, 0, Qt::AlignVCenter);
        connect(btnBrowse, &QPushButton::clicked, this, [this]() {
            const QString f = QFileDialog::getOpenFileName(
                this, tr("选择图像编辑器"), m_editorPath->text(),
                tr("程序 (*.exe);;所有文件 (*)"));
            if (!f.isEmpty()) {
                m_editorPath->setText(f);
                applyInstantValue(QStringLiteral("etc/image_editor"), f);
            }
        });
        connect(btnPreset, &QPushButton::clicked, this, [this]() {
            const QString preset = QStringLiteral("C:/Windows/System32/mspaint.exe");
            m_editorPath->setText(preset);
            applyInstantValue(QStringLiteral("etc/image_editor"), preset);
        });
    }

    sec = beginSection(page, tr("RAW 图片"));
    // 本批已接线：翻页音效 / 按图调窗 / 禁用屏保 / 不打开 RAW / JPEG 同名过滤 / RAW 缩略图。
    addSwitchRow(sec, tr("不打开 RAW 图片文件"),
                 tr("下次打开 RAW 时生效"), "etc/no_raw_load", false, false);
    addSwitchRow(sec, tr("打开 JPEG 文件时，不打开同名 RAW 文件"),
                 tr("打开 JPEG 时不再自动带出同名 RAW"),
                 "etc/skip_raw_when_jpg", false, true);
    addSwitchRow(sec, tr("打开 RAW 图片文件时，显示已有缩略图"),
                 tr("打开 RAW 时先显示内嵌缩略图"),
                 "etc/raw_thumbnail", false, true);

    sec = beginSection(page, tr("窗口与声音"));
    addSwitchRow(sec, tr("翻页时播放音效"), tr("下次翻页时生效"),
                 "etc/pageturn_wav", false, false);
    addSwitchRow(sec, tr("根据图片大小调节窗口大小"),
                 tr("默认关闭；开启后下次打开图片时生效"),
                 "etc/fit_window_to_image", false, false);
    addSwitchRow(sec, tr("运行 QuikView 时禁用 Windows 屏幕保护程序"),
                 tr("退出 QuikView 后恢复系统屏保设置"),
                 "etc/disable_screensaver", false, false);

    addHint(sec, tr("RAW 选项只影响 RAW 文件的打开方式；勾选「不打开 RAW 图片文件」时，"
                    "JPEG 旁的 RAW 也不会被自动带出。翻页音效在下次翻页时生效。"));
    applyPageRhythm(page);
}

// ===========================================================================
// 10. 语言
// ===========================================================================

void SettingsDialog::buildLangPage(QWidget *page)
{
    QWidget *sec = beginSection(page, tr("界面语言"));
    {
        SettingRowParts parts = addSettingRow(
            sec, tr("界面语言"),
            tr("保存后生效；重启 QuikView 后切换界面"));
        m_language = new QComboBox(parts.row);
        populateLanguageCombo(m_language);
        m_language->setCursor(Qt::PointingHandCursor);
        m_language->setFixedHeight(kComboH);
        m_language->setFixedWidth(200);
        m_language->setAccessibleName(tr("界面语言"));
        parts.controls->addWidget(m_language, 0, Qt::AlignVCenter);
    }

    sec = beginSection(page, tr("语言资源"));
    {
        SettingRowParts parts = addSettingRow(
            sec, tr("打开语言文件夹"),
            tr("从 translations 目录加载 .qm 语言文件"));
        auto *btn = addButton(nullptr, tr("打开文件夹"));
        parts.controls->addWidget(btn, 0, Qt::AlignVCenter);
        connect(btn, &QPushButton::clicked, this, [this]() {
            const QString dir = QCoreApplication::applicationDirPath()
                                + QStringLiteral("/translations");
            QDir().mkpath(dir);
            if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
                QMessageBox::warning(this, tr("打开语言文件夹"),
                                     tr("无法调用系统文件管理器打开：\n%1").arg(dir));
            }
        });
    }
    {
        SettingRowParts parts = addSettingRow(
            sec, tr("下载更多语言"),
            tr("在浏览器中打开项目页面获取语言包"));
        auto *btn = addButton(nullptr, tr("下载更多语言"));
        parts.controls->addWidget(btn, 0, Qt::AlignVCenter);
        connect(btn, &QPushButton::clicked, this, [this]() {
            // 外部网页：只报告系统是否接受了打开请求；网络可达性不做验证。
            if (!QDesktopServices::openUrl(
                    QUrl(QStringLiteral("https://github.com/xdsT2/quikview")))) {
                QMessageBox::warning(this, tr("下载更多语言"),
                                     tr("无法打开浏览器。请手动访问：\n"
                                        "https://github.com/xdsT2/quikview"));
            }
        });
    }
    addHint(sec, tr("语言文件按程序目录下的 translations 加载；"
                    "两个入口都会离开 QuikView，应用内只报告启动是否成功。"
                    "语言选择是待保存项：关闭时可选「保存并关闭」，重启后生效。"));
    applyPageRhythm(page);
}

// ===========================================================================
// 配置读写
// ===========================================================================

void SettingsDialog::loadFromConfig()
{
    m_loading = true;
    for (const ChkBind &b : m_chkBinds) {
        const bool v = bGet(b.key, b.def);
        b.cb->setChecked(b.inverted ? !v : v);
    }
    for (const ComboBind &b : m_comboBinds) {
        const int idx = b.combo->findData(sGet(b.key, b.def));
        b.combo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    // 旧配置 edge_action="ask"：「询问」档灰显（无实现），运行态已由
    // Config::edgeAction() 折算为「停止」，下拉显示同步折算，不再呈现
    // 「选着一个灰项」；配置原值保留，翻页批次 2 交付后按新定义启用。
    for (const ComboBind &b : m_comboBinds) {
        if (qstrcmp(b.key, "edge_action") != 0
            || sGet(b.key, b.def) != QLatin1String("ask"))
            continue;
        const int stopIdx = b.combo->findData(QStringLiteral("stop"));
        if (stopIdx >= 0)
            b.combo->setCurrentIndex(stopIdx);
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
        const QString stored = sGet(r.key.toLatin1().constData(), r.def);
        int idx = r.combo->findData(stored);
        // 旧配置里的不可执行档位（灰显项）按运行态安全值「无操作」显示：
        // 滚轮任一方向非对应档 → 双向缩放都停；中键非「拖拽平移」→ 连单击
        // 全屏一起停；右键 / X / Y 的 pan 在 Config 读取时折算回内置行为。
        // 配置原值不回写，翻页批次 2 的独立映射迁移仍读原始档位。
        if (idx >= 0) {
            if (auto *model = qobject_cast<QStandardItemModel *>(r.combo->model())) {
                if (auto *item = model->item(idx); item && !item->isEnabled())
                    idx = r.combo->findData(QStringLiteral("none"));
            }
        }
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
    // 外观页两件套不进绑定表，单独回读（历史 theme 三值 → 跟随 + 浅色两开关）。
    if (m_themeCombo)
        syncThemeControls(sGet("theme", QStringLiteral("dark")));
    if (m_blurMaterial)
        m_blurMaterial->setChecked(bGet("ui/blur_material", false));
    m_loading = false;
}

// ---------------------------------------------------------------------------
// 即时写盘（普通项默认路径）+ 待保存项（热键 / 语言）
// ---------------------------------------------------------------------------

void SettingsDialog::refreshSettingsInstanceIfError()
{
    QSettings &s = Config::instance().settings();
    if (s.status() == QSettings::NoError)
        return;
    // QSettings::status() 会被首个错误粘住：重建实例并重放当前内存键，
    // 让「留在设置页稍后重试」拿到干净状态；同时不丢主窗口刚写入的键。
    QHash<QString, QVariant> snapshot;
    for (const QString &k : s.allKeys())
        snapshot.insert(k, s.value(k));
    Config::instance().reopenSettings();
    QSettings &retry = Config::instance().settings();
    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it)
        retry.setValue(it.key(), it.value());
}

bool SettingsDialog::writeValues(const QList<QPair<QString, QVariant>> &values, bool retryable,
                                 QString *error)
{
    refreshSettingsInstanceIfError();
    QSettings &s = Config::instance().settings();

    // 记录旧值：草稿写失败时要按旧值回滚内存，不能把用户放弃的键留在
    // QSettings 里（否则「不保存」路径的 sync 会把草稿刷到磁盘）。
    QHash<QString, QVariant> previous;
    QSet<QString> previousPresent;
    for (const auto &kv : values) {
        const QString full = QStringLiteral("config/") + kv.first;
        if (s.contains(full))
            previousPresent.insert(full);
        previous.insert(full, s.value(full));
        s.setValue(full, kv.second);
        if (retryable)
            m_pendingWrites.insert(kv.first, kv.second);
    }
    s.sync();
    if (s.status() == QSettings::NoError) {
        if (retryable) {
            for (const auto &kv : values)
                m_pendingWrites.remove(kv.first);
        }
        return true;
    }

    if (!retryable) {
        for (const auto &kv : values) {
            const QString full = QStringLiteral("config/") + kv.first;
            if (previousPresent.contains(full))
                s.setValue(full, previous.value(full));
            else
                s.remove(full);
        }
    }
    if (error)
        *error = tr("配置文件写入失败（%1）")
                     .arg(QString::number(static_cast<int>(s.status())));
    return false;
}

bool SettingsDialog::commitValues(const QList<QPair<QString, QVariant>> &values, QString *error)
{
    // 热键改绑 / 语言草稿：失败即回滚内存，草稿只存在于界面上。
    return writeValues(values, false, error);
}

bool SettingsDialog::flushPendingWrites(QString *error)
{
    if (m_pendingWrites.isEmpty()) {
        if (!m_forceSync)
            return true;
        refreshSettingsInstanceIfError();
        QSettings &s = Config::instance().settings();
        s.sync();
        if (s.status() != QSettings::NoError) {
            if (error)
                *error = tr("配置文件写入失败（%1）")
                             .arg(QString::number(static_cast<int>(s.status())));
            return false;
        }
        m_forceSync = false;
        return true;
    }
    QList<QPair<QString, QVariant>> values;
    for (auto it = m_pendingWrites.cbegin(); it != m_pendingWrites.cend(); ++it)
        values.append({ it.key(), it.value() });
    if (!writeValues(values, true, error))
        return false;
    m_forceSync = false;
    return true;
}

void SettingsDialog::warnSaveFailure(const QString &error)
{
    QMessageBox::warning(this, tr("设置"),
                         tr("保存失败：%1。\n"
                            "改动仍保留在界面上，可稍后重试。")
                             .arg(error));
}

bool SettingsDialog::applyInstantValue(const QString &key, const QVariant &value)
{
    if (m_loading)
        return true;
    // 运行态由设置页在可下发项上即时调用 MainWindow::applyLiveSetting；
    // 本函数只保证「不点保存也落盘」，并与关闭提交共用同一个写盘入口。
    return writeValues({{ key, value }}, true, nullptr);
}

void SettingsDialog::writeBridgeKeys()
{
    // —— 兼容桥：运行逻辑沿用的旧 key 由新设置项推导 ——
    // 三档必须一一对应；Config::interpolation() 会把 fast→bilinear、
    // smooth→bicubic 翻译成画布认识的模式，lanczos 原样透传。
    // 只有推导值与现有配置不同才写，未变更键不重写。
    QSettings &s = Config::instance().settings();
    const auto writeIfChanged = [&s](const QString &key, const QVariant &want) {
        if (!s.value(QStringLiteral("config/") + key).isValid()
            || s.value(QStringLiteral("config/") + key) != want)
            return QPair<QString, QVariant>(key, want);
        return QPair<QString, QVariant>();
    };

    const QString interp = sGet("imageproc/interpolate", QStringLiteral("0"));
    const QPair<QString, QVariant> interpPair = writeIfChanged(
        QStringLiteral("interpolation"),
        interp == QStringLiteral("0") ? QStringLiteral("fast")
            : interp == QStringLiteral("1") ? QStringLiteral("smooth")
                                           : QStringLiteral("lanczos"));
    const QPair<QString, QVariant> wheelPair = writeIfChanged(
        QStringLiteral("wheel_zoom"),
        sGet("mouse/wheel_up", QStringLiteral("zoom_in")) == QStringLiteral("zoom_in")
            && sGet("mouse/wheel_down", QStringLiteral("zoom_out")) == QStringLiteral("zoom_out"));
    const QPair<QString, QVariant> middlePair = writeIfChanged(
        QStringLiteral("middle_pan"),
        sGet("mouse/btn_middle", QStringLiteral("pan")) == QStringLiteral("pan"));

    QList<QPair<QString, QVariant>> changes;
    for (const QPair<QString, QVariant> &p : { interpPair, wheelPair, middlePair }) {
        if (!p.first.isEmpty())
            changes.append(p);
    }
    if (!changes.isEmpty())
        writeValues(changes, true, nullptr);   // 兼容桥随即时项一起重试，不丢推导值
}

void SettingsDialog::connectInstantSignals()
{
    const auto isLive = [this](QCheckBox *cb) {
        for (const LiveCheck &item : m_liveChecks) {
            if (item.cb == cb)
                return true;
        }
        return false;
    };
    for (const ChkBind &b : m_chkBinds) {
        if (isLive(b.cb))
            continue;   // 即时预览项由 applyLiveCheck 统一下发 + 写盘
        connect(b.cb, &QCheckBox::toggled, this, [this, b](bool on) {
            applyInstantValue(QString::fromLatin1(b.key),
                              b.inverted ? !on : on);
            updatePendingNote();
        });
    }
    for (const ComboBind &b : m_comboBinds) {
        connect(b.combo, &QComboBox::currentIndexChanged, this, [this, b]() {
            applyInstantValue(QString::fromLatin1(b.key),
                              b.combo->currentData().toString());
            writeBridgeKeys();
        });
    }
    if (m_cacheSize) {
        connect(m_cacheSize, &QSpinBox::valueChanged, this, [this](int v) {
            applyInstantValue(QStringLiteral("imageproc/cache_size_mb"), v);
        });
    }
    for (const MouseRow &r : m_mouseRows) {
        auto *combo = r.combo;
        const QString key = r.key;
        connect(combo, &QComboBox::currentIndexChanged, this, [this, key, combo]() {
            applyInstantValue(key, combo->currentData().toString());
            writeBridgeKeys();
        });
    }
    if (m_maxBookmarks) {
        connect(m_maxBookmarks, &QComboBox::currentIndexChanged, this, [this]() {
            applyInstantValue(QStringLiteral("bookmark/max_bookmarks"),
                              m_maxBookmarks->currentData().toInt());
        });
    }
    if (m_photoFolder1) {
        connect(m_photoFolder1, &QLineEdit::editingFinished, this, [this]() {
            applyInstantValue(QStringLiteral("etc/photo_folder_1"), m_photoFolder1->text());
        });
    }
    if (m_photoFolder2) {
        connect(m_photoFolder2, &QLineEdit::editingFinished, this, [this]() {
            applyInstantValue(QStringLiteral("etc/photo_folder_2"), m_photoFolder2->text());
        });
    }
    if (m_editorPath) {
        connect(m_editorPath, &QLineEdit::editingFinished, this, [this]() {
            applyInstantValue(QStringLiteral("etc/image_editor"), m_editorPath->text());
        });
    }
    if (m_blurMaterial) {
        connect(m_blurMaterial, &QCheckBox::toggled, this, [this](bool on) {
            if (m_loading)
                return;
            applyInstantValue(QStringLiteral("ui/blur_material"), on);
            UiStyle::setBlurMaterial(on);
            refreshDialogStyle();
        });
    }
    // 热键改绑 / 语言只更新待保存计数，不写盘；「启用」是即时项：
    // 先写配置，再在切换点让 MainWindow 重装热键表（不能等对话框关闭）。
    for (const KeyRow &r : m_keyRows) {
        const QString action = r.action;
        QCheckBox *cb = r.cb;
        connect(cb, &QCheckBox::toggled, this, [this, action, cb](bool on) {
            if (m_loading)
                return;
            const QString key = QStringLiteral("keyboard/%1_enabled").arg(action);
            applyInstantValue(key, on);
            // 运行态即时生效：失败时控件回退，避免「看是关、实际还能按」。
            if (MainWindow *host = qobject_cast<MainWindow *>(parentWidget())) {
                QString error;
                if (!host->applyLiveSetting(key, on, &error)) {
                    {
                        const QSignalBlocker b(cb);
                        cb->setChecked(!on);
                    }
                    applyInstantValue(key, !on);
                    QMessageBox::warning(this, tr("设置"),
                                         tr("「%1」未能即时生效：%2\n已恢复原值。")
                                             .arg(cb->text(), error));
                }
            }
        });
        connect(r.edit, &QKeySequenceEdit::keySequenceChanged, this, [this]() {
            updatePendingNote();
        });
    }
    if (m_language) {
        connect(m_language, &QComboBox::currentIndexChanged, this, [this]() {
            updatePendingNote();
        });
    }
}

void SettingsDialog::capturePendingBaseline()
{
    m_keyBaseline.clear();
    for (const KeyRow &r : m_keyRows) {
        m_keyBaseline.insert(r.action,
                             r.edit->keySequence().toString(QKeySequence::PortableText));
    }
    if (m_language)
        m_langBaseline = m_language->currentData().toString();
}

QStringList SettingsDialog::pendingChangeNames() const
{
    QStringList names;
    for (const KeyRow &r : m_keyRows) {
        const QString now = r.edit->keySequence().toString(QKeySequence::PortableText);
        if (now != m_keyBaseline.value(r.action))
            names << tr("热键：%1").arg(r.label);
    }
    if (m_language && m_language->currentData().toString() != m_langBaseline)
        names << tr("界面语言");
    return names;
}

void SettingsDialog::updatePendingNote()
{
    if (!m_footerNote)
        return;
    const QStringList names = pendingChangeNames();
    if (names.isEmpty()) {
        m_footerNote->setText(tr("普通设置即时生效；热键改绑与界面语言在保存后生效"));
        return;
    }
    m_footerNote->setText(tr("有 %1 项待保存（%2）；关闭时会询问")
                              .arg(names.size())
                              .arg(names.join(QStringLiteral("、"))));
}

// ---------------------------------------------------------------------------
// 侧栏搜索：按设置名 / 分组名 / 选项文字匹配，命中页签保留并跳转
// ---------------------------------------------------------------------------

void SettingsDialog::buildSearchIndex()
{
    m_pageSearchIndex.clear();
    for (QWidget *page : m_pages) {
        QStringList parts;
        for (QLabel *lab : page->findChildren<QLabel *>()) {
            const QString name = lab->objectName();
            if (name == QLatin1String("rowLabel")
                || name == QLatin1String("rowDesc")
                || name == QLatin1String("sectionTitle")
                || name == QLatin1String("pageTitle")
                || name == QLatin1String("pageDesc")
                || name == QLatin1String("pageHint")) {
                parts << lab->text();
            }
        }
        for (QComboBox *combo : page->findChildren<QComboBox *>()) {
            for (int i = 0; i < combo->count(); ++i)
                parts << combo->itemText(i);
        }
        for (QPushButton *btn : page->findChildren<QPushButton *>())
            parts << btn->text();
        m_pageSearchIndex << parts.join(QStringLiteral(" ")).toLower();
    }
}

void SettingsDialog::selectFirstVisiblePage()
{
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (!m_tabs[i]->isHidden()) {
            selectPage(i);
            return;
        }
    }
}

void SettingsDialog::filterNavigation(const QString &text)
{
    const QString term = text.trimmed().toLower();
    bool any = false;
    for (int i = 0; i < m_tabs.size(); ++i) {
        const bool hit = term.isEmpty() || m_pageSearchIndex.value(i).contains(term);
        m_tabs[i]->setVisible(hit);
        any = any || hit;
    }
    if (m_searchEmpty)
        m_searchEmpty->setVisible(!term.isEmpty() && !any);
    // 当前页被过滤掉时跳到第一个命中页，避免右侧停在「无匹配」的旧页。
    if (!term.isEmpty() && any) {
        const int cur = m_stack ? m_stack->currentIndex() : 0;
        if (cur >= 0 && cur < m_tabs.size() && m_tabs[cur]->isHidden())
            selectFirstVisiblePage();
    }
}

// ---------------------------------------------------------------------------
// SET-02 即时预览会话
// ---------------------------------------------------------------------------

void SettingsDialog::connectLiveChecks()
{
    for (int i = 0; i < m_liveChecks.size(); ++i) {
        LiveCheck &item = m_liveChecks[i];
        item.lastValue = item.inverted ? !item.cb->isChecked() : item.cb->isChecked();
        connect(item.cb, &QCheckBox::toggled, this, [this, i](bool on) {
            applyLiveCheck(i, on);
        });
    }
}

void SettingsDialog::applyLiveCheck(int index, bool checked)
{
    if (m_loading || index < 0 || index >= m_liveChecks.size())
        return;
    LiveCheck &item = m_liveChecks[index];
    const bool want = item.inverted ? !checked : checked;
    if (want == item.lastValue)   // 反复切换/程序化回设：幂等，不重复下发或写盘
        return;

    const QString key = QString::fromLatin1(item.key);
    MainWindow *host = qobject_cast<MainWindow *>(parentWidget());
    QString error;
    if (!host)
        error = tr("未找到主窗口，无法下发运行态。");
    if (!host || !host->applyLiveSetting(key, want, &error)) {
        // 运行态没变 → 控件必须退回上一次成功值，避免 UI 与实际状态分叉；
        // 配置未写入，不会留下半套状态。
        const bool previousChecked = item.inverted ? !item.lastValue : item.lastValue;
        {
            const QSignalBlocker b(item.cb);
            item.cb->setChecked(previousChecked);
        }
        if (host)
            host->applyLiveSetting(key, item.lastValue, nullptr);   // 兜底：运行态回到上次成功值
        QMessageBox::warning(this, tr("设置"),
                             tr("「%1」未能即时生效：%2\n已恢复原值，未写入配置。")
                                 .arg(item.cb->text(), error));
        return;
    }

    // 即时生效 + 立刻写盘：关闭窗口（含「不保存」）也保留该值。
    item.lastValue = want;
    applyInstantValue(key, want);
}

bool SettingsDialog::onAccepted()
{
    // —— 键盘冲突检测先于一切写盘：同一键位被两个启用的动作占用（或撞上
    // 固定动作、同一动作被重复指派）→ 高亮冲突行、提示占用者、整次保存拒绝 ——
    clearKeyConflicts();
    const QString conflictMsg = keyboardConflicts();
    if (!conflictMsg.isEmpty()) {
        // 不写盘、不关闭：用户的改动留在界面上，改完可再次保存成功（P0-1）。
        QMessageBox::warning(this, tr("设置"), conflictMsg);
        return false;
    }

    // 排队中的主题切换先落运行态（写盘经 applyInstantValue 同一入口）。
    flushPendingTheme();

    // —— 只提交实际变更的键：普通即时项在变更点已经落盘，这里不再整表重写。
    // 待保存项只有热键改绑与界面语言；「启用」是即时项，不在这里重复写。 ——
    QList<QPair<QString, QVariant>> changes;
    QSettings &s = Config::instance().settings();
    for (const KeyRow &r : m_keyRows) {
        // _key 存 PortableText（"Ctrl+P"、"Right"…），MainWindow 按
        // QKeySequence::fromString(…, PortableText) 读回。
        const QString now = r.edit->keySequence().toString(QKeySequence::PortableText);
        if (now != m_keyBaseline.value(r.action)) {
            changes.append({ QStringLiteral("keyboard/%1_key").arg(r.action), now });
        }
        // 动作固定；仅当历史改绑残留的旧值与目标动作不一致时才清理一次。
        const QString actionKey = QStringLiteral("keyboard/%1_action").arg(r.action);
        const QVariant storedAction = s.value(QStringLiteral("config/") + actionKey);
        if (storedAction.isValid() && storedAction.toString() != r.action)
            changes.append({ actionKey, r.action });
    }
    if (m_language) {
        const QString now = m_language->currentData().toString();
        if (now != m_langBaseline)
            changes.append({ QStringLiteral("language"), now });
    }

    QString error;
    // 先重试此前 sync 失败的即时项：失败则不动草稿，避免「草稿已落盘、窗口还开着」。
    if (!flushPendingWrites(&error)) {
        warnSaveFailure(error);
        return false;
    }
    // 只提交实际变更的键：普通即时项在变更点已经落盘，这里不再整表重写。
    if (!changes.isEmpty() && !commitValues(changes, &error)) {
        warnSaveFailure(error);
        return false;
    }
    return true;
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
    m_forceSync = true;   // 直接改内存的批量变更，由关闭/后续写盘统一 sync
    loadFromConfig();

    // 恢复默认同样是「即时项立即生效」：配置已清成默认值，运行态按默认值重放；
    // 热键 / 语言若与进入设置时不同，进入待保存计数（关闭时会询问）。
    for (int i = 0; i < m_liveChecks.size(); ++i)
        applyLiveCheck(i, m_liveChecks[i].cb->isChecked());
    applyThemePreview();
    if (m_blurMaterial) {
        UiStyle::setBlurMaterial(m_blurMaterial->isChecked());
        refreshDialogStyle();
    }
    updatePendingNote();

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

// 实时冲突反馈：与保存前的检测同一套判定，但每次键位/勾选变化都重算一次
// （规格 §焦点与键盘状态：冲突态要指出动作与键位、键帽与行内文字标红、
// 保存按钮不可用；修正后自动清除并恢复保存）。
void SettingsDialog::refreshKeyConflictBanner()
{
    if (m_keyRows.isEmpty())
        return;
    const QString msg = keyboardConflicts();   // 内部会逐行设置 conflict 属性
    const bool has = !msg.isEmpty();

    if (m_keyBanner && m_keyBannerText) {
        m_keyBannerText->setText(msg.split(QLatin1Char('\n')).join(QStringLiteral("  ")));
        m_keyBanner->setVisible(has);
        m_keyBanner->setAccessibleName(msg);
        m_keyBanner->setAccessibleDescription(msg);
    }
    // 冲突时用红条顶替页首说明，避免两条提示叠在一起（设计图 06）。
    if (m_keyHint)
        m_keyHint->setVisible(!has);
    if (m_btnOk) {
        // 冲突时保存不可用：enabled=false 负责拦截点击/回车，颜色用控件级声明
        // 覆盖（Qt 的 QStyleSheetStyle 对运行期 :disabled 伪状态不重解析，实测
        // 对话框级 `#btnPrimary:disabled` 不生效；控件级声明优先级最高，稳）。
        m_btnOk->setEnabled(!has);
        if (has) {
            m_btnOk->setStyleSheet(QStringLiteral("color:%1;")
                                       .arg(QLatin1String(UiStyle::fluent::disabled())));
        } else {
            m_btnOk->setStyleSheet(QString());
        }
        m_btnOk->style()->unpolish(m_btnOk);
        m_btnOk->style()->polish(m_btnOk);
        m_btnOk->update();
    }
    if (m_footerNote) {
        if (has)
            m_footerNote->setText(tr("解决键位冲突后可保存。"));
        else
            updatePendingNote();
    }

    for (const KeyRow &r : m_keyRows) {
        if (!r.stateLab)
            continue;
        const bool bad = r.row->property("conflict").toBool();
        if (bad) {
            r.stateLab->setText(tr("冲突"));
            r.stateLab->setProperty("state", QStringLiteral("conflict"));
            r.stateLab->show();
        } else if (!r.stateLab->property("state").toString()
                       .compare(QLatin1String("conflict"))) {
            // 冲突解除后交还给录入态判定
            r.stateLab->setProperty("state", QString());
            r.stateLab->hide();
        }
        r.stateLab->style()->unpolish(r.stateLab);
        r.stateLab->style()->polish(r.stateLab);
    }
    refreshKeyEditingState();
}

// 录入态：只有真正持有键盘焦点的热键框显示「正在录入」，页首说明同步切换。
void SettingsDialog::refreshKeyEditingState()
{
    if (m_keyRows.isEmpty())
        return;
    bool anyEditing = false;
    for (const KeyRow &r : m_keyRows) {
        const bool editing = r.edit
            && (r.edit->hasFocus() || (r.edit->focusProxy() && r.edit->focusProxy()->hasFocus()));
        if (r.row->property("editing").toBool() != editing) {
            r.row->setProperty("editing", editing);
            r.row->style()->unpolish(r.row);
            r.row->style()->polish(r.row);
            r.row->update();
        }
        if (editing)
            anyEditing = true;
        if (!r.stateLab)
            continue;
        if (r.row->property("conflict").toBool())
            continue;   // 冲突优先，红字不被「正在录入」覆盖
        if (editing) {
            r.stateLab->setText(tr("正在录入"));
            r.stateLab->setProperty("state", QStringLiteral("editing"));
            r.stateLab->show();
        } else {
            r.stateLab->setText(QString());
            r.stateLab->setProperty("state", QString());
            r.stateLab->hide();
        }
        r.stateLab->style()->unpolish(r.stateLab);
        r.stateLab->style()->polish(r.stateLab);
    }
    setKeyHintMode(anyEditing);
}

void SettingsDialog::setKeyHintMode(bool editing)
{
    if (!m_keyHint)
        return;
    const QString text = editing
        ? tr("按下新的组合键。Esc 清空当前热键。")
        : tr("双击热键录入，Esc 清空。勾选即时生效，改绑需保存。");
    if (m_keyHint->text() != text)
        m_keyHint->setText(text);
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
// 有冲突：高亮全部冲突行并返回提示文案（每条冲突一行，指出键位与冲突的两个动作）；
// 无冲突返回空串。
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
    QStringList msgs;
    auto markBad = [&bad, &msgs](QList<int> rows, const QString &m) {
        for (int r : rows)
            bad.insert(r);
        if (!msgs.contains(m))
            msgs.append(m);
    };
    auto keyText = [](const QKeySequence &s) {
        return s.toString(QKeySequence::NativeText);
    };

    // 1) 两个已启用且已改绑的行键位相同（同目标动作内部重复由 MainWindow 聚合去重，不算冲突）
    for (int i = 0; i < st.size(); ++i) {
        if (!st[i].enabled || st[i].factory || st[i].key.isEmpty())
            continue;
        for (int j = i + 1; j < st.size(); ++j) {
            if (!st[j].enabled || st[j].factory || st[j].key.isEmpty())
                continue;
            if (st[i].target == st[j].target)
                continue;
            if (st[i].key == st[j].key)
                markBad({ i, j },
                        tr("按键「%1」冲突：『%2』与『%3』被绑到同一组合键。")
                            .arg(keyText(st[i].key), label(st[i].target),
                                 label(st[j].target)));
        }
    }
    // 2) 已启用的改绑行 vs 出厂/固定键位（停用行不占键；目标动作自己的出厂表不参与）
    for (int i = 0; i < st.size(); ++i) {
        if (!st[i].enabled || st[i].factory || st[i].key.isEmpty())
            continue;
        for (int k = 0; k < occupied.size(); ++k) {
            if (!occupiedIds[k].isEmpty() && occupiedIds[k] == st[i].target)
                continue;
            if (occupied[k].second.contains(st[i].key)) {
                // 规格：冲突的两行都要标红（键帽 + 行内文字）。占用的若是
                // 可配置动作，把它对应的行一起标记；固定动作没有行，只标自己。
                QList<int> rows{i};
                if (!occupiedIds[k].isEmpty()) {
                    const int other = rowIndexOfAction(occupiedIds[k]);
                    if (other >= 0 && other != i)
                        rows.append(other);
                }
                markBad(rows,
                        tr("按键「%1」冲突：『%2』与『%3』被绑到同一组合键。")
                            .arg(keyText(st[i].key), label(st[i].target),
                                 occupied[k].first));
            }
        }
    }

    for (int i = 0; i < st.size(); ++i)
        setKeyRowConflict(i, bad.contains(i));
    return msgs.join(QStringLiteral("\n"));
}

// —— 验收探针（--smoke keyconflict）——
void SettingsDialog::probeKeyConflict()
{
    // 切到键盘页（导航第 5 项，0=一般；格式关联插在第 4 项），让冲突行高亮进截图
    selectPage(5);
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
    if (!onAccepted()) {
        // 冲突行高亮态：窗口此时未被关闭（P0-1 之前这里已被 accept() 关掉）。
        grab().save(QStringLiteral("Temp/keys-conflict-row.png"));
    }
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

// —— 验收探针（--smoke settingscombo，见 MainWindow::runSmokeCommand）——
void SettingsDialog::probeOpenCombo(int page, int index)
{
    selectPage(page);
    QTimer::singleShot(500, this, [this, index]() {
        QList<QComboBox *> combos;
        for (QComboBox *c : findChildren<QComboBox *>()) {
            if (c->isVisible()) {
                combos << c;
            }
        }
        // 清单落盘：索引是「可见下拉」的创建顺序，换顺序时应以这份清单为准
        if (QFile f(QStringLiteral("Temp/settings-combos.txt")); f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            for (int i = 0; i < combos.size(); ++i) {
                ts << i << ": current=\"" << combos.at(i)->currentText()
                   << "\" items=" << combos.at(i)->count() << "\n";
            }
        }
        if (index >= 0 && index < combos.size()) {
            combos.at(index)->showPopup();
        }
    });
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
    // 设置框外壳走 Fluent 定稿的「内容背景」，1px 边框也用定稿描边色；
    // 转换对话框有自己的 paintEvent，不受影响。
    p.fillPath(path, UiStyle::color(UiStyle::fluent::contentBg()));
    p.setPen(QPen(UiStyle::color(UiStyle::fluent::cardLine()), 1));
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
    // 搜索框内 Esc 先清搜索；Enter 不做「保存设置」。
    if (m_search && m_search->hasFocus()
        && event->key() == Qt::Key_Escape && !m_search->text().isEmpty()) {
        m_search->clear();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        QWidget *f = focusWidget();
        // 输入类控件里回车属于控件自身（选择下拉项 / 结束键位录制 / 编辑框提交），
        // 不能顺带保存并关窗。
        if (qobject_cast<QLineEdit *>(f) || qobject_cast<QAbstractSpinBox *>(f)
            || qobject_cast<QComboBox *>(f) || qobject_cast<QKeySequenceEdit *>(f)
            || (f && qobject_cast<QKeySequenceEdit *>(f->parentWidget()))) {
            return;
        }
        accept();   // 与保存按钮同一条校验路径，冲突时不关闭窗口（P0-1）
        return;
    }
    QDialog::keyPressEvent(event); // ESC -> reject() -> 待保存询问
}

void SettingsDialog::accept()
{
    // 唯一的「保存并关闭」入口：按钮、Enter/Return、默认按钮都汇到这里。
    // 校验（键盘冲突）或写盘未通过时不调基类，窗口保持打开。
    if (!onAccepted())
        return;
    QDialog::accept();
}

void SettingsDialog::reject()
{
    // 同一次退出里，MainWindow::closeEvent 会显式调一次 reject()，Qt 关主窗口时
    // 也可能向非模态子对话框再发一次关闭。没有这层守卫会连弹两次「保存更改？」。
    if (m_prompting)
        return;
    // 关闭语义（2026-11-07）：普通设置已即时写盘，关闭即保留；
    // 只有热键改绑 / 界面语言是待保存项。有草稿时弹三选一。
    flushPendingTheme();   // 排队中的主题切换先落运行态并写盘，再决定关闭方式
    const QStringList names = pendingChangeNames();
    if (names.isEmpty()) {
        // 直接关闭：同样要确认写盘成功，不能「看着关了、其实没落盘」。
        QString error;
        if (!flushPendingWrites(&error)) {
            warnSaveFailure(error);
            return;   // 留在设置页，允许稍后重试
        }
        QDialog::reject();
        return;
    }

    QMessageBox box(this);
    box.setWindowTitle(tr("保存更改？"));
    box.setIcon(QMessageBox::Question);
    box.setText(tr("有 %1 项更改尚未保存。").arg(names.size()));
    QStringList shown = names.mid(0, 4);
    if (names.size() > shown.size())
        shown << tr("等 %1 项").arg(names.size());
    box.setInformativeText(tr("待保存：%1\n\n"
                             "「不保存」只放弃这些草稿；已即时应用的普通设置会保留。")
                               .arg(shown.join(QStringLiteral("、"))));
    auto *saveBtn = box.addButton(tr("保存并关闭"), QMessageBox::AcceptRole);
    box.addButton(tr("不保存"), QMessageBox::DestructiveRole);
    auto *keepBtn = box.addButton(tr("继续编辑"), QMessageBox::RejectRole);
    box.setDefaultButton(saveBtn);
    box.setEscapeButton(keepBtn);
    m_prompting = true;
    box.exec();
    m_prompting = false;

    if (box.clickedButton() == saveBtn) {
        accept();   // 冲突 / 写盘失败时 accept 内部已拦截，窗口保持打开
        return;
    }
    if (box.clickedButton() && box.clickedButton() != keepBtn) {
        // 不保存：只丢弃热键 / 语言草稿；已即时应用的普通项仍须确认落盘。
        QString error;
        if (!flushPendingWrites(&error)) {
            warnSaveFailure(error);
            return;   // 留在设置页，允许稍后重试
        }
        QDialog::reject();   // 丢弃待保存草稿，已即时项保留
    }
}
