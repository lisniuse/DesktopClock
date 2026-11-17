cmake_minimum_required(VERSION 3.20)

project(DesktopClock LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(DesktopClock WIN32
    src/main.cpp
    src/clock_window.cpp
    src/config.cpp
    res/app.rc
)

target_compile_definitions(DesktopClock PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if (MSVC)
    target_compile_options(DesktopClock PRIVATE /utf-8 /W4)
endif()

target_link_libraries(DesktopClock PRIVATE
    gdiplus
    user32
    gdi32
    shell32
    advapi32
)
