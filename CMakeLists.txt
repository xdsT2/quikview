cmake_minimum_required(VERSION 3.16)
project(quikview VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent PrintSupport LinguistTools)

# M1：RAW 解码用 libraw（pkg-config，MSYS2 自带）
# HEIC/AVIF 解码用 libheif（pkg-config，MSYS2 自带）
# EXIF：自研极简解析器（ExifParser），零依赖、MIT-clean（不用 GPL 的 libexiv2）
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBRAW QUIET IMPORTED_TARGET libraw)
    pkg_check_modules(LIBHEIF QUIET IMPORTED_TARGET libheif)
endif()

add_executable(quikview WIN32
    src/main.cpp
    src/Config.cpp
    src/Config.h
    src/ImageCanvas.cpp
    src/ImageCanvas.h
    src/ImageCache.cpp
    src/ImageCache.h
    src/DirectoryLoader.cpp
    src/DirectoryLoader.h
    src/ImageEffects.cpp
    src/ImageEffects.h
    src/RawImageLoader.cpp
    src/RawImageLoader.h
    src/HeifLoader.cpp
    src/HeifLoader.h
    src/MetadataPanel.cpp
    src/MetadataPanel.h
    src/ExifParser.cpp
    src/ExifParser.h
    src/Wallpaper.cpp
    src/Wallpaper.h
    src/SettingsDialog.cpp
    src/SettingsDialog.h
    src/Chrome.cpp
    src/Chrome.h
    src/BatchConvertDialog.cpp
    src/BatchConvertDialog.h
    src/ThumbnailBrowser.cpp
    src/ThumbnailBrowser.h
    src/FrameNavigator.cpp
    src/FrameNavigator.h
    src/Theme.h
    src/UiStyle.h
    src/MainWindow.cpp
    src/MainWindow.h
    resources/ui.qrc
)

target_include_directories(quikview PRIVATE src)

# 翻译：.ts → .qm，嵌入资源 :/i18n
qt_add_translations(quikview
    TS_FILES translations/quikview_en_US.ts translations/quikview_ko_KR.ts translations/quikview_ja_JP.ts translations/quikview_fr_FR.ts translations/quikview_de_DE.ts translations/quikview_es_ES.ts translations/quikview_ru_RU.ts translations/quikview_zh_TW.ts
)

target_link_libraries(quikview PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::PrintSupport
    dwmapi
)

if(LIBRAW_FOUND)
    target_link_libraries(quikview PRIVATE PkgConfig::LIBRAW)
    target_compile_definitions(quikview PRIVATE HAS_LIBRAW=1)
endif()

if(LIBHEIF_FOUND)
    target_link_libraries(quikview PRIVATE PkgConfig::LIBHEIF)
    target_compile_definitions(quikview PRIVATE HAS_LIBHEIF=1)
endif()

# Windows 需要子系统 GUI；Qt 应用建议加 high-DPI 属性
if(WIN32)
    set_target_properties(quikview PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    # WM_NCCALCSIZE 最大化修正用到 GetDpiForWindow / GetSystemMetricsForDpi（Win10 1607+）
    target_compile_definitions(quikview PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()
