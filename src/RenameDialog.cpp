#include "RenameDialog.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "UiStyle.h"

RenameDialog::RenameDialog(const QString &currentFile,
                           const QStringList &dirFiles,
                           QWidget *parent)
    : UiShell::DialogShell(parent)
    , m_currentFile(currentFile)
    , m_dirFiles(dirFiles)
{
    setWindowTitle(tr("重命名"));
    setStyleSheet(UiShell::DialogShell::shellQss() + QStringLiteral(R"(
QLabel#dlgSubtitle { color: @GenDesc@; font-size: 12px; }
QLabel#renameSummary { color: @GenDesc@; font-size: 12px; }
QLabel#renameError { color: @GenError@; background: @GenDangerBg@;
    border-radius: 5px; padding: 6px 10px; font-size: 12px; }
QPushButton#tokenBtn { background: @GenCtrlBg@; border: 1px solid @GenCtrlLine@;
    border-radius: 5px; padding: 2px 8px; min-height: 24px; font-size: 11px; color: @GenText@; }
QPushButton#tokenBtn:hover { background: @GenHover@; }
QPushButton#tokenBtn:pressed { background: @GenPressed@; }
QTableWidget#renameTable { background: @GenCardBg@; border: 1px solid @GenCardLine@;
    border-radius: 6px; gridline-color: @GenRowLine@; }
)"));
    setMinimumSize(640, 480);
    resize(680, 520);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // —— 标题栏 43px ——
    auto *titleBar = new QWidget(this);
    titleBar->setObjectName(QStringLiteral("dlgTitleBar"));
    titleBar->setFixedHeight(UiStyle::TitleBarH);
    attachTitleBar(titleBar);
    auto *tb = new QHBoxLayout(titleBar);
    tb->setContentsMargins(18, 0, 14, 0);
    tb->setSpacing(9);
    auto *title = new QLabel(tr("重命名"), titleBar);
    title->setObjectName(QStringLiteral("dlgTitle"));
    tb->addWidget(title);
    tb->addStretch(1);
    auto *closeBtn = new QPushButton(QStringLiteral("\u2715"), titleBar);
    closeBtn->setObjectName(QStringLiteral("closeBtn"));
    closeBtn->setFixedSize(32, 30);
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    tb->addWidget(closeBtn);
    root->addWidget(titleBar);

    // —— 内容区 ——
    auto *body = new QWidget(this);
    auto *bl = new QVBoxLayout(body);
    bl->setContentsMargins(20, 14, 20, 14);
    bl->setSpacing(10);

    // 作用范围切换
    auto *scopeRow = new QHBoxLayout;
    scopeRow->setSpacing(16);
    m_singleRadio = new QRadioButton(tr("仅当前文件"), body);
    m_batchRadio = new QRadioButton(
        tr("当前目录全部图片 (%1 张)").arg(m_dirFiles.size()), body);
    auto *scopeGroup = new QButtonGroup(this);
    scopeGroup->addButton(m_singleRadio);
    scopeGroup->addButton(m_batchRadio);
    m_singleRadio->setChecked(true);
    m_batchRadio->setEnabled(m_dirFiles.size() > 1);
    scopeRow->addWidget(m_singleRadio);
    scopeRow->addWidget(m_batchRadio);
    scopeRow->addStretch(1);
    bl->addLayout(scopeRow);

    // 模板输入行
    auto *patRow = new QHBoxLayout;
    patRow->setSpacing(8);
    auto *patLbl = new QLabel(tr("命名模板："), body);
    m_patternEdit = new QLineEdit(QStringLiteral("{name}"), body);
    m_patternEdit->setPlaceholderText(tr("例如：{name}_{index:03} 或 {date}_{index:03}"));
    patRow->addWidget(patLbl);
    patRow->addWidget(m_patternEdit, 1);

    auto *idxLbl = new QLabel(tr("起始序号："), body);
    m_startIndex = new QSpinBox(body);
    m_startIndex->setRange(0, 99999);
    m_startIndex->setValue(1);
    m_startIndex->setFixedWidth(78);
    patRow->addWidget(idxLbl);
    patRow->addWidget(m_startIndex);

    m_keepExt = new QCheckBox(tr("保留原扩展名"), body);
    m_keepExt->setChecked(true);
    patRow->addWidget(m_keepExt);
    bl->addLayout(patRow);

    // 变量快捷插入行
    auto *tokRow = new QHBoxLayout;
    tokRow->setSpacing(6);
    auto *tokHint = new QLabel(tr("插入变量："), body);
    tokHint->setObjectName(QStringLiteral("dlgSubtitle"));
    tokRow->addWidget(tokHint);
    const struct {
        const char *token;
        const char *tip;
    } tokens[] = {
        { "{name}",      "原文件名（不含扩展名）" },
        { "{index:03}",  "三位补零序号（001, 002…）" },
        { "{date}",      "修改日期（YYYYMMDD）" },
        { "{time}",      "修改时间（HHMMSS）" },
        { "{ext}",       "原扩展名（不含点号）" },
    };
    for (const auto &t : tokens) {
        const QString tok = QLatin1String(t.token);
        auto *btn = new QPushButton(tok, body);
        btn->setObjectName(QStringLiteral("tokenBtn"));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(tr(t.tip));
        connect(btn, &QPushButton::clicked, this, [this, tok]() { insertToken(tok); });
        tokRow->addWidget(btn);
    }
    tokRow->addStretch(1);
    bl->addLayout(tokRow);

    // 实时预览表
    m_table = new QTableWidget(body);
    m_table->setObjectName(QStringLiteral("renameTable"));
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({ tr("原文件名"), tr("新文件名"), tr("状态") });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(28);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    bl->addWidget(m_table, 1);

    m_errorLabel = new QLabel(body);
    m_errorLabel->setObjectName(QStringLiteral("renameError"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    bl->addWidget(m_errorLabel);

    root->addWidget(body, 1);

    // —— 底栏 62px ——
    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("dlgFooter"));
    footer->setFixedHeight(UiStyle::FooterH);
    auto *fl = new QHBoxLayout(footer);
    fl->setContentsMargins(18, 0, 18, 0);
    fl->setSpacing(9);
    m_summaryLabel = new QLabel(footer);
    m_summaryLabel->setObjectName(QStringLiteral("renameSummary"));
    fl->addWidget(m_summaryLabel, 1);

    auto *cancelBtn = new QPushButton(tr("取消"), footer);
    cancelBtn->setObjectName(QStringLiteral("genNeutral"));
    cancelBtn->setFixedHeight(32);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    fl->addWidget(cancelBtn);

    m_execBtn = new QPushButton(tr("执行重命名"), footer);
    m_execBtn->setObjectName(QStringLiteral("btnPrimary"));
    m_execBtn->setFixedHeight(32);
    m_execBtn->setDefault(true);
    m_execBtn->setCursor(Qt::PointingHandCursor);
    connect(m_execBtn, &QPushButton::clicked, this, &RenameDialog::onExecute);
    fl->addWidget(m_execBtn);
    root->addWidget(footer);

    connect(m_singleRadio, &QRadioButton::toggled, this, &RenameDialog::onModeChanged);
    connect(m_batchRadio, &QRadioButton::toggled, this, &RenameDialog::onModeChanged);
    connect(m_patternEdit, &QLineEdit::textEdited, this, [this]() {
        m_userEditedPattern = true;
    });
    connect(m_patternEdit, &QLineEdit::textChanged, this, &RenameDialog::onPatternChanged);
    connect(m_startIndex, qOverload<int>(&QSpinBox::valueChanged),
            this, &RenameDialog::onPatternChanged);
    connect(m_keepExt, &QCheckBox::toggled, this, &RenameDialog::onPatternChanged);

    rebuildPreview();
    m_patternEdit->setFocus();
    m_patternEdit->selectAll();

    if (parent && parent->isVisible()) {
        const QRect pg = parent->frameGeometry();
        move(pg.center() - rect().center());
    }
}

void RenameDialog::probeSetPattern(const QString &pattern, bool batchMode)
{
    m_userEditedPattern = true;
    if (batchMode && m_batchRadio->isEnabled()) {
        m_batchRadio->setChecked(true);
    } else {
        m_singleRadio->setChecked(true);
    }
    m_patternEdit->setText(pattern);
    rebuildPreview();
}

void RenameDialog::onModeChanged()
{
    if (!m_userEditedPattern) {
        const QString defPat = m_batchRadio->isChecked()
                                   ? QStringLiteral("{name}_{index:03}")
                                   : QStringLiteral("{name}");
        m_patternEdit->blockSignals(true);
        m_patternEdit->setText(defPat);
        m_patternEdit->blockSignals(false);
    }
    rebuildPreview();
}

void RenameDialog::onPatternChanged()
{
    m_errorLabel->hide();
    rebuildPreview();
}

void RenameDialog::insertToken(const QString &token)
{
    m_userEditedPattern = true;
    m_patternEdit->insert(token);
    m_patternEdit->setFocus();
}

void RenameDialog::rebuildPreview()
{
    QStringList targets;
    if (m_batchRadio && m_batchRadio->isChecked() && !m_dirFiles.isEmpty()) {
        targets = m_dirFiles;
    } else if (!m_currentFile.isEmpty()) {
        targets = { m_currentFile };
    }

    BatchRename::Options opt;
    opt.pattern = m_patternEdit->text();
    opt.startIndex = m_startIndex->value();
    opt.keepExtension = m_keepExt->isChecked();
    m_plan = BatchRename::buildPlan(targets, opt);

    m_table->setRowCount(m_plan.items.size());
    const QColor conflictBg = UiStyle::color(UiStyle::fluent::dangerBg());
    const QColor conflictFg = UiStyle::color(UiStyle::fluent::error());
    const QColor mutedFg = UiStyle::color(UiStyle::fluent::desc());
    const QColor okFg = UiStyle::color(UiStyle::fluent::statusOk());

    for (int i = 0; i < m_plan.items.size(); ++i) {
        const BatchRename::Item &it = m_plan.items.at(i);
        auto *c0 = new QTableWidgetItem(it.oldName);
        auto *c1 = new QTableWidgetItem(it.newName);
        QString stText;
        QColor stColor = okFg;
        if (!it.error.isEmpty()) {
            stText = it.error;
            stColor = conflictFg;
        } else if (it.unchanged) {
            stText = tr("无变化");
            stColor = mutedFg;
        } else {
            stText = tr("就绪");
        }
        auto *c2 = new QTableWidgetItem(stText);
        c2->setForeground(stColor);

        if (!it.error.isEmpty()) {
            c0->setBackground(conflictBg);
            c1->setBackground(conflictBg);
            c2->setBackground(conflictBg);
            c1->setForeground(conflictFg);
        }
        m_table->setItem(i, 0, c0);
        m_table->setItem(i, 1, c1);
        m_table->setItem(i, 2, c2);
    }

    if (m_plan.hasConflicts()) {
        m_summaryLabel->setText(
            tr("发现 %1 处冲突，已阻止整批执行").arg(m_plan.conflictCount));
        m_summaryLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;")
                                          .arg(QLatin1String(UiStyle::fluent::error())));
        m_execBtn->setEnabled(false);
    } else if (m_plan.changeCount == 0) {
        m_summaryLabel->setText(tr("共 %1 项，当前无文件名变更").arg(m_plan.items.size()));
        m_summaryLabel->setStyleSheet(QString());
        m_execBtn->setEnabled(false);
    } else {
        m_summaryLabel->setText(
            tr("共 %1 项，将重命名 %2 项").arg(m_plan.items.size()).arg(m_plan.changeCount));
        m_summaryLabel->setStyleSheet(QString());
        m_execBtn->setEnabled(true);
    }
}

void RenameDialog::onExecute()
{
    if (m_plan.hasConflicts() || m_plan.changeCount == 0) {
        return;
    }
    m_result = BatchRename::executePlan(m_plan);
    if (!m_result.ok) {
        m_errorLabel->setText(m_result.error);
        m_errorLabel->show();
        return;
    }
    accept();
}
