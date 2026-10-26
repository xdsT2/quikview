#pragma once

#include <QDialog>
#include <QList>
#include <QPair>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QAbstractButton;
class QCheckBox;
class QComboBox;
class QEvent;
class QHBoxLayout;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QMouseEvent;
class QPaintEvent;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QVBoxLayout;
class QWidget;

// 设置对话框：QuikView 深色三栏布局（视觉真源 Temp/quikview-secondary-prototype.html）。
// 顶部 43px 标题栏 + 左侧 204px 导航（10 个 navItem）+ 1px 竖分隔 + 内容区 + 62px footer。
// 10 页：一般 / 浏览 / 图像处理 / 格式关联 / 键盘 / 鼠标 / 书签 / 上下文菜单 / 杂项 / 语言。
// 打开时快照 config 组：保存设置 → 写回 Config；× / ESC → 回滚快照；恢复默认 → 确认后清空。
// 控件全部使用标准 Qt 控件，配色/字号由 UiStyle::dialogQss() 提供。
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void reject() override;

private slots:
    void onAccepted();
    void onReset();

private:
    // 绑定表条目：load/save 统一遍历。inverted=true 表示 key 存正向语义
    // （如 show_menu_bar，勾选"不显示菜单栏" = key 写 false）。
    struct ChkBind {
        QCheckBox *cb;
        const char *key;
        bool inverted;
        bool def;
    };
    struct ComboBind {
        QComboBox *combo;
        const char *key;
        QString def;
    };

    // —— 页面骨架 ——
    QWidget *addPage(const QString &title, const QString &desc);
    void selectPage(int index);

    // —— 十个页面 ——
    void buildGeneralPage(QWidget *page);
    void buildViewPage(QWidget *page);
    void buildImageProcPage(QWidget *page);
    void buildAssocPage(QWidget *page);
    void buildKeyboardPage(QWidget *page);
    void buildMousePage(QWidget *page);
    void buildBookmarkPage(QWidget *page);
    void buildContextMenuPage(QWidget *page);
    void buildEtcPage(QWidget *page);
    void buildLangPage(QWidget *page);

    // —— 控件工厂（全部走布局，不绝对定位）——
    QWidget *beginSection(QWidget *page, const QString &title);
    QHBoxLayout *addRow(QWidget *section, const QString &label, int labelWidth = 155);
    QWidget *addRowWidget(QWidget *section, const QString &label, int labelWidth = 155);
    QCheckBox *addCheck(QWidget *section, const QString &text, const char *key,
                        bool inverted, bool def);
    QCheckBox *addCheckRaw(QWidget *section, const QString &text);
    QComboBox *addComboRow(QWidget *section, const QString &label, const char *key,
                           const QString &def,
                           const QList<QPair<QString, QString>> &options,
                           int comboWidth = 160);
    QLabel *addHint(QWidget *section, const QString &text);
    QPushButton *addButton(QWidget *parent, const QString &text);
    QPushButton *addMiniButton(QWidget *parent, const QString &text);

    // —— 诚实化：零消费项统一灰显 ——
    // 配置 key 仍留在绑定表里（保存照常写入，向后兼容），但视觉与交互必须是
    // 明确的不可用态，避免「能勾、能存、界面正常、实际无效」的假开关。
    void markUnavailable(QWidget *w);
    QCheckBox *addCheckUnavailable(QWidget *section, const QString &text, const char *key,
                                   bool inverted, bool def);
    QComboBox *addComboRowUnavailable(QWidget *section, const QString &label, const char *key,
                                      const QString &def,
                                      const QList<QPair<QString, QString>> &options,
                                      int comboWidth = 160);

    void loadFromConfig();
    void takeSnapshot();
    void restoreSnapshot();

    QList<QWidget *> m_pages;              // 各页内容 widget（QScrollArea 的 widget）
    QList<QAbstractButton *> m_tabs;       // 左侧 navItem
    QStringList m_pageTitles;
    QStringList m_pageDescs;
    QStackedWidget *m_stack = nullptr;
    QVBoxLayout *m_navLayout = nullptr;
    QLabel *m_pageTitle = nullptr;
    QLabel *m_pageDesc = nullptr;
    QPushButton *m_btnReset = nullptr;
    QPushButton *m_btnOk = nullptr;
    QList<QCheckBox *> m_assocChecks;      // 格式关联页扩展名勾选（直接读写 config/assoc/ext_*）

    // 标题栏拖拽
    bool m_dragging = false;
    bool m_centered = false;
    QPoint m_dragOffset;

    // 打开时的配置快照（取消时回滚）
    QVariantMap m_snapshot;

    // —— 绑定表 ——
    QList<ChkBind> m_chkBinds;
    QList<ComboBind> m_comboBinds;

    // —— 专项控件 ——
    QSpinBox *m_cacheSize = nullptr;      // 图像处理：缓存大小 MB
    QLineEdit *m_bkgndPath = nullptr;     // 浏览：背景图片路径
    QWidget *m_bkgndColorBtn = nullptr;   // 浏览：背景颜色
    QWidget *m_borderColorBtn = nullptr;  // 浏览：边框颜色
    struct KeyRow {                       // 键盘页一行（QString 值拷贝：
        QCheckBox *cb;                    // 数据源是构建函数的栈数组，const char*
        QLineEdit *edit;                  // 悬空会导致热键列显示乱码）
        QComboBox *combo;
        QString action;
        QString defHotkey;
    };
    QList<KeyRow> m_keyRows;
    struct MouseRow {                     // 鼠标页按键动作行（同样避免悬空指针）
        QComboBox *combo;
        QString key;
        QString def;
    };
    QList<MouseRow> m_mouseRows;
    QComboBox *m_maxBookmarks = nullptr;  // 书签页
    QLineEdit *m_photoFolder1 = nullptr;
    QLineEdit *m_photoFolder2 = nullptr;
    QLineEdit *m_editorPath = nullptr;    // 杂项页
    QComboBox *m_language = nullptr;      // 语言页
};
