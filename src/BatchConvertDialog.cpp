#include "BatchConvertDialog.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

BatchConvertDialog::BatchConvertDialog(const QStringList &files, QWidget *parent)
    : QDialog(parent)
    , m_files(files)
{
    setWindowTitle(tr("批量转换"));
    setMinimumWidth(420);

    auto *form = new QFormLayout;

    m_format = new QComboBox(this);
    m_format->addItem(QStringLiteral("PNG"), QStringLiteral("png"));
    m_format->addItem(QStringLiteral("JPEG"), QStringLiteral("jpg"));
    m_format->addItem(QStringLiteral("BMP"), QStringLiteral("bmp"));
    m_format->addItem(QStringLiteral("WebP"), QStringLiteral("webp"));
    form->addRow(tr("目标格式"), m_format);

    auto *dirRow = new QHBoxLayout;
    m_outDir = new QLineEdit(this);
    const QDir base = files.isEmpty() ? QDir::home() : QFileInfo(files.first()).dir();
    m_outDir->setText(base.absoluteFilePath(QStringLiteral("converted")));
    auto *browse = new QPushButton(tr("浏览…"), this);
    connect(browse, &QPushButton::clicked, this, &BatchConvertDialog::onBrowse);
    dirRow->addWidget(m_outDir);
    dirRow->addWidget(browse);
    form->addRow(tr("输出目录"), dirRow);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, m_files.size());
    form->addRow(QString(), m_progress);

    m_result = new QLabel(tr("共 %1 张图片").arg(m_files.size()), this);
    form->addRow(QString(), m_result);

    m_convertBtn = new QPushButton(tr("开始转换"), this);
    connect(m_convertBtn, &QPushButton::clicked, this, &BatchConvertDialog::onConvert);
    auto *closeBtn = new QPushButton(tr("关闭"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    auto *btns = new QHBoxLayout;
    btns->addWidget(m_convertBtn);
    btns->addWidget(closeBtn);

    auto *vbox = new QVBoxLayout(this);
    vbox->addLayout(form);
    vbox->addLayout(btns);
}

void BatchConvertDialog::onBrowse()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("选择输出目录"), m_outDir->text());
    if (!dir.isEmpty()) {
        m_outDir->setText(dir);
    }
}

void BatchConvertDialog::onConvert()
{
    const QString fmt = m_format->currentData().toString();
    const QString outDir = m_outDir->text();
    QDir().mkpath(outDir);

    m_convertBtn->setEnabled(false);
    int ok = 0;
    for (int i = 0; i < m_files.size(); ++i) {
        m_progress->setValue(i);
        QImageReader reader(m_files.at(i));
        reader.setAutoTransform(true);
        const QImage img = reader.read();
        if (img.isNull()) {
            continue;
        }
        const QString out = outDir + QLatin1Char('/')
            + QFileInfo(m_files.at(i)).completeBaseName() + QLatin1Char('.') + fmt;
        if (img.save(out, fmt.toLatin1().constData())) {
            ++ok;
        }
    }
    m_progress->setValue(m_files.size());
    m_result->setText(tr("完成：成功 %1 / 共 %2").arg(ok).arg(m_files.size()));
    m_convertBtn->setEnabled(true);
}
