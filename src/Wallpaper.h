#pragma once

#include <QString>

// 设为桌面壁纸（仅 Windows）。返回是否成功。
bool setDesktopWallpaper(const QString &imagePath, QString *errorOut = nullptr);
