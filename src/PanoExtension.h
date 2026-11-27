#pragma once

#include "PanoMetadata.h"

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// 可选组件「360° 全景查看器」的安装状态与安装动作（派单 2026-11-20）。
// ---------------------------------------------------------------------------
// 组件 = 与 soleview.exe 同目录的 soleview-pano.exe + pano-version.txt。
// 状态只认文件事实（不看注册表），因此安装向导勾选安装与设置页下载安装
// 天然共用同一份状态：两条路径写的是同样的两个文件，不会出现「装了但设置页
// 说没装」的分叉。
//
// 下载源：config/ext/pano_source（受控入口，用户不需要打开 GitHub）。
// 支持 http(s) 与本地文件（file:// 或盘符路径，本地测试源用这个）。
// 包格式：zip，内含 soleview-pano.exe 与 pano-version.txt；
// 另需同址 <url>.sha256（一行十六进制摘要，可带文件名）做完整性校验，
// 校验不通过一律不落盘，保留原有可用状态。
namespace PanoExt {

enum class State {
    NotInstalled = 0,
    Installing,
    Installed,
    Failed,
};

struct Status {
    State state = State::NotInstalled;
    QString version;      // 已安装组件的版本（来自 pano-version.txt）
    QString path;         // soleview-pano.exe 的绝对路径
    QString error;        // 失败原因（可展示）
    int percent = 0;      // 安装进度 0-100
    bool compatible = true;   // 版本是否与主程序可用（major.minor 一致）
};

class Manager : public QObject
{
    Q_OBJECT
public:
    static Manager &instance();

    Status status() const;
    // 安装源：环境变量 SOLEVIEW_PANO_SOURCE（回退 QUIKVIEW_PANO_SOURCE）优先，其次 config/ext/pano_source。
    QString sourceUrl() const;
    bool sourceConfigured() const;
    // 下载 → 校验 → 落盘。已在安装中则忽略重复调用。
    void startInstall();
    void cancelInstall();
    bool uninstall(QString *error);

    // 启动组件进程：分参数传递（--image / --heading / --pitch / --fov），
    // 中文与带空格路径不靠引号拼接。失败返回 false 并填 error，调用方保留原图状态。
    bool launch(const QString &imagePath, const Pano::Info &view, QString *error);

    static QString exePath();
    static QString versionFilePath();
    // 组件版本是否与主程序兼容（major.minor 相同）。
    static bool versionCompatible(const QString &componentVersion);

signals:
    void changed();
    void progressChanged(int percent);

private:
    explicit Manager(QObject *parent = nullptr);
    void setFailure(const QString &message);
    void finishInstallFromFile(const QString &zipPath, const QString &shaPath);
    void verifyAndExtract(const QByteArray &zipBytes, const QString &expectedSha);
    QString downloadTempPath() const;

    QNetworkAccessManager *m_net = nullptr;
    QNetworkReply *m_reply = nullptr;
    QNetworkReply *m_shaReply = nullptr;
    QTimer *m_timeout = nullptr;
    State m_state = State::NotInstalled;
    QString m_error;
    int m_percent = 0;
};

} // namespace PanoExt
