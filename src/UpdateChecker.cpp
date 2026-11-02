#include "UpdateChecker.h"

#include "Config.h"

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QFontMetrics>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QList>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QTimer>
#include <QUrl>
#include <QWidget>

namespace {

// 请求超时：自动/手动统一 8s（HANDOFF 第 10 期硬要求）
constexpr int kTimeoutMs = 8000;
// 自动检查频控：≥24h
constexpr qint64 kAutoCheckIntervalSecs = 24 * 60 * 60;

#ifndef QUIKVIEW_VERSION
#define QUIKVIEW_VERSION "0.0.0"
#endif

QUrl updateUrl()
{
    // 测试逃生口：上游仓库还没有 Release 时，用 QUIKVIEW_UPDATE_URL 指向
    // 任意公开仓库的 releases/latest，验证解析/比较链路。正常用户不会设置。
    const QByteArray override = qgetenv("QUIKVIEW_UPDATE_URL");
    if (!override.isEmpty())
        return QUrl(QString::fromUtf8(override));
    return QUrl(QStringLiteral("https://api.github.com/repos/xdsT2/quikview/releases/latest"));
}

QString displayVersion(const QString &tag)
{
    QString v = tag.trimmed();
    if (!v.startsWith(QLatin1Char('v')) && !v.startsWith(QLatin1Char('V')))
        v.prepend(QLatin1Char('v'));
    return v;
}

struct ParsedVersion {
    QList<qlonglong> core;
    QString pre;
};

ParsedVersion parseVersion(QString v)
{
    v = v.trimmed();
    if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
        v.remove(0, 1);
    // 剥掉 +build 元数据；- 之后是预发布后缀
    const int plus = v.indexOf(QLatin1Char('+'));
    if (plus >= 0)
        v.truncate(plus);
    const int dash = v.indexOf(QLatin1Char('-'));
    ParsedVersion out;
    const QString core = dash >= 0 ? v.left(dash) : v;
    out.pre = dash >= 0 ? v.mid(dash + 1) : QString();
    const QStringList parts = core.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        bool ok = false;
        const qlonglong n = p.toLongLong(&ok);
        out.core.append(ok ? n : 0);   // 非数字段按 0，保证不崩
    }
    if (out.core.isEmpty())
        out.core.append(0);
    return out;
}

// 顶部提示条：深色圆角，和主窗口 Toast 同一套观感；有下载地址时多一个「前往下载」链接。
class UpdateToast : public QWidget
{
public:
    UpdateToast(QWidget *anchor, const QString &text, const QString &url)
        : QWidget(anchor)
        , m_text(text)
        , m_url(url)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        if (anchor)
            setGeometry(0, 8, anchor->width(), 44);
        show();
        raise();
        QTimer::singleShot(5000, this, &QWidget::close);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QFont f = font();
        f.setPointSizeF(9.5);
        p.setFont(f);
        const QFontMetrics fm = p.fontMetrics();

        const QString linkText = m_url.isEmpty() ? QString() : QObject::tr("前往下载");
        const int gap = linkText.isEmpty() ? 0 : 18;
        const int linkW = linkText.isEmpty() ? 0 : fm.horizontalAdvance(linkText);
        int w = fm.horizontalAdvance(m_text) + gap + linkW + 34;
        if (parentWidget())
            w = qMin(w, parentWidget()->width() - 24);
        const QRectF r((width() - w) / 2.0, 2, w, 30);
        p.setPen(QPen(QColor(QStringLiteral("#555555")), 1));
        p.setBrush(QColor(QStringLiteral("#383838")));
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);

        const qreal baseline = r.center().y() + (fm.ascent() - fm.descent()) / 2.0;
        const qreal textX = r.left() + 17;
        p.setPen(QColor(QStringLiteral("#eeeeee")));
        p.drawText(QPointF(textX, baseline), m_text);

        m_linkRect = QRect();
        if (!linkText.isEmpty()) {
            const qreal linkX = textX + fm.horizontalAdvance(m_text) + gap;
            m_linkRect = QRect(int(linkX), int(r.top()), linkW + 2, int(r.height()));
            p.setPen(QColor(QStringLiteral("#6cb6ff")));
            p.drawText(QPointF(linkX, baseline), linkText);
        }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (!m_url.isEmpty() && m_linkRect.contains(e->pos())) {
            QDesktopServices::openUrl(QUrl(m_url));
            close();
            return;
        }
        QWidget::mousePressEvent(e);
    }

private:
    QString m_text;
    QString m_url;
    QRect m_linkRect;
};

} // namespace

// ===========================================================================
// UpdateChecker
// ===========================================================================

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_timeout(new QTimer(this))
{
    // 默认跟随系统代理（本机由「Internet 选项」配置为 127.0.0.1:7890）。
    // 不设应用级代理：没有系统代理的环境自然直连。
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    m_timeout->setSingleShot(true);
    m_timeout->setInterval(kTimeoutMs);
    connect(m_timeout, &QTimer::timeout, this, &UpdateChecker::onTimeout);
    connect(m_nam, &QNetworkAccessManager::finished, this,
            [this](QNetworkReply *reply) {
                if (reply == m_reply)
                    onReplyFinished();
            });
}

UpdateChecker &UpdateChecker::instance()
{
    static UpdateChecker inst;
    return inst;
}

bool UpdateChecker::check(Mode mode)
{
    if (m_reply) {
        qDebug() << "[update] ignored: a request is already in flight";
        return false;
    }

    const bool manual = (mode == Mode::Manual);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (!manual) {
        const qint64 last = Config::instance().lastUpdateCheck();
        const qint64 age = now - last;
        if (last > 0 && age >= 0 && age < kAutoCheckIntervalSecs) {
            qDebug() << "[update] auto check skipped:" << age << "s since last check";
            return false;
        }
    }

    m_manual = manual;
    m_timedOut = false;
    // 发起即记时间戳（失败也算一次），避免失败后每次启动都重试。
    // 手动检查同样记录：紧跟其后的启动自动检查不会重复发请求。
    Config::instance().setLastUpdateCheck(now);

    const QUrl url = updateUrl();
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("QuikView/%1").arg(QStringLiteral(QUIKVIEW_VERSION)));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(kTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    qDebug() << "[update] request ->" << url.toString()
             << (manual ? "(manual)" : "(auto)");
    m_reply = m_nam->get(req);
    m_timeout->start();
    return true;
}

void UpdateChecker::onTimeout()
{
    if (!m_reply)
        return;
    m_timedOut = true;
    qDebug() << "[update] timeout after" << kTimeoutMs << "ms, aborting";
    m_reply->abort();   // 触发 finished，走统一的失败分支
}

void UpdateChecker::onReplyFinished()
{
    if (!m_reply)
        return;
    m_timeout->stop();

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;

    const bool manual = m_manual;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError err = reply->error();
    // abort() 之后 reply 已关闭，readAll 会打 "device not open" 警告；失败路径不需要 body。
    const QByteArray body = (m_timedOut || err != QNetworkReply::NoError)
                                ? QByteArray()
                                : reply->readAll();
    reply->deleteLater();

    // 仓库还没发布过 Release 时 GitHub 返回 404：这不是网络故障，
    // 对用户就是「没有更新」。
    if (status == 404) {
        qDebug() << "[update] HTTP 404 (no release published) -> up to date";
        emit upToDate(manual);
        emit finished(manual);
        return;
    }

    if (m_timedOut || err != QNetworkReply::NoError || status < 200 || status >= 300) {
        qDebug() << "[update] failed: timedOut=" << m_timedOut
                 << "error=" << err << "status=" << status;
        emit checkFailed(manual);
        emit finished(manual);
        return;
    }

    QJsonParseError perr {};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        qDebug() << "[update] failed: bad JSON" << perr.errorString();
        emit checkFailed(manual);
        emit finished(manual);
        return;
    }
    const QJsonObject obj = doc.object();
    const QString tag = obj.value(QStringLiteral("tag_name")).toString().trimmed();
    const QString url = obj.value(QStringLiteral("html_url")).toString();
    if (tag.isEmpty()) {
        qDebug() << "[update] failed: no tag_name in response";
        emit checkFailed(manual);
        emit finished(manual);
        return;
    }

    const QString current = QStringLiteral(QUIKVIEW_VERSION);
    const bool newer = isNewer(tag, current);
    qDebug() << "[update] latest tag =" << tag << "current =" << current
             << "newer =" << newer;

    if (newer)
        emit updateAvailable(displayVersion(tag), url, manual);
    else
        emit upToDate(manual);
    emit finished(manual);
}

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
    const ParsedVersion pa = parseVersion(a);
    const ParsedVersion pb = parseVersion(b);

    const int n = qMax(pa.core.size(), pb.core.size());
    for (int i = 0; i < n; ++i) {
        const qlonglong va = i < pa.core.size() ? pa.core.at(i) : 0;
        const qlonglong vb = i < pb.core.size() ? pb.core.at(i) : 0;
        if (va != vb)
            return va < vb ? -1 : 1;
    }

    // 核心号相同：正式版 > 预发布版（0.4.0-dev < 0.4.0）
    if (pa.pre.isEmpty() != pb.pre.isEmpty())
        return pa.pre.isEmpty() ? 1 : -1;
    const int c = QString::compare(pa.pre, pb.pre);
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

bool UpdateChecker::isNewer(const QString &candidate, const QString &current)
{
    return compareVersions(candidate, current) > 0;
}

// ===========================================================================
// UpdateNotifier
// ===========================================================================

UpdateNotifier::UpdateNotifier(QWidget *fallbackAnchor, QObject *parent)
    : QObject(parent)
    , m_fallback(fallbackAnchor)
{
    UpdateChecker &checker = UpdateChecker::instance();

    connect(&checker, &UpdateChecker::updateAvailable, this,
            [this](const QString &version, const QString &url, bool) {
                if (QWidget *anchor = targetAnchor())
                    new UpdateToast(anchor, tr("发现新版本 %1").arg(version), url);
            });
    connect(&checker, &UpdateChecker::upToDate, this, [this](bool manual) {
        if (!manual)
            return;
        if (QWidget *anchor = targetAnchor())
            new UpdateToast(anchor, tr("已是最新版本"), QString());
    });
    connect(&checker, &UpdateChecker::checkFailed, this, [this](bool manual) {
        if (!manual)
            return;
        if (QWidget *anchor = targetAnchor())
            new UpdateToast(anchor, tr("检查失败，请检查网络"), QString());
    });
}

QWidget *UpdateNotifier::targetAnchor() const
{
    // 设置对话框打开时它是活动窗口，提示条要落在它上面，否则会被挡在后面。
    if (QWidget *active = QApplication::activeWindow())
        return active;
    return m_fallback;
}
