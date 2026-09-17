cmake_minimum_required(VERSION 3.21)
project(PhoenixTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "C:/msys64/mingw64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Catch2 3 REQUIRED)

# The units under test are pure C++17/STL. HttpClient is deliberately
# excluded: it wraps WinHTTP and is already covered end-to-end by the
# --self-test-* suite in the real binary. The linked sources must stay free of
# Qt GUI / WinHTTP dependencies so the suite stays a fast, isolated gate.
# Forced as a console app: without this the MinGW linker pulls in the GUI
# entry point and demands a WinMain the test binary does not have.
add_executable(phoenix_unit_tests EXCLUDE_FROM_ALL
    test_main.cpp
    test_urlcodec.cpp
    test_urlmatcher.cpp
    test_checksum.cpp
    test_downloaderror.cpp
    test_ratelimiter.cpp
    test_uuid.cpp
    test_resumestore.cpp
    test_sessionstore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/UrlCodec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/UrlMatcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/Checksum.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/RateLimiter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/Uuid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/ResumeStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/SessionStore.cpp
)

target_include_directories(phoenix_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
# Catch2 v3 ships main() in the separate Catch2WithMain library, not in the
# header-only Catch2 core.
target_link_libraries(phoenix_unit_tests PRIVATE Catch2::Catch2WithMain Qt6::Core)
set_target_properties(phoenix_unit_tests PROPERTIES
    WIN32_EXECUTABLE OFF
    # The default Qt6 executable template links with -mwindows, which makes the
    # MinGW CRT expect WinMain instead of main. Force the console subsystem.
    LINK_FLAGS "-mconsole"
)

# Report run duration per test case; exit non-zero on any failure (CI gate).
list(APPEND CATCH_ARGS "--reporter console" "--durations yes")
add_custom_target(unit_tests ALL
    COMMAND phoenix_unit_tests ${CATCH_ARGS}
    DEPENDS phoenix_unit_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running Phoenix unit tests"
)
