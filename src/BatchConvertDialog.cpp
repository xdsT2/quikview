#include "BatchConvertDialog.h"

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>

#include "Config.h"
#include "UiStyle.h"

namespace {

constexpr int kDlgW       = 780;   // 原型 .convert width:min(780px,94%)
constexpr int kLeftW      = 180;   // .transform-layout 第一列
constexpr int kFileListH  = 132;   // .file-list height
constexpr int kPreviewH   = 116;   // .transform-preview height
constexpr int kGapCol     = 20;    // .transform-layout gap

QString settingsGet(const char *key)
{
    return Config::instance().settings().value(QStringLiteral("config/") + key).toString();
}

} // namespace

// 预览块：按 cover 裁切填满（原型 .transform-preview 的 overflow:hidden + object-fit:cover）
class ConvertPreview : public QWidget
{
public:
    explicit ConvertPreview(QWidget *parent = nullptr) : QWidget(parent) {}

    void setImage(const QImage &img)
    {
        m_image = img;
        update();
    }

    void setNote(const QString &note)
    {
        m_note = note;
        m_image = QImage();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(box, 6, 6);

        p.save();
        p.setClipPath(clip);
        p.fillRect(rect(), QColor(QStringLiteral("#191b1e")));
        if (!m_image.isNull()) {
            const QSize target = m_image.size().scaled(box.size().toSize(),
                                                       Qt::KeepAspectRatioByExpanding);
            const QImage scaled = m_image.scaled(target, Qt::IgnoreAspectRatio,
                                                 Qt::SmoothTransformation);
            const QRect crop(qMax(0, (scaled.width() - int(box.width())) / 2),
                             qMax(0, (scaled.height() - int(box.height())) / 2),
                             int(box.width()), int(box.height()));
            p.drawImage(box.topLeft(), scaled, crop);
        }
        p.restore();

        p.setPen(QPen(UiStyle::line(), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(box, 6, 6);

        if (m_image.isNull() && !m_note.isEmpty()) {
            p.setPen(UiStyle::dim());
            p.drawText(rect(), Qt::AlignCenter, m_note);
        }
    }

private:
    QImage m_image;
    QString m_note;
};

BatchConvertDialog::BatchConvertDialog(const QStringList &files, QWidget *parent)
    : QDialog(parent)
    , m_files(files)
{
    setWindowTitle(tr("图片转换"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setStyleSheet(UiStyle::dialogQss() + QStringLiteral(R"(
QDialog { background: transparent; }
QWidget#dlgTitleBar { border-top-left-radius: 10px; border-top-right-radius: 10px; }
QWidget#dlgFooter { border-bottom-left-radius: 10px; border-bottom-right-radius: 10px; }

QLabel#formLabel { font-size: 12px; color: #c7cbd0; }
QLabel#fileCaption { font-size: 11px; color: #c7cbd0; }
QLabel#unitHint { font-size: 12px; color: #92979e; }
QLabel#rowLabel { font-size: 12px; color: #c7cbd0; }
QLabel#qualityValue { font-size: 11px; color: #55c0f0; }
QLabel#qualityValue:disabled { color: #5d626a; }
QLabel#namePreview { background: #1f2124; border: 1px solid rgba(255,255,255,0.085);
    border-radius: 5px; padding: 0 10px; color: #9fa5ac; font-size: 11px; }
QLabel#progressText, QLabel#progressCount { font-size: 11px; color: #92979e; }

QRadioButton#radio { font-size: 11px; color: #c9cdd2; spacing: 6px; }

QListWidget#fileList { background: #1e2023; border: 1px solid rgba(255,255,255,0.085);
    border-radius: 6px; padding: 5px; font-size: 11px; outline: 0; }
QListWidget#fileList::item { padding: 6px 7px; border-radius: 4px; color: #b9bec5; }
QListWidget#fileList::item:hover { background: #2a2c30; }
QListWidget#fileList::item:selected { background: rgba(85,192,240,0.12); color: #55c0f0; }

QLineEdit#sizeInput { min-height: 32px; }
QPushButton#keepRatio:checked { background: rgba(85,192,240,0.12); border-color: #55c0f0;
    color: #55c0f0; }

QComboBox#formatSelect { min-height: 31px; }

QProgressBar#convertProgress { background: #383b40; border: 0; border-radius: 3px; }
QProgressBar#convertProgress::chunk { background: #55c0f0; border-radius: 3px; }

QSlider#qualitySlider { min-width: 110px; }
QSlider#qualitySlider::groove:horizontal { height: 3px; background: #5d6166; border-radius: 3px; }
QSlider#qualitySlider::sub-page:horizontal { background: #55c0f0; border-radius: 3px; }
QSlider#qualitySlider::handle:horizontal { width: 11px; height: 11px; margin: -4px 0;
    border-radius: 6px; background: #f2f4f5; }
QSlider#qualitySlider::sub-page:horizontal:disabled { background: #4f5359; }
QSlider#qualitySlider::handle:horizontal:disabled { background: #70767e; }
)"));
    setFont(UiStyle::font(12));

    buildUi();
    loadSourceInfo();
    adjustSize();

    if (parent && parent->isVisible()) {
        const QRect pg = parent->frameGeometry();
        move(pg.center() - rect().center());
    }
}

// ---------------------------------------------------------------------------
// 骨架
// ---------------------------------------------------------------------------
void BatchConvertDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // —— 标题栏 43px ——
    auto *titleBar = new QWidget(this);
    titleBar->setObjectName(QStringLiteral("dlgTitleBar"));
    titleBar->setFixedHeight(UiStyle::TitleBarH);
    titleBar->installEventFilter(this);
    auto *tb = new QHBoxLayout(titleBar);
    tb->setContentsMargins(18, 0, 14, 0);
    tb->setSpacing(9);
    auto *title = new QLabel(tr("图片转换"), titleBar);
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

    // —— 主体：23px 24px 20px ——
    auto *body = new QWidget(this);
    auto *bodyLay = new QVBoxLayout(body);
    bodyLay->setContentsMargins(24, 23, 24, 20);
    bodyLay->setSpacing(0);

    auto *columns = new QHBoxLayout;
    columns->setSpacing(kGapCol);

    // —— 左栏：文件列表 + 预览 ——
    auto *left = new QWidget(body);
    left->setFixedWidth(kLeftW);
    auto *lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->setSpacing(0);

    m_fileCaption = new QLabel(tr("文件列表 · %1 张").arg(m_files.size()), left);
    m_fileCaption->setObjectName(QStringLiteral("fileCaption"));
    lv->addWidget(m_fileCaption);
    lv->addSpacing(8);

    m_fileList = new QListWidget(left);
    m_fileList->setObjectName(QStringLiteral("fileList"));
    m_fileList->setFixedHeight(kFileListH);
    m_fileList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fileList->setUniformItemSizes(true);
    for (const QString &f : m_files) {
        auto *item = new QListWidgetItem(QFileInfo(f).fileName(), m_fileList);
        item->setSizeHint(QSize(0, 25));
        item->setToolTip(f);
    }
    if (m_fileList->count() > 0) {
        m_fileList->setCurrentRow(0);
    }
    connect(m_fileList, &QListWidget::currentRowChanged, this, &BatchConvertDialog::onFileChanged);
    lv->addWidget(m_fileList);

    lv->addSpacing(10);
    m_preview = new ConvertPreview(left);
    m_preview->setObjectName(QStringLiteral("transformPreview"));
    m_preview->setFixedHeight(kPreviewH);
    lv->addWidget(m_preview);
    lv->addStretch(1);
    columns->addWidget(left);

    // —— 右栏：选项 ——
    auto *right = new QWidget(body);
    auto *rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(0);

    // 表单行：标签 + 8px + 控件 + 13px 行距
    auto addRow = [right, rv](const QString &labelText, QLayout *ctrl) {
        auto *l = new QLabel(labelText, right);
        l->setObjectName(QStringLiteral("formLabel"));
        rv->addWidget(l);
        rv->addSpacing(8);
        rv->addLayout(ctrl);
        rv->addSpacing(13);
    };

    // 1) 旋转
    auto *rotGroup = new QButtonGroup(this);
    auto *rotLay = new QHBoxLayout;
    rotLay->setContentsMargins(0, 0, 0, 0);
    rotLay->setSpacing(14);
    const QStringList rotTexts = {tr("不旋转"), tr("逆时针 90°"), tr("顺时针 90°"),
                                  tr("旋转 180°"), tr("按 EXIF 自动旋转")};
    for (int i = 0; i < rotTexts.size(); ++i) {
        m_rot[i] = new QRadioButton(rotTexts.at(i), right);
        m_rot[i]->setObjectName(QStringLiteral("radio"));
        m_rot[i]->setCursor(Qt::PointingHandCursor);
        rotGroup->addButton(m_rot[i], i);
        rotLay->addWidget(m_rot[i]);
    }
    m_rot[0]->setChecked(true);
    rotLay->addStretch(1);
    addRow(tr("旋转"), rotLay);

    // 2) 裁剪 / 新尺寸
    auto *sizeLay = new QHBoxLayout;
    sizeLay->setContentsMargins(0, 0, 0, 0);
    sizeLay->setSpacing(8);
    auto *sizeValidator = new QIntValidator(0, 20000, this);
    m_widthEdit = new QLineEdit(right);
    m_widthEdit->setObjectName(QStringLiteral("sizeInput"));
    m_widthEdit->setValidator(sizeValidator);
    m_widthEdit->setPlaceholderText(tr("原始"));
    m_heightEdit = new QLineEdit(right);
    m_heightEdit->setObjectName(QStringLiteral("sizeInput"));
    m_heightEdit->setValidator(sizeValidator);
    m_heightEdit->setPlaceholderText(tr("原始"));
    auto *times = new QLabel(QStringLiteral("\u00d7"), right);
    times->setObjectName(QStringLiteral("unitHint"));
    m_keepRatio = new QPushButton(tr("保持比例"), right);
    m_keepRatio->setObjectName(QStringLiteral("keepRatio"));
    m_keepRatio->setCheckable(true);
    m_keepRatio->setChecked(true);
    m_keepRatio->setCursor(Qt::PointingHandCursor);
    m_keepRatio->setFixedHeight(32);
    sizeLay->addWidget(m_widthEdit, 1);
    sizeLay->addWidget(times);
    sizeLay->addWidget(m_heightEdit, 1);
    sizeLay->addWidget(m_keepRatio);
    connect(m_widthEdit, &QLineEdit::textEdited, this, [this]() {
        if (m_keepRatio->isChecked() && m_srcW > 0 && m_srcH > 0) {
            const int w = m_widthEdit->text().toInt();
            if (w > 0) {
                const QSignalBlocker block(m_heightEdit);
                m_heightEdit->setText(QString::number(qMax(1, qRound(double(w) * m_srcH / m_srcW))));
            }
        }
    });
    connect(m_heightEdit, &QLineEdit::textEdited, this, [this]() {
        if (m_keepRatio->isChecked() && m_srcW > 0 && m_srcH > 0) {
            const int h = m_heightEdit->text().toInt();
            if (h > 0) {
                const QSignalBlocker block(m_widthEdit);
                m_widthEdit->setText(QString::number(qMax(1, qRound(double(h) * m_srcW / m_srcH))));
            }
        }
    });
    addRow(tr("裁剪 / 新尺寸"), sizeLay);

    // 3) 格式设定：格式 + 品质 + 百分比
    auto *fmtLay = new QHBoxLayout;
    fmtLay->setContentsMargins(0, 0, 0, 0);
    fmtLay->setSpacing(8);
    auto *fmtLabel = new QLabel(tr("格式"), right);
    fmtLabel->setObjectName(QStringLiteral("rowLabel"));
    fmtLabel->setFixedWidth(52);
    m_format = new QComboBox(right);
    m_format->setObjectName(QStringLiteral("formatSelect"));
    m_format->setFixedWidth(160);
    m_format->addItem(QStringLiteral("PNG"), QStringLiteral("png"));
    m_format->addItem(QStringLiteral("JPEG"), QStringLiteral("jpg"));
    m_format->addItem(QStringLiteral("BMP"), QStringLiteral("bmp"));
    m_format->addItem(QStringLiteral("WebP"), QStringLiteral("webp"));
    auto *qLabel = new QLabel(tr("品质"), right);
    qLabel->setObjectName(QStringLiteral("rowLabel"));
    qLabel->setFixedWidth(52);
    qLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_quality = new QSlider(Qt::Horizontal, right);
    m_quality->setObjectName(QStringLiteral("qualitySlider"));
    m_quality->setRange(50, 100);
    m_quality->setValue(90);
    m_quality->setFixedWidth(110);
    m_qualityValue = new QLabel(QStringLiteral("90%"), right);
    m_qualityValue->setObjectName(QStringLiteral("qualityValue"));
    m_qualityValue->setFixedWidth(32);
    fmtLay->addWidget(fmtLabel);
    fmtLay->addWidget(m_format);
    fmtLay->addSpacing(12);
    fmtLay->addWidget(qLabel);
    fmtLay->addWidget(m_quality);
    fmtLay->addWidget(m_qualityValue);
    fmtLay->addStretch(1);
    connect(m_format, &QComboBox::currentIndexChanged, this, &BatchConvertDialog::onFormatChanged);
    connect(m_quality, &QSlider::valueChanged, this, &BatchConvertDialog::onQualityChanged);
    addRow(tr("格式设定"), fmtLay);

    // 4) 输出文件夹
    auto *targetLay = new QVBoxLayout;
    targetLay->setContentsMargins(0, 0, 0, 0);
    targetLay->setSpacing(8);
    auto *dirGroup = new QButtonGroup(this);
    auto *radioLay = new QHBoxLayout;
    radioLay->setContentsMargins(0, 0, 0, 0);
    radioLay->setSpacing(14);
    m_dirOriginal = new QRadioButton(tr("保存到原文件夹"), right);
    m_dirPicture = new QRadioButton(tr("保存到图片文件夹"), right);
    m_dirCustom = new QRadioButton(tr("保存到："), right);
    for (QRadioButton *r : {m_dirOriginal, m_dirPicture, m_dirCustom}) {
        r->setObjectName(QStringLiteral("radio"));
        r->setCursor(Qt::PointingHandCursor);
        dirGroup->addButton(r);
        radioLay->addWidget(r);
        connect(r, &QRadioButton::toggled, this, &BatchConvertDialog::onTargetChanged);
    }
    m_dirCustom->setChecked(true);
    radioLay->addStretch(1);
    targetLay->addLayout(radioLay);

    auto *pathLay = new QHBoxLayout;
    pathLay->setContentsMargins(0, 0, 0, 0);
    pathLay->setSpacing(8);
    m_outDir = new QLineEdit(right);
    m_outDir->setFixedHeight(32);
    const QDir base = m_files.isEmpty() ? QDir::home() : QFileInfo(m_files.first()).dir();
    m_outDir->setText(QDir::toNativeSeparators(base.absoluteFilePath(QStringLiteral("converted"))));
    m_outDir->setPlaceholderText(tr("选择输出文件夹"));
    m_browseBtn = new QPushButton(tr("浏览…"), right);
    m_browseBtn->setFixedHeight(32);
    m_browseBtn->setCursor(Qt::PointingHandCursor);
    connect(m_browseBtn, &QPushButton::clicked, this, &BatchConvertDialog::onBrowse);
    pathLay->addWidget(m_outDir, 1);
    pathLay->addWidget(m_browseBtn);
    targetLay->addLayout(pathLay);
    addRow(tr("输出文件夹"), targetLay);

    // 5) 文件名前缀
    auto *prefixLay = new QVBoxLayout;
    prefixLay->setContentsMargins(0, 0, 0, 0);
    prefixLay->setSpacing(8);
    m_prefix = new QLineEdit(QStringLiteral("converted_"), right);
    m_prefix->setFixedHeight(32);
    prefixLay->addWidget(m_prefix);
    m_namePreview = new QLabel(right);
    m_namePreview->setObjectName(QStringLiteral("namePreview"));
    m_namePreview->setFixedHeight(28);
    prefixLay->addWidget(m_namePreview);
    connect(m_prefix, &QLineEdit::textChanged, this, &BatchConvertDialog::updateNamePreview);
    addRow(tr("文件名前缀"), prefixLay);
    rv->addStretch(1);
    columns->addWidget(right, 1);

    bodyLay->addLayout(columns);

    // —— 进度区（转换时才显示）——
    m_progressArea = new QWidget(body);
    auto *pv = new QVBoxLayout(m_progressArea);
    pv->setContentsMargins(0, 15, 0, 0);
    pv->setSpacing(8);
    m_progress = new QProgressBar(m_progressArea);
    m_progress->setObjectName(QStringLiteral("convertProgress"));
    m_progress->setFixedHeight(5);
    m_progress->setTextVisible(false);
    m_progress->setRange(0, qMax(1, m_files.size()));
    auto *pl = new QHBoxLayout;
    pl->setContentsMargins(0, 0, 0, 0);
    pl->setSpacing(0);
    m_progressText = new QLabel(tr("正在转换…"), m_progressArea);
    m_progressText->setObjectName(QStringLiteral("progressText"));
    m_progressCount = new QLabel(QStringLiteral("0 / %1").arg(m_files.size()), m_progressArea);
    m_progressCount->setObjectName(QStringLiteral("progressCount"));
    pl->addWidget(m_progressText);
    pl->addStretch(1);
    pl->addWidget(m_progressCount);
    pv->addWidget(m_progress);
    pv->addLayout(pl);
    m_progressArea->setVisible(false);
    bodyLay->addWidget(m_progressArea);

    root->addWidget(body);

    // —— 底部 62px ——
    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("dlgFooter"));
    footer->setFixedHeight(UiStyle::FooterH);
    auto *fl = new QHBoxLayout(footer);
    fl->setContentsMargins(18, 0, 18, 0);
    fl->setSpacing(9);
    auto *resetBtn = new QPushButton(tr("重置"), footer);
    resetBtn->setObjectName(QStringLiteral("btnSubtle"));
    resetBtn->setFixedHeight(32);
    resetBtn->setCursor(Qt::PointingHandCursor);
    connect(resetBtn, &QPushButton::clicked, this, &BatchConvertDialog::onReset);
    fl->addWidget(resetBtn);
    auto *note = new QLabel(tr("原始文件会保留不变"), footer);
    note->setObjectName(QStringLiteral("footerNote"));
    fl->addWidget(note);
    fl->addStretch(1);

    auto *closeFooterBtn = new QPushButton(tr("关闭"), footer);
    closeFooterBtn->setFixedHeight(32);
    closeFooterBtn->setCursor(Qt::PointingHandCursor);
    connect(closeFooterBtn, &QPushButton::clicked, this, &QDialog::reject);
    fl->addWidget(closeFooterBtn);

    m_openFolderBtn = new QPushButton(tr("打开文件夹"), footer);
    m_openFolderBtn->setFixedHeight(32);
    m_openFolderBtn->setCursor(Qt::PointingHandCursor);
    connect(m_openFolderBtn, &QPushButton::clicked, this, [this]() {
        QString dir = m_lastOutDir;
        if (dir.isEmpty() && !m_files.isEmpty()) {
            dir = QFileInfo(m_files.first()).absolutePath();
        }
        if (!dir.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        }
    });
    fl->addWidget(m_openFolderBtn);

    m_convertBtn = new QPushButton(tr("开始"), footer);
    m_convertBtn->setObjectName(QStringLiteral("btnPrimary"));
    m_convertBtn->setFixedHeight(32);
    m_convertBtn->setMinimumWidth(86);
    m_convertBtn->setCursor(Qt::PointingHandCursor);
    connect(m_convertBtn, &QPushButton::clicked, this, &BatchConvertDialog::onConvert);
    fl->addWidget(m_convertBtn);

    root->addWidget(footer);

    setFixedWidth(kDlgW);
}

// ---------------------------------------------------------------------------
// 首图尺寸 / 预览 / 文件名示例
// ---------------------------------------------------------------------------
void BatchConvertDialog::loadSourceInfo()
{
    m_srcW = 0;
    m_srcH = 0;
    if (!m_files.isEmpty()) {
        QImageReader reader(m_files.first());
        const QSize sz = reader.size();
        if (sz.isValid()) {
            m_srcW = sz.width();
            m_srcH = sz.height();
        }
    }
    m_widthEdit->setText(m_srcW > 0 ? QString::number(m_srcW) : QString());
    m_heightEdit->setText(m_srcH > 0 ? QString::number(m_srcH) : QString());

    refreshPreview();
    updateQualityEnabled();
    updateNamePreview();
}

void BatchConvertDialog::refreshPreview()
{
    const int row = m_fileList->currentRow();
    const QString path = m_files.value(row);
    if (path.isEmpty()) {
        m_preview->setImage(QImage());
        return;
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize orig = reader.size();
    if (orig.isValid()) {
        reader.setScaledSize(orig.scaled(QSize(kLeftW, kPreviewH), Qt::KeepAspectRatioByExpanding));
    }
    const QImage img = reader.read();
    if (img.isNull()) {
        m_preview->setNote(tr("无法预览"));
        return;
    }
    m_preview->setImage(img);
}

void BatchConvertDialog::onFileChanged(int row)
{
    Q_UNUSED(row)
    // 尺寸框是「统一目标尺寸」，不随选中文件改写
    refreshPreview();
    updateNamePreview();
}

void BatchConvertDialog::updateNamePreview()
{
    const int row = m_fileList->currentRow();
    QString stem = QFileInfo(m_files.value(row >= 0 ? row : 0)).completeBaseName();
    if (stem.isEmpty()) {
        stem = QStringLiteral("image");
    }
    m_namePreview->setText(m_prefix->text() + stem + QLatin1Char('.') + currentExtension());
}

void BatchConvertDialog::updateQualityEnabled()
{
    const QString ext = currentExtension();
    const bool lossy = (ext == QLatin1String("jpg") || ext == QLatin1String("webp"));
    m_quality->setEnabled(lossy);
    m_qualityValue->setEnabled(lossy);
}

QString BatchConvertDialog::currentExtension() const
{
    return m_format ? m_format->currentData().toString() : QStringLiteral("png");
}

void BatchConvertDialog::onFormatChanged()
{
    updateQualityEnabled();
    updateNamePreview();
}

void BatchConvertDialog::onQualityChanged(int value)
{
    m_qualityValue->setText(QStringLiteral("%1%").arg(value));
}

void BatchConvertDialog::onTargetChanged()
{
    if (!m_outDir || !m_browseBtn) {
        return;   // 构建期 setChecked 触发的早期信号
    }
    const bool custom = m_dirCustom->isChecked();
    m_outDir->setEnabled(custom);
    m_browseBtn->setEnabled(custom);
}

// ---------------------------------------------------------------------------
// 交互
// ---------------------------------------------------------------------------
void BatchConvertDialog::onBrowse()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("选择输出文件夹"), m_outDir->text());
    if (!dir.isEmpty()) {
        m_outDir->setText(QDir::toNativeSeparators(dir));
        m_dirCustom->setChecked(true);
    }
}

void BatchConvertDialog::onReset()
{
    m_rot[0]->setChecked(true);
    m_keepRatio->setChecked(true);
    m_format->setCurrentIndex(0);
    m_quality->setValue(90);
    m_prefix->setText(QStringLiteral("converted_"));
    m_dirCustom->setChecked(true);
    const QDir base = m_files.isEmpty() ? QDir::home() : QFileInfo(m_files.first()).dir();
    m_outDir->setText(QDir::toNativeSeparators(base.absoluteFilePath(QStringLiteral("converted"))));
    loadSourceInfo();
}

QString BatchConvertDialog::resolveOutputDir(const QString &sourceFile) const
{
    const QString srcDir = QFileInfo(sourceFile).absolutePath();
    if (m_dirOriginal->isChecked()) {
        return srcDir;
    }
    if (m_dirPicture->isChecked()) {
        const QString pic = settingsGet("etc/photo_folder_1");
        return pic.isEmpty() ? srcDir : QDir::toNativeSeparators(pic);
    }
    const QString custom = m_outDir->text().trimmed();
    return custom.isEmpty() ? srcDir : custom;
}

void BatchConvertDialog::onConvert()
{
    if (m_files.isEmpty()) {
        QMessageBox::information(this, tr("图片转换"), tr("没有可转换的图片。"));
        return;
    }

    // —— 锁定参数 ——
    int rotateIndex = 0;
    for (int i = 0; i < 5; ++i) {
        if (m_rot[i]->isChecked()) {
            rotateIndex = i;
            break;
        }
    }
    const QString ext = currentExtension();
    const int quality = (ext == QLatin1String("jpg") || ext == QLatin1String("webp"))
        ? m_quality->value() : -1;
    const bool keepRatio = m_keepRatio->isChecked();
    int targetW = m_widthEdit->text().toInt();
    int targetH = m_heightEdit->text().toInt();
    if (targetW > 0 && targetH <= 0 && keepRatio && m_srcW > 0) {
        targetH = qMax(1, qRound(double(targetW) * m_srcH / m_srcW));
    }
    if (targetH > 0 && targetW <= 0 && keepRatio && m_srcH > 0) {
        targetW = qMax(1, qRound(double(targetH) * m_srcW / m_srcH));
    }
    const bool needScale = targetW > 0 || targetH > 0;
    const QString prefix = m_prefix->text();

    m_progressArea->setVisible(true);
    m_progress->setRange(0, m_files.size());
    m_progress->setValue(0);
    m_progressText->setText(tr("正在转换…"));
    m_convertBtn->setEnabled(false);
    adjustSize();
    QApplication::processEvents();

    int ok = 0;
    int failed = 0;
    for (int i = 0; i < m_files.size(); ++i) {
        const QString src = m_files.at(i);
        m_progressCount->setText(QStringLiteral("%1 / %2").arg(i + 1).arg(m_files.size()));

        QImageReader reader(src);
        reader.setAutoTransform(rotateIndex == 4);
        QImage img = reader.read();
        if (img.isNull()) {
            ++failed;
            m_progress->setValue(i + 1);
            QApplication::processEvents();
            continue;
        }

        // 旋转
        if (rotateIndex >= 1 && rotateIndex <= 3) {
            QTransform tf;
            tf.rotate(rotateIndex == 1 ? -90.0 : (rotateIndex == 2 ? 90.0 : 180.0));
            img = img.transformed(tf, Qt::SmoothTransformation);
        }

        // 缩放：保持比例时按宽（或唯一给出的那一维）计算
        if (needScale) {
            QSize target(targetW, targetH);
            if (keepRatio) {
                if (targetW > 0 && targetH > 0) {
                    target = img.size().scaled(targetW, targetH, Qt::KeepAspectRatio);
                } else if (targetW > 0) {
                    target.setWidth(targetW);
                    target.setHeight(qMax(1, qRound(double(targetW) * img.height() / img.width())));
                } else {
                    target.setHeight(targetH);
                    target.setWidth(qMax(1, qRound(double(targetH) * img.width() / img.height())));
                }
            } else {
                if (targetW <= 0) target.setWidth(img.width());
                if (targetH <= 0) target.setHeight(img.height());
            }
            if (target != img.size()) {
                img = img.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
        }

        // 输出路径
        QString outDir = resolveOutputDir(src);
        QDir().mkpath(outDir);
        QString outPath = QDir(outDir).absoluteFilePath(
            prefix + QFileInfo(src).completeBaseName() + QLatin1Char('.') + ext);
        // 不覆盖原始文件：同名同路径时补后缀
        if (QFileInfo(outPath).absoluteFilePath() == QFileInfo(src).absoluteFilePath()) {
            outPath = QDir(outDir).absoluteFilePath(
                prefix + QFileInfo(src).completeBaseName() + QStringLiteral("_converted.")
                + ext);
        }
        if (img.save(outPath, ext.toLatin1().constData(), quality)) {
            ++ok;
            m_lastOutDir = outDir;
        } else {
            ++failed;
        }
        m_progress->setValue(i + 1);
        QApplication::processEvents();
    }

    m_progress->setValue(m_files.size());
    m_progressText->setText(tr("转换完成"));
    m_convertBtn->setEnabled(true);

    QMessageBox::information(this, tr("图片转换"),
                             tr("成功 %1 / 共 %2 张%3")
                                 .arg(ok)
                                 .arg(m_files.size())
                                 .arg(failed > 0 ? tr("（失败 %1 张）").arg(failed) : QString()));
}

// ---------------------------------------------------------------------------
// 无边框窗口
// ---------------------------------------------------------------------------
void BatchConvertDialog::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(UiStyle::color(UiStyle::Border), 1));
    p.setBrush(UiStyle::color(UiStyle::Panel));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                      UiStyle::RadiusDialog, UiStyle::RadiusDialog);
}

bool BatchConvertDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched && watched->objectName() == QLatin1String("dlgTitleBar")) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_dragOffset = me->globalPosition().toPoint() - frameGeometry().topLeft();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->buttons() & Qt::LeftButton) {
                move(me->globalPosition().toPoint() - m_dragOffset);
                return true;
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}
