#include "MetadataPanel.h"

#include <QColorSpace>
#include <QDesktopServices>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStyle>
#include <QStyleOption>
#include <QTabBar>
#include <QUrl>
#include <QVBoxLayout>

#include "ExifParser.h"
#include "UiStyle.h"

// ---------------------------------------------------------------------------
// 单行省略文字：QLabel 不会自己截断，这里留全文、按宽度重新算省略串。
// ---------------------------------------------------------------------------
class ElidedLabel : public QLabel
{
public:
    explicit ElidedLabel(Qt::TextElideMode mode = Qt::ElideMiddle, QWidget *parent = nullptr)
        : QLabel(parent), m_mode(mode) {}

    void setFullText(const QString &text)
    {
        m_full = text;
        setToolTip(text);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        updateElide();
    }

    // QLabel 的默认高度刚好切掉下划线这类贴底笔画，这里多留 4px。
    QSize sizeHint() const override
    {
        QSize hint = QLabel::sizeHint();
        hint.setHeight(qMax(hint.height(), fontMetrics().height() + 4));
        return hint;
    }

    QSize minimumSizeHint() const override { return QSize(0, sizeHint().height()); }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateElide();
    }

    void changeEvent(QEvent *event) override
    {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::FontChange) {
            updateElide();
        }
    }

private:
    void updateElide()
    {
        if (m_full.isEmpty()) {
            return;
        }
        const int avail = qMax(0, width() - 2);
        const QString shown = avail > 0 ? fontMetrics().elidedText(m_full, m_mode, avail)
                                        : m_full;
        if (shown != text()) {
            QLabel::setText(shown);
        }
    }

    QString m_full;
    Qt::TextElideMode m_mode = Qt::ElideMiddle;
};

// ---------------------------------------------------------------------------
// 预览块：QSS 画底板+描边，图片按 cover 比例裁中填满（对应原型 object-fit:cover）。
// ---------------------------------------------------------------------------
class PreviewBox : public QWidget
{
public:
    explicit PreviewBox(QWidget *parent = nullptr) : QWidget(parent) {}

    void setImage(const QImage &image)
    {
        m_image = image;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QStyleOption opt;
        opt.initFrom(this);
        style()->drawPrimitive(QStyle::PE_Widget, &opt, &painter, this);

        if (m_image.isNull()) {
            return;
        }
        const QRectF box = QRectF(rect()).adjusted(1, 1, -1, -1);
        if (box.width() < 2 || box.height() < 2) {
            return;
        }
        QPainterPath clip;
        clip.addRoundedRect(box, 6, 6);
        painter.setClipPath(clip);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QSizeF src = m_image.size();
        const qreal scale = qMax(box.width() / src.width(), box.height() / src.height());
        const QSizeF scaled = src * scale;
        const QRectF dst(box.center().x() - scaled.width() / 2,
                         box.center().y() - scaled.height() / 2,
                         scaled.width(), scaled.height());
        painter.drawImage(dst, m_image);
    }

private:
    QImage m_image;
};

// ---------------------------------------------------------------------------
// 直方图：深色底板 + 青色柱体 + 0 / 255 标注（原型 .histogram 的深色版）。
// ---------------------------------------------------------------------------
class HistogramView : public QWidget
{
public:
    explicit HistogramView(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(90);
    }

    void setBins(const int *bins)
    {
        for (int i = 0; i < 256; ++i) {
            m_bins[i] = bins[i];
        }
        update();
    }

    void clearBins()
    {
        for (int i = 0; i < 256; ++i) {
            m_bins[i] = 0;
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath plate;
        plate.addRoundedRect(box, 6, 6);
        painter.fillPath(plate, QColor(0x1e, 0x20, 0x23));
        painter.setPen(QPen(UiStyle::line(), 1));
        painter.drawPath(plate);

        const int padX = 9;
        const int padTop = 9;
        const int labelH = 15;
        QRectF bars(box.left() + padX, box.top() + padTop,
                    box.width() - padX * 2, box.height() - padTop - labelH - 5);

        QFont small = font();
        small.setPixelSize(11);
        painter.setFont(small);
        painter.setPen(UiStyle::muted());
        const QRectF labelRow(box.left() + padX, bars.bottom() + 2,
                              box.width() - padX * 2, labelH);
        painter.drawText(labelRow, Qt::AlignLeft | Qt::AlignTop, QStringLiteral("0"));
        painter.drawText(labelRow, Qt::AlignRight | Qt::AlignTop, QStringLiteral("255"));

        if (bars.height() <= 0) {
            return;
        }
        // 256 个 bin 在这个宽度下会糊成一片，合并成 64 根（和原型那种一根根分开的柱体一致）
        constexpr int kGroups = 64;
        int sums[kGroups] = {};
        int peak = 1;
        for (int g = 0; g < kGroups; ++g) {
            int sum = 0;
            for (int i = 0; i < 4; ++i) {
                sum += m_bins[g * 4 + i];
            }
            sums[g] = sum;
            peak = qMax(peak, sum);
        }

        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(Qt::NoPen);
        painter.setBrush(UiStyle::accent());
        const qreal slot = bars.width() / kGroups;
        for (int g = 0; g < kGroups; ++g) {
            if (sums[g] <= 0) {
                continue;
            }
            const qreal h = bars.height() * sums[g] / peak;
            if (h < 1.0) {
                continue;  // 不足 1px 的低柱画出来只会变成一串虚线点
            }
            painter.drawRect(QRectF(bars.left() + g * slot, bars.bottom() - h,
                                    qMax<qreal>(1.0, slot - 1.0), h));
        }
    }

private:
    int m_bins[256] = {};
};

// ---------------------------------------------------------------------------
// 元数据分组：11px 组标题 + 每行「85px 标签 / 值」（原型 .meta-group / .meta-row）。
// ---------------------------------------------------------------------------
class MetaGroup : public QWidget
{
public:
    explicit MetaGroup(const QString &title, QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("metaGroup"));
        m_body = new QVBoxLayout(this);
        m_body->setContentsMargins(0, 0, 0, 16);  // 原型 .meta-group margin-bottom:16px
        m_body->setSpacing(0);

        auto *caption = new QLabel(title, this);
        caption->setObjectName(QStringLiteral("groupTitle"));
        m_body->addWidget(caption);
        m_body->addSpacing(5);
    }

    void add(const QString &key, const QString &value)
    {
        auto *row = new QWidget(this);
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 6, 0, 6);
        line->setSpacing(10);

        auto *keyLabel = new QLabel(key, row);
        keyLabel->setObjectName(QStringLiteral("metaKey"));
        keyLabel->setFixedWidth(85);
        keyLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        keyLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

        auto *valueLabel = new QLabel(value, row);
        valueLabel->setObjectName(QStringLiteral("metaValue"));
        valueLabel->setWordWrap(true);
        valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        valueLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);

        line->addWidget(keyLabel, 0);
        line->addWidget(valueLabel, 1);
        m_body->addWidget(row);
    }

    void addWidget(QWidget *widget)
    {
        m_body->addWidget(widget);
    }

private:
    QVBoxLayout *m_body = nullptr;
};

namespace {

// 只补 panelQss() 没覆盖到的细节（滚动区/分隔线/地图链接/空提示），颜色仍取 UiStyle 令牌。
QString extraQss()
{
    return QStringLiteral(
               "QScrollArea, QScrollArea > QWidget { background: transparent; border: 0; }"
               "QScrollArea > QWidget > QWidget { background: transparent; }"
               "QFrame#panelDivider { background: %1; border: 0; }"
               "QLabel#groupTitle { font-size: 11px; color: #858b93; }"
               "QLabel#emptyHint { font-size: 11px; color: %2; }"
               "QPushButton#mapLink { background: transparent; border: 0; padding: 0;"
               " text-align: left; font-size: 11px; color: %3; }"
               "QPushButton#mapLink:hover { color: #72caf1; }")
        .arg(QString::fromLatin1(UiStyle::Line), QString::fromLatin1(UiStyle::Muted),
             QString::fromLatin1(UiStyle::Accent));
}

void clearLayout(QVBoxLayout *layout)
{
    if (!layout) {
        return;
    }
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            delete widget;
        }
        delete item;
    }
}

} // namespace

// ---------------------------------------------------------------------------

MetadataPanel::MetadataPanel(QWidget *parent) : QWidget(parent)
{
    buildUi();
}

void MetadataPanel::buildUi()
{
    setObjectName(QStringLiteral("infoPanel"));
    // 普通 QWidget 子类要显式开这个属性，样式表的背景/圆角才会生效。
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(UiStyle::panelQss() + extraQss());

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // —— 头部：44px，标题 + 关闭 ——
    auto *head = new QWidget(this);
    head->setObjectName(QStringLiteral("infoHead"));
    head->setFixedHeight(44);
    auto *headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(16, 0, 12, 0);
    headLayout->setSpacing(0);

    auto *title = new QLabel(tr("图片信息"), head);
    title->setObjectName(QStringLiteral("infoTitle"));
    QFont titleFont = UiStyle::font(13);
    titleFont.setWeight(QFont::Medium);  // 原型 .info-head strong:500
    title->setFont(titleFont);
    headLayout->addWidget(title);
    headLayout->addStretch(1);

    auto *closeBtn = new QPushButton(QStringLiteral("×"), head);
    closeBtn->setObjectName(QStringLiteral("iconAction"));
    closeBtn->setFixedSize(30, 30);
    closeBtn->setToolTip(tr("关闭"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, [this]() {
        hide();
        emit closed();
    });
    headLayout->addWidget(closeBtn);
    root->addWidget(head);

    // —— 滚动区 ——
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto *content = new QWidget(m_scroll);
    content->setObjectName(QStringLiteral("infoContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(16, 16, 16, 16);
    contentLayout->setSpacing(0);
    m_scroll->setWidget(content);
    root->addWidget(m_scroll, 1);

    // 预览 142px
    m_preview = new PreviewBox(content);
    m_preview->setObjectName(QStringLiteral("previewBox"));
    m_preview->setFixedHeight(142);
    contentLayout->addWidget(m_preview);
    contentLayout->addSpacing(14);

    // 文件名 / 完整路径
    m_fileName = new ElidedLabel(Qt::ElideRight, content);
    m_fileName->setObjectName(QStringLiteral("fileName"));
    QFont nameFont = UiStyle::font(14);  // 与 QSS 一致，避免首帧按 12px 算高而切掉下伸部
    nameFont.setWeight(QFont::Medium);   // 原型 .file-name:500
    m_fileName->setFont(nameFont);
    contentLayout->addWidget(m_fileName);
    contentLayout->addSpacing(5);

    m_filePath = new ElidedLabel(Qt::ElideMiddle, content);
    m_filePath->setObjectName(QStringLiteral("filePath"));
    m_filePath->setFont(UiStyle::font(11));
    contentLayout->addWidget(m_filePath);
    contentLayout->addSpacing(16);

    // 三个页签（QTabBar 只用做选择器，样式来自 appQss 的 QTabBar::tab）
    m_tabs = new QTabBar(content);
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);
    m_tabs->setUsesScrollButtons(false);
    m_tabs->setFocusPolicy(Qt::NoFocus);
    m_tabs->setFixedHeight(33);
    m_tabs->addTab(tr("基本信息"));
    m_tabs->addTab(tr("EXIF"));
    m_tabs->addTab(tr("直方图"));
    contentLayout->addWidget(m_tabs);

    auto *divider = new QFrame(content);
    divider->setObjectName(QStringLiteral("panelDivider"));
    divider->setFixedHeight(1);
    contentLayout->addWidget(divider);
    contentLayout->addSpacing(14);

    // 基本信息页
    m_basicPage = new QWidget(content);
    m_basicLayout = new QVBoxLayout(m_basicPage);
    m_basicLayout->setContentsMargins(0, 0, 0, 0);
    m_basicLayout->setSpacing(0);
    contentLayout->addWidget(m_basicPage);

    // EXIF 页
    m_exifPage = new QWidget(content);
    m_exifLayout = new QVBoxLayout(m_exifPage);
    m_exifLayout->setContentsMargins(0, 0, 0, 0);
    m_exifLayout->setSpacing(0);
    contentLayout->addWidget(m_exifPage);

    // 直方图页
    m_histPage = new QWidget(content);
    m_histLayout = new QVBoxLayout(m_histPage);
    m_histLayout->setContentsMargins(0, 0, 0, 0);
    m_histLayout->setSpacing(0);
    auto *histGroup = new MetaGroup(tr("直方图"), m_histPage);
    m_histogram = new HistogramView(histGroup);
    histGroup->addWidget(m_histogram);
    m_histLayout->addWidget(histGroup);
    contentLayout->addWidget(m_histPage);

    contentLayout->addStretch(1);

    connect(m_tabs, &QTabBar::currentChanged, this, &MetadataPanel::selectTab);
    selectTab(0);

    clear();
}

void MetadataPanel::selectTab(int index)
{
    if (!m_basicPage) {
        return;
    }
    m_basicPage->setVisible(index == 0);
    m_exifPage->setVisible(index == 1);
    m_histPage->setVisible(index == 2);
}

void MetadataPanel::clear()
{
    if (!m_preview) {
        return;
    }
    m_preview->setImage(QImage());
    m_fileName->setFullText(QString());
    m_filePath->setFullText(QString());
    m_mapUrl.clear();
    m_mapBtn = nullptr;
    m_histogramBasic = nullptr;  // 随基本信息页一起被删掉
    clearLayout(m_basicLayout);
    clearLayout(m_exifLayout);
    m_histogram->clearBins();
}

void MetadataPanel::showImage(const QString &path, const QImage &image)
{
    clear();

    const QFileInfo info(path);
    m_preview->setImage(image);
    m_fileName->setFullText(info.fileName());
    m_filePath->setFullText(info.absoluteFilePath());

    // EXIF 先解析：GPS 结果决定基本信息页要不要加「在地图中查看」。
    loadExif(path);
    fillBasicPage(path, image);
    renderHistogram(image);
}

void MetadataPanel::fillBasicPage(const QString &path, const QImage &image)
{
    const QFileInfo info(path);

    auto *fileGroup = new MetaGroup(tr("文件"), m_basicPage);
    const qint64 bytes = info.size();
    if (bytes >= 1024 * 1024) {
        fileGroup->add(tr("文件大小"), tr("%1 MB").arg(QString::number(bytes / 1048576.0, 'f', 1)));
    } else if (bytes >= 1024) {
        fileGroup->add(tr("文件大小"), tr("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 0)));
    } else {
        fileGroup->add(tr("文件大小"), tr("%1 B").arg(bytes));
    }
    fileGroup->add(tr("修改时间"), info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    fileGroup->add(tr("格式"), info.suffix().toUpper());
    m_basicLayout->addWidget(fileGroup);

    // GPS 入口：原型没有，功能保留（青色文字链接）
    if (!m_mapUrl.isEmpty()) {
        m_mapBtn = new QPushButton(tr("在地图中查看"), m_basicPage);
        m_mapBtn->setObjectName(QStringLiteral("mapLink"));
        m_mapBtn->setCursor(Qt::PointingHandCursor);
        m_mapBtn->setFlat(true);
        connect(m_mapBtn, &QPushButton::clicked, this, [this]() {
            if (!m_mapUrl.isEmpty()) {
                QDesktopServices::openUrl(QUrl(m_mapUrl));
            }
        });
        m_basicLayout->addWidget(m_mapBtn, 0, Qt::AlignLeft);
        m_basicLayout->addSpacing(12);
    }

    auto *imageGroup = new MetaGroup(tr("图像"), m_basicPage);
    imageGroup->add(tr("尺寸"), image.isNull()
                                    ? tr("未知")
                                    : tr("%1 × %2 像素").arg(image.width()).arg(image.height()));

    QString colorSpaceText;
    const QColorSpace space = image.colorSpace();
    if (space.isValid()) {
        colorSpaceText = space.description();
        if (colorSpaceText.isEmpty()) {
            colorSpaceText = tr("未知");
        }
    } else {
        colorSpaceText = QStringLiteral("sRGB");  // 无 ICC 时按 sRGB 解释
    }
    imageGroup->add(tr("色彩空间"), colorSpaceText);

    // RGB32 只是内部对齐，真实色深仍是 24 位（多出来的字节是填充）
    int depth = image.depth();
    if (depth == 32 && !image.hasAlphaChannel()) {
        depth = 24;
    }
    imageGroup->add(tr("位深"), tr("%1 bit").arg(depth));
    m_basicLayout->addWidget(imageGroup);

    // 原型在基本信息页底部也放了一份直方图（与「直方图」页签同一份数据）
    auto *histGroup = new MetaGroup(tr("直方图"), m_basicPage);
    m_histogramBasic = new HistogramView(histGroup);
    histGroup->addWidget(m_histogramBasic);
    m_basicLayout->addWidget(histGroup);
}

void MetadataPanel::loadExif(const QString &path)
{
    const ExifParser::Result exif = ExifParser::parse(path);

    if (exif.hasGps) {
        // 对照 Honeyview：Google Maps 模板
        m_mapUrl = QStringLiteral("https://maps.google.com/maps?q=%1,%2&z=15")
                       .arg(QString::number(exif.lat, 'f', 6), QString::number(exif.lng, 'f', 6));
    }

    // 按用途归类，但字段一个都不丢：没归到任何组的都进「其他」。
    const QList<QPair<QString, QStringList>> groups = {
        {tr("相机"),
         {QStringLiteral("相机制造商"), QStringLiteral("相机型号"), QStringLiteral("镜头"),
          QStringLiteral("软件"), QStringLiteral("作者"), QStringLiteral("版权")}},
        {tr("拍摄参数"),
         {QStringLiteral("拍摄时间"), QStringLiteral("ISO"), QStringLiteral("曝光时间"),
          QStringLiteral("光圈"), QStringLiteral("焦距"), QStringLiteral("曝光补偿")}},
        {tr("尺寸"),
         {QStringLiteral("宽度"), QStringLiteral("高度")}},
        {tr("位置"),
         {QStringLiteral("GPS 纬度"), QStringLiteral("GPS 经度"), QStringLiteral("海拔")}},
    };

    QMap<QString, QString> rest = exif.tags;
    for (const auto &group : groups) {
        QStringList present;
        for (const QString &key : group.second) {
            if (rest.contains(key)) {
                present.append(key);
            }
        }
        if (present.isEmpty()) {
            continue;
        }
        auto *box = new MetaGroup(group.first, m_exifPage);
        for (const QString &key : present) {
            box->add(key, rest.take(key));
        }
        m_exifLayout->addWidget(box);
    }

    if (!rest.isEmpty()) {
        auto *box = new MetaGroup(tr("其他"), m_exifPage);
        for (auto it = rest.constBegin(); it != rest.constEnd(); ++it) {
            box->add(it.key(), it.value());
        }
        m_exifLayout->addWidget(box);
    }

    if (exif.tags.isEmpty()) {
        auto *empty = new QLabel(tr("这张图片没有 EXIF 信息"), m_exifPage);
        empty->setObjectName(QStringLiteral("emptyHint"));
        empty->setWordWrap(true);
        m_exifLayout->addWidget(empty);
    }
}

void MetadataPanel::renderHistogram(const QImage &image)
{
    if (image.isNull() || image.width() * image.height() > 24 * 1000 * 1000) {
        m_histogram->clearBins();
        if (m_histogramBasic) {
            m_histogramBasic->clearBins();
        }
        return;
    }

    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    int bins[256] = {};
    const int step = qMax(1, gray.width() * gray.height() / 400000);  // 抽样，控制耗时
    for (int y = 0; y < gray.height(); y += step) {
        const uchar *line = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); x += step) {
            ++bins[line[x]];
        }
    }
    m_histogram->setBins(bins);
    if (m_histogramBasic) {
        m_histogramBasic->setBins(bins);
    }
}
