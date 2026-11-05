#include "MetadataPanel.h"

#include <QApplication>
#include <QColorSpace>
#include <QClipboard>
#include <QDesktopServices>
#include <QEvent>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
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
#include <QStyle>
#include <QStyleOption>
#include <QUrl>
#include <QVBoxLayout>

#include "ExifParser.h"
#include "UiStyle.h"

// ---------------------------------------------------------------------------
// 可换行值标签：值列不省略（工单 1.3），长文件名/长 GPS 允许 WrapAnywhere 折成
// 多行读全；Tooltip 保留全文。宽度由网格按浮层宽分配，不产生横向滚动。
// ---------------------------------------------------------------------------
class WrappingLabel : public QLabel
{
public:
    explicit WrappingLabel(QWidget *parent = nullptr) : QLabel(parent)
    {
        setWordWrap(true);
        // 水平 Ignored：列宽交给 QGridLayout 按浮层宽分配；高度按折行后的
        // heightForWidth 走，避免「占两行的值被压成一行」。
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }

    void setFullText(const QString &text)
    {
        m_full = text;
        setToolTip(text);
        // QLabel 的 wordWrap 只在词边界断行，连续长词（超长文件名 aaaa…、无空格
        // URL）会一行到底溢出被裁。在每个字符间插零宽空格（宽度为 0，不改变
        // 显示与度量），折行即等价 WrapAnywhere：过长就回车切断，下一行继续。
        QString display;
        display.reserve(text.size() * 2);
        for (int i = 0; i < text.size(); ++i) {
            if (i > 0) {
                display += QChar(0x200B);
            }
            display += text[i];
        }
        QLabel::setText(display);
    }

    // QLabel 的默认高度刚好切掉下划线这类贴底笔画，这里多留 4px。
    // 未布局时 width()==0：heightForWidth(0) 会把每个字符都当成一行（实测 23 行
    // 键值撑出 3060px），所以 0 宽按浮层上限宽估，真实换行交给布局的 hfw。
    QSize sizeHint() const override
    {
        QSize hint = QLabel::sizeHint();
        const int w = width() > 0 ? width() : 300;
        hint.setHeight(qMax(fontMetrics().height() + 4, heightForWidth(w) + 4));
        return hint;
    }

    QSize minimumSizeHint() const override { return QSize(0, sizeHint().height()); }

    int naturalHeight(int w) const { return heightForWidth(qMax(1, w)) + 4; }

private:
    QString m_full;
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
        // 浅色底板不能用纯白：白面板上等于没有边界。用略深的凹陷井 #f2f3f5，
        // 配合 line() 描边把直方图区域勾出来。
        painter.fillPath(plate, UiStyle::color(UiStyle::pick("#1e2023", "#f2f3f5")));
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
        setObjectName(QStringLiteral("infoRoundBtn"));   // QSS 尺寸归零，几何由 setFixedSize 定
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

        // 圆钮压在浮层底板上：深色底板用「白色半透明」，浅色白卡片必须反过来用
        // 「黑色半透明」，否则白底白圈白图标 = 完全看不见（2026-11-05 实测）。
        const bool light = UiStyle::isLight();
        const bool hot = underMouse() || isDown();
        QColor bg = light ? QColor(0, 0, 0, isDown() ? 26 : (hot ? 20 : 8))
                          : QColor(255, 255, 255, isDown() ? 46 : (hot ? 38 : 15));
        QColor border = light ? QColor(0, 0, 0, isDown() ? 55 : (hot ? 45 : 26))
                              : QColor(255, 255, 255, isDown() ? 61 : (hot ? 51 : 31));
        p.setPen(QPen(border, 1));
        p.setBrush(bg);
        p.drawEllipse(rect().adjusted(0.5, 0.5, -0.5, -0.5));

        QColor ink = light ? QColor(0x3a, 0x3d, 0x41) : QColor(0xd7, 0xd9, 0xdc);
        if (hot) {
            ink = light ? QColor(0x10, 0x12, 0x14) : QColor(Qt::white);
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

    // —— 内容区（2026-11-04 修订：不滚动）——
    // 用户定稿：默认全部显示，不要鼠标往下翻；内容比画面高就让画面截断底边，
    // 无所谓。所以这里直接放普通容器，不再套 QScrollArea。
    m_scroll = new QWidget(this);   // 字段名沿用 m_scroll，类型改普通容器
    auto *scrollLay = new QVBoxLayout(m_scroll);
    scrollLay->setContentsMargins(0, 0, 0, 0);
    scrollLay->setSpacing(0);
    root->addWidget(m_scroll, 1);

    m_body = new QWidget(m_scroll);
    m_bodyLayout = new QVBoxLayout(m_body);
    m_bodyLayout->setContentsMargins(0, 0, 0, 0);
    m_bodyLayout->setSpacing(8);
    scrollLay->addWidget(m_body, 0, Qt::AlignTop);   // 内容少时顶对齐收紧，不拉伸

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

    // —— 底部可折叠直方图（默认展开，去页签后的唯一图形化信息）——
    m_histToggle = new QPushButton(m_body);
    m_histToggle->setObjectName(QStringLiteral("histToggle"));
    m_histToggle->setCursor(Qt::PointingHandCursor);
    m_histToggle->setFlat(true);
    m_histToggle->setFocusPolicy(Qt::NoFocus);
    m_histToggle->setText(tr("▾ 直方图"));
    connect(m_histToggle, &QPushButton::clicked, this, [this]() {
        const bool show = m_histBody->isHidden();
        m_histBody->setVisible(show);
        m_histToggle->setText(show ? tr("▾ 直方图") : tr("▸ 直方图"));
        // 高度随内容收紧/长高：折叠改了内容，布局缓存必须整体失效——只调
        // updateGeometry() 会让外层仍拿到旧的 totalSizeHint（实测 574 不降）。
        // QLayout::invalidate() 从该布局向上逐层失效到顶层，sizeHint 链路才算重算。
        if (layout()) {
            layout()->invalidate();
        }
        updateGeometry();
        emit heightChanged();
    });
    m_bodyLayout->addWidget(m_histToggle, 0, Qt::AlignLeft);

    m_histBody = new QWidget(m_body);
    auto *histLayout = new QVBoxLayout(m_histBody);
    histLayout->setContentsMargins(0, 0, 0, 0);
    m_histogram = new HistogramView(m_histBody);
    histLayout->addWidget(m_histogram);
    m_histBody->show();
    m_bodyLayout->addWidget(m_histBody);

    // 不加 addStretch：面板高度=内容自然高（2026-11-04 定稿），多余的 stretch
    // 只会在「面板被外部几何拉高」时把内容推开，形成无意义的空白。


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
    if (m_histogram) {
        m_histogram->clearBins();
    }
    rebuildGrid();
}

void MetadataPanel::addGroup(const QList<Row> &rows)
{
    // 工单 1.2：没有的数据不占位——空串 / 仅 '-' / 「未知」整行不生成；
    // 组内过滤后没有任何行就整组跳过（连组间距一起省掉）。
    QList<Row> live;
    for (const Row &r : rows) {
        const QString v = r.value.trimmed();
        if (v.isEmpty() || v == QLatin1String("-") || v == tr("未知")
            || v.compare(QLatin1String("unknown"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        live.append(r);
    }
    if (live.isEmpty()) {
        return;
    }

    int row = m_grid->rowCount();
    const int rowPx = m_smallFont ? 10 : 12;   // 「用小字体显示 EXIF / 文件信息」
    for (const Row &r : live) {
        // 键显示文本带冒号（对照原型 .info-key「相机制造商:」）：键值两列左对齐
        const QString keyText = r.key + QStringLiteral(":");
        auto *key = new QLabel(keyText, m_gridHolder);
        key->setObjectName(QStringLiteral("infoKey"));
        key->setFont(UiStyle::font(rowPx));

        // 值列不省略：超长值换行读全（1.3），Tooltip 保留全文，复制走 m_copyLines 全文
        auto *value = new WrappingLabel(m_gridHolder);
        value->setObjectName(QStringLiteral("infoVal"));
        value->setFont(UiStyle::font(rowPx));
        value->setFullText(r.value);

        // 自然宽度用各 label 自己的字体度量累计；值列只累计单行宽（不逼浮层到上限），
        // 超出上限的值在浮层内换行而不是把卡片撑到 440。
        m_maxKeyW = qMax(m_maxKeyW, key->fontMetrics().horizontalAdvance(keyText));
        const int singleLine = value->fontMetrics().horizontalAdvance(r.value);
        m_maxValW = qMax(m_maxValW, qMin(singleLine, 300));

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
    m_lastPath = path;
    m_lastImage = image;
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

    // 工单 1.2：没有 EXIF 就什么都不显示——不生成「这张图片没有 EXIF 信息」
    // 占位行，也不排一排 '-'；面板只剩文件基础信息 + 直方图，高度自然收紧。

    if (!m_mapUrl.isEmpty()) {
        m_mapBtn->show();
    }

    // —— 直方图（同一份数据，折叠块）——
    renderHistogram(image);

    finishLayout();
}

void MetadataPanel::setSmallFont(bool small)
{
    if (m_smallFont == small) {
        return;
    }
    m_smallFont = small;
    // 已有内容按最近一次的入参重建：字号变了，键列/值列的自然宽度也要重算。
    if (!m_lastPath.isEmpty()) {
        showImage(m_lastPath, m_lastImage);
    }
}

void MetadataPanel::finishLayout()
{
    // 宽度自适应：键列取最宽键、值列取最长单行值（超长值在浮层内换行，不撑宽），
    // 钳到 [280, 440]（原型上限 440）。
    const int natural = m_maxKeyW + 2 + m_maxValW;   // 两列 + 列间距
    m_contentWidth = qBound(280, natural, 440);
    setFixedWidth(m_contentWidth);
    if (m_gridHolder) {
        m_gridHolder->setFixedWidth(m_contentWidth - 20);
    }
    updateGeometry();
}

void MetadataPanel::dumpHeights(const QString &path, const QString &tag) const
{
    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream ts(&f);
    ts << (tag.isEmpty() ? QStringLiteral("DUMP") : tag) << " hint=" << sizeHint().height() << " actual=" << height()
       << " minHint=" << minimumSizeHint().height()
       << " scrollHint=" << (m_scroll ? m_scroll->sizeHint().height() : -1)
       << " bodyHint=" << (m_body ? m_body->sizeHint().height() : -1)
       << " bodyActual=" << (m_body ? m_body->height() : -1)
       << " gridHolderHint=" << (m_gridHolder ? m_gridHolder->sizeHint().height() : -1)
       << " rows=" << (m_grid ? m_grid->rowCount() : -1)
       << " rootHint=" << (layout() ? layout()->sizeHint().height() : -1)
       << " histVis=" << (m_histBody ? m_histBody->isVisibleTo(this) : -1)
       << " histH=" << (m_histBody ? m_histBody->sizeHint().height() : -1)
       << "\n";
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
