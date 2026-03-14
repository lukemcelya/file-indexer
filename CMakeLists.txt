cmake_minimum_required(VERSION 3.24)
project(FileIndex LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core library
add_library(fileindex_core
        src/IndexApp.cpp
        src/Cli.cpp
        src/Index.cpp
        src/Database.cpp
        external/sqlite/sqlite3.c
)

target_include_directories(fileindex_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/external/sqlite
)

add_executable(index
        src/main.cpp
)

target_link_libraries(index PRIVATE
        fileindex_core
)
