#include "ThumbnailBrowser.h"

#include <QFileInfo>
#include <QListWidget>
#include <QVBoxLayout>
#include <QScrollBar>

#include "ImageCache.h"

ThumbnailBrowser::ThumbnailBrowser(ImageCache *cache, QWidget *parent)
    : QWidget(parent)
    , m_cache(cache)
{
    setObjectName(QStringLiteral("thumbnailBrowser"));
    // 普通子部件面板（原版 Honeyview 的缩略图栏无标题栏）：深色底 + 1px 边框
    setStyleSheet(QStringLiteral(
        "ThumbnailBrowser { background: #1f1f1f; border: 1px solid #3c3c3c; }"
        "QListWidget { background: #1f1f1f; border: none; color: #d8d8d8; }"));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(m_thumbSize, m_thumbSize));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setSpacing(4);
    m_list->setUniformItemSizes(true);
    lay->addWidget(m_list);

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < m_files.size()) {
            emit activateImage(idx);
        }
    });

    connect(m_cache, &ImageCache::thumbnailReady, this, &ThumbnailBrowser::onThumbReady);
}

void ThumbnailBrowser::setThumbSize(int px)
{
    m_thumbSize = px;
    m_list->setIconSize(QSize(m_thumbSize, m_thumbSize));
}

void ThumbnailBrowser::setFiles(const QStringList &files, int currentIndex)
{
    m_files = files;
    m_list->clear();
    for (int i = 0; i < files.size(); ++i) {
        auto *item = new QListWidgetItem(QFileInfo(files.at(i)).fileName(), m_list);
        item->setData(Qt::UserRole, i);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        m_cache->requestThumbnail(files.at(i), QSize(m_thumbSize, m_thumbSize));
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
    if (idx < 0 || idx >= m_list->count()) {
        return;
    }
    m_list->item(idx)->setIcon(QIcon(QPixmap::fromImage(image)));
}
