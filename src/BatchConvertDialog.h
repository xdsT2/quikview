#pragma once

#include <QDialog>
#include <QPoint>
#include <QStringList>

class ConvertPreview;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSlider;

// 图片转换：批量旋转 / 缩放 / 转格式 / 输出到指定文件夹。
// 视觉对照 Temp/quikview-secondary-prototype.html 的 .dialog.convert（780 宽两栏）。
class BatchConvertDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BatchConvertDialog(const QStringList &files, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onBrowse();
    void onConvert();
    void onReset();
    void onFormatChanged();
    void onQualityChanged(int value);
    void onFileChanged(int row);
    void onTargetChanged();

private:
    void buildUi();
    void loadSourceInfo();
    void refreshPreview();
    void updateNamePreview();
    void updateQualityEnabled();
    QString resolveOutputDir(const QString &sourceFile) const;
    QString currentExtension() const;

    QStringList m_files;
    int m_srcW = 0;
    int m_srcH = 0;
    QString m_lastOutDir;
    QPoint m_dragOffset;

    QLabel *m_fileCaption = nullptr;
    QListWidget *m_fileList = nullptr;
    ConvertPreview *m_preview = nullptr;

    QRadioButton *m_rot[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    QLineEdit *m_widthEdit = nullptr;
    QLineEdit *m_heightEdit = nullptr;
    QPushButton *m_keepRatio = nullptr;

    QComboBox *m_format = nullptr;
    QSlider *m_quality = nullptr;
    QLabel *m_qualityValue = nullptr;

    QRadioButton *m_dirOriginal = nullptr;
    QRadioButton *m_dirPicture = nullptr;
    QRadioButton *m_dirCustom = nullptr;
    QLineEdit *m_outDir = nullptr;
    QPushButton *m_browseBtn = nullptr;

    QLineEdit *m_prefix = nullptr;
    QLabel *m_namePreview = nullptr;

    QWidget *m_progressArea = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_progressText = nullptr;
    QLabel *m_progressCount = nullptr;

    QPushButton *m_convertBtn = nullptr;
    QPushButton *m_openFolderBtn = nullptr;
};
