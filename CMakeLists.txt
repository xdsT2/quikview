cmake_minimum_required(VERSION 3.16)
project(SoleView VERSION 0.6.2 LANGUAGES CXX)

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
    pkg_check_modules(LIBWEBPDEMUX QUIET IMPORTED_TARGET libwebpdemux libwebp)
    pkg_check_modules(LIBOPENJP2 QUIET IMPORTED_TARGET libopenjp2)
    pkg_check_modules(LIBJXL QUIET IMPORTED_TARGET libjxl libjxl_threads)
    pkg_check_modules(OPENEXR QUIET IMPORTED_TARGET OpenEXR)
endif()

# 完整性级别标签修正（2026-11-28 排查纯逻辑红测与 GUI 弹窗用例）：
# 本工作区根目录被安全策略打上 Low Mandatory Level 标签，目录内产物 exe 会继承该标签；
# Windows 在 CreateProcess 时若可执行文件完整性级别低于父进程，会把新进程降到与文件
# 相同的 Low 级别。Low 级别进程无法在 Medium 级别的 %TEMP% 下创建目录（QTemporaryDir
# ::isValid() 恒为 false），GUI 主程序在测试执行器的隐藏窗口站下也无法正常起窗。
# 这里在链接后显式把文件完整性标签重置回 Medium（桌面默认级别）；只改文件安全标签，
# 不改程序运行时权限与功能逻辑。
function(soleview_reset_integrity_label target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND icacls "$<TARGET_FILE:${target}>" /setintegritylevel medium
            VERBATIM)
    endif()
endfunction()

add_executable(soleview WIN32
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
    src/DecodeQueue.cpp
    src/DecodeQueue.h
    src/ImageProbe.cpp
    src/ImageProbe.h
    src/MappedFile.h
    src/DirectoryLoader.cpp
    src/DirectoryLoader.h
    src/RawJpgGroup.cpp
    src/RawJpgGroup.h
    src/ImageEffects.cpp
    src/ToneMap.cpp
    src/ToneMap.h
    src/DisplayFrame.cpp
    src/DisplayFrame.h
    src/ImageEffects.h
    src/RawImageLoader.cpp
    src/RawImageLoader.h
    src/HeifLoader.cpp
    src/HeifLoader.h
    src/ExtraImageLoader.cpp
    src/ExtraImageLoader.h
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
    # 容器后缀唯一判定表（2026-11-28 I4-1）：header-only，纯函数，便于单测
    src/ArchiveKinds.h
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
    # 球形渲染与交互在独立进程 soleview-pano（见下方 target）。
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

target_include_directories(soleview PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/shared)

# 版本号单一来源：CMakeLists 的 project(VERSION ...)。
# main.cpp 的 QApplication::setApplicationVersion 与「关于」对话框都读这个宏，
# 别再各写各的字面量。
target_compile_definitions(soleview PRIVATE SOLEVIEW_VERSION="${PROJECT_VERSION}")

# 数字版本（四段纯数字）只给安装包与 PE 版本资源用：VIProductVersion / FILEVERSION
# 不接受 "0.4.0" 这种三段式，也不接受 -rc1 后缀。project(VERSION) 永远纯数字，
# 预发布后缀只允许出现在 git tag 与展示名里。
set(SOLEVIEW_VERSION_RC
    "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.0")
set(QUIKVIEW_VERSION_RC "${SOLEVIEW_VERSION_RC}")
message(STATUS "SoleView version ${PROJECT_VERSION} (installed as ${SOLEVIEW_VERSION_RC})")

# 翻译：.ts → .qm，嵌入资源 :/i18n
qt_add_translations(soleview
    TS_FILES translations/soleview_en_US.ts translations/soleview_ko_KR.ts translations/soleview_ja_JP.ts translations/soleview_fr_FR.ts translations/soleview_de_DE.ts translations/soleview_es_ES.ts translations/soleview_ru_RU.ts translations/soleview_zh_TW.ts
)

target_include_directories(soleview PRIVATE third_party/psd_sdk)

target_link_libraries(soleview PRIVATE
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

soleview_reset_integrity_label(soleview)

if(LIBRAW_FOUND)
    target_link_libraries(soleview PRIVATE PkgConfig::LIBRAW)
    target_compile_definitions(soleview PRIVATE HAS_LIBRAW=1)
endif()

if(LIBHEIF_FOUND)
    target_link_libraries(soleview PRIVATE PkgConfig::LIBHEIF)
    target_compile_definitions(soleview PRIVATE HAS_LIBHEIF=1)
endif()

if(LIBWEBPDEMUX_FOUND)
    target_link_libraries(soleview PRIVATE PkgConfig::LIBWEBPDEMUX)
    target_compile_definitions(soleview PRIVATE HAS_LIBWEBPDEMUX=1)
endif()

if(LIBOPENJP2_FOUND)
    target_link_libraries(soleview PRIVATE PkgConfig::LIBOPENJP2)
    target_compile_definitions(soleview PRIVATE HAS_LIBOPENJP2=1)
endif()

if(LIBJXL_FOUND)
    target_link_libraries(soleview PRIVATE PkgConfig::LIBJXL)
    target_compile_definitions(soleview PRIVATE HAS_LIBJXL=1)
endif()

if(OPENEXR_FOUND)
    target_link_libraries(soleview PRIVATE PkgConfig::OPENEXR)
    target_compile_definitions(soleview PRIVATE HAS_OPENEXR=1)
endif()

# Windows 需要子系统 GUI；Qt 应用建议加 high-DPI 属性
if(WIN32)
    set_target_properties(soleview PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    # 把图标编进 PE：ExtractIconW / 资源管理器 / 快捷方式默认图标都靠它
    target_sources(soleview PRIVATE resources/app.rc)
    # app.rc 的 VERSIONINFO 读这里生成的版本头；同样由 project(VERSION) 派生
    configure_file(resources/qv_version.h.in "${CMAKE_CURRENT_BINARY_DIR}/qv_version.h" @ONLY)
    set_source_files_properties(resources/app.rc PROPERTIES
        COMPILE_OPTIONS "--include-dir=${CMAKE_CURRENT_SOURCE_DIR}/resources;--include-dir=${CMAKE_CURRENT_BINARY_DIR}"
        OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/qv_version.h"
    )
    # WM_NCCALCSIZE 最大化修正用到 GetDpiForWindow / GetSystemMetricsForDpi（Win10 1607+）
    target_compile_definitions(soleview PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    # 翻页音效（pageturn_wav）走 winmm PlaySound（MainWindow.cpp 运行时合成 WAV）
    # WIC（.jxr/.wdp/.hdp/BC6H/BC7）走 windowscodecs，WMF/EMF 走 gdiplus
    target_link_libraries(soleview PRIVATE winmm shell32 ole32 windowscodecs gdiplus)

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
    get_filename_component(SOLEVIEW_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    get_filename_component(SOLEVIEW_COMPILER_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(SOLEVIEW_COMPILER_PREFIX "${SOLEVIEW_COMPILER_BIN}/.." ABSOLUTE)
    set(SOLEVIEW_IMAGEFORMAT_DIRS
        "${SOLEVIEW_QT_PREFIX}/plugins/imageformats"
        "${SOLEVIEW_QT_PREFIX}/share/qt6/plugins/imageformats"
        "${SOLEVIEW_COMPILER_PREFIX}/share/qt6/plugins/imageformats")
    if(DEFINED QUIKVIEW_REQUIRE_ALL_IMAGEFORMATS AND NOT DEFINED SOLEVIEW_REQUIRE_ALL_IMAGEFORMATS)
        set(SOLEVIEW_REQUIRE_ALL_IMAGEFORMATS "${QUIKVIEW_REQUIRE_ALL_IMAGEFORMATS}")
    endif()
    option(SOLEVIEW_REQUIRE_ALL_IMAGEFORMATS "Fail configure when optional Qt imageformat plugins are missing" ON)
    set(SOLEVIEW_IMAGEFORMAT_PLUGINS
        qjpeg.dll qgif.dll qico.dll qwebp.dll qtiff.dll
        qicns.dll qmng.dll qtga.dll qwbmp.dll)
    foreach(_plugin IN LISTS SOLEVIEW_IMAGEFORMAT_PLUGINS)
        unset(_plugin_source CACHE)
        find_file(_plugin_source NAMES "${_plugin}"
            PATHS ${SOLEVIEW_IMAGEFORMAT_DIRS} NO_DEFAULT_PATH)
        if(NOT _plugin_source)
            if(SOLEVIEW_REQUIRE_ALL_IMAGEFORMATS)
                message(FATAL_ERROR "Qt image format plugin missing: ${_plugin}; searched ${SOLEVIEW_IMAGEFORMAT_DIRS}")
            else()
                message(WARNING "Qt image format plugin missing (skipped): ${_plugin}; searched ${SOLEVIEW_IMAGEFORMAT_DIRS}")
                continue()
            endif()
        endif()
        add_custom_command(TARGET soleview POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:soleview>/imageformats"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_plugin_source}"
                    "$<TARGET_FILE_DIR:soleview>/imageformats/${_plugin}"
            VERBATIM)
    endforeach()

    # 插件自身的动态依赖也要跟着部署：MSYS2 的 qtiff 链接 libtiff-6.dll，
    # 只复制插件不复制依赖时，QImageReader 根本注册不上 TIFF handler，
    # 表现仍是「打不开」且没有任何提示。这些 DLL 不在 soleview.exe 的导入表
    # 里，现有构建目录的依赖闭包覆盖不到，所以单独列清单。
    # 缺文件时只告警：换一套不带这些插件的 Qt 时不该阻断整个构建。
    set(SOLEVIEW_IMAGEFORMAT_RUNTIME_DLLS
        libtiff-6.dll libjpeg-8.dll libwebp-7.dll libwebpdemux-2.dll libdeflate.dll
        libjbig-0.dll libLerc.dll liblzma-5.dll zlib1.dll libzstd.dll
        libopenjp2-7.dll libopenjph-0.32.dll
        libjxl.dll libjxl_threads.dll libjxl_cms.dll libhwy.dll libbrotlienc.dll
        libOpenEXR-3_4.dll libOpenEXRCore-3_4.dll libIex-3_4.dll libIlmThread-3_4.dll libImath-3_2.dll)
    foreach(_dll IN LISTS SOLEVIEW_IMAGEFORMAT_RUNTIME_DLLS)
        unset(_dll_source CACHE)
        find_file(_dll_source NAMES "${_dll}"
            PATHS "${SOLEVIEW_COMPILER_PREFIX}/bin" "${SOLEVIEW_QT_PREFIX}/bin"
            NO_DEFAULT_PATH)
        if(_dll_source)
            add_custom_command(TARGET soleview POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_dll_source}"
                        "$<TARGET_FILE_DIR:soleview>/${_dll}"
                VERBATIM)
        else()
            message(WARNING "Qt image plugin runtime dependency missing: ${_dll}")
        endif()
    endforeach()
endif()

# ===========================================================================
# 360° 全景查看器（可选组件，派单 2026-11-20）
# ---------------------------------------------------------------------------
# 独立进程，故意不并入 soleview.exe：
#   · 可选安装，主程序不带它也能完整使用；
#   · 球形渲染把 CPU 吃满时不会卡住看图的主界面；
#   · 出问题只死扩展窗口，主窗口的原图与缩放状态原地保留。
# 与主程序同目录安装（复用同一套 Qt 运行时 DLL），版本口径同样取
# CMakeLists 的 project(VERSION)，不另写版本字面量。
add_executable(soleview-pano WIN32
    src/pano/main.cpp
    src/pano/PanoWindow.cpp
    src/pano/PanoWindow.h
    src/pano/PanoGlobe.cpp
    src/pano/PanoGlobe.h
)
target_include_directories(soleview-pano PRIVATE src)
target_compile_definitions(soleview-pano PRIVATE
    SOLEVIEW_PANO_VERSION="${PROJECT_VERSION}")
target_link_libraries(soleview-pano PRIVATE Qt6::Widgets)
set_target_properties(soleview-pano PROPERTIES
    OUTPUT_NAME "soleview-pano"
    WIN32_EXECUTABLE TRUE
)
soleview_reset_integrity_label(soleview-pano)

# ===========================================================================
# 显隐回归（CTest）：弹出菜单与上下悬浮铬件的隔离
# ---------------------------------------------------------------------------
# 用例表在 tests/chrome_popup_cases.cmake；执行器在 tests/chrome_popup_isolation.cmake。
# 每个用例启动一次真实程序，用 --smoke 命令驱动显隐状态机，断言 report 落盘的
# dock / top / popup 等状态字段（不靠截图判定）。用例间工作目录、临时配置互相隔离。
include(CTest)
if(BUILD_TESTING)
    # OpenPicker 纯函数无窗口单测（筛选表构造 / 文件夹目标解析），不弹原生框。
    add_executable(soleview-openpicker-test tests/unit/openpicker_test.cpp src/OpenPicker.cpp)
    target_include_directories(soleview-openpicker-test PRIVATE src)
    target_link_libraries(soleview-openpicker-test PRIVATE Qt6::Core)
    if(WIN32)
        target_link_libraries(soleview-openpicker-test PRIVATE ole32 shell32 uuid)
    endif()
    add_test(NAME openpicker.pure COMMAND soleview-openpicker-test)
    soleview_reset_integrity_label(soleview-openpicker-test)

    add_executable(soleview-zoommotion-test tests/unit/zoom_motion_test.cpp src/ZoomMotion.cpp)
    target_include_directories(soleview-zoommotion-test PRIVATE src)
    add_test(NAME zoommotion.pure COMMAND soleview-zoommotion-test)
    soleview_reset_integrity_label(soleview-zoommotion-test)

    # D2-2：解码优先级队列纯逻辑（抢占/去重/上限收敛），不依赖线程与解码器。
    add_executable(soleview-decodequeue-test
        tests/unit/decode_queue_test.cpp
        src/DecodeQueue.cpp)
    target_include_directories(soleview-decodequeue-test PRIVATE src)
    target_link_libraries(soleview-decodequeue-test PRIVATE Qt6::Core)
    add_test(NAME decodequeue.pure COMMAND soleview-decodequeue-test)
    soleview_reset_integrity_label(soleview-decodequeue-test)

    # I4-1：容器后缀表纯逻辑（ArchiveKinds，header-only，只需 Qt6::Core 的 QString）。
    add_executable(soleview-archivekinds-test tests/unit/archive_kinds_test.cpp)
    target_include_directories(soleview-archivekinds-test PRIVATE src)
    target_link_libraries(soleview-archivekinds-test PRIVATE Qt6::Core)
    add_test(NAME archivekinds.pure COMMAND soleview-archivekinds-test)
    soleview_reset_integrity_label(soleview-archivekinds-test)

    add_executable(soleview-minimap-test tests/unit/minimap_math_test.cpp)
    target_include_directories(soleview-minimap-test PRIVATE src)
    target_link_libraries(soleview-minimap-test PRIVATE Qt6::Core)
    add_test(NAME minimap.pure COMMAND soleview-minimap-test)
    soleview_reset_integrity_label(soleview-minimap-test)

    add_executable(soleview-batchrename-test tests/unit/batch_rename_test.cpp src/BatchRename.cpp)
    target_include_directories(soleview-batchrename-test PRIVATE src)
    target_link_libraries(soleview-batchrename-test PRIVATE Qt6::Core)
    add_test(NAME batchrename.pure COMMAND soleview-batchrename-test)
    soleview_reset_integrity_label(soleview-batchrename-test)

    add_executable(soleview-photofolders-test tests/unit/photo_folders_test.cpp src/Config.cpp)
    target_include_directories(soleview-photofolders-test PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_link_libraries(soleview-photofolders-test PRIVATE Qt6::Gui)
    add_test(NAME photofolders.pure COMMAND soleview-photofolders-test)
    soleview_reset_integrity_label(soleview-photofolders-test)

    add_executable(soleview-migration-test tests/unit/migration_test.cpp src/Config.cpp)
    target_include_directories(soleview-migration-test PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_link_libraries(soleview-migration-test PRIVATE Qt6::Gui)
    add_test(NAME migration.pure COMMAND soleview-migration-test)
    soleview_reset_integrity_label(soleview-migration-test)

    add_executable(soleview-formats-test
        tests/unit/extra_formats_test.cpp
        src/DirectoryLoader.cpp
        src/ArchiveStore.cpp
        src/ZipArchive.cpp
        src/PsdLoader.cpp
        src/SvgLoader.cpp
        src/RawImageLoader.cpp
        src/HeifLoader.cpp
        src/ExtraImageLoader.cpp
        src/FrameNavigator.cpp
        src/RawJpgGroup.cpp
        src/RotateAutoSave.cpp
        src/ToneMap.cpp
        src/Config.cpp
    )
    target_include_directories(soleview-formats-test PRIVATE src third_party/psd_sdk ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_definitions(soleview-formats-test PRIVATE
        SOLEVIEW_FORMAT_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/format-samples")
    target_link_libraries(soleview-formats-test PRIVATE Qt6::Widgets Qt6::Svg Psd)
    if(WIN32)
        target_link_libraries(soleview-formats-test PRIVATE windowscodecs gdiplus)
    endif()
    if(LIBRAW_FOUND)
        target_link_libraries(soleview-formats-test PRIVATE PkgConfig::LIBRAW)
        target_compile_definitions(soleview-formats-test PRIVATE HAS_LIBRAW=1)
    endif()
    if(LIBHEIF_FOUND)
        target_link_libraries(soleview-formats-test PRIVATE PkgConfig::LIBHEIF)
        target_compile_definitions(soleview-formats-test PRIVATE HAS_LIBHEIF=1)
    endif()
    if(LIBWEBPDEMUX_FOUND)
        target_link_libraries(soleview-formats-test PRIVATE PkgConfig::LIBWEBPDEMUX)
        target_compile_definitions(soleview-formats-test PRIVATE HAS_LIBWEBPDEMUX=1)
    endif()
    if(LIBOPENJP2_FOUND)
        target_link_libraries(soleview-formats-test PRIVATE PkgConfig::LIBOPENJP2)
        target_compile_definitions(soleview-formats-test PRIVATE HAS_LIBOPENJP2=1)
    endif()
    if(LIBJXL_FOUND)
        target_link_libraries(soleview-formats-test PRIVATE PkgConfig::LIBJXL)
        target_compile_definitions(soleview-formats-test PRIVATE HAS_LIBJXL=1)
    endif()
    if(OPENEXR_FOUND)
        target_link_libraries(soleview-formats-test PRIVATE PkgConfig::OPENEXR)
        target_compile_definitions(soleview-formats-test PRIVATE HAS_OPENEXR=1)
    endif()
    add_test(NAME formats.pure COMMAND soleview-formats-test)
    soleview_reset_integrity_label(soleview-formats-test)

    add_executable(soleview-display-frame-test
        tests/unit/display_frame_test.cpp
        src/DisplayFrame.cpp
        src/DirectoryLoader.cpp
        src/ArchiveStore.cpp
        src/ZipArchive.cpp
        src/PsdLoader.cpp
        src/SvgLoader.cpp
        src/RawImageLoader.cpp
        src/HeifLoader.cpp
        src/ExtraImageLoader.cpp
        src/FrameNavigator.cpp
        src/RawJpgGroup.cpp
        src/RotateAutoSave.cpp
        src/ToneMap.cpp
        src/Config.cpp
    )
    target_include_directories(soleview-display-frame-test PRIVATE src third_party/psd_sdk ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_definitions(soleview-display-frame-test PRIVATE
        SOLEVIEW_FORMAT_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/format-samples")
    target_link_libraries(soleview-display-frame-test PRIVATE Qt6::Widgets Qt6::Svg Psd)
    if(WIN32)
        target_link_libraries(soleview-display-frame-test PRIVATE windowscodecs gdiplus)
    endif()
    if(LIBRAW_FOUND)
        target_link_libraries(soleview-display-frame-test PRIVATE PkgConfig::LIBRAW)
        target_compile_definitions(soleview-display-frame-test PRIVATE HAS_LIBRAW=1)
    endif()
    if(LIBHEIF_FOUND)
        target_link_libraries(soleview-display-frame-test PRIVATE PkgConfig::LIBHEIF)
        target_compile_definitions(soleview-display-frame-test PRIVATE HAS_LIBHEIF=1)
    endif()
    if(LIBWEBPDEMUX_FOUND)
        target_link_libraries(soleview-display-frame-test PRIVATE PkgConfig::LIBWEBPDEMUX)
        target_compile_definitions(soleview-display-frame-test PRIVATE HAS_LIBWEBPDEMUX=1)
    endif()
    if(LIBOPENJP2_FOUND)
        target_link_libraries(soleview-display-frame-test PRIVATE PkgConfig::LIBOPENJP2)
        target_compile_definitions(soleview-display-frame-test PRIVATE HAS_LIBOPENJP2=1)
    endif()
    if(LIBJXL_FOUND)
        target_link_libraries(soleview-display-frame-test PRIVATE PkgConfig::LIBJXL)
        target_compile_definitions(soleview-display-frame-test PRIVATE HAS_LIBJXL=1)
    endif()
    if(OPENEXR_FOUND)
        target_link_libraries(soleview-display-frame-test PRIVATE PkgConfig::OPENEXR)
        target_compile_definitions(soleview-display-frame-test PRIVATE HAS_OPENEXR=1)
    endif()
    add_test(NAME display-frame.pure COMMAND soleview-display-frame-test)
    # 可选大图样本目录（4000×3000 AdobeRGB / RGBA、两张实拍大图）；目录缺失时测试内部跳过。
    set(SOLEVIEW_EQ_BIG_DIR "E:/Agent/ByteDance/Doubao/Temp/display-frame-20261128/samples"
        CACHE PATH "等价校验大图样本目录")
    # 可选真 HDR 素材目录（EXR/RGBE；经现有 loader 折 8bit 后作为超大全景样本入等价）。
    # 指向 ASCII junction，避开中文路径在 CTest/运行时的编码风险；目录缺失时测试内部跳过。
    set(SOLEVIEW_EQ_HDR_DIR "E:/Agent/ByteDance/Doubao/Temp/hdr-samples"
        CACHE PATH "等价校验真 HDR 素材目录（解码层折 8bit）")
    set_tests_properties(display-frame.pure PROPERTIES
        ENVIRONMENT "SOLEVIEW_EQ_BIG_DIR=${SOLEVIEW_EQ_BIG_DIR};SOLEVIEW_EQ_HDR_DIR=${SOLEVIEW_EQ_HDR_DIR}")
    soleview_reset_integrity_label(soleview-display-frame-test)

    # 显示管线分段基准（离线 offscreen，不注册 ctest；由测量脚本手动调用）。
    add_executable(soleview-display-bench
        tests/bench/display_bench.cpp
        src/DisplayFrame.cpp
        src/DirectoryLoader.cpp
        src/ArchiveStore.cpp
        src/ZipArchive.cpp
        src/PsdLoader.cpp
        src/SvgLoader.cpp
        src/RawImageLoader.cpp
        src/HeifLoader.cpp
        src/ExtraImageLoader.cpp
        src/FrameNavigator.cpp
        src/RawJpgGroup.cpp
        src/RotateAutoSave.cpp
        src/ToneMap.cpp
        src/Config.cpp
    )
    target_include_directories(soleview-display-bench PRIVATE src third_party/psd_sdk ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_link_libraries(soleview-display-bench PRIVATE Qt6::Widgets Qt6::Svg Psd)
    if(WIN32)
        target_link_libraries(soleview-display-bench PRIVATE windowscodecs gdiplus)
    endif()
    if(LIBRAW_FOUND)
        target_link_libraries(soleview-display-bench PRIVATE PkgConfig::LIBRAW)
        target_compile_definitions(soleview-display-bench PRIVATE HAS_LIBRAW=1)
    endif()
    if(LIBHEIF_FOUND)
        target_link_libraries(soleview-display-bench PRIVATE PkgConfig::LIBHEIF)
        target_compile_definitions(soleview-display-bench PRIVATE HAS_LIBHEIF=1)
    endif()
    if(LIBWEBPDEMUX_FOUND)
        target_link_libraries(soleview-display-bench PRIVATE PkgConfig::LIBWEBPDEMUX)
        target_compile_definitions(soleview-display-bench PRIVATE HAS_LIBWEBPDEMUX=1)
    endif()
    if(LIBOPENJP2_FOUND)
        target_link_libraries(soleview-display-bench PRIVATE PkgConfig::LIBOPENJP2)
        target_compile_definitions(soleview-display-bench PRIVATE HAS_LIBOPENJP2=1)
    endif()
    if(LIBJXL_FOUND)
        target_link_libraries(soleview-display-bench PRIVATE PkgConfig::LIBJXL)
        target_compile_definitions(soleview-display-bench PRIVATE HAS_LIBJXL=1)
    endif()
    if(OPENEXR_FOUND)
        target_link_libraries(soleview-display-bench PRIVATE PkgConfig::OPENEXR)
        target_compile_definitions(soleview-display-bench PRIVATE HAS_OPENEXR=1)
    endif()
    soleview_reset_integrity_label(soleview-display-bench)

    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/chrome_popup_cases.cmake")
    foreach(_chrome_case IN LISTS SOLEVIEW_CHROME_CASES)
        add_test(NAME chrome_popup.${_chrome_case}
            COMMAND "${CMAKE_COMMAND}"
                    "-DSOLEVIEW_EXE=$<TARGET_FILE:soleview>"
                    "-DSOLEVIEW_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                    "-DSOLEVIEW_WORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/ctest-chrome-popup"
                    "-DSOLEVIEW_CASE=${_chrome_case}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/chrome_popup_isolation.cmake")
        set_tests_properties(chrome_popup.${_chrome_case} PROPERTIES
            TIMEOUT 180
            RUN_SERIAL TRUE
            LABELS "chrome;popup;visibility")
    endforeach()
endif()
