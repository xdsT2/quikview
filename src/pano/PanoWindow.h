#pragma once

#include <QWidget>

class PanoGlobe;
class QLabel;
class QPushButton;

// 全景查看器窗口（独立进程）：只负责展示一张球面图并接受拖动 / 缩放 / 重置，
// 不读 SoleView 配置、不写任何文件；关闭即退出，主程序的原图与缩放不受影响。
class PanoWindow : public QWidget
{
    Q_OBJECT
public:
    explicit PanoWindow(const QString &imagePath, double heading, double pitch, double fov,
                        QWidget *parent = nullptr, float exposureEv = 0.0f);

    bool ok() const { return m_ok; }
    PanoGlobe *globe() const { return m_globe; }

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void placeHud();
    void syncHud();

private:
    PanoGlobe *m_globe = nullptr;
    QWidget *m_hud = nullptr;
    QPushButton *m_resetBtn = nullptr;
    QLabel *m_fovLabel = nullptr;
    bool m_ok = false;
};
