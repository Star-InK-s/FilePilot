cmake_minimum_required(VERSION 3.21)

project(FilePilot
    VERSION 0.1.0
    DESCRIPTION "Local file organization and backup tool"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Sql)

include(cmake/CompilerWarnings.cmake)

option(FILEPILOT_BUILD_TESTS "Build FilePilot automated tests" ON)

add_subdirectory(src)

if(FILEPILOT_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
