#include "ThumbnailBrowser.h"

#include <QFileInfo>
#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>

#include "ImageCache.h"

namespace {
constexpr int kPanelW = 160;      // 面板总宽
constexpr int kPreview = 128;     // 每张图的方形预览区（等比内含，留白为深色底）
constexpr int kCardW = 140;       // 卡片宽（留出滚动条与左右间隙）
constexpr int kCardH = 156;       // 预览 128 + 文件名行
constexpr int kPad = 6;           // 面板内边距
} // namespace

ThumbnailBrowser::ThumbnailBrowser(ImageCache *cache, QWidget *parent)
    : QWidget(parent)
    , m_cache(cache)
{
    setObjectName(QStringLiteral("thumbnailBrowser"));
    // 普通 QWidget 子类默认不画样式表背景（会露出一整块生硬的黑底、也没有
    // 边框圆角），必须显式开 WA_StyledBackground 才会按下面的规则绘制。
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelW);
    setStyleSheet(QStringLiteral(
        "#thumbnailBrowser { background: #22242a; border: 1px solid rgba(255,255,255,10%);"
        " border-radius: 8px; }"
        "QListWidget { background: transparent; border: none; color: #c9ccd1; outline: none; }"
        "QListWidget::item { border-radius: 6px; }"
        "QListWidget::item:hover { background: rgba(255,255,255,6%); }"
        "QListWidget::item:selected { background: rgba(85,192,240,18%);"
        " color: #ffffff; border: 1px solid rgba(85,192,240,45%); }"
        "QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: rgba(255,255,255,22%);"
        " border-radius: 4px; min-height: 28px; }"
        "QScrollBar::handle:vertical:hover { background: rgba(255,255,255,34%); }"
        "QScrollBar::add-line, QScrollBar::sub-line,"
        " QScrollBar::add-page, QScrollBar::sub-page { background: transparent; height: 0; }"));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(kPad, kPad, kPad, kPad);
    lay->setSpacing(0);
    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(kPreview, kPreview));
    m_list->setGridSize(QSize(kCardW, kCardH));
    m_list->setFlow(QListView::TopToBottom);   // 单列竖排，不折行
    m_list->setWrapping(false);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setSpacing(0);
    m_list->setUniformItemSizes(true);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setFont(QFont(m_list->font().family(), 12));
    lay->addWidget(m_list);

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < m_files.size()) {
            emit activateImage(idx);
        }
    });

    connect(m_cache, &ImageCache::thumbnailReady, this, &ThumbnailBrowser::onThumbReady);
}

int ThumbnailBrowser::preferredHeight() const
{
    // 只按实际卡片数量取高：图片少的时候不铺一块空黑面板。
    if (m_files.isEmpty()) {
        return 2 * kPad + 8;
    }
    return 2 * kPad + m_files.size() * kCardH;
}

void ThumbnailBrowser::setFiles(const QStringList &files, int currentIndex)
{
    m_files = files;
    m_list->clear();
    for (int i = 0; i < files.size(); ++i) {
        auto *item = new QListWidgetItem(QFileInfo(files.at(i)).fileName(), m_list);
        item->setData(Qt::UserRole, i);
        item->setSizeHint(QSize(kCardW, kCardH));
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        item->setToolTip(files.at(i));   // 文件名省略时靠 Tooltip 看全名
        m_cache->requestThumbnail(files.at(i), QSize(kPreview, kPreview));
    }
    setCurrent(currentIndex);
}

void ThumbnailBrowser::setCurrent(int index)
{
    if (index >= 0 && index < m_list->count()) {
        m_list->setCurrentRow(index);
        m_list->scrollToItem(m_list->item(index));
    }
}

void ThumbnailBrowser::onThumbReady(const QString &path, const QImage &image)
{
    const int idx = m_files.indexOf(path);
    if (idx < 0 || idx >= m_list->count() || image.isNull()) {
        return;
    }
    // 一律铺在 128×128 的深色方形底上再居中绘制：缩略图本身的宽高比不动，
    // 横图上下留白、竖图左右留白，绝不拉伸也绝不裁切。
    QPixmap box(kPreview, kPreview);
    box.fill(QColor(QStringLiteral("#1b1d20")));
    {
        QPainter p(&box);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QImage fitted = image.scaled(kPreview, kPreview, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation);
        p.drawImage((kPreview - fitted.width()) / 2, (kPreview - fitted.height()) / 2, fitted);
    }
    m_list->item(idx)->setIcon(QIcon(box));
}
