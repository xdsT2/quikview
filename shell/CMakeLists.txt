cmake_minimum_required(VERSION 3.16)
project(quikview_shell LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(quikview_shell SHARED
    ThumbnailProvider.cpp
    ThumbnailProvider.h
    PreviewHandler.cpp
    PreviewHandler.h
    dllmain.cpp
)

target_compile_definitions(quikview_shell PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)

# 纯 Win32 COM + WIC，不依赖 Qt（避免把 Qt 拖进 explorer.exe）
target_link_libraries(quikview_shell PRIVATE
    ole32
    shlwapi
    windowscodecs
    uuid
    advapi32
)
