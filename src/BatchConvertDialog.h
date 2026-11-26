#pragma once

#include "DialogShell.h"

#include <QPoint>
#include <QStringList>

class ConvertPreview;
class QCloseEvent;
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
// 外壳（无边框圆角 / 主题底与描边 / 标题栏拖拽）沿用 UiShell::DialogShell。
class BatchConvertDialog : public UiShell::DialogShell
{
    Q_OBJECT
public:
    explicit BatchConvertDialog(const QStringList &files, QWidget *parent = nullptr);

protected:
    void reject() override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onBrowse();
    void onConvertClicked();
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
    bool m_running = false;         // 转换循环进行中：开始按钮临时充当取消
    bool m_cancelRequested = false;

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

    QPushButton *m_resetBtn = nullptr;
    QPushButton *m_convertBtn = nullptr;
    QPushButton *m_openFolderBtn = nullptr;
};
