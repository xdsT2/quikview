#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QWidget;

// 检查更新：查询 GitHub Releases 的 latest，并与当前版本逐段比较。
//
// 设计约束（HANDOFF 第 10 期）：
//  - 网络走 QNetworkAccessManager，默认使用系统代理；请求 8s 超时。
//  - 自动检查在启动后延迟 5s 才发（由 main.cpp 挂接），且 ≥24h 才真发一次
//    （时间戳记在 config 的 etc/last_update_check）；手动检查不受频控。
//  - 失败时自动检查完全静默，手动检查由 UpdateNotifier 提示「检查失败，请检查网络」。
//  - 版本比较是逐段数字比较，不按字符串比（0.10.0 > 0.9.9）。
class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    enum class Mode { Manual, Automatic };

    static UpdateChecker &instance();

    // 发起一次检查。返回 false 表示没有发请求（已有请求在飞 / 自动检查未到 24h）。
    bool check(Mode mode);

    // 版本比较：a < b → -1，a == b → 0，a > b → 1。
    // 规则：剥掉前缀 v/V 与 +build 元数据；核心段按数字逐段比，缺段按 0；
    // 同号时预发布（0.4.0-dev）低于正式版（0.4.0）。
    static int compareVersions(const QString &a, const QString &b);
    static bool isNewer(const QString &candidate, const QString &current);

signals:
    // 一次检查结束（任何结果都发），用于恢复「检查更新」按钮。
    void finished(bool manual);
    // version 已带 v 前缀（如 v0.4.1），url 为 Release 页面。
    void updateAvailable(const QString &version, const QString &url, bool manual);
    void upToDate(bool manual);
    void checkFailed(bool manual);

private:
    explicit UpdateChecker(QObject *parent = nullptr);

    void onReplyFinished();
    void onTimeout();

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QTimer *m_timeout = nullptr;
    bool m_manual = false;
    bool m_timedOut = false;
};

// 把 UpdateChecker 的结果变成顶部提示条。
// 手动/自动共用：有新版都提示（带「前往下载」），失败/已最新只在手动检查时提示。
class UpdateNotifier : public QObject
{
    Q_OBJECT
public:
    explicit UpdateNotifier(QWidget *fallbackAnchor, QObject *parent = nullptr);

private:
    QWidget *targetAnchor() const;

    QWidget *m_fallback = nullptr;
};
