cmake_minimum_required(VERSION 3.16)
project(quikview VERSION 0.6.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent PrintSupport Network Svg LinguistTools)

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
    src/MinimapMath.h
    src/ZoomMotion.cpp
    src/ZoomMotion.h
    src/ImageCache.cpp
    src/ImageCache.h
    src/DirectoryLoader.cpp
    src/DirectoryLoader.h
    src/RawJpgGroup.cpp
    src/RawJpgGroup.h
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
    src/HudShell.cpp
    src/HudShell.h
    src/ExifParser.cpp
    src/ExifParser.h
    src/RotateAutoSave.cpp
    src/RotateAutoSave.h
    src/Wallpaper.cpp
    src/Wallpaper.h
    src/BatchRename.cpp
    src/BatchRename.h
    src/RenameDialog.cpp
    src/RenameDialog.h
    src/PhotoFolderPanel.cpp
    src/PhotoFolderPanel.h
    # 压缩包直读（派单 2026-11-18）：自研中央目录解析 + 复用 psd_sdk 内置 miniz
    src/ZipArchive.cpp
    src/ZipArchive.h
    src/ArchiveStore.cpp
    src/ArchiveStore.h
    src/SettingsDialog.cpp
    src/SettingsDialog.h
    src/PillToggle.cpp
    src/PillToggle.h
    src/Chrome.cpp
    src/Chrome.h
    src/CtxMenu.cpp
    src/CtxMenu.h
    src/MenuIcons.cpp
    src/MenuIcons.h
    src/AppIcon.cpp
    src/AppIcon.h
    src/MenuCatalog.h
    src/AppMenuPreview.cpp
    src/AppMenuPreview.h
    src/BatchConvertDialog.cpp
    src/BatchConvertDialog.h
    src/DialogShell.h
    src/GammaDialog.cpp
    src/GammaDialog.h
    src/ThumbnailBrowser.cpp
    src/ThumbnailBrowser.h
    src/OverviewPanel.cpp
    src/OverviewPanel.h
    src/OpenPicker.cpp
    src/OpenPicker.h
    # 总览分组口径（2026-11-22 派单）：尺寸只读文件头、拍摄日期只读 EXIF
    src/ImageFacts.cpp
    src/ImageFacts.h
    src/FrameNavigator.cpp
    src/FrameNavigator.h
    src/Theme.h
    src/UiStyle.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/UpdateChecker.cpp
    src/UpdateChecker.h
    # 360° 全景（派单 2026-11-20）：GPano 识别在主程序内（只读头部、零渲染），
    # 球形渲染与交互在独立进程 quikview-pano（见下方 target）。
    src/PanoMetadata.cpp
    src/PanoMetadata.h
    src/PanoExtension.cpp
    src/PanoExtension.h
    src/PanoChip.cpp
    src/PanoChip.h
    src/PsdLoader.cpp
    src/PsdLoader.h
    # 矢量 SVG 打开（派单 2026-11-18 格式扩展三件）：QSvgRenderer 栅格化，
    # 依赖上面已链的 Qt6::Svg；不依赖 imageformats/qsvg 插件
    src/SvgLoader.cpp
    src/SvgLoader.h
    src/LayerPanel.cpp
    src/LayerPanel.h
    resources/ui.qrc
)

target_include_directories(quikview PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/shared)

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
    # 图标统一派单 2026-11-18：菜单/工具栏/设置页图标走 Fluent SVG，
    # 唯一技术路线是 QSvgRenderer（派单 §4 方案 A），故显式依赖 Qt SVG。
    # 发布包必须带上 Qt6Svg.dll，windeployqt 会按 import 自动收；缺失时
    # AppIcon::available() 为 false，界面回退无图标（不黑块、不崩溃）。
    Qt6::Svg
    Psd
    dwmapi
    # archreport 的进程峰值工作集（GetProcessMemoryInfo）走 psapi
    psapi
    # 原生 IFileOpenDialog / IFileDialogCustomize（2026-11-25 原生对话框替换单）
    ole32
    shell32
    uuid
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
    target_link_libraries(quikview PRIVATE winmm shell32 ole32)

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
    option(QUIKVIEW_REQUIRE_ALL_IMAGEFORMATS "Fail configure when optional Qt imageformat plugins are missing" ON)
    set(QUIKVIEW_IMAGEFORMAT_PLUGINS
        qjpeg.dll qgif.dll qico.dll qwebp.dll qtiff.dll
        qicns.dll qmng.dll qtga.dll qwbmp.dll)
    foreach(_plugin IN LISTS QUIKVIEW_IMAGEFORMAT_PLUGINS)
        unset(_plugin_source CACHE)
        find_file(_plugin_source NAMES "${_plugin}"
            PATHS ${QUIKVIEW_IMAGEFORMAT_DIRS} NO_DEFAULT_PATH)
        if(NOT _plugin_source)
            if(QUIKVIEW_REQUIRE_ALL_IMAGEFORMATS)
                message(FATAL_ERROR "Qt image format plugin missing: ${_plugin}; searched ${QUIKVIEW_IMAGEFORMAT_DIRS}")
            else()
                message(WARNING "Qt image format plugin missing (skipped): ${_plugin}; searched ${QUIKVIEW_IMAGEFORMAT_DIRS}")
                continue()
            endif()
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
# 360° 全景查看器（可选组件，派单 2026-11-20）
# ---------------------------------------------------------------------------
# 独立进程，故意不并入 quikview.exe：
#   · 可选安装，主程序不带它也能完整使用；
#   · 球形渲染把 CPU 吃满时不会卡住看图的主界面；
#   · 出问题只死扩展窗口，主窗口的原图与缩放状态原地保留。
# 与主程序同目录安装（复用同一套 Qt 运行时 DLL），版本口径同样取
# CMakeLists 的 project(VERSION)，不另写版本字面量。
add_executable(quikview-pano WIN32
    src/pano/main.cpp
    src/pano/PanoWindow.cpp
    src/pano/PanoWindow.h
    src/pano/PanoGlobe.cpp
    src/pano/PanoGlobe.h
)
target_include_directories(quikview-pano PRIVATE src)
target_compile_definitions(quikview-pano PRIVATE
    QUIKVIEW_PANO_VERSION="${PROJECT_VERSION}")
target_link_libraries(quikview-pano PRIVATE Qt6::Widgets)
set_target_properties(quikview-pano PROPERTIES
    OUTPUT_NAME "quikview-pano"
    WIN32_EXECUTABLE TRUE
)

# ===========================================================================
# 显隐回归（CTest）：弹出菜单与上下悬浮铬件的隔离
# ---------------------------------------------------------------------------
# 用例表在 tests/chrome_popup_cases.cmake；执行器在 tests/chrome_popup_isolation.cmake。
# 每个用例启动一次真实程序，用 --smoke 命令驱动显隐状态机，断言 report 落盘的
# dock / top / popup 等状态字段（不靠截图判定）。用例间工作目录、临时配置互相隔离。
include(CTest)
if(BUILD_TESTING)
    # OpenPicker 纯函数无窗口单测（筛选表构造 / 文件夹目标解析），不弹原生框。
    add_executable(quikview-openpicker-test tests/unit/openpicker_test.cpp src/OpenPicker.cpp)
    target_include_directories(quikview-openpicker-test PRIVATE src)
    target_link_libraries(quikview-openpicker-test PRIVATE Qt6::Core)
    if(WIN32)
        target_link_libraries(quikview-openpicker-test PRIVATE ole32 shell32 uuid)
    endif()
    add_test(NAME openpicker.pure COMMAND quikview-openpicker-test)

    add_executable(quikview-zoommotion-test tests/unit/zoom_motion_test.cpp src/ZoomMotion.cpp)
    target_include_directories(quikview-zoommotion-test PRIVATE src)
    add_test(NAME zoommotion.pure COMMAND quikview-zoommotion-test)

    add_executable(quikview-minimap-test tests/unit/minimap_math_test.cpp)
    target_include_directories(quikview-minimap-test PRIVATE src)
    target_link_libraries(quikview-minimap-test PRIVATE Qt6::Core)
    add_test(NAME minimap.pure COMMAND quikview-minimap-test)

    add_executable(quikview-batchrename-test tests/unit/batch_rename_test.cpp src/BatchRename.cpp)
    target_include_directories(quikview-batchrename-test PRIVATE src)
    target_link_libraries(quikview-batchrename-test PRIVATE Qt6::Core)
    add_test(NAME batchrename.pure COMMAND quikview-batchrename-test)

    add_executable(quikview-photofolders-test tests/unit/photo_folders_test.cpp src/Config.cpp)
    target_include_directories(quikview-photofolders-test PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_link_libraries(quikview-photofolders-test PRIVATE Qt6::Gui)
    add_test(NAME photofolders.pure COMMAND quikview-photofolders-test)

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
