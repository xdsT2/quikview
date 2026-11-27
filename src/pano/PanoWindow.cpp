#include "PanoWindow.h"

#include "PanoGlobe.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

namespace {

// 与主程序一致的石墨底（UiStyle::Bg / Panel），此处独立进程不依赖主题系统，
// 只取同色系常量，避免全景图周围出现浅色边框干扰观感。
const char *kBackdrop = "#101113";

QString hudQss()
{
    return QStringLiteral(
        "#panoHud { background-color: rgba(24, 25, 28, 0.92);"
        " border: 1px solid rgba(255, 255, 255, 0.14); border-radius: 10px; }"
        "#panoHud QPushButton { color: #d5d7da; background: #2a2c30;"
        " border: 1px solid rgba(255,255,255,0.14); border-radius: 6px;"
        " padding: 0 12px; font-size: 12px; min-height: 24px; }"
        "#panoHud QPushButton:hover { background: #35383e; color: #f2f2f2; }"
        "#panoHud QPushButton:pressed { background: #2d2f34; }"
        "#panoHud QPushButton:focus { border: 1px solid #55c0f0; }"
        "#panoHud QLabel { color: #92979e; font-size: 11px; background: transparent; }"
        "#panoFov { color: #d5d7da; }"
        "#panoEv { color: #d5d7da; }");
}

// EV 文案：带符号一位小数，如 EV +2.0 / EV 0.0 / EV -1.5。
QString evText(double ev)
{
    const QString num = QString::number(ev, 'f', 1);
    const QString sign = ev > 0.0 ? QStringLiteral("+") : QString();
    return QStringLiteral("EV %1%2").arg(sign, num);
}

} // namespace

PanoWindow::PanoWindow(const QString &imagePath, double heading, double pitch, double fov,
                       double ev, QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle(tr("360° 全景 — %1").arg(QFileInfo(imagePath).fileName()));
    setStyleSheet(QStringLiteral("background:%1;").arg(QLatin1String(kBackdrop)));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_globe = new PanoGlobe(this);
    QString error;
    QString errorCategory;
    if (!m_globe->load(imagePath, &error, &errorCategory)) {
        // 主程序已在入口处确认过可读性；走到这里说明文件在打开瞬间被改/被锁，
        // 直接把原因带回去，由主程序 toast 出来，不让用户面对一块黑屏。
        setProperty("panoError", error);
        setProperty("panoErrorCategory", errorCategory);
        return;
    }
    lay->addWidget(m_globe);
    m_globe->setInitialView(heading, pitch, fov);
    m_globe->setEv(ev);
    m_ok = true;

    // —— 底部操作条（重置视角 / 当前视野 / 当前曝光 / 用法提示）——
    m_hud = new QWidget(this);
    m_hud->setObjectName(QStringLiteral("panoHud"));
    m_hud->setAttribute(Qt::WA_StyledBackground, true);
    m_hud->setStyleSheet(hudQss());
    auto *hl = new QHBoxLayout(m_hud);
    hl->setContentsMargins(10, 6, 12, 6);
    hl->setSpacing(10);

    m_resetBtn = new QPushButton(tr("重置视角"), m_hud);
    m_resetBtn->setCursor(Qt::PointingHandCursor);
    m_resetBtn->setFocusPolicy(Qt::StrongFocus);
    m_resetBtn->setToolTip(tr("回到图片标记的初始方向与初始视野（R）；不改变曝光"));
    connect(m_resetBtn, &QPushButton::clicked, this, [this]() { m_globe->resetView(); });
    hl->addWidget(m_resetBtn, 0, Qt::AlignVCenter);

    m_fovLabel = new QLabel(m_hud);
    m_fovLabel->setObjectName(QStringLiteral("panoFov"));
    hl->addWidget(m_fovLabel, 0, Qt::AlignVCenter);

    m_evLabel = new QLabel(m_hud);
    m_evLabel->setObjectName(QStringLiteral("panoEv"));
    m_evLabel->setToolTip(tr("曝光补偿：+ / - 或 Shift+滚轮 每档 0.5 EV，0 复位曝光；仅 HDR/EXR 生效"));
    hl->addWidget(m_evLabel, 0, Qt::AlignVCenter);

    auto *hint = new QLabel(tr("拖动转视角 · 滚轮视野 · +/- 调曝光 · 0 复位曝光 · R 复位视角 · Esc 退出"), m_hud);
    hl->addWidget(hint, 0, Qt::AlignVCenter);

    m_hud->adjustSize();
    connect(m_globe, &PanoGlobe::viewChanged, this, [this]() { syncHud(); });
    connect(m_globe, &PanoGlobe::evChanged, this, [this]() { syncHud(); });
    syncHud();

    // 初始尺寸：优先按图片宽高比的 70%，夹在屏幕可用区域内，居中。
    QSize target(1280, 800);
    const QSize src = m_globe->sourceSize();
    if (src.isValid() && src.height() > 0) {
        const double aspect = static_cast<double>(src.width()) / src.height();
        target = QSize(1280, qRound(1280.0 / qMax(1.0, aspect)));
        target.setHeight(qBound(520, target.height(), 900));
    }
    if (QScreen *screen = this->screen()) {
        const QRect avail = screen->availableGeometry();
        target.setWidth(qMin(target.width(), avail.width() - 80));
        target.setHeight(qMin(target.height(), avail.height() - 80));
        resize(target);
        move(avail.center() - QRect(QPoint(0, 0), target).center());
    } else {
        resize(target);
    }
}

void PanoWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(QLatin1String(kBackdrop)));
}

void PanoWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    placeHud();
}

void PanoWindow::placeHud()
{
    if (!m_hud) {
        return;
    }
    m_hud->adjustSize();
    const int w = m_hud->width();
    const int h = m_hud->height();
    m_hud->setGeometry((width() - w) / 2, height() - h - 18, w, h);
    m_hud->raise();
}

void PanoWindow::syncHud()
{
    if (!m_globe || !m_fovLabel) {
        return;
    }
    // 视野按垂直角度展示：与滚轮档位一一对应，用户能看出是否已到 20°/110° 边界。
    m_fovLabel->setText(tr("视野 %1°").arg(QString::number(m_globe->fov(), 'f', 0)));
    if (m_evLabel) {
        // SDR 图不经曝光链路：标注 n/a，避免误以为 +/- 会改变画面。
        m_evLabel->setText(m_globe->isHdr() ? evText(m_globe->ev()) : QStringLiteral("EV n/a"));
    }
    placeHud();
}

void PanoWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    // R / 双击：只复位视角，不动曝光；0：只复位曝光，不动视角。两套复位互不打架。
    if (event->key() == Qt::Key_R) {
        m_globe->resetView();
        return;
    }
    if (event->key() == Qt::Key_0) {
        m_globe->resetEv();
        return;
    }
    if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal) {
        m_globe->stepEv(0.5);
        return;
    }
    if (event->key() == Qt::Key_Minus || event->key() == Qt::Key_Underscore) {
        m_globe->stepEv(-0.5);
        return;
    }
    QWidget::keyPressEvent(event);
}
