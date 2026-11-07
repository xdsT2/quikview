cmake_minimum_required(VERSION 3.16)
project(quikview VERSION 0.4.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent PrintSupport Network LinguistTools)

# 图标资源脚本（win32）：exe 里必须有 RT_GROUP_ICON，否则安装后快捷方式/任务栏全白板
if(WIN32)
    enable_language(RC)
endif()

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
    src/ToneMap.cpp
    src/ToneMap.h
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
    src/PillToggle.cpp
    src/PillToggle.h
    src/Chrome.cpp
    src/Chrome.h
    src/BatchConvertDialog.cpp
    src/BatchConvertDialog.h
    src/GammaDialog.cpp
    src/GammaDialog.h
    src/ThumbnailBrowser.cpp
    src/ThumbnailBrowser.h
    src/FrameNavigator.cpp
    src/FrameNavigator.h
    src/Theme.h
    src/UiStyle.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/UpdateChecker.cpp
    src/UpdateChecker.h
    resources/ui.qrc
)

target_include_directories(quikview PRIVATE src)

# 版本号单一来源：CMakeLists 的 project(VERSION ...)。
# main.cpp 的 QApplication::setApplicationVersion 与「关于」对话框都读这个宏，
# 别再各写各的字面量。
target_compile_definitions(quikview PRIVATE QUIKVIEW_VERSION="${PROJECT_VERSION}")

# 数字版本（四段纯数字）只给安装包与 PE 版本资源用：VIProductVersion / FILEVERSION
# 不接受 "0.4.0" 这种三段式，也不接受 -rc1 后缀。project(VERSION) 永远纯数字，
# 预发布后缀只允许出现在 git tag 与展示名里。
set(QUIKVIEW_VERSION_RC
    "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.0")
message(STATUS "QuikView version ${PROJECT_VERSION} (installed as ${QUIKVIEW_VERSION_RC})")

# 翻译：.ts → .qm，嵌入资源 :/i18n
qt_add_translations(quikview
    TS_FILES translations/quikview_en_US.ts translations/quikview_ko_KR.ts translations/quikview_ja_JP.ts translations/quikview_fr_FR.ts translations/quikview_de_DE.ts translations/quikview_es_ES.ts translations/quikview_ru_RU.ts translations/quikview_zh_TW.ts
)

target_link_libraries(quikview PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::PrintSupport
    Qt6::Network
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
    # 把图标编进 PE：ExtractIconW / 资源管理器 / 快捷方式默认图标都靠它
    target_sources(quikview PRIVATE resources/app.rc)
    # app.rc 的 VERSIONINFO 读这里生成的版本头；同样由 project(VERSION) 派生
    configure_file(resources/qv_version.h.in "${CMAKE_CURRENT_BINARY_DIR}/qv_version.h" @ONLY)
    set_source_files_properties(resources/app.rc PROPERTIES
        COMPILE_OPTIONS "--include-dir=${CMAKE_CURRENT_SOURCE_DIR}/resources;--include-dir=${CMAKE_CURRENT_BINARY_DIR}"
        OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/qv_version.h"
    )
    # WM_NCCALCSIZE 最大化修正用到 GetDpiForWindow / GetSystemMetricsForDpi（Win10 1607+）
    target_compile_definitions(quikview PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    # 翻页音效（pageturn_wav）走 winmm PlaySound（MainWindow.cpp 运行时合成 WAV）
    target_link_libraries(quikview PRIVATE winmm)
endif()
