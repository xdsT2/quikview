#include "PhotoFolderPanel.h"

#include "Config.h"
#include "UiStyle.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

QString panelStyleSheet()
{
    return UiShell::DialogShell::shellQss() + UiStyle::resolveQss(QStringLiteral(R"(
QLabel#pfHint { color: @GenDesc@; font-size: 12px; }
QLabel#pfCount { color: @GenDesc@; font-size: 12px; }
QScrollArea#pfScroll { background: transparent; border: 0; }
QWidget#pfListHost { background: transparent; }
QFrame#pfRowCard { background: @GenCardBg@; border: 1px solid @GenCardLine@; border-radius: 6px; }
QLabel#pfIndex { background: @GenCtrlBg@; color: @GenText@; border: 1px solid @GenCtrlLine@;
    border-radius: 4px; font-size: 11px; font-weight: 600; }
QPushButton#pfToolBtn { background: @GenCtrlBg@; color: @GenText@; border: 1px solid @GenCtrlLine@;
    border-radius: 5px; padding: 0 8px; min-height: 26px; font-size: 12px; }
QPushButton#pfToolBtn:hover { background: @GenHover@; }
QPushButton#pfToolBtn:pressed { background: @GenPressed@; }
QPushButton#pfToolBtn:disabled { color: @GenDisabled@; }
QPushButton#pfSmallBtn { background: @GenCtrlBg@; color: @GenText@; border: 1px solid @GenCtrlLine@;
    border-radius: 5px; padding: 0px; min-width: 26px; min-height: 26px; font-size: 12px; }
QPushButton#pfSmallBtn:hover { background: @GenHover@; }
QPushButton#pfSmallBtn:pressed { background: @GenPressed@; }
QPushButton#pfSmallBtn:disabled { color: @GenDisabled@; }
)"));
}

} // namespace

PhotoFolderPanel::PhotoFolderPanel(QWidget *parent)
    : UiShell::DialogShell(parent)
{
    setWindowTitle(tr("照片文件夹"));
    setStyleSheet(panelStyleSheet());
    setMinimumSize(680, 380);
    resize(740, 480);

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
    auto *title = new QLabel(tr("照片文件夹"), titleBar);
    title->setObjectName(QStringLiteral("dlgTitle"));
    tb->addWidget(title);

    m_countLabel = new QLabel(titleBar);
    m_countLabel->setObjectName(QStringLiteral("pfCount"));
    tb->addWidget(m_countLabel);
    tb->addStretch(1);

    m_addBtn = new QPushButton(tr("+ 添加文件夹"), titleBar);
    m_addBtn->setObjectName(QStringLiteral("btnPrimary"));
    m_addBtn->setFixedHeight(28);
    m_addBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addBtn, &QPushButton::clicked, this, &PhotoFolderPanel::addFolderRow);
    tb->addWidget(m_addBtn);

    auto *closeBtn = new QPushButton(QStringLiteral("\u2715"), titleBar);
    closeBtn->setObjectName(QStringLiteral("closeBtn"));
    closeBtn->setFixedSize(32, 30);
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::hide);
    tb->addWidget(closeBtn);
    root->addWidget(titleBar);

    // —— 列表主体 ——
    auto *body = new QWidget(this);
    auto *bl = new QVBoxLayout(body);
    bl->setContentsMargins(18, 12, 18, 16);
    bl->setSpacing(10);

    auto *hint = new QLabel(
        tr("可配置 1–20 个目标文件夹；Insert 复制到文件夹 1，Ctrl+Insert 移动到文件夹 1，Alt+P 开合本面板。"),
        body);
    hint->setObjectName(QStringLiteral("pfHint"));
    hint->setWordWrap(true);
    bl->addWidget(hint);

    m_scroll = new QScrollArea(body);
    m_scroll->setObjectName(QStringLiteral("pfScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);

    m_listHost = new QWidget(m_scroll);
    m_listHost->setObjectName(QStringLiteral("pfListHost"));
    m_listLayout = new QVBoxLayout(m_listHost);
    m_listLayout->setContentsMargins(0, 0, 4, 0);
    m_listLayout->setSpacing(8);
    m_scroll->setWidget(m_listHost);
    bl->addWidget(m_scroll, 1);

    root->addWidget(body, 1);

    reloadFromConfig();
}

void PhotoFolderPanel::refreshThemeStyles()
{
    setStyleSheet(panelStyleSheet());
    update();
}

void PhotoFolderPanel::reloadFromConfig()
{
    m_folders = Config::instance().photoFolders();
    rebuildRows();
}

void PhotoFolderPanel::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_P && (event->modifiers() & Qt::AltModifier)) {
        hide();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        hide();
        event->accept();
        return;
    }
    UiShell::DialogShell::keyPressEvent(event);
}

void PhotoFolderPanel::saveFolders()
{
    Config::instance().setPhotoFolders(m_folders);
    emit foldersChanged();
}

void PhotoFolderPanel::addFolderRow()
{
    if (m_folders.size() >= Config::kMaxPhotoFolders) {
        return;
    }
    m_folders.append(QString());
    saveFolders();
    rebuildRows();
}

void PhotoFolderPanel::removeFolderRow(int row)
{
    if (m_folders.size() <= Config::kMinPhotoFolders || row < 0 || row >= m_folders.size()) {
        return;
    }
    m_folders.removeAt(row);
    saveFolders();
    rebuildRows();
}

void PhotoFolderPanel::moveFolderRow(int row, int delta)
{
    const int target = row + delta;
    if (row < 0 || row >= m_folders.size() || target < 0 || target >= m_folders.size()) {
        return;
    }
    m_folders.swapItemsAt(row, target);
    saveFolders();
    rebuildRows();
}

void PhotoFolderPanel::rebuildRows()
{
    while (QLayoutItem *item = m_listLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }

    const int n = m_folders.size();
    m_countLabel->setText(tr("(%1 / %2)").arg(n).arg(Config::kMaxPhotoFolders));
    m_addBtn->setEnabled(n < Config::kMaxPhotoFolders);

    for (int i = 0; i < n; ++i) {
        const int idx1 = i + 1;
        auto *card = new QFrame(m_listHost);
        card->setObjectName(QStringLiteral("pfRowCard"));
        auto *rl = new QHBoxLayout(card);
        rl->setContentsMargins(10, 8, 10, 8);
        rl->setSpacing(6);

        auto *badge = new QLabel(QString::number(idx1), card);
        badge->setObjectName(QStringLiteral("pfIndex"));
        badge->setFixedSize(26, 26);
        badge->setAlignment(Qt::AlignCenter);
        rl->addWidget(badge);

        auto *edit = new QLineEdit(m_folders.at(i), card);
        edit->setPlaceholderText(tr("选择或输入文件夹 %1 路径…").arg(idx1));
        connect(edit, &QLineEdit::editingFinished, this, [this, i, edit]() {
            if (i >= 0 && i < m_folders.size() && m_folders.at(i) != edit->text().trimmed()) {
                m_folders[i] = edit->text().trimmed();
                saveFolders();
            }
        });
        rl->addWidget(edit, 1);

        auto *browseBtn = new QPushButton(QStringLiteral("…"), card);
        browseBtn->setObjectName(QStringLiteral("pfSmallBtn"));
        browseBtn->setFixedSize(28, 26);
        browseBtn->setToolTip(tr("浏览文件夹"));
        browseBtn->setCursor(Qt::PointingHandCursor);
        connect(browseBtn, &QPushButton::clicked, this, [this, i, edit]() {
            const QString start = edit->text().trimmed();
            const QString dir = QFileDialog::getExistingDirectory(
                this, tr("选择图片文件夹 %1").arg(i + 1), start);
            if (!dir.isEmpty() && i >= 0 && i < m_folders.size()) {
                m_folders[i] = dir;
                edit->setText(dir);
                saveFolders();
            }
        });
        rl->addWidget(browseBtn);

        auto *upBtn = new QPushButton(QStringLiteral("↑"), card);
        upBtn->setObjectName(QStringLiteral("pfSmallBtn"));
        upBtn->setFixedSize(26, 26);
        upBtn->setEnabled(i > 0);
        upBtn->setToolTip(tr("上移"));
        upBtn->setCursor(Qt::PointingHandCursor);
        connect(upBtn, &QPushButton::clicked, this, [this, i]() { moveFolderRow(i, -1); });
        rl->addWidget(upBtn);

        auto *downBtn = new QPushButton(QStringLiteral("↓"), card);
        downBtn->setObjectName(QStringLiteral("pfSmallBtn"));
        downBtn->setFixedSize(26, 26);
        downBtn->setEnabled(i + 1 < n);
        downBtn->setToolTip(tr("下移"));
        downBtn->setCursor(Qt::PointingHandCursor);
        connect(downBtn, &QPushButton::clicked, this, [this, i]() { moveFolderRow(i, 1); });
        rl->addWidget(downBtn);

        auto *delBtn = new QPushButton(QStringLiteral("×"), card);
        delBtn->setObjectName(QStringLiteral("pfSmallBtn"));
        delBtn->setFixedSize(26, 26);
        delBtn->setEnabled(n > Config::kMinPhotoFolders);
        delBtn->setToolTip(tr("删除此行"));
        delBtn->setCursor(Qt::PointingHandCursor);
        connect(delBtn, &QPushButton::clicked, this, [this, i]() { removeFolderRow(i); });
        rl->addWidget(delBtn);

        auto *copyBtn = new QPushButton(tr("复制"), card);
        copyBtn->setObjectName(QStringLiteral("pfToolBtn"));
        copyBtn->setCursor(Qt::PointingHandCursor);
        connect(copyBtn, &QPushButton::clicked, this, [this, idx1]() {
            emit copyRequested(idx1);
        });
        rl->addWidget(copyBtn);

        auto *moveBtn = new QPushButton(tr("移动"), card);
        moveBtn->setObjectName(QStringLiteral("pfToolBtn"));
        moveBtn->setCursor(Qt::PointingHandCursor);
        connect(moveBtn, &QPushButton::clicked, this, [this, idx1]() {
            emit moveRequested(idx1);
        });
        rl->addWidget(moveBtn);

        auto *openBtn = new QPushButton(tr("打开"), card);
        openBtn->setObjectName(QStringLiteral("pfToolBtn"));
        openBtn->setCursor(Qt::PointingHandCursor);
        connect(openBtn, &QPushButton::clicked, this, [this, idx1]() {
            emit openRequested(idx1);
        });
        rl->addWidget(openBtn);

        m_listLayout->addWidget(card);
    }
    m_listLayout->addStretch(1);
}
