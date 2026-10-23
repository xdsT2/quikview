#pragma once

#include <QDialog>

class QComboBox;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QLabel;

// 批量格式转换：把当前目录所有图片转成目标格式（可指定输出目录）。
class BatchConvertDialog : public QDialog
{
    Q_OBJECT
public:
    BatchConvertDialog(const QStringList &files, QWidget *parent = nullptr);

private slots:
    void onBrowse();
    void onConvert();

private:
    QStringList m_files;
    QComboBox *m_format = nullptr;
    QLineEdit *m_outDir = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_result = nullptr;
    QPushButton *m_convertBtn = nullptr;
};
