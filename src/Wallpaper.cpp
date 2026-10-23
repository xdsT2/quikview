#include "Wallpaper.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

bool setDesktopWallpaper(const QString &imagePath, QString *errorOut)
{
#ifdef Q_OS_WIN
    // SystemParametersInfoW 要求路径为绝对路径（BMP 最稳，JPG/PNG 视系统支持）
    const int ok = SystemParametersInfoW(
        SPI_SETDESKWALLPAPER, 0,
        reinterpret_cast<void *>(const_cast<wchar_t *>(reinterpret_cast<const wchar_t *>(imagePath.utf16()))),
        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    if (!ok && errorOut) {
        *errorOut = QStringLiteral("SystemParametersInfo 失败 (GetLastError=%1)").arg(GetLastError());
    }
    return ok != 0;
#else
    Q_UNUSED(imagePath);
    if (errorOut) *errorOut = QStringLiteral("设壁纸仅支持 Windows");
    return false;
#endif
}
