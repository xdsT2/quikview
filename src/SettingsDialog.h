#pragma once

#include <QDialog>
#include <QHash>
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
class QFrame;
class QHBoxLayout;
class QKeyEvent;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QMouseEvent;
class QPaintEvent;
class QPushButton;
class QResizeEvent;
class QSpacerItem;
class QSpinBox;
class InputModeWatcher;
class PillToggle;
class QStackedWidget;
class QVBoxLayout;
class QWidget;

// 设置对话框：QuikView 深色三栏布局（视觉真源 Temp/quikview-secondary-prototype.html）。
// 顶部 43px 标题栏 + 左侧 204px 导航（搜索 + 11 个 navItem）+ 1px 竖分隔 + 内容区 + 62px footer。
// 11 页：一般 / 浏览 / 外观 / 图像处理 / 格式关联 / 键盘 / 鼠标 / 书签 / 上下文菜单 / 杂项 / 语言。
//
// 2026-11-07 设置页排版统一首批：
//   · 统一普通设置行（左文案 + 右控件列，组卡片 + 整行悬停），复用到 11 页；
//   · 普通开关 / 下拉 / 数值 / 路径输入改为即时写盘（关闭窗口不保存也保留），
//     运行态由 MainWindow 在对话框关闭后按配置重放；
//   · 只有热键改绑与界面语言仍待保存：关闭时若有改动，弹「保存更改？」。
//     不保存只丢弃这两类草稿，已即时应用的设置保留；改回原值即清除待保存状态。
//   · 左侧导航顶部「搜索设置…」按设置名匹配并跳转，清空恢复全部导航。
// 控件全部使用标准 Qt 控件与胶囊开关，配色/字号由 UiStyle::dialogQss() 提供。
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override;

    // 输入来源追踪（v2 焦点可见性）：鼠标按下即切指针模式、清掉所有 kbFocus；
    // 键盘按键切键盘模式，只给当前焦点控件加环。公开供内部过滤器与本类共享。
    bool keyboardInputMode() const;
    void refreshFocusVisibility();

    // —— 键盘自定义验收探针（--smoke keyconflict / keyreset，非交互路径）——
    // probeKeyConflict：把「删除」行热键改成 Ctrl+P（撞固定动作「打印」）走真实
    // 保存链路，应弹占用提示、冲突行高亮、config 不落盘（两份证据截图落 Temp/）。
    // probeResetDefaults：点 footer「恢复默认」并自动确认，走 onReset 同一条链路。
    void probeKeyConflict();
    void probeResetDefaults();
    // probeOpenCombo：切到第 page 页并展开其中第 index 个可见下拉，用于
    // 「设置里的下拉展开时主顶栏是否被误唤出」的截图验收；同时把各下拉的
    // 序号/当前值写到 Temp/settings-combos.txt，避免靠猜索引取错控件。
    void probeOpenCombo(int page, int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void reject() override;
    // 所有关闭保存路径（保存按钮、Enter、默认按钮）的唯一入口：
    // 先校验再写盘，校验失败不关闭窗口（P0-1）。
    void accept() override;

private slots:
    // 校验 + 写盘；键盘冲突 / 写盘失败时不写盘并返回 false。
    bool onAccepted();
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
    // 按当前窗口宽度算「一般」页内容列的左右边界：最大 660，超出时居中；
    // 其余页回到 0 边距（column 自带 28 内边距），与改前一致。
    void updateContentColumnWidth();
    // 一般页页头「标题—说明—内容」间距：标准 8+8，紧凑 6+6；其余页 13/15。
    void updateGeneralHeaderSpacing();
    // v2 紧凑档（窗口宽 < 1000）：左栏 180、导航行 34 / 行距 2、标题 40 /
    // 底栏 43、组间距 10、行高约 40。标准档恢复 211/36/4/41/44/15/49。
    void applyCompactLayout(bool compact);

    // —— 十一个页面 ——
    void buildGeneralPage(QWidget *page);
    void buildViewPage(QWidget *page);
    void buildAppearancePage(QWidget *page);
    void buildImageProcPage(QWidget *page);
    void buildAssocPage(QWidget *page);
    void buildKeyboardPage(QWidget *page);
    void buildMousePage(QWidget *page);
    void buildBookmarkPage(QWidget *page);
    void buildContextMenuPage(QWidget *page);
    void buildEtcPage(QWidget *page);
    void buildLangPage(QWidget *page);

    // —— 统一设置行工厂（全部走布局，不绝对定位）——
    // beginSection：分组标题 + 卡片壳；标题与行文案共享同一条左边线。
    // topGap < 0 时用全局默认 kGroupTop；一般页样板传 24 拉开分组层次。
    QWidget *beginSection(QWidget *page, const QString &title, int topGap = -1);
    // 组卡片本体（beginSection 内部创建，行 / 分隔线都追加到这里）。
    QWidget *groupCard(QWidget *section) const;
    // 统一页节奏：首组 12 / 组间 20、行高 42、右侧控件列整页同宽并右对齐、
    // 分组标题提级 13px/600。九页在各自 builder 末尾调用一次。
    // 负数参数 = 沿用 kPage* 默认值；「一般」样板传定稿的 22/14/44/160，
    // 其他页取值与调用方式一律不变（在途的九页节奏保持原状）。
    void applyPageRhythm(QWidget *page, int firstGap = -1, int groupGap = -1,
                         int rowH = -1, int ctlW = -1);
    struct SettingRowParts {
        QWidget *row = nullptr;
        QLabel *label = nullptr;
        QLabel *desc = nullptr;
        QHBoxLayout *controls = nullptr;
    };
    // 统一行：左侧名称 + 可选说明（随文本列换行），右侧控件列共用右边线。
    SettingRowParts addSettingRow(QWidget *section, const QString &label,
                                  const QString &desc = QString());
    // 二态项：胶囊开关（保持 40×20 与现有点击命中）。
    PillToggle *addSwitchRow(QWidget *section, const QString &label, const QString &desc,
                             const char *key, bool inverted, bool def);
    QHBoxLayout *addRow(QWidget *section, const QString &label,
                        const QString &desc = QString());
    QCheckBox *addCheck(QWidget *section, const QString &text, const char *key,
                        bool inverted, bool def);
    // v2 pop-up：互斥值字段（当前值 + 下箭头 + 勾选 + 禁用原因）。
    QComboBox *makePopupCombo(QWidget *parent);
    QComboBox *addComboRow(QWidget *section, const QString &label, const QString &desc,
                           const char *key, const QString &def,
                           const QList<QPair<QString, QString>> &options,
                           int minWidth = 0);
    QLabel *addHint(QWidget *section, const QString &text);
    // 胶囊拨动开关行：左侧标题 + 说明，右侧开关（右对齐）；返回三件套便于
    // 禁用态联动（跟随系统开启时「浅色模式」整块灰显）。
    struct ToggleRow {
        PillToggle *toggle = nullptr;
        QLabel *label = nullptr;
        QLabel *desc = nullptr;
        QWidget *row = nullptr;
    };
    ToggleRow addToggleRow(QWidget *section, const QString &label,
                           const QString &desc, const QString &accessibleName);
    // 胶囊二态项 + 绑定表：标题 + 说明 + 右侧胶囊。
    PillToggle *addPillCheck(QWidget *section, const QString &label, const char *key,
                             bool inverted, bool def, const QString &timing);
    QPushButton *addButton(QWidget *parent, const QString &text);
    QPushButton *addMiniButton(QWidget *parent, const QString &text);
    // 同页下拉统一宽度：按最长选项计算，取整页各行的最大需求。
    void levelComboWidths(QWidget *page);

    // —— 诚实化：零消费项统一灰显 ——
    // 配置 key 仍留在绑定表里（保存照常写入，向后兼容），但视觉与交互必须是
    // 明确的不可用态，避免「能勾、能存、界面正常、实际无效」的假开关。
    void markUnavailable(QWidget *w);
    QCheckBox *addCheckUnavailable(QWidget *section, const QString &text, const char *key,
                                   bool inverted, bool def);
    QComboBox *addComboRowUnavailable(QWidget *section, const QString &label,
                                      const QString &desc, const char *key,
                                      const QString &def,
                                      const QList<QPair<QString, QString>> &options,
                                      int minWidth = 0);

    void loadFromConfig();

    // —— 唯一写盘入口（2026-11-07 关闭流程统一）——
    // 即时项、兼容桥键、关闭时的按变更键提交都经 writeValues：
    // 写内存 → sync → 检查 QSettings::status()。
    // retryable=true（即时项）：失败时值保留在内存并记入 m_pendingWrites，
    // 下一次提交 / 关闭时重试；retryable=false（热键 / 语言草稿）：失败时
    // 立即回滚内存，草稿绝不留在 QSettings 里，后续「不保存」无从把它们刷盘。
    bool commitValues(const QList<QPair<QString, QVariant>> &values, QString *error = nullptr);
    bool flushPendingWrites(QString *error = nullptr);
    bool writeValues(const QList<QPair<QString, QVariant>> &values, bool retryable,
                     QString *error);
    void refreshSettingsInstanceIfError();
    void warnSaveFailure(const QString &error);
    QHash<QString, QVariant> m_pendingWrites;   // 已写入内存、尚未 sync 成功的即时项
    bool m_forceSync = false;                   // 恢复默认等直接改内存的批量变更待同步

    // —— 即时写盘（普通项默认路径）——
    // 控件变更立刻写 config + 同步写兼容桥键；运行态在可下发项上由设置页
    // 即时调用 MainWindow::applyLiveSetting，其余按配置在下一次操作生效。
    void connectInstantSignals();
    bool applyInstantValue(const QString &key, const QVariant &value);
    void writeBridgeKeys();
    bool m_loading = false;

    // —— 待保存项（热键改绑 / 界面语言）——
    QStringList pendingChangeNames() const;
    void capturePendingBaseline();
    void updatePendingNote();
    QLabel *m_footerNote = nullptr;
    QHash<QString, QString> m_keyBaseline;   // action → PortableText（进入设置时）
    QString m_langBaseline;                  // 进入设置时的语言代码

    // —— 搜索 ——
    QLineEdit *m_search = nullptr;
    QLabel *m_searchEmpty = nullptr;
    QStringList m_pageSearchIndex;   // 每页可匹配文字（小写）
    void buildSearchIndex();
    void filterNavigation(const QString &text);
    void selectFirstVisiblePage();

    // —— SET-02 即时预览会话（样板：view/exif_small_font）——
    // 控件变更下发 MainWindow 运行态并立即写盘；失败时控件与运行态回退，
    // 不留下半套状态。取消关闭不再回滚（已即时应用的设置保留）。
    struct LiveCheck {
        QCheckBox *cb;
        const char *key;
        bool inverted;
        bool lastValue;         // 最近一次成功下发的值（幂等 / 失败回退）
    };
    void connectLiveChecks();
    void applyLiveCheck(int index, bool checked);
    QList<LiveCheck> m_liveChecks;

    // —— 键盘自定义（批次 D1）冲突检测 ——
    // 保存前调用：同一键位只归一个启用的绑定、键位不得与固定动作相撞、
    // 同一动作不得被两个启用槽位指派。有冲突：高亮冲突行并返回第一条提示。
    QString keyboardConflicts();
    void clearKeyConflicts();
    void setKeyRowConflict(int index, bool on);   // 整行高亮（QSS 动态属性）
    // 实时冲突反馈：红条文案 + 保存按钮可用性 + 行内「冲突」标记。
    void refreshKeyConflictBanner();
    // 录入态：页首说明与行内「正在录入」跟随焦点切换。
    void refreshKeyEditingState();
    void setKeyHintMode(bool editing);

    QList<QWidget *> m_pages;              // 各页内容 widget（QScrollArea 的 widget）
    QList<QAbstractButton *> m_tabs;       // 左侧 navItem
    QStringList m_pageTitles;
    QStringList m_pageDescs;
    QStackedWidget *m_stack = nullptr;
    QVBoxLayout *m_navLayout = nullptr;
    // 「一般」样板外壳：只有 index 0 把内容列限到 748 并居中、隐藏页头分隔线、
    // 页标题与卡片左缘对齐；切到其他页时逐项还原，保证其余十页零变化。
    QWidget *m_contentColumn = nullptr;
    QVBoxLayout *m_contentLay = nullptr;
    QFrame *m_headLine = nullptr;
    QSpacerItem *m_headGapTop = nullptr;     // 页头说明与分隔线之间（一般页收窄）
    QSpacerItem *m_headGapBottom = nullptr;  // 分隔线与内容之间
    QSpacerItem *m_footerGap = nullptr;      // 底栏提示与卡片左缘对齐的弹性间隔
    // v2 紧凑档需要按窗口宽度改这些外壳尺寸
    QWidget *m_navRail = nullptr;
    QWidget *m_titleBar = nullptr;
    QWidget *m_footer = nullptr;
    QPushButton *m_btnAbout = nullptr;
    bool m_compact = false;
    // 输入来源追踪：只有键盘导航才让控件显示焦点环（v2 焦点规则）。
    friend class InputModeWatcher;
    InputModeWatcher *m_inputMode = nullptr;
    QLabel *m_pageTitle = nullptr;
    QLabel *m_pageDesc = nullptr;
    QPushButton *m_btnReset = nullptr;
    QPushButton *m_btnOk = nullptr;

    // 标题栏拖拽
    bool m_dragging = false;
    bool m_centered = false;
    QPoint m_dragOffset;

    // —— 绑定表 ——
    QList<ChkBind> m_chkBinds;
    QList<ComboBind> m_comboBinds;

    // —— 专项控件 ——
    QSpinBox *m_cacheSize = nullptr;      // 图像处理：缓存大小 MB
    // 外观页（第 18 期）：主题三件套——不进绑定表，saveAppearance() 手工落盘，
    // 因为 config/theme 是三态字符串（dark/light/system），不是布尔/枚举绑定能表达的。
    // 2026-11-06：改为胶囊拨动开关，「深色模式」更名为「浅色模式」（浅色开 = light）。
    // 界面主题：单一 pop-up 字段，值 system / light / dark（2026-11-11 修订）。
    QComboBox *m_themeCombo = nullptr;
    PillToggle *m_blurMaterial = nullptr;  // 「磨砂材质」ui/blur_material（④ 接线生效）


    // —— 主题即时应用 ——
    // 主题是复合控件（跟随 + 浅色两个开关映射同一 config/theme），不走
    // LiveCheck（那个结构是一键一布尔）；单独处理：切换即下发运行态并写盘。
    void applyThemePreview();
    // 延迟下发：点击回调只更新状态并排队，重任务等开关圆点过渡走完再执行，
    // 否则全程重抛光会把过渡拦腰截断；serial 用于丢弃被新点击取代的旧请求。
    void applyPendingTheme(int serial);
    void flushPendingTheme();
    void syncThemeControls(const QString &theme);
    void updateThemeControlStates();
    void refreshDialogStyle();
    QString themeFromControls() const;
    QLabel *makeToggleLabel(const QString &text, const QString &objectName);
    bool m_themeApplying = false;
    QString m_themeLast;   // 最近一次成功下发的主题（失败时回退用它）
    // 运行态实际已应用的「来源值」与「配色」：等色切换只改来源，不重复换肤。
    QString m_themeAppliedSetting;
    QString m_themeAppliedMode;   // "light" / "dark"
    QString m_themePending;       // 待下发的来源值
    bool m_themePendingActive = false;
    int m_themeSerial = 0;
    QTimer *m_themeDefer = nullptr;   // 变色下发的延迟器（按圆点过渡时长）

    struct KeyRow {                       // 键盘页一行（QString 值拷贝：
        QCheckBox *cb;                    // 数据源是构建函数的栈数组，const char*
        QKeySequenceEdit *edit;           // 悬空会导致热键列显示乱码）
        QLabel *actionLab;                // 「操作」列只读展示（2026-11-04 用户拍板：
        QWidget *row;                     // 动作不允许改，只有勾选与热键可改）
        QString action;
        QString defHotkey;                // 出厂键位（PortableText，与 MainWindow 一致）
        QString label;                    // 动作显示名（冲突提示用）
        QLabel *stateLab = nullptr;       // 键帽右侧「正在录入 / 冲突」行内状态
    };
    QList<KeyRow> m_keyRows;
    // 键盘页三态（2026-11-11 修订）：页首说明随录入切换、冲突红条常驻可读，
    // 有冲突时「保存设置」不可用；解决后自动复原。
    QLabel *m_keyHint = nullptr;
    QWidget *m_keyBanner = nullptr;
    QLabel *m_keyBannerText = nullptr;
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
    QPushButton *m_checkUpdateBtn = nullptr;  // 一般页：手动检查更新
    // 三选一确认框执行中：主窗口 closeEvent 与 Qt 自动关闭子窗口可能
    // 在同一次退出里各调一次 reject()，此标志保证只弹一次。
    bool m_prompting = false;
};
