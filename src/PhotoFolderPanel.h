#pragma once

#include "DialogShell.h"

#include <QStringList>

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

// 照片文件夹快捷面板（H1-1，Alt+P 开关）：
//   - 管理 1–20 个目标文件夹的路径与顺序（与 config/etc/photo_folder_* 双向同步）；
//   - 每行支持浏览、上移/下移、删除，以及一键「复制 / 移动 / 打开」。
class PhotoFolderPanel : public UiShell::DialogShell
{
    Q_OBJECT
public:
    explicit PhotoFolderPanel(QWidget *parent = nullptr);

    void reloadFromConfig();
    void refreshThemeStyles();

signals:
    void copyRequested(int index1Based);
    void moveRequested(int index1Based);
    void openRequested(int index1Based);
    void foldersChanged();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void rebuildRows();
    void saveFolders();
    void addFolderRow();
    void removeFolderRow(int row);
    void moveFolderRow(int row, int delta);

    QStringList m_folders;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_listHost = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    QPushButton *m_addBtn = nullptr;
    QLabel *m_countLabel = nullptr;
};
