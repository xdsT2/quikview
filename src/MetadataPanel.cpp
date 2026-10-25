#include "MetadataPanel.h"

#include <QApplication>
#include <QColorSpace>
#include <QClipboard>
#include <QDesktopServices>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStyle>
#include <QStyleOption>
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
    Qt::TextElideMode m_mode = Qt::ElideRight;
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
// 顶部圆钮：22px 圆形描边按钮（原型 .info-action），自绘 × 与复制图标。
// ---------------------------------------------------------------------------
class RoundIconBtn : public QPushButton
{
public:
    enum Glyph { Close, Copy };

    RoundIconBtn(Glyph glyph, const QString &tip, QWidget *parent = nullptr)
        : QPushButton(parent), m_glyph(glyph)
    {
        setFixedSize(22, 22);
        setToolTip(tip);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const bool hot = underMouse() || isDown();
        QColor bg(255, 255, 255, isDown() ? 46 : (hot ? 38 : 15));
        QColor border(255, 255, 255, isDown() ? 61 : (hot ? 51 : 31));
        p.setPen(QPen(border, 1));
        p.setBrush(bg);
        p.drawEllipse(rect().adjusted(0.5, 0.5, -0.5, -0.5));

        QColor ink(0xd7, 0xd9, 0xdc);
        if (hot) {
            ink = QColor(Qt::white);
        }
        QPen pen(ink, 1.6);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        const QPointF c = rect().center();
        if (m_glyph == Close) {
            p.drawLine(c + QPointF(-3.2, -3.2), c + QPointF(3.2, 3.2));
            p.drawLine(c + QPointF(3.2, -3.2), c + QPointF(-3.2, 3.2));
        } else {
            // 两张叠纸（原型 #i-copy）：后纸在右上，前纸在左下
            p.drawRoundedRect(QRectF(c.x() - 5.5, c.y() - 2.0, 7.5, 8.0), 1.5, 1.5);
            p.drawRoundedRect(QRectF(c.x() - 2.0, c.y() - 6.0, 7.5, 8.0), 1.5, 1.5);
        }
    }

private:
    Glyph m_glyph;
};

namespace {

// GPS 十进制度 → 「N 30°36'19.03"」样式（对照原型 GPS 信息行的度分秒格式）
QString formatDms(double deg, QChar positive, QChar negative)
{
    const QChar hem = deg >= 0 ? positive : negative;
    qreal a = qAbs(deg);
    const int d = int(a);
    a = (a - d) * 60.0;
    const int m = int(a);
    const qreal s = (a - m) * 60.0;
    return QStringLiteral("%1 %2°%3'%4\"")
        .arg(hem).arg(d).arg(m).arg(s, 0, 'f', 2);
}

} // namespace

// ---------------------------------------------------------------------------

MetadataPanel::MetadataPanel(QWidget *parent) : QWidget(parent)
{
    buildUi();
    clear();
}

void MetadataPanel::buildUi()
{
    setObjectName(QStringLiteral("infoPanel"));
    // 普通 QWidget 子类要显式开这个属性，样式表的背景/圆角才会生效。
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(UiStyle::panelQss());

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 6, 10, 9);   // 原型 .info-overlay padding
    root->setSpacing(6);

    // —— 顶部圆钮行：关闭 / 复制参数（无标题文字，原型 .info-actions）——
    auto *actions = new QWidget(this);
    actions->setObjectName(QStringLiteral("infoActions"));
    auto *actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(6);
    auto *closeBtn = new RoundIconBtn(RoundIconBtn::Close, tr("关闭 (Tab)"), actions);
    connect(closeBtn, &QPushButton::clicked, this, [this]() {
        hide();
        emit closed();
    });
    actionsLayout->addWidget(closeBtn);
    auto *copyBtn = new RoundIconBtn(RoundIconBtn::Copy, tr("复制参数"), actions);
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_copyLines.join(QLatin1Char('\n')));
        emit copied();
    });
    actionsLayout->addWidget(copyBtn);
    actionsLayout->addStretch(1);
    root->addWidget(actions);

    // —— 滚动区：内容超出可用高度时整块内部滚动，不撑出窗口 ——
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    root->addWidget(m_scroll, 1);

    m_body = new QWidget(m_scroll);
    m_bodyLayout = new QVBoxLayout(m_body);
    m_bodyLayout->setContentsMargins(0, 0, 0, 0);
    m_bodyLayout->setSpacing(8);
    m_scroll->setWidget(m_body);

    rebuildGrid();

    // GPS 入口：原型没有，功能保留（青色文字链接）
    m_mapBtn = new QPushButton(tr("在地图中查看"), m_body);
    m_mapBtn->setObjectName(QStringLiteral("mapLink"));
    m_mapBtn->setCursor(Qt::PointingHandCursor);
    m_mapBtn->setFlat(true);
    connect(m_mapBtn, &QPushButton::clicked, this, [this]() {
        if (!m_mapUrl.isEmpty()) {
            QDesktopServices::openUrl(QUrl(m_mapUrl));
        }
    });
    m_bodyLayout->addWidget(m_mapBtn, 0, Qt::AlignLeft);
    m_mapBtn->hide();

    // —— 底部可折叠直方图（默认收起，去页签后的唯一图形化信息）——
    m_histToggle = new QPushButton(m_body);
    m_histToggle->setObjectName(QStringLiteral("histToggle"));
    m_histToggle->setCursor(Qt::PointingHandCursor);
    m_histToggle->setFlat(true);
    m_histToggle->setFocusPolicy(Qt::NoFocus);
    m_histToggle->setText(tr("▸ 直方图"));
    connect(m_histToggle, &QPushButton::clicked, this, [this]() {
        const bool show = m_histBody->isHidden();
        m_histBody->setVisible(show);
        m_histToggle->setText(show ? tr("▾ 直方图") : tr("▸ 直方图"));
    });
    m_bodyLayout->addWidget(m_histToggle, 0, Qt::AlignLeft);

    m_histBody = new QWidget(m_body);
    auto *histLayout = new QVBoxLayout(m_histBody);
    histLayout->setContentsMargins(0, 0, 0, 0);
    m_histogram = new HistogramView(m_histBody);
    histLayout->addWidget(m_histogram);
    m_histBody->hide();
    m_bodyLayout->addWidget(m_histBody);

    m_bodyLayout->addStretch(1);
}

// 键值 grid 每次换图整个重建：QGridLayout 移除 item 后行数/行高不会缩回去，
// 复用会踩到上一张图留下的空行与 7px 行高。
void MetadataPanel::rebuildGrid()
{
    if (m_gridHolder) {
        m_gridHolder->deleteLater();
        m_gridHolder = nullptr;
        m_grid = nullptr;
    }
    m_gridHolder = new QWidget(m_body);
    m_grid = new QGridLayout(m_gridHolder);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(2);   // 原型 .info-rows column-gap:2px
    m_grid->setVerticalSpacing(1);     // 原型 row-gap:1px
    m_grid->setColumnStretch(0, 0);
    m_grid->setColumnStretch(1, 1);
    m_bodyLayout->insertWidget(0, m_gridHolder);
}

void MetadataPanel::clear()
{
    m_copyLines.clear();
    m_maxKeyW = 0;
    m_maxValW = 0;
    m_mapUrl.clear();
    if (m_mapBtn) {
        m_mapBtn->hide();
    }
    m_noExifHint = nullptr;
    if (m_histogram) {
        m_histogram->clearBins();
    }
    rebuildGrid();
}

void MetadataPanel::addGroup(const QList<Row> &rows)
{
    if (rows.isEmpty()) {
        return;
    }
    int row = m_grid->rowCount();
    for (const Row &r : rows) {
        auto *key = new QLabel(r.key, m_gridHolder);
        key->setObjectName(QStringLiteral("infoKey"));
        key->setFont(UiStyle::font(12));

        auto *value = new ElidedLabel(Qt::ElideRight, m_gridHolder);
        value->setObjectName(QStringLiteral("infoVal"));
        value->setFont(UiStyle::font(12));
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setFullText(r.value);

        // 自然宽度用各 label 自己的字体度量累计，与最终渲染一致
        m_maxKeyW = qMax(m_maxKeyW, key->fontMetrics().horizontalAdvance(r.key));
        m_maxValW = qMax(m_maxValW, value->fontMetrics().horizontalAdvance(r.value));

        m_grid->addWidget(key, row, 0, Qt::AlignLeft | Qt::AlignVCenter);
        m_grid->addWidget(value, row, 1);
        m_copyLines.append(r.key + QStringLiteral(": ") + r.value);
        ++row;
    }
}

void MetadataPanel::addGroupGap()
{
    // 原型 .info-gap：组间 7px 空隙；复制文本里对应一个空行（空组也输出空行）
    const int row = qMax(0, m_grid->rowCount());
    m_grid->setRowMinimumHeight(row, 7);
    m_grid->setRowStretch(row, 0);
    if (!m_copyLines.isEmpty() && !m_copyLines.last().isEmpty()) {
        m_copyLines.append(QString());
    }
}

void MetadataPanel::showImage(const QString &path, const QImage &image)
{
    clear();

    const QFileInfo info(path);

    // —— 组 1：文件（对照原型首组：文件名 / 图片大小 / 修改日期 / 图片信息）——
    QList<MetadataPanel::Row> fileRows;
    fileRows.append({ tr("文件名"), info.fileName() });
    const qint64 bytes = info.size();
    if (bytes >= 1024 * 1024) {
        fileRows.append({ tr("图片大小"), tr("%1 MB").arg(QString::number(bytes / 1048576.0, 'f', 1)) });
    } else if (bytes >= 1024) {
        fileRows.append({ tr("图片大小"), tr("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 0)) });
    } else {
        fileRows.append({ tr("图片大小"), tr("%1 B").arg(bytes) });
    }
    fileRows.append({ tr("修改日期"),
                      info.lastModified().toString(QStringLiteral("yyyy/MM/dd HH:mm:ss")) });

    const QColorSpace space = image.colorSpace();
    QString colorSpaceText;
    if (space.isValid()) {
        colorSpaceText = space.description();
        // QColorSpace 的全名带规范号（"sRGB IEC61966-2.1"），按原型只留色域短名
        if (int cut = colorSpaceText.indexOf(QLatin1Char(' ')); cut > 0) {
            colorSpaceText.truncate(cut);
        }
        if (colorSpaceText.isEmpty()) {
            colorSpaceText = tr("未知");
        }
    } else {
        colorSpaceText = QStringLiteral("sRGB");  // 无 ICC 时按 sRGB 解释
    }
    QString imageInfo = image.isNull()
        ? tr("未知")
        : tr("%1 × %2 (%3, %4)").arg(image.width()).arg(image.height())
              .arg(info.suffix().toUpper(), colorSpaceText);
    fileRows.append({ tr("图片信息"), imageInfo });

    // RGB32 只是内部对齐，真实色深仍是 24 位（多出来的字节是填充）
    int depth = image.depth();
    if (depth == 32 && !image.hasAlphaChannel()) {
        depth = 24;
    }
    fileRows.append({ tr("位深"), tr("%1 bit").arg(depth) });
    addGroup(fileRows);

    // —— 组 2 起：EXIF（去页签后按原型形态铺成键值行，组间 7px 空隙）——
    addGroupGap();
    const ExifParser::Result exif = ExifParser::parse(path);
    if (exif.hasGps) {
        // 对照 Honeyview：Google Maps 模板
        m_mapUrl = QStringLiteral("https://maps.google.com/maps?q=%1,%2&z=15")
                       .arg(QString::number(exif.lat, 'f', 6), QString::number(exif.lng, 'f', 6));
    }

    // 按用途归类（组标题不再显示，空隙即分组）；没归到任何组的进「其他」。
    const QList<QPair<QString, QStringList>> groups = {
        {tr("相机"),
         {QStringLiteral("相机制造商"), QStringLiteral("相机型号"), QStringLiteral("镜头"),
          QStringLiteral("软件"), QStringLiteral("作者"), QStringLiteral("版权")}},
        {tr("拍摄参数"),
         {QStringLiteral("拍摄时间"), QStringLiteral("ISO"), QStringLiteral("曝光时间"),
          QStringLiteral("光圈"), QStringLiteral("焦距"), QStringLiteral("曝光补偿")}},
    };

    QMap<QString, QString> rest = exif.tags;
    bool exifRows = false;
    for (const auto &group : groups) {
        QList<MetadataPanel::Row> rows;
        for (const QString &key : group.second) {
            if (rest.contains(key)) {
                rows.append({ key, rest.take(key) });
            }
        }
        if (!rows.isEmpty()) {
            if (exifRows) {
                addGroupGap();
            }
            addGroup(rows);
            exifRows = true;
        }
    }

    // 位置：原型把 GPS 折成一行「GPS 信息」（度分秒），地图链接保留。
    // ExifParser 会同时往 tags 里塞「GPS 纬度/经度」十进制行，与 DMS 行重复，去掉。
    if (exif.hasGps) {
        if (exifRows) {
            addGroupGap();
        }
        QList<MetadataPanel::Row> gps;
        gps.append({ tr("GPS 信息"),
                     formatDms(exif.lat, 'N', 'S') + QStringLiteral(", ")
                         + formatDms(exif.lng, 'E', 'W') });
        addGroup(gps);
        exifRows = true;
        rest.remove(QStringLiteral("GPS 纬度"));
        rest.remove(QStringLiteral("GPS 经度"));
    }

    if (!rest.isEmpty()) {
        if (exifRows) {
            addGroupGap();
        }
        QList<MetadataPanel::Row> other;
        for (auto it = rest.constBegin(); it != rest.constEnd(); ++it) {
            other.append({ it.key(), it.value() });
        }
        addGroup(other);
        exifRows = true;
    }

    if (!exifRows) {
        auto *empty = new QLabel(tr("这张图片没有 EXIF 信息"), m_gridHolder);
        empty->setObjectName(QStringLiteral("emptyHint"));
        empty->setWordWrap(true);
        m_grid->addWidget(empty, qMax(0, m_grid->rowCount()), 0, 1, 2, Qt::AlignLeft);
        m_noExifHint = empty;
        m_copyLines.append(tr("这张图片没有 EXIF 信息"));
    }
    if (!m_mapUrl.isEmpty()) {
        m_mapBtn->show();
    }

    // —— 直方图（同一份数据，折叠块）——
    renderHistogram(image);

    finishLayout();
}

void MetadataPanel::finishLayout()
{
    // 宽度自适应：键列取最宽键、值列取最长未省略值，钳到 [280, 440]（原型上限 440）。
    if (m_noExifHint) {
        m_maxKeyW = qMax(m_maxKeyW, m_noExifHint->fontMetrics().horizontalAdvance(
                                            m_noExifHint->text()));
    }
    const int natural = m_maxKeyW + 2 + m_maxValW;   // 两列 + 列间距
    m_contentWidth = qBound(280, natural, 440);
    setFixedWidth(m_contentWidth);
    if (m_gridHolder) {
        m_gridHolder->setFixedWidth(m_contentWidth - 20);
    }
    updateGeometry();
}

void MetadataPanel::renderHistogram(const QImage &image)
{
    if (image.isNull() || image.width() * image.height() > 24 * 1000 * 1000) {
        m_histogram->clearBins();
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
}
