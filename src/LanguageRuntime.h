#pragma once
// 运行期界面语言（派单 全量即时化批次23 #87）。
// ---------------------------------------------------------------------------
// 改前现状：`main.cpp` 用一个栈上 QTranslator 只在启动时 load("quikview_<lang>",
// ":/i18n") + installTranslator；运行期没有 retranslate，也没有 QEvent::LanguageChange
// 处理，所以「界面语言」只能保存后重启才生效（SET-01 台账记的「写配置通、加载断」）。
//
// 本文件把翻译器改成应用级单例，并提供两条运行期能力：
//   1. apply()：重新装载 .qm（内置 :/i18n 优先，其次程序目录 translations/，
//      与「打开语言文件夹」按钮的语义一致），失败不改动当前语言并给出原因；
//   2. retranslateAll()：把已显示的文案按「源文 → 新译文」重译一遍。
//
// 重译靠一张运行时映射表：翻译器在每次 translate() 时记录「当前显示文本 →
// (context, source)」。中文（源语言）时 Qt 不会调用任何翻译器，故这里即使没有
// .qm 也装一个透传实现——它返回空串让 Qt 回落到源文，同时把源文登记进表里。
// 这样中英日之间任意往返都能找回源文，不用给 400 多处 tr() 逐个补 retranslate。
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QGroupBox>
#include <QHash>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <QTranslator>
#include <QWidget>

namespace LangRuntime {

// 「下载更多语言」与「打开语言文件夹」的落点都是程序目录下的 translations：
// 用户把 quikview_<语言>.qm 放进去，apply() 就能装到。测试可用
// QUIKVIEW_TEST_LANGDIR 指到隔离目录，避免在真实安装目录里造目录。
inline QString translationsDir()
{
    const QByteArray override = qgetenv("QUIKVIEW_TEST_LANGDIR");
    if (!override.isEmpty())
        return QDir::cleanPath(QString::fromLocal8Bit(override));
    return QCoreApplication::applicationDirPath() + QStringLiteral("/translations");
}

// 随包发布的 8 种非源语言（zh_CN 是源语言，不需要 .qm）。
inline QStringList translatedCodes()
{
    return { QStringLiteral("zh_TW"), QStringLiteral("en_US"), QStringLiteral("ja_JP"),
             QStringLiteral("ko_KR"), QStringLiteral("fr_FR"), QStringLiteral("de_DE"),
             QStringLiteral("es_ES"), QStringLiteral("ru_RU") };
}

// 「跟随系统」的解析口径：系统语言在受支持集合内就用它，否则保持中文。
inline QString systemLanguageCode()
{
    const QString name = QLocale::system().name();
    if (name.startsWith(QLatin1String("zh"), Qt::CaseInsensitive)) {
        return (name.contains(QLatin1String("TW"), Qt::CaseInsensitive)
                || name.contains(QLatin1String("HK"), Qt::CaseInsensitive))
                   ? QStringLiteral("zh_TW")
                   : QStringLiteral("zh_CN");
    }
    for (const QString &code : translatedCodes()) {
        if (name.compare(code, Qt::CaseInsensitive) == 0)
            return code;
    }
    return QStringLiteral("zh_CN");
}

// 语言代码 → 界面上的语言名（与 populateLanguageCombo 的档位名一致，
// 语言名本身用各语言自己的写法，只有「跟随系统」跟随界面翻译）。
inline QString displayName(const QString &code)
{
    if (code == QLatin1String("system"))
        // 用与 populateLanguageCombo 相同的 context（QObject::tr），复用既有译文。
        return QCoreApplication::translate("QObject", "跟随系统");
    if (code == QLatin1String("zh_CN"))
        return QStringLiteral("中文(简体)");
    if (code == QLatin1String("zh_TW"))
        return QStringLiteral("中文(繁體)");
    if (code == QLatin1String("en_US"))
        return QStringLiteral("English");
    if (code == QLatin1String("ja_JP"))
        return QStringLiteral("日本語");
    if (code == QLatin1String("ko_KR"))
        return QStringLiteral("한국어");
    if (code == QLatin1String("fr_FR"))
        return QStringLiteral("Français");
    if (code == QLatin1String("de_DE"))
        return QStringLiteral("Deutsch");
    if (code == QLatin1String("es_ES"))
        return QStringLiteral("Español");
    if (code == QLatin1String("ru_RU"))
        return QStringLiteral("Русский");
    return code;
}

struct TextOrigin {
    QString context;
    QString source;
};

// 唯一安装到 QCoreApplication 的翻译器：内层 QTranslator 持有 .qm，
// 本层只负责「记下每条显示文本的源文」并原样转发译文。
class RecordingTranslator : public QTranslator
{
public:
    QString translate(const char *context, const char *sourceText,
                      const char *disambiguation = nullptr, int n = -1) const override
    {
        const QString hit = m_qm ? m_qm->translate(context, sourceText, disambiguation, n)
                                 : QString();
        const QString shown = hit.isEmpty() ? QString::fromUtf8(sourceText) : hit;
        if (!shown.isEmpty()) {
            QMutexLocker lock(&m_mapMutex);
            if (!m_map.contains(shown))
                m_map.insert(shown, { QString::fromUtf8(context),
                                      QString::fromUtf8(sourceText) });
        }
        return hit;   // 空串 = 未命中，Qt 自己回落到源文
    }

    // 换 .qm：成功时替换内层翻译器；失败保留原语言。
    bool loadQm(const QString &code)
    {
        auto *next = new QTranslator;
        const QString base = QStringLiteral("quikview_") + code;
        const bool ok = next->load(base, QStringLiteral(":/i18n"))
                        || next->load(translationsDir() + QLatin1Char('/') + base);
        if (!ok) {
            delete next;
            return false;
        }
        delete m_qm;
        m_qm = next;
        return true;
    }

    // 回到源语言：卸掉 .qm，源文映射继续保留（中文界面靠它登记/重译）。
    void dropQm()
    {
        delete m_qm;
        m_qm = nullptr;
    }

    bool lookup(const QString &shown, TextOrigin *out) const
    {
        QMutexLocker lock(&m_mapMutex);
        auto it = m_map.constFind(shown);
        if (it == m_map.cend())
            return false;
        if (out)
            *out = it.value();
        return true;
    }

    bool hasQm() const { return m_qm != nullptr; }

private:
    QTranslator *m_qm = nullptr;
    mutable QMutex m_mapMutex;
    mutable QHash<QString, TextOrigin> m_map;
};

// 单窗口整树重译：只动「文案类」控件，不碰输入框 / 数值控件里的用户数据。
inline bool retranslateText(QString *text);

class Manager : public QObject
{
public:
    static Manager &instance()
    {
        static Manager *self = new Manager;
        return *self;
    }

    // 启动：把记录型翻译器装上（即使走源语言也装，用于登记源文），
    // 再按配置装载 .qm。返回 false 表示配置要求的 .qm 缺失（界面回落中文）。
    bool installAtStartup(const QString &code, QString *error = nullptr)
    {
        QCoreApplication::installTranslator(&m_tr);
        m_installed = true;
        return apply(code, error, /*retranslate=*/false);
    }

    // 运行期切换：装 .qm → 让当前所有顶层窗口重译。
    bool apply(const QString &code, QString *error, bool doRetranslate = true)
    {
        const QString req = code.isEmpty() ? QStringLiteral("system") : code;
        const QString eff = (req == QLatin1String("system")) ? systemLanguageCode() : req;
        if (eff == QLatin1String("zh_CN")) {
            // 源语言：卸掉 .qm，保留记录器（源文映射靠它登记）。
            m_tr.dropQm();
            m_code = req;
            m_effective = eff;
            if (doRetranslate)
                retranslateAll();
            return true;
        }
        if (!m_tr.loadQm(eff)) {
            if (error) {
                *error = QCoreApplication::translate(
                             "SettingsDialog",
                             "缺少 %1.qm（已查找内置 i18n 与 translations 目录）")
                             .arg(QStringLiteral("quikview_") + eff);
            }
            return false;
        }
        m_code = req;
        m_effective = eff;
        if (doRetranslate)
            retranslateAll();
        return true;
    }

    QString code() const { return m_code; }
    QString effectiveCode() const { return m_effective; }
    bool hasQm() const { return m_tr.hasQm(); }

    bool lookup(const QString &shown, TextOrigin *out) const { return m_tr.lookup(shown, out); }

    void retranslateAll();

private:
    RecordingTranslator m_tr;
    bool m_installed = false;
    QString m_code = QStringLiteral("system");
    QString m_effective = QStringLiteral("zh_CN");
};

inline bool retranslateText(QString *text)
{
    if (!text || text->isEmpty())
        return false;
    TextOrigin origin;
    if (!Manager::instance().lookup(*text, &origin))
        return false;
    const QByteArray ctx = origin.context.toUtf8();
    const QByteArray src = origin.source.toUtf8();
    const QString next = QCoreApplication::translate(ctx.constData(), src.constData());
    if (next.isEmpty() || next == *text)
        return false;
    *text = next;
    return true;
}

// 深度优先把窗口里所有文案控件重译一遍；返回改动条数（探针取证用）。
inline int retranslateWidget(QWidget *w)
{
    if (!w)
        return 0;
    int changed = 0;
    auto applyText = [&changed](QString *text) {
        if (retranslateText(text))
            ++changed;
    };

    if (auto *lab = qobject_cast<QLabel *>(w)) {
        QString t = lab->text();
        applyText(&t);
        if (t != lab->text())
            lab->setText(t);
    } else if (auto *box = qobject_cast<QGroupBox *>(w)) {
        QString t = box->title();
        applyText(&t);
        if (t != box->title())
            box->setTitle(t);
    } else if (auto *btn = qobject_cast<QAbstractButton *>(w)) {
        QString t = btn->text();
        applyText(&t);
        if (t != btn->text())
            btn->setText(t);
        QString tip = btn->toolTip();
        if (retranslateText(&tip))
            btn->setToolTip(tip);
    } else if (auto *combo = qobject_cast<QComboBox *>(w)) {
        for (int i = 0; i < combo->count(); ++i) {
            QString t = combo->itemText(i);
            applyText(&t);
            if (t != combo->itemText(i))
                combo->setItemText(i, t);
        }
    }

    if (w->isWindow()) {
        QString t = w->windowTitle();
        applyText(&t);
        if (t != w->windowTitle())
            w->setWindowTitle(t);
    }

    for (QAction *act : w->actions()) {
        QString t = act->text();
        applyText(&t);
        if (t != act->text())
            act->setText(t);
        QString tip = act->toolTip();
        if (retranslateText(&tip))
            act->setToolTip(tip);
    }

    const QObjectList kids = w->children();
    for (QObject *child : kids) {
        if (auto *cw = qobject_cast<QWidget *>(child))
            changed += retranslateWidget(cw);
    }
    return changed;
}

inline void Manager::retranslateAll()
{
    for (QWidget *top : QApplication::topLevelWidgets())
        retranslateWidget(top);
}

} // namespace LangRuntime
