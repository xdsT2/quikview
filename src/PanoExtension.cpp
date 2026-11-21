#include "PanoExtension.h"

#include "Config.h"
#include "ZipArchive.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace {

const char *kExeName = "quikview-pano.exe";
const char *kVersionName = "pano-version.txt";
// 组件包上限：单个 exe 加版本文件，正常不超过 8MB；留 64MB 余量防呆。
constexpr int kMaxPackageBytes = 64 * 1024 * 1024;
constexpr int kTransferTimeoutMs = 15000;

#ifndef QUIKVIEW_VERSION
#define QUIKVIEW_VERSION "0.0.0"
#endif

QString appVersion()
{
    return QStringLiteral(QUIKVIEW_VERSION);
}

// "0.5.6" → {0,5,6}；预发布后缀与构建元数据一律忽略。
QList<int> coreVersion(const QString &raw)
{
    QString v = raw.trimmed();
    if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
        v.remove(0, 1);
    const int cut = v.indexOf(QRegularExpression(QStringLiteral("[^0-9.]")));
    if (cut >= 0)
        v.truncate(cut);
    QList<int> out;
    for (const QString &p : v.split(QLatin1Char('.'), Qt::SkipEmptyParts)) {
        bool ok = false;
        out.append(p.toInt(&ok));
        if (!ok)
            out.append(0);
    }
    return out;
}

QString readVersionFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QString text = QString::fromUtf8(f.readAll());
    return text.section(QLatin1Char('\n'), 0, 0).trimmed();
}

bool isLocalSource(const QString &source)
{
    if (source.startsWith(QLatin1String("file://"), Qt::CaseInsensitive))
        return true;
    // 盘符路径（C:\...）或 UNC（\\server\share）都按本地文件处理；
    // 只有带 scheme 的 http(s) 才走网络。
    const QUrl url(source);
    if (url.isLocalFile())
        return true;
    return !source.contains(QLatin1String("://"));
}

QString localPathOf(const QString &source)
{
    if (source.startsWith(QLatin1String("file://"), Qt::CaseInsensitive)) {
        const QUrl url(source);
        return url.toLocalFile();
    }
    return QDir::fromNativeSeparators(source);
}

} // namespace

PanoExt::Manager &PanoExt::Manager::instance()
{
    static Manager m;
    return m;
}

PanoExt::Manager::Manager(QObject *parent)
    : QObject(parent)
{
    m_net = new QNetworkAccessManager(this);
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(kTransferTimeoutMs);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        if (m_state != State::Installing)
            return;
        if (m_reply || m_shaReply)
            setFailure(tr("组件下载超时，请检查网络后重试"));
    });
}

QString PanoExt::Manager::exePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QLatin1String(kExeName));
}

QString PanoExt::Manager::versionFilePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QLatin1String(kVersionName));
}

bool PanoExt::Manager::versionCompatible(const QString &componentVersion)
{
    if (componentVersion.trimmed().isEmpty())
        return false;
    const QList<int> main = coreVersion(appVersion());
    const QList<int> comp = coreVersion(componentVersion);
    if (main.size() < 2 || comp.size() < 2)
        return false;
    return main.at(0) == comp.at(0) && main.at(1) == comp.at(1);
}

PanoExt::Status PanoExt::Manager::status() const
{
    Status s;
    s.error = m_error;
    s.percent = m_percent;
    if (m_state == State::Installing) {
        s.state = State::Installing;
    } else if (m_state == State::Failed) {
        s.state = State::Failed;
    } else if (QFile::exists(exePath())) {
        s.state = State::Installed;
        s.version = readVersionFile(versionFilePath());
        if (s.version.isEmpty())
            s.version = QStringLiteral("未知");
        s.path = exePath();
        s.compatible = versionCompatible(s.version);
    } else {
        s.state = State::NotInstalled;
    }
    return s;
}

QString PanoExt::Manager::sourceUrl() const
{
    const QByteArray env = qgetenv("QUIKVIEW_PANO_SOURCE");
    if (!env.isEmpty())
        return QString::fromUtf8(env).trimmed();
    return Config::instance()
        .settings()
        .value(QStringLiteral("config/ext/pano_source"))
        .toString()
        .trimmed();
}

bool PanoExt::Manager::sourceConfigured() const
{
    return !sourceUrl().isEmpty();
}

QString PanoExt::Manager::downloadTempPath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(dir).filePath(QStringLiteral("quikview-pano-%1.zip")
                                  .arg(QRandomGenerator::global()->generate()));
}

void PanoExt::Manager::setFailure(const QString &message)
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_shaReply) {
        m_shaReply->disconnect(this);
        m_shaReply->abort();
        m_shaReply->deleteLater();
        m_shaReply = nullptr;
    }
    m_timeout->stop();
    m_state = State::Failed;
    m_error = message;
    m_percent = 0;
    emit changed();
}

void PanoExt::Manager::cancelInstall()
{
    if (m_state != State::Installing)
        return;
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_shaReply) {
        m_shaReply->disconnect(this);
        m_shaReply->abort();
        m_shaReply->deleteLater();
        m_shaReply = nullptr;
    }
    m_timeout->stop();
    // 取消不是失败：回到「未安装」，不清空上一次的可用状态（本来也没有）。
    m_state = State::NotInstalled;
    m_error.clear();
    m_percent = 0;
    emit changed();
}

void PanoExt::Manager::startInstall()
{
    if (m_state == State::Installing)
        return;
    m_error.clear();
    m_percent = 0;

    const QString source = sourceUrl();
    if (source.isEmpty()) {
        setFailure(tr("组件下载源未配置。请运行安装包并勾选「360° 全景查看器」，"
                      "或在 config.ini 里配置 config/ext/pano_source 后重试。"));
        return;
    }

    m_state = State::Installing;
    emit changed();

    if (isLocalSource(source)) {
        // 本地测试源：直接读盘，不做网络进度（进度直接到 100%）。
        const QString zipPath = localPathOf(source);
        const QString shaPath = zipPath + QStringLiteral(".sha256");
        m_percent = 100;
        emit progressChanged(m_percent);
        finishInstallFromFile(zipPath, shaPath);
        return;
    }

    QNetworkRequest req{QUrl(source)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(kTransferTimeoutMs);
    m_timeout->start();
    m_reply = m_net->get(req);

    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 got, qint64 total) {
                if (m_state != State::Installing)
                    return;
                if (total > 0) {
                    // 下载只占进度的 90%，剩下 10% 留给校验与写盘。
                    const int p = static_cast<int>(qMin<qint64>(90, got * 90 / total));
                    if (p != m_percent) {
                        m_percent = p;
                        emit progressChanged(m_percent);
                    }
                }
                if (got > kMaxPackageBytes) {
                    setFailure(tr("组件包超出预期大小，已取消安装"));
                }
            });

    connect(m_reply, &QNetworkReply::finished, this, [this, source]() {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        if (!r)
            return;
        r->deleteLater();
        if (m_state != State::Installing)
            return;
        if (r->error() != QNetworkReply::NoError) {
            // Qt 的 errorString 是英文原文，这里按常见错误补一句「怎么办」，
            // 免得用户只看到 Connection refused 不知道要不要重试。
            QString hint;
            switch (r->error()) {
            case QNetworkReply::ConnectionRefusedError:
            case QNetworkReply::HostNotFoundError:
            case QNetworkReply::RemoteHostClosedError:
                hint = tr("连不上下载源，检查网络或 config/ext/pano_source 里的地址");
                break;
            case QNetworkReply::TimeoutError:
            case QNetworkReply::OperationCanceledError:
                hint = tr("下载源无响应，检查网络或稍后重试");
                break;
            case QNetworkReply::ContentNotFoundError:
                hint = tr("下载源上找不到组件包，检查文件名或目录");
                break;
            default:
                break;
            }
            if (hint.isEmpty())
                setFailure(tr("无法获取组件包：%1").arg(r->errorString()));
            else
                setFailure(tr("无法获取组件包：%1（%2）").arg(hint, r->errorString()));
            return;
        }
        const QByteArray bytes = r->readAll();
        if (bytes.isEmpty() || bytes.size() > kMaxPackageBytes) {
            setFailure(tr("组件包为空或超出预期大小"));
            return;
        }

        // 先取同址 .sha256；没有摘要就没有可信校验，一律按校验失败处理。
        QNetworkRequest shaReq{QUrl(source + QStringLiteral(".sha256"))};
        shaReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                            QNetworkRequest::NoLessSafeRedirectPolicy);
        shaReq.setTransferTimeout(kTransferTimeoutMs);
        m_shaReply = m_net->get(shaReq);
        connect(m_shaReply, &QNetworkReply::finished, this, [this, bytes]() {
            QNetworkReply *sr = m_shaReply;
            m_shaReply = nullptr;
            if (!sr)
                return;
            sr->deleteLater();
            if (m_state != State::Installing)
                return;
            m_timeout->stop();
            if (sr->error() != QNetworkReply::NoError) {
                setFailure(tr("组件包缺少校验摘要，已拒绝安装"));
                return;
            }
            const QString sha = QString::fromUtf8(sr->readAll()).trimmed()
                                    .section(QLatin1Char(' '), 0, 0)
                                    .section(QLatin1Char('\t'), 0, 0);
            verifyAndExtract(bytes, sha);
        });
    });
}

void PanoExt::Manager::finishInstallFromFile(const QString &zipPath, const QString &shaPath)
{
    QFile z(zipPath);
    if (!z.open(QIODevice::ReadOnly)) {
        setFailure(tr("无法读取本地组件包：%1").arg(zipPath));
        return;
    }
    const QByteArray bytes = z.readAll();
    z.close();
    QFile s(shaPath);
    QString sha;
    if (s.open(QIODevice::ReadOnly | QIODevice::Text)) {
        sha = QString::fromUtf8(s.readAll()).trimmed().section(QLatin1Char(' '), 0, 0)
                  .section(QLatin1Char('\t'), 0, 0);
        s.close();
    }
    verifyAndExtract(bytes, sha);
}

void PanoExt::Manager::verifyAndExtract(const QByteArray &zipBytes, const QString &expectedSha)
{
    if (expectedSha.size() != 64) {
        setFailure(tr("组件包缺少可用的 SHA-256 摘要，已拒绝安装"));
        return;
    }
    const QString actual = QString::fromLatin1(
        QCryptographicHash::hash(zipBytes, QCryptographicHash::Sha256).toHex());
    if (actual.compare(expectedSha, Qt::CaseInsensitive) != 0) {
        setFailure(tr("组件包校验失败（摘要不一致），已保留原有状态"));
        return;
    }

    // ZipArchive 只认文件：把已校验的字节落到临时文件再解析。
    const QString tmpZip = downloadTempPath();
    QFile tmp(tmpZip);
    if (!tmp.open(QIODevice::WriteOnly)) {
        setFailure(tr("无法写入临时文件"));
        return;
    }
    tmp.write(zipBytes);
    tmp.close();

    ZipArchive zip;
    QString zipError;
    if (!zip.open(tmpZip, &zipError)) {
        QFile::remove(tmpZip);
        setFailure(tr("组件包不是有效的 zip：%1").arg(zipError));
        return;
    }

    int exeIndex = -1;
    int verIndex = -1;
    for (int i = 0; i < zip.entries().size(); ++i) {
        const QString name = zip.entries().at(i).name;
        const QString leaf = name.section(QLatin1Char('/'), -1);
        if (leaf.compare(QLatin1String(kExeName), Qt::CaseInsensitive) == 0)
            exeIndex = i;
        else if (leaf.compare(QLatin1String(kVersionName), Qt::CaseInsensitive) == 0)
            verIndex = i;
    }
    if (exeIndex < 0 || verIndex < 0) {
        QFile::remove(tmpZip);
        setFailure(tr("组件包内容不完整（缺少 quikview-pano.exe 或 pano-version.txt）"));
        return;
    }

    QString readError;
    const QByteArray exeBytes = zip.read(zip.entries().at(exeIndex), &readError);
    const QByteArray verBytes = zip.read(zip.entries().at(verIndex), &readError);
    zip.close();
    QFile::remove(tmpZip);
    if (exeBytes.isEmpty() || verBytes.isEmpty()) {
        setFailure(tr("组件包解压失败：%1").arg(readError));
        return;
    }
    // 最低限度的真实性检查：必须是 PE 可执行文件。
    if (exeBytes.size() < 1024 || !exeBytes.startsWith(QByteArray("MZ"))) {
        setFailure(tr("组件包里的 quikview-pano.exe 不是有效的可执行文件"));
        return;
    }
    const QString version = QString::fromUtf8(verBytes).section(QLatin1Char('\n'), 0, 0).trimmed();
    if (!versionCompatible(version)) {
        setFailure(tr("组件版本 %1 与当前 QuikView %2 不兼容，已拒绝安装")
                       .arg(version.isEmpty() ? QStringLiteral("未知") : version, appVersion()));
        return;
    }

    // 落盘：先在安装目录写临时文件（能立刻暴露「目录不可写」），再替换正式文件。
    const QString dir = QCoreApplication::applicationDirPath();
    const QString exeTmp = QDir(dir).filePath(QStringLiteral("quikview-pano.exe.new"));
    QFile out(exeTmp);
    if (!out.open(QIODevice::WriteOnly)) {
        setFailure(tr("安装目录不可写（%1）。请把 QuikView 装到有写权限的目录，"
                      "或在安装向导里勾选该组件。")
                       .arg(QDir::toNativeSeparators(dir)));
        return;
    }
    const bool wrote = out.write(exeBytes) == exeBytes.size();
    out.close();
    if (!wrote) {
        QFile::remove(exeTmp);
        setFailure(tr("组件写入失败（磁盘空间或权限不足）"));
        return;
    }

    const QString exeFinal = exePath();
    if (QFile::exists(exeFinal) && !QFile::remove(exeFinal)) {
        QFile::remove(exeTmp);
        setFailure(tr("旧组件正在运行或被占用，请先关闭全景窗口再重试"));
        return;
    }
    if (!QFile::rename(exeTmp, exeFinal)) {
        QFile::remove(exeTmp);
        setFailure(tr("组件文件改名失败，已保留原有状态"));
        return;
    }

    QFile vf(versionFilePath());
    if (vf.open(QIODevice::WriteOnly | QIODevice::Text)) {
        vf.write(version.toUtf8() + '\n');
        vf.write(actual.toUtf8() + '\n');
        vf.close();
    }

    m_state = State::Installed;
    m_error.clear();
    m_percent = 100;
    emit progressChanged(m_percent);
    emit changed();
}

bool PanoExt::Manager::uninstall(QString *error)
{
    if (m_state == State::Installing)
        cancelInstall();
    if (!QFile::exists(exePath())) {
        QFile::remove(versionFilePath());
        m_state = State::NotInstalled;
        m_error.clear();
        emit changed();
        return true;
    }
    if (!QFile::remove(exePath())) {
        if (error)
            *error = tr("组件正在运行或被占用：请先关闭所有 360° 全景窗口再卸载");
        return false;
    }
    QFile::remove(versionFilePath());
    m_state = State::NotInstalled;
    m_error.clear();
    m_percent = 0;
    emit changed();
    return true;
}

bool PanoExt::Manager::launch(const QString &imagePath, const Pano::Info &view, QString *error)
{
    const Status s = status();
    if (s.state != State::Installed) {
        if (error)
            *error = tr("360° 全景查看器尚未安装");
        return false;
    }
    if (!s.compatible) {
        if (error)
            *error = tr("已安装的组件版本 %1 与当前 QuikView 不兼容，请在「设置 → 扩展」重新安装")
                         .arg(s.version);
        return false;
    }
    // 分参数传递：不带引号拼接，路径里的空格与中文由 argv 原样送达。
    const QStringList args{
        QStringLiteral("--image"), imagePath,
        QStringLiteral("--heading"), QString::number(view.heading, 'f', 3),
        QStringLiteral("--pitch"), QString::number(view.pitch, 'f', 3),
        QStringLiteral("--fov"), QString::number(view.fov, 'f', 3),
    };
    if (!QProcess::startDetached(s.path, args, QCoreApplication::applicationDirPath())) {
        if (error)
            *error = tr("无法启动 360° 全景查看器（组件文件可能已被删除或阻止运行）");
        return false;
    }
    return true;
}
