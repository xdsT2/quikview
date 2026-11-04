cmake_minimum_required(VERSION 3.15)
project(quikview-shell-ext CXX)

# QuikView 缩略图壳扩展（独立工程，不并入主 CMake 目标）。
# 64 位用 MinGW-w64；32 位本机 mingw32 无编译器，用 MSVC x86（见 README）。

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(quikthumb SHARED
    src/guids.cpp
    src/module.cpp
    src/thumbnail.cpp
    src/contextmenu.cpp
    quikview.rc)
target_include_directories(quikthumb PRIVATE src)
target_compile_definitions(quikthumb PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
# windowscodecs：MSVC 下 WIC 的 GUID 走导入库（MinGW 由 initguid 实例化，双保险）
target_link_libraries(quikthumb PRIVATE ole32 advapi32 gdi32 user32 uuid windowscodecs)

# thumb_selftest 的 quikview.exe 替身：InvokeCommand 用例真启动一个进程并核对
# 命令行（写标记文件）。GUI 子系统不弹窗；EXCLUDE_FROM_ALL，非交付物。
add_executable(ctx_fake_quikview EXCLUDE_FROM_ALL
    src/fake_quikview.cpp)
set_target_properties(ctx_fake_quikview PROPERTIES WIN32_EXECUTABLE TRUE)
target_compile_definitions(ctx_fake_quikview PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)

add_executable(thumb_selftest EXCLUDE_FROM_ALL
    src/guids.cpp
    src/selftest.cpp)
target_include_directories(thumb_selftest PRIVATE src)
target_compile_definitions(thumb_selftest PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
target_link_libraries(thumb_selftest PRIVATE ole32 shlwapi gdi32 user32 uuid)

# 无论单/多配置生成器，统一把 DLL 拷到构建目录根部（注册脚本默认找这里）。
add_custom_command(TARGET quikthumb POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:quikthumb> ${CMAKE_BINARY_DIR}/quikthumb.dll
    COMMENT "Copy quikthumb.dll to build root")

if(MINGW)
    target_compile_options(quikthumb PRIVATE -Wall -Wextra)
    # 全静态（libgcc/libstdc++/winpthread）：壳扩展 DLL 必须自包含，
    # 目标机不保证有 MinGW 运行时
    target_link_options(quikthumb PRIVATE -static)
    target_compile_options(thumb_selftest PRIVATE -Wall -Wextra -Wno-cast-function-type)
    target_compile_options(ctx_fake_quikview PRIVATE -Wall -Wextra)
    # 注意：selftest 不用 -static。静态 exe + 静态 DLL 会在 DLL 加载阶段崩
    # （两份静态 winpthread/libgcc 运行时在同进程冲突），DLL 交付保持 -static，
    # 测试器动态链接 MinGW 运行时（仅本机自测用，不随产品分发）。
    target_link_options(thumb_selftest PRIVATE -static-libgcc)
elseif(MSVC)
    # 源码是 UTF-8（无 BOM）中文注释
    target_compile_options(quikthumb PRIVATE /utf-8 /W4)
    target_compile_options(thumb_selftest PRIVATE /utf-8 /W4)
    target_compile_options(ctx_fake_quikview PRIVATE /utf-8 /W4)
    # 四个标准入口按无装饰名导出（regsvr32/COM 按此查找）；
    # x86 __stdcall 符号带修饰，需显式映射
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_link_options(quikthumb PRIVATE
            "/EXPORT:DllGetClassObject=_DllGetClassObject@12"
            "/EXPORT:DllCanUnloadNow=_DllCanUnloadNow@0"
            "/EXPORT:DllRegisterServer=_DllRegisterServer@0"
            "/EXPORT:DllUnregisterServer=_DllUnregisterServer@0")
    else()
        target_link_options(quikthumb PRIVATE
            "/EXPORT:DllGetClassObject"
            "/EXPORT:DllCanUnloadNow"
            "/EXPORT:DllRegisterServer"
            "/EXPORT:DllUnregisterServer")
    endif()
endif()
