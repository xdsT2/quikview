#pragma once

#include <QDialog>
#include <QHash>
#include <QList>
#include <QPair>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <functional>

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
class QProgressBar;
class QResizeEvent;
class QScrollArea;
class QSpacerItem;
class QSpinBox;
class InputModeWatcher;
class PillToggle;
class QStackedWidget;
class QTimer;
class QVBoxLayout;
class QWidget;

namespace Chrome {
class Toast;
}

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
    void syncAppliedTheme(const QString &theme);
    // 主题切换验收探针（--smoke settheme:<dark|light|system>，非交互路径）：
    // 直接改主题下拉的 currentIndex，走与鼠标选择相同的 currentIndexChanged
    // → applyThemePreview 链路，避免鼠标坐标漂移影响延迟采样。
    void probeTheme(const QString &theme);
    // 批次22 即时化验收探针（--smoke etcprobe:<spec>）：切到杂项页，按 cfgKey
    // 找到 #84/#85/#86 三个开关并点击到目标态（走 applyLiveCheck 真实链路），
    // 把切换前后「控件态 / Config getter / QSettings 原始值」dump 到 Temp/。
    // spec 形如 "etc/raw_thumbnail=off+etc/fit_window_to_image=on"（+ 分隔，
    // 因 --smoke 用逗号拆命令；目标态 on/off）。
    void probeEtcLive(const QString &spec);
    // 批次03 浏览页四项验收探针（--smoke viewprobe / viewcancel / vievesc /
    // viewclose / viewsave:<spec>）：切到浏览页，按 cfgKey 点击开关到目标态（走
    // applyLiveCheck 真实链路），逐项 dump「控件态 / getter / ini 原始值 / 主窗口
    // 运行态」到 Temp/view-live-probe.txt；closeMode 1=取消钮 2=Esc 3=✕ 4=保存。
    void probeViewLive(const QString &spec, int closeMode, bool thenSave);
    // 批次04 图像处理页四项探针（--smoke iprobe/ipcancel/ipesc/ipclose/ipsave）：
    // 走真实控件点击链路（PillToggle::click / 数值框 setValue）→ applyLiveSetting → 写盘。
    void probeImageProcLive(const QString &spec, int closeMode, bool thenSave);
    // 验收探针（--smoke setcfglive:<cfgKey>=on|off）：切到「图像处理」页，按 cfgKey
    // 真实点击开关，dump 前后「控件态 / Config getter / QSettings 原始值」到 Temp/。
    void probeCfgLive(const QString &spec);
    // 九页逐项即时化取证探针（--smoke cfgclick:<页名>|<key>=<值>+<key>=<值>…）：
    // 按页签先建页，再按绑定表（m_chkBinds / m_comboBinds）定位真实控件，
    // 开关走 PillToggle::click、下拉走 setCurrentIndex 的同一条绑定链路，
    // 每项 dump「控件态 / ini 原始值 / present / 主窗口运行态 / 是否达到目标态」。
    // 页面用文案片段（如「外观与显示」）定位，或直接用 0 起的页序号；省略则用当前页。
    void probeCfgClick(const QString &spec);
    // 批次21（#81 翻页音效 / #82 不打开 RAW / #83 JPEG 同名 RAW）即时化验收探针。
    // --smoke batch21probe:<g81|g82:<rawPath>|g83|close:<cancel|esc|x|save>>
    void probeBatch21(const QString &spec);
    // 批次18 验收探针（--smoke folderprobe:<root>）：切到「浏览」页的「图片文件夹 1」行，
    // 走真实输入框 / 「浏览…」回填 / 「打开」按钮链路，逐态 dump「输入框 / 配置原始值 /
    // 行内状态行 / 真实复制落点」到 <root>/folder1-live-probe.txt，并抓设置窗截图。
    // root 由调用方给独立临时目录（样本图与落点都在里面，不碰用户图片目录）。
    void probeFolderLive(const QString &root);
    // 重启读回探针（--smoke folderrestore:<root>）：新进程按隔离 ini 打开设置页，
    // dump「输入框回填值 / 配置原始值」到 <root>/folder1-restore.txt 并截图。
    void probeFolderRestore(const QString &root);
    // 批次05 图像处理页五项验收探针（--smoke iprobe:<k=v+…> / ipclose:<mode>）：
    // 切到对应页，按配置键真实切档（下拉走 currentIndexChanged、开关走 click），
    // 逐项 dump「控件值 / ini 原始值 / 主窗口运行态 / 页内状态行」到
    // Temp/imgproc-live-probe.txt；closeMode 取 cancel / esc / close / save。
    void probeImgProcLive(const QString &spec, const QString &closeMode);
    // 追加一步（--smoke ipstep:<k=v+…>）：不关窗，按配置键真实切档并 dump 一步。
    void probeImgProcApply(const QString &spec);
    void probeImgProcDump(const QString &tag);
    // 验收探针（--smoke switchprobe:<cfgKey>=on|off）：按 cfgKey 定位任意开关并真实点击，
    // dump 前后控件态 / Config getter（由调用方给）/ QSettings 原始值，验「改开关即时写盘」。
    void probeOneSwitch(const QString &key, bool want,
                        const std::function<bool()> &getter,
                        const QString &outFile);
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
    // probeSavePrompt(out)：造一条待保存草稿（键盘页「删除」改绑到不冲突的
    // Ctrl+Alt+F12）后调真实 reject()，弹「保存更改？」三选一；在嵌套循环里把
    // 按钮的排版顺序 / 角色 / 默认钮 / 初始焦点 / 几何落盘到 out（第 4 批弹窗验收）。
    void probeSavePrompt(const QString &out);
    // 批次13 键盘页即时化验收探针（--smoke kblive:<action>+<step>…，非交互路径）：
    // 切到键盘页，对指定动作行依次执行「点启用 / 录入热键 / 保存 / 关闭」等真实
    // 控件链路（点勾选走 QCheckBox::toggled → applyInstantValue + applyLiveSetting，
    // 录入走 QKeySequenceEdit 的真实按键事件），每步把「勾选态 / 可用态 / 行内
    // 状态 / 配置值 / 运行态键位表」追加到 outPath。步骤：dump[:tag] / on / off /
    // record:<键> / clear / save / esc；'+' 分隔（--smoke 用逗号拆命令）。
    void probeKeyLive(const QString &spec, const QString &outPath = QString());
    // probeOpenCombo：切到第 page 页并展开其中第 index 个可见下拉，用于
    // 「设置里的下拉展开时主顶栏是否被误唤出」的截图验收；同时把各下拉的
    // 序号/当前值写到 Temp/settings-combos.txt，避免靠猜索引取错控件。
    void probeOpenCombo(int page, int index);
    // probePageAtSize：切到第 page 页并把设置窗摆到指定尺寸（最小窗口 900×660
    // 的滚动/末组可达验收用；--smoke settingsgeo:<page>:<W>x<H>[:bottom][:fold]）。
    void probePageAtSize(int page, int w, int h, bool toBottom = false,
                         bool expandFormats = false);
    // probeSwitchAcceptance(outDir, w, h)：开关统一规格验收（2026-11-22）。
    // 逐页访问并把该页所有 PillToggle 的逻辑尺寸 / sizeHint / dpr / 物理尺寸 /
    // 勾选与可用状态写入 outDir/switch-probe.txt；再跑四项交互自检（开关点击、
    // 整行点击、空格、右键菜单父关子灰联动）与两张键盘焦点环截图；同时按当前
    // 主题落盘一般页与右键菜单页整窗截图。w/h > 0 时先把窗口摆到该尺寸。
    void probeSwitchAcceptance(const QString &outDir, int w = 0, int h = 0);
    // probeCtxToggle：切到上下文菜单页并点击 ctxmenu.<leaf> 开关（--smoke
    // settingsctx:<leaf>）。与真实鼠标点击同一条 PillToggle::toggled →
    // writeValues 链路，供资源管理器右键验收驱动设置页开关。
    void probeCtxToggle(const QString &leaf);
    // 应用内右键菜单可见性探针（--smoke appmenu:<id>[,<id>…] / appmenureset）：
    // 切到上下文菜单页后依次点击可见性开关（appMenuId）
    // 或「恢复默认菜单」按钮，供「父关子灰 / 恢复默认」状态截图与回归验证。
    void probeAppMenuToggle(const QString &ids);
    void probeAppMenuReset();
    // probeAppMenuPageGrab(out)：切到上下文菜单页后把整页内容渲染落盘
    // （grab 不受滚动视口裁剪，长图一次拿全）。
    void probeAppMenuPageGrab(const QString &out);
    // 大纲视图整窗截图（--smoke appmenushot:<out>:<W>x<H>）：切到本页后可指定
    // 窗口尺寸，抓整个设置窗（含侧栏、页头、页脚），用于设计对照与缩放验收。
    // 验收探针：逐页 selectPage 一次，记录每页「首次打开」端到端耗时。
    void probeVisitAllPages();
    // 外部深连（派单 2026-11-20）：主程序在组件未装时点击全景入口，
    // 定位到「设置 → 扩展」并让组件行进入视口、拿到键盘焦点。
    void focusExtensionsPage();
    // 验收探针：把文字写进搜索框（触发「首次搜索补齐全部页」路径）。
    void probeSearch(const QString &text);
    // 正文留白滚轮验收探针（--smoke settingswheel:<dir>，派单 2026-11-22）：
    // 宽窗 / 窄窗分别在卡片列、左留白、右留白注入真实滚轮刻度（按落点找控件
    // 再派发，与平台路径一致），记录滚动条位置、上下边界，以及大纲 / 常驻预览
    // / 下拉弹层的表现；截图与文本证据写到 <dir>。
    void probeWheelMargins(const QString &outDir);
    void probeAppMenuWindow(const QString &out, int w = 0, int h = 0,
                            const QString &scrollTo = QString(),
                            const QString &steps = QString());
    // 设置导出 / 导入验收探针（--smoke setio:<steps>，非交互路径）：
    // steps 用 '+' 分隔，支持 page（切到杂项页）/ export|<path> / import|<path> /
    // grab|<out>（整窗渲染落盘）。导出/导入走与按钮槽同一条核心链路，
    // 只是把原生文件对话框换成探针给的路径；每步结果追加到 Temp/setio-probe.txt。
    void probeSettingsIO(const QString &steps);
    // 批次23 语言页三项验收探针（--smoke langprobe:<root>）：逐档切换界面语言、
    // 缺 .qm 回退、打开语言文件夹三态、下载更多语言两态；不真拉起外部程序。
    void probeLanguageLive(const QString &root);
    // —— 常驻一级预览 + 浮动二级（实装派单 2026-11-16）——
    // 设置页右侧常驻查看器右键菜单一级预览；二级以独立顶层浮层展开。
    // 树结构与可见性由本页从 Config+MenuCatalog 实时计算（开关改动立即重建），
    // 运行态（勾选/互斥单选/禁用/有无图片）由 MainWindow 在打开设置后一次性
    // 推送快照，避免两处各自维护一份动作真源。
    struct AppMenuRunState {
        bool hasImage = false;
        QHash<QString, bool> checked;   // stable id -> checked（互斥单选 / 布尔勾选）
        QHash<QString, bool> enabled;   // stable id -> enabled（未登记 = 视命令本身启用）
        QString currentViewMode;        // actual / fit / fitwidth / spread_l2r / spread_r2l
        QString currentInterp;          // fast / smooth / lanczos
        QString currentEffect;          // none / soften / sharpen
        int sortMode = -1;              // 0..3，-1 表示不适用
        bool sortDescending = false;
        bool zoomLocked = false;
        int netRotation = 0;            // 0/90/180/270
        bool negative = false;
        bool flippedH = false;
        bool flippedV = false;
        bool alwaysOnTop = false;
    };
    void setAppMenuRunState(const AppMenuRunState &state);
    void refreshAppMenuPreview(bool relayout = true);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    QSize minimumSizeHint() const override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    // 无边框窗口的边缘缩放：在非客户区命中测试里把四边/四角交回系统处理，
    // 光标与拖动改大小都由 Windows 原生完成（Qt 只负责后续布局重排）。
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void reject() override;
    // 所有关闭保存路径（保存按钮、Enter、默认按钮）的唯一入口：
    // 先校验再写盘，校验失败不关闭窗口（P0-1）。
    void accept() override;

private slots:
    // 校验 + 写盘；键盘冲突 / 写盘失败时不写盘并返回 false。
    bool onAccepted();
    void onReset();
    // 杂项页「导出设置 / 导入设置」两个标准按钮的点击槽。
    void onExportSettings();
    void onImportSettings();

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
    // 延后构建：只建页壳 + 导航项，页面内容登记构建器，首次进入（或首次搜索）
    // 时才真正创建。2026-11-16 实测旧 12 页同步构建约占设置窗开窗 169ms 中的
    // 160ms；延后后首屏回到个位数毫秒，代价是首次进入该页要付一次构建耗时。
    QWidget *addLazyPage(const QString &title, const QString &desc,
                         void (SettingsDialog::*builder)(QWidget *));
    void ensurePageBuilt(int index);
    void ensureAllPagesBuilt();
    // 页内容建完后补尾部弹簧（键盘页与查看器右键菜单页例外，见实现）。
    void finishPageLayout(int index);
    void selectPage(int index);
    // 按当前窗口宽度算「通用」页内容列的左右边界：最大 660，超出时居中；
    // 其余页回到 0 边距（column 自带 28 内边距），与改前一致。
    void updateContentColumnWidth();
    // 一般页页头「标题—说明—内容」间距：标准 8+8，紧凑 6+6；其余页 13/15。
    void updateGeneralHeaderSpacing();
    // v2 紧凑档（窗口宽 < 1000）：左栏 180、导航行 34 / 行距 2、标题 40 /
    // 底栏 43、组间距 10、行高约 40。标准档恢复 211/36/4/41/44/15/49。
    void applyCompactLayout(bool compact);
    // 只把紧凑/标准档的组间距与行高落到单页（延后构建的页建完时补一次）。
    void applyCompactMetricsToPage(QWidget *page);

    // —— 十个页面 ——
    void buildGeneralPage(QWidget *page);
    void buildViewPage(QWidget *page);
    void buildAppearancePage(QWidget *page);
    void buildImageProcPage(QWidget *page);
    void buildKeyboardPage(QWidget *page);
    void buildMousePage(QWidget *page);
    void buildWindowsPage(QWidget *page);
    void buildExtensionsPage(QWidget *page);
    // 关于页（2026-11-22）：产品信息卡片 + 链接卡片，全部为只读内容，
    // 不参与待保存项与「恢复默认」。
    void buildAboutPage(QWidget *page);
    // 组件行实时状态（下载进度 / 已安装版本 / 失败原因）；连 PanoExt::Manager 的信号，
    // 页面没建时直接返回（懒构建）。
    void refreshExtensionsPage();
    // 组件行动作：未安装→安装，安装中→取消，已安装→卸载，失败→重试。
    void onExtensionsAction();
    // 查看器右键菜单页的应用内右键菜单可见性区（2026-11-14 全量配置修订）：
    // 打开与定位 / 常用动作 / 任务菜单 / 图像处理 / 文件操作 / 危险操作 / 应用
    // 七段 + 列表开头的「恢复默认菜单」。
    void buildAppMenuSections(QWidget *page);

    // —— 设置导出 / 导入（需求池 H1-3）——
    // 作用对象是「当前生效的那份 ini」：QUIKVIEW_CONFIG_PATH > 便携目录 config.ini
    // > 系统配置目录，与 Config::defaultConfigPath 同一来源，不写死 %APPDATA%。
    QString settingsIniPath() const;
    // 三重校验：QSettings 能解析 / 含 [config] 段 / 至少 5 个本项目已知键。
    bool isValidSettingsFile(const QString &path, QString *error) const;
    // 导出：当前 ini 全量字节副本，QSaveFile 原子写（目标已存在也能安全替换）。
    bool exportSettingsTo(const QString &path, QString *error);
    // 导入：先备份为 config.ini.bak-YYYYMMDDHHMM，再原子覆盖；失败原文件不动。
    bool applyImportedSettings(const QString &path, QString *error);
    // 按钮槽与探针共用的一条完整链路（推送待保存项 → 校验 → 备份覆盖 → 回填界面 → toast）。
    void exportSettingsPath(const QString &path);
    bool importSettingsPath(const QString &path);
    // 设置窗内的轻提示（复用 Chrome::Toast，不新造样式）。
    void showSettingsToast(const QString &text, bool ok);

    // —— 统一设置行工厂（全部走布局，不绝对定位）——
    // beginSection：分组标题 + 卡片壳；标题与行文案共享同一条左边线。
    // topGap < 0 时用全局默认 kGroupTop；一般页样板传 24 拉开分组层次。
    QWidget *beginSection(QWidget *page, const QString &title, int topGap = -1);
    // 带图标与状态摘要的分组头（查看器右键菜单页用）：图标 + 名称 + 已开启/总数。
    // 折叠交互已停用（用户实测反馈点标题会误收），卡片始终展开；iconName 为空则不显示图标。
    // 返回的分组壳与 beginSection 同构，groupCard() 仍能找到卡片。
    QWidget *beginCollapsibleSection(QWidget *page, const QString &title,
                                     const QString &iconName = QString(), int topGap = -1);
    // 组卡片本体（beginSection 内部创建，行 / 分隔线都追加到这里）。
    QWidget *groupCard(QWidget *section) const;
    // 统一页节奏：首组 12 / 组间 20、行高 42、右侧控件列整页同宽并右对齐、
    // 分组标题提级 13px/600。九页在各自 builder 末尾调用一次。
    // 负数参数 = 沿用 kPage* 默认值；「通用」样板传定稿的 22/14/44/160，
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
    // 只回填指定区间的绑定（延后构建的页建完时补一次，不覆盖其它页的用户草稿）。
    void loadBindingsFromConfig(int chkFrom, int comboFrom, int keyFrom, int mouseFrom);
    // 回填页专属控件（缓存大小/书签上限/文件夹/编辑器/语言/主题/磨砂）。
    // page 非空时只回填该页拥有的控件。
    void loadPageWidgets(QWidget *page);

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
    void connectInstantSignals(int chkFrom = 0, int comboFrom = 0, int keyFrom = 0,
                               int mouseFrom = 0);
    // 页专属控件（缓存大小/书签上限/文件夹/编辑器/磨砂/语言）的连接；
    // 只连该页拥有的，且在该页首次构建时调用一次。
    void connectPageWidgets(QWidget *page);
    bool applyInstantValue(const QString &key, const QVariant &value);
    void writeBridgeKeys();
    bool m_loading = false;

    // —— 待保存项（热键改绑 / 界面语言）——
    QStringList pendingChangeNames() const;
    // keyFrom>0 时只补登记新增热键行的基线，不动已有草稿的判定。
    void capturePendingBaseline(int keyFrom = 0);
    void updatePendingNote();
    QLabel *m_footerNote = nullptr;
    QHash<QString, QString> m_keyBaseline;   // action → PortableText（进入设置时）
    QString m_langBaseline;                  // 进入设置时的语言代码

    // —— 搜索 ——
    QLineEdit *m_search = nullptr;
    QLabel *m_searchEmpty = nullptr;
    QStringList m_pageSearchIndex;   // 每页可匹配文字（小写）
    void buildSearchIndex();
    void rebuildPageSearchIndex(int index);
    void filterNavigation(const QString &text);
    void selectFirstVisiblePage();

    // —— SET-02 即时预览会话（样板：view/exif_small_font）——
    // 控件变更下发 MainWindow 运行态并立即写盘；失败时控件与运行态回退，
    // 不留下半套状态。取消关闭默认不回滚（已即时应用的设置保留）；
    // 例外是下方批次03 的浏览页四项——按派单要求取消 / Esc / ✕ 三链路回滚。
    struct LiveCheck {
        QCheckBox *cb;
        const char *key;
        bool inverted;
        bool lastValue;         // 最近一次成功下发的值（幂等 / 失败回退）
    };
    void connectLiveChecks(int from = 0);
    void applyLiveCheck(int index, bool checked);
    QList<LiveCheck> m_liveChecks;

    // —— 批次05 图像处理页五项：下拉类的即时预览（与 LiveCheck 同构）——
    // 下拉不能走 LiveCheck（那个结构是一键一布尔），这里一一对应：
    // 变更 → applyLiveSetting（运行态）→ 成功才写盘；失败把选中项退回上次成功值。
    struct LiveCombo {
        QComboBox *combo;
        QString key;        // QString：注册点传字面量，生命周期与控件一致
        QString name;       // 设置项显示名（状态行文案用）
        QString lastData;   // 最近一次成功下发的 data（幂等 / 失败回退）
    };
    void connectLiveCombos(int from = 0);
    void applyLiveCombo(int index, const QString &data);
    QList<LiveCombo> m_liveCombos;

    // —— 批次05：进入设置时的档位快照（取消 / Esc / ✕ 回滚，保存才保留）——
    // 配置与运行态同时回到进入设置前：解码首帧策略 / 透明底色 / 插值档 / 翻页档。
    void captureImgProcBaseline();
    void rollbackImgProcBaseline();
    QHash<QString, QVariant> m_imgProcBaseline;   // key → 进入设置时的档位（字符串/布尔）
    // 页内联「已即时生效」状态行：图像处理页 / 浏览页各一行（外观页沿用
    // m_browseLiveNote）。三项独立，切页时各自说话，不互相覆盖。
    // 具体标签复用下方 m_viewLiveNote / m_imageProcLiveNote（批次21 建立），
    // 批次05 改为统一由 addLiveNote(page) 贴到页首。
    QLabel *addLiveNote(QWidget *page);
    QLabel *liveNoteFor(const QString &key) const;
    void updateLiveNote(const QString &key, const QVariant &value);

    // —— 批次03 浏览页四项：进入设置时的快照（取消 / Esc / ✕ 回滚）——
    // 与上面「取消保留」的通用语义不同：这四项按派单 §2.4 要求三链路回滚，
    // 即 reject 路径把配置值与运行态同时还原到进入设置前；保存（accept）才保留。
    void captureBrowseLiveBaseline();
    void rollbackBrowseLiveBaseline();
    void updateBrowseLiveNote(const QString &key, bool value);
    QHash<QString, bool> m_browseLiveBaseline;   // key → 进入设置时的值
    QLabel *m_browseLiveNote = nullptr;          // 浏览页内联「已即时生效」状态行
    QLabel *m_viewLiveNote = nullptr;            // 浏览页「翻页」组内的状态行（批次21 #81）
    QLabel *m_imageProcLiveNote = nullptr;       // 图像处理页同款状态行（批次21 #82/#83）

    // —— 批次04 图像处理页四项：快照回滚 + 页内状态行（#20/#21/#22 及浏览页 #23）——
    // 与批次03 同一口径：取消 / Esc / ✕ 把配置值与运行态一起还原到进入设置前，
    // 保存才保留。数值框（#22）走单独下发入口，不进 LiveCheck（那是「一键一布尔」）。
    void captureImageProcLiveBaseline();
    void rollbackImageProcLiveBaseline();
    void updateImageProcLiveNote(const QString &key, const QString &valueText);
    void applyCacheSizeLive(int mb);
    QHash<QString, QVariant> m_imageProcLiveBaseline;   // key → 进入设置时的值
    // 状态行复用批次21 的 m_imageProcLiveNote（同一页只放一条）
    int m_cacheSizeApplied = 300;                       // 最近一次成功下发的缓存上限（MB）

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
    // 键位可用性（批次13）：保存值优先、否则出厂键位；两者都为空 = 该动作实际
    // 拿不到任何键（MainWindow::applyShortcutConfig 会直接跳过），此时「启用」
    // 无意义，必须灰显并说明原因，不留「勾着但按键无效」的可操作假开关。
    bool keyRowHasEffectiveKey(int index) const;
    void refreshKeyRowAvail(int index);

    QList<QWidget *> m_pages;              // 各页内容 widget（QScrollArea 的 widget）
    QList<QAbstractButton *> m_tabs;       // 左侧 navItem
    QStringList m_pageTitles;
    QStringList m_pageDescs;
    // 延后构建状态：与 m_pages 同下标。只建了壳、未建内容的页为 false。
    QList<std::function<void()>> m_pageBuilders;
    QList<bool> m_pageBuilt;
    bool m_allPagesBuilt = false;
    QStackedWidget *m_stack = nullptr;
    QVBoxLayout *m_navLayout = nullptr;
    // 「通用」样板外壳：只有 index 0 把内容列限到 748 并居中、隐藏页头分隔线、
    // 页标题与卡片左缘对齐；切到其他页时逐项还原，保证其余十页零变化。
    QWidget *m_contentColumn = nullptr;
    QVBoxLayout *m_contentLay = nullptr;
    // —— 正文外侧留白滚轮（派单 2026-11-22）——
    // 内容列居中限宽（kGenContentMaxW），页内滚动区只覆盖列宽；窗口拉宽后
    // 两侧留白不属于任何滚动区，滚轮事件会冒泡到正文外壳。这里按原滚动位置
    // 与上下边界转交给当前页自己的滚动区，卡片宽度不参与滚轮范围。
    QWidget *m_contentPane = nullptr;
    bool m_wheelForwarding = false;     // 转发重入保护：到边界的事件会再冒泡回来
    QScrollArea *wheelTarget(const QPoint &contentPos) const;
    QFrame *m_headLine = nullptr;
    QSpacerItem *m_headGapTop = nullptr;     // 页头说明与分隔线之间（一般页收窄）
    QSpacerItem *m_headGapBottom = nullptr;  // 分隔线与内容之间
    QSpacerItem *m_footerGap = nullptr;      // 底栏提示与卡片左缘对齐的弹性间隔
    // v2 紧凑档需要按窗口宽度改这些外壳尺寸
    QWidget *m_navRail = nullptr;
    QWidget *m_titleBar = nullptr;
    QWidget *m_footer = nullptr;
    bool m_compact = false;
    // 输入来源追踪：只有键盘导航才让控件显示焦点环（v2 焦点规则）。
    friend class InputModeWatcher;
    InputModeWatcher *m_inputMode = nullptr;
    QLabel *m_pageTitle = nullptr;
    QLabel *m_pageDesc = nullptr;
    // 页头右侧动作位：目前只有「查看器右键菜单」页用（恢复默认菜单），
    // 其它页为空且隐藏，不改变既有页头排版。
    QWidget *m_pageActionHost = nullptr;
    QWidget *m_appMenuPage = nullptr;        // 大纲视图所在页（selectPage 用来切动作位）
    int m_appMenuPageIndex = -1;             // 该页在 m_pages 的下标（列宽按页放宽用）
    // Windows 集成页「支持的格式」折叠体与展开按钮（默认收起）。
    QWidget *m_formatsFoldBody = nullptr;
    QPushButton *m_formatsToggleBtn = nullptr;
    // —— 扩展页 · 360° 全景查看器组件行（派单 2026-11-20）——
    QWidget *m_extRow = nullptr;
    QLabel *m_extStatus = nullptr;        // 未安装 / 安装中 42% / 已安装 0.5.6 / 安装失败
    QProgressBar *m_extProgress = nullptr; // 仅安装中显示
    QPushButton *m_extAction = nullptr;    // 安装 / 取消 / 卸载 / 重试
    QLabel *m_extWhereHint = nullptr;      // 安装位置与清理范围的说明
    QString m_extLastStatusText;           // 供外部深连高亮对照
    int m_extPageIndex = -1;               // 扩展页下标（2026-11-22 起「关于」排在它后面）
    int m_aboutPageIndex = -1;             // 关于页下标（验收探针定位用）
    std::function<void(int)> m_layoutAppMenuOutline;
    std::function<std::function<void()>()> m_saveAppMenuScroll;
    std::function<void()> m_refreshAppMenuStyle;
    QWidget *m_appMenuOutline = nullptr;     // 大纲滚动区容器（搜过定位滚动用）

    // —— 查看器右键菜单 · 常驻一级预览（实装派单 2026-11-16）——
    // 页面右侧一级预览列 widget（常驻，不随页头按钮开关）。
    QWidget *m_appMenuPreview = nullptr;         // 预览表面（右侧常驻列）
    AppMenuRunState m_appMenuRunState;           // MainWindow 推送的运行态快照
    std::function<void()> m_refreshAppMenuPreviewFn; // 内部刷新（含二级浮层对位）
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
    QComboBox *m_gifBgCombo = nullptr;    // 图像处理：GIF 透明背景档（批次 2）
    QPushButton *m_gifColorBtn = nullptr; // 图像处理：GIF 自定义铺底色样按钮
    void syncGifColorRow();               // 色样取 Config 现值；仅「自定义」档可点
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
        // 命令 ID 化（派单 2026-11-18 H1-7）：行绑定的命令（CMD_*）。键位与启用
        // 状态读写 config/keyboard/<cmd>_keys / _enabled；action 只作旧槽位名
        // （冲突检测基准与降级镜像键），不再充当配置真源。
        QString cmd;
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
    // 批次18：图片文件夹 1 行（#72 输入框 / #73 浏览… / #74 打开）共用一条即时链路。
    // 输入用 300ms 防抖写盘 + 下发运行态；行下方状态行常驻「是否可用」，
    // 让设置页开着时就能看出来，而不是关页后才生效。
    QLabel *m_photoFolder1Status = nullptr;
    QTimer *m_folderDebounce = nullptr;          // 输入防抖（停止输入 0.3s 落盘）
    QString m_folderPendingText;                 // 防抖期间待应用的文本
    QString m_probeModalShot;                    // 探针：模态提示截图落点（空=不截）
    void applyPhotoFolderLive(const QString &path);
    void updatePhotoFolderStatus(const QString &path, bool applied);
    // —— 批次23 #87/#88/#89：语言页三项即时化 ——
    void applyLanguageSelection();                    // 选中即装载 + 重译 + 写盘
    void refreshLanguageStatus();                     // 行内「当前档位 / 生效语言」
    void setLanguageStatus(const QString &text);
    void openLanguageFolderNow(QPushButton *btn);     // #89 建目录 + 交给系统
    void openDownloadPageNow(QPushButton *btn);       // #88 发布页 + 结果态
    void openPhotoFolderFromRow();
    QLineEdit *m_photoFolder2 = nullptr;
    QLineEdit *m_editorPath = nullptr;    // 杂项页
    QPushButton *m_btnExportSettings = nullptr;   // 杂项页：导出设置
    QPushButton *m_btnImportSettings = nullptr;   // 杂项页：导入设置
    Chrome::Toast *m_toast = nullptr;             // 设置窗内轻提示（导出/导入结果）
    QComboBox *m_language = nullptr;      // 语言页
    // 批次23 #87/#88/#89：语言页三行的行内状态（页面内即时反馈，不靠弹窗/外部程序）
    QLabel *m_langStatus = nullptr;
    QLabel *m_langFolderStatus = nullptr;
    QLabel *m_langDownloadStatus = nullptr;
    QPushButton *m_checkUpdateBtn = nullptr;  // 一般页：手动检查更新
    // 三选一确认框执行中：主窗口 closeEvent 与 Qt 自动关闭子窗口可能
    // 在同一次退出里各调一次 reject()，此标志保证只弹一次。
    bool m_prompting = false;
};
