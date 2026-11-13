cmake_minimum_required(VERSION 3.16)
project(quikview VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent PrintSupport Network LinguistTools)

# PSD 图层解析：MolecularMatters/psd_sdk（BSD-2-Clause，与本项目 MIT 兼容）。
# 自带 miniz，覆盖 RAW/RLE/ZIP/ZIP+Prediction、8/16/32bit、分组/蒙版/智能对象。
# 只用它做二进制解析，渲染与可见性合成仍由本项目负责。
add_subdirectory(third_party/psd_sdk)

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
    src/PsdLoader.cpp
    src/PsdLoader.h
    src/LayerPanel.cpp
    src/LayerPanel.h
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

target_include_directories(quikview PRIVATE third_party/psd_sdk)

target_link_libraries(quikview PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::PrintSupport
    Qt6::Network
    Psd
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

    # 开发构建也必须可直接运行：把 Qt 图像格式插件稳定部署到 exe 同级目录。
    # 发行 staging 仍由 tools/package.sh 负责完整依赖闭包；这里明确排除 qjp2，
    # 保持 jasper 崩溃规避策略一致。
    #
    # 位置必须是 <exe>/imageformats：Qt 6 在 Windows 上从 applicationDirPath
    # 直接找分类子目录（windeployqt 产出的也是这个布局）。放到
    # <exe>/plugins/imageformats 不会被加载，表现为「目录建立了但图像不上屏」。
    #
    # 来源分两处：官方 Qt 只带 jpeg/gif/ico，其余格式从同版本（6.11.2）MSYS2
    # Qt6 插件目录补齐 —— 两者 ABI 相同，实测可加载。
    get_filename_component(QUIKVIEW_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    get_filename_component(QUIKVIEW_COMPILER_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(QUIKVIEW_COMPILER_PREFIX "${QUIKVIEW_COMPILER_BIN}/.." ABSOLUTE)
    set(QUIKVIEW_IMAGEFORMAT_DIRS
        "${QUIKVIEW_QT_PREFIX}/plugins/imageformats"
        "${QUIKVIEW_QT_PREFIX}/share/qt6/plugins/imageformats"
        "${QUIKVIEW_COMPILER_PREFIX}/share/qt6/plugins/imageformats")
    set(QUIKVIEW_IMAGEFORMAT_PLUGINS
        qjpeg.dll qgif.dll qico.dll qwebp.dll qtiff.dll
        qicns.dll qmng.dll qtga.dll qwbmp.dll)
    foreach(_plugin IN LISTS QUIKVIEW_IMAGEFORMAT_PLUGINS)
        unset(_plugin_source CACHE)
        find_file(_plugin_source NAMES "${_plugin}"
            PATHS ${QUIKVIEW_IMAGEFORMAT_DIRS} NO_DEFAULT_PATH)
        if(NOT _plugin_source)
            message(FATAL_ERROR "Qt image format plugin missing: ${_plugin}; searched ${QUIKVIEW_IMAGEFORMAT_DIRS}")
        endif()
        add_custom_command(TARGET quikview POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:quikview>/imageformats"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_plugin_source}"
                    "$<TARGET_FILE_DIR:quikview>/imageformats/${_plugin}"
            VERBATIM)
    endforeach()

    # 插件自身的动态依赖也要跟着部署：MSYS2 的 qtiff 链接 libtiff-6.dll，
    # 只复制插件不复制依赖时，QImageReader 根本注册不上 TIFF handler，
    # 表现仍是「打不开」且没有任何提示。这些 DLL 不在 quikview.exe 的导入表
    # 里，现有构建目录的依赖闭包覆盖不到，所以单独列清单。
    # 缺文件时只告警：换一套不带这些插件的 Qt 时不该阻断整个构建。
    set(QUIKVIEW_IMAGEFORMAT_RUNTIME_DLLS
        libtiff-6.dll libjpeg-8.dll libwebp-7.dll libdeflate.dll
        libjbig-0.dll libLerc.dll liblzma-5.dll zlib1.dll libzstd.dll)
    foreach(_dll IN LISTS QUIKVIEW_IMAGEFORMAT_RUNTIME_DLLS)
        unset(_dll_source CACHE)
        find_file(_dll_source NAMES "${_dll}"
            PATHS "${QUIKVIEW_COMPILER_PREFIX}/bin" "${QUIKVIEW_QT_PREFIX}/bin"
            NO_DEFAULT_PATH)
        if(_dll_source)
            add_custom_command(TARGET quikview POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_dll_source}"
                        "$<TARGET_FILE_DIR:quikview>/${_dll}"
                VERBATIM)
        else()
            message(WARNING "Qt image plugin runtime dependency missing: ${_dll}")
        endif()
    endforeach()
endif()

# ===========================================================================
# 显隐回归（CTest）：弹出菜单与上下悬浮铬件的隔离
# ---------------------------------------------------------------------------
# 用例表在 tests/chrome_popup_cases.cmake；执行器在 tests/chrome_popup_isolation.cmake。
# 每个用例启动一次真实程序，用 --smoke 命令驱动显隐状态机，断言 report 落盘的
# dock / top / popup 等状态字段（不靠截图判定）。用例间工作目录、临时配置互相隔离。
include(CTest)
if(BUILD_TESTING)
    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/chrome_popup_cases.cmake")
    foreach(_chrome_case IN LISTS QUIKVIEW_CHROME_CASES)
        add_test(NAME chrome_popup.${_chrome_case}
            COMMAND "${CMAKE_COMMAND}"
                    "-DQUIKVIEW_EXE=$<TARGET_FILE:quikview>"
                    "-DQUIKVIEW_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                    "-DQUIKVIEW_WORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/ctest-chrome-popup"
                    "-DQUIKVIEW_CASE=${_chrome_case}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/chrome_popup_isolation.cmake")
        set_tests_properties(chrome_popup.${_chrome_case} PROPERTIES
            TIMEOUT 180
            RUN_SERIAL TRUE
            LABELS "chrome;popup;visibility")
    endforeach()
endif()
