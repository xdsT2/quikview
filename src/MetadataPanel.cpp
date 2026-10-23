#include "MetadataPanel.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QUrl>

#include "ExifParser.h"

MetadataPanel::MetadataPanel(QWidget *parent)
    : QDockWidget(tr("信息"), parent)
{
    setObjectName(QStringLiteral("metadataPanel"));

    auto *container = new QWidget(this);
    auto *vbox = new QVBoxLayout(container);
    vbox->setContentsMargins(4, 4, 4, 4);

    m_tree = new QTreeWidget(container);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(2);
    m_tree->setIndentation(12);
    vbox->addWidget(m_tree);

    m_mapBtn = new QPushButton(tr("在地图中查看"), container);
    m_mapBtn->setEnabled(false);
    m_mapBtn->setVisible(false);
    connect(m_mapBtn, &QPushButton::clicked, this, [this]() {
        if (!m_mapUrl.isEmpty()) {
            QDesktopServices::openUrl(QUrl(m_mapUrl));
        }
    });
    vbox->addWidget(m_mapBtn);

    m_histogram = new QLabel(container);
    m_histogram->setMinimumHeight(100);
    m_histogram->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    m_histogram->setVisible(false);
    vbox->addWidget(m_histogram);

    setWidget(container);
}

void MetadataPanel::clear()
{
    m_tree->clear();
    m_mapBtn->setEnabled(false);
    m_mapBtn->setVisible(false);
    m_histogram->setVisible(false);
    m_mapUrl.clear();
}

void MetadataPanel::showImage(const QString &path, const QImage &image)
{
    clear();

    const QFileInfo fi(path);
    addSection(tr("文件"));
    addItem(tr("文件名"), fi.fileName());
    addItem(tr("路径"), fi.absoluteFilePath());
    addItem(tr("大小"), tr("%1 KB").arg(fi.size() / 1024));
    addItem(tr("修改时间"), fi.lastModified().toString(Qt::ISODate));

    addSection(tr("图像"));
    addItem(tr("尺寸"), tr("%1 x %2").arg(image.width()).arg(image.height()));
    addItem(tr("位深"), tr("%1 bit").arg(image.depth()));
    addItem(tr("格式"), fi.suffix().toUpper());

    renderHistogram(image);
    loadExif(path);
}

void MetadataPanel::renderHistogram(const QImage &image)
{
    if (image.isNull() || image.width() * image.height() > 24 * 1000 * 1000) {
        m_histogram->setVisible(false);
        return;
    }
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    int bins[256] = {};
    const int step = qMax(1, gray.width() * gray.height() / 400000); // 抽样，控制耗时
    for (int y = 0; y < gray.height(); y += step) {
        const uchar *line = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); x += step) {
            ++bins[line[x]];
        }
    }
    int peak = 1;
    for (int i = 0; i < 256; ++i) peak = qMax(peak, bins[i]);

    const int W = 256, H = 90;
    QImage plot(W, H, QImage::Format_RGB32);
    plot.fill(QColor(40, 40, 40));
    for (int x = 0; x < 256; ++x) {
        const int h = bins[x] * H / peak;
        for (int y = H - 1; y >= H - h; --y) {
            plot.setPixel(x, y, qRgb(120, 200, 255));
        }
    }
    m_histogram->setPixmap(QPixmap::fromImage(plot));
    m_histogram->setVisible(true);
}

void MetadataPanel::addSection(const QString &title)
{
    auto *item = new QTreeWidgetItem(m_tree);
    item->setText(0, title);
    QFont f = item->font(0);
    f.setBold(true);
    item->setFont(0, f);
    item->setExpanded(true);
}

void MetadataPanel::addItem(const QString &key, const QString &value)
{
    auto *item = new QTreeWidgetItem(m_tree);
    item->setText(0, key);
    item->setText(1, value);
}

void MetadataPanel::loadExif(const QString &path)
{
    const ExifParser::Result exif = ExifParser::parse(path);

    if (!exif.tags.isEmpty()) {
        addSection(tr("EXIF"));
        for (auto it = exif.tags.constBegin(); it != exif.tags.constEnd(); ++it) {
            addItem(it.key(), it.value());
        }
    }

    if (exif.hasGps) {
        // 对照 Honeyview：Google Maps 模板
        m_mapUrl = QStringLiteral("https://maps.google.com/maps?q=%1,%2&z=15")
                       .arg(QString::number(exif.lat, 'f', 6), QString::number(exif.lng, 'f', 6));
        m_mapBtn->setVisible(true);
        m_mapBtn->setEnabled(true);
    }
}
