cmake_minimum_required(VERSION 3.21)
project(make_icon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "C:/msys64/mingw64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Svg Gui)

add_executable(make_icon main.cpp)
target_link_libraries(make_icon PRIVATE Qt6::Svg Qt6::Gui Qt6::Core)

# Renders assets/phoenix-source.png (or --png <file>, else assets/phoenix.svg)
# to assets/icons/*.png and assets/phoenix.ico.
add_custom_target(icon ALL
    COMMAND make_icon "$<TARGET_FILE_DIR:make_icon>"
    COMMENT "Generating Phoenix icon set"
)