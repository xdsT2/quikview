#pragma once

#include <QDialog>
#include <QList>
#include <QPair>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVariantMap>

class QAbstractButton;
class QCheckBox;
class QComboBox;
class QEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QMouseEvent;
class QPaintEvent;
class QSpinBox;
class QWidget;

// 设置对话框：Honeyview 5.53 像素级复刻（对照原版截图逐页重建）。
// 白色无边框 + 自绘标题/关闭 × + 左侧奶黄页签列 + 左下 重置(奶黄)/确定(白底蓝边)。
// 10 页：一般 / 浏览 / 图像处理 / 格式关联 / 键盘 / 鼠标 / 书签 / 上下文菜单 / 杂项 / 语言。
// 打开时快照 config 组：确定 → 写回 Config；× / ESC → 回滚快照；重置 → 确认后清空恢复默认。
// 自定义控件（HvTabButton/HvButton/HvCheckBox/HvComboBox/HvSpinBox/HvLineEdit/
// HvLink/HvColorButton/色块）在 .cpp 匿名命名空间定义，此处仅持基类指针。
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
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

    QWidget *addPage(const QString &title, const QString &header);

    // —— 十个页面（对照原版截图布局）——
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

    // —— 控件工厂 ——
    void addChk(QWidget *page, int y, const QString &text, const char *key,
                bool inverted, bool def, int indent = 0);
    QComboBox *addComboRow(QWidget *page, int y, const QString &label, const char *key,
                           const QString &def,
                           const QList<QPair<QString, QString>> &options,
                           int labelRight = 310, int comboX = 318, int comboWidth = 290);
    QLabel *addSectionBold(QWidget *page, int y, const QString &text);
    QLabel *addNote(QWidget *page, int y, const QString &text);
    void addHLine(QWidget *page, int y, int x = 16, int w = 0);
    void addColumnHeader(QWidget *page, int y, const QList<QPair<QString, int>> &cols);

    void loadFromConfig();
    void takeSnapshot();
    void restoreSnapshot();
    void setCloseHover(bool hover);

    QList<QWidget *> m_pages;
    QList<QAbstractButton *> m_tabs;
    QAbstractButton *m_btnReset = nullptr;
    QAbstractButton *m_btnOk = nullptr;

    // 自绘标题栏
    QRect m_closeRect;
    bool m_closeHover = false;
    bool m_dragging = false;
    QPoint m_dragOffset;

    // 打开时的配置快照（取消时回滚）
    QVariantMap m_snapshot;

    // —— 绑定表 ——
    QList<ChkBind> m_chkBinds;
    QList<ComboBind> m_comboBinds;

    // —— 专项控件 ——
    QSpinBox *m_cacheSize = nullptr;      // 图像处理：缓存大小 MB
    QLineEdit *m_bkgndPath = nullptr;     // 浏览：背景图片路径
    QWidget *m_bkgndColorBtn = nullptr;   // 浏览：背景颜色（当前色预览 + 取色）
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
