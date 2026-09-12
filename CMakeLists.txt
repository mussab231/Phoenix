cmake_minimum_required(VERSION 3.21)
project(Phoenix VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# MSYS2 MinGW Qt location (override with -DCMAKE_PREFIX_PATH=... if needed)
if(NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "C:/msys64/mingw64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# NOTE: no WIN32 flag on purpose during development so console
# output (--self-test) stays visible. Added for release builds later.
add_executable(Phoenix
    src/main.cpp
    src/core/HttpClient.cpp
    src/core/ResumeStore.cpp
    src/core/DownloadEngine.cpp
    src/gui/MainWindow.cpp
)

target_include_directories(Phoenix PRIVATE src)
target_link_libraries(Phoenix PRIVATE Qt6::Widgets winhttp)
