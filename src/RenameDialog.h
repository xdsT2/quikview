#pragma once

#include "BatchRename.h"
#include "DialogShell.h"

class QCheckBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QLineEdit;
class QTableWidget;

// 文件 / 批量重命名对话框（A1，F6 触发）：
//   - 模式切换：仅当前文件 / 当前目录全部图片；
//   - 模板输入 + 变量快捷插入按钮（{name}/{index:03}/{date}/{time}/{ext}）；
//   - 实时预览表逐行展示原名、新名与冲突原因；存在冲突时整批禁止执行。
class RenameDialog : public UiShell::DialogShell
{
    Q_OBJECT
public:
    explicit RenameDialog(const QString &currentFile,
                          const QStringList &dirFiles,
                          QWidget *parent = nullptr);

    const BatchRename::ExecuteResult &result() const { return m_result; }

    // 冒烟取证：预置模板以演示正常预览或冲突拦截画面。
    void probeSetPattern(const QString &pattern, bool batchMode);

private slots:
    void onModeChanged();
    void onPatternChanged();
    void onExecute();

private:
    void insertToken(const QString &token);
    void rebuildPreview();

    QString m_currentFile;
    QStringList m_dirFiles;
    QRadioButton *m_singleRadio = nullptr;
    QRadioButton *m_batchRadio = nullptr;
    QLineEdit *m_patternEdit = nullptr;
    QSpinBox *m_startIndex = nullptr;
    QCheckBox *m_keepExt = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_execBtn = nullptr;
    bool m_userEditedPattern = false;
    BatchRename::Plan m_plan;
    BatchRename::ExecuteResult m_result;
};
