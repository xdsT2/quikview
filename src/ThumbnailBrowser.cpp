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
    // 普通 QWidget 子类默认不画样式表背景（会露出一整块生硬的黑底、也没有
    // 边框圆角），必须显式开 WA_StyledBackground 才会按下面的规则绘制。
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "#thumbnailBrowser { background: #22242a; border: 1px solid rgba(255,255,255,10%);"
        " border-radius: 8px; }"
        "QListWidget { background: transparent; border: none; color: #c9ccd1; outline: none; }"
        "QListWidget::item { padding: 4px 2px 6px 2px; border-radius: 6px; }"
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
    lay->setContentsMargins(6, 6, 6, 6);
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
