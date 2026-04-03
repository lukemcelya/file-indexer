cmake_minimum_required(VERSION 3.24)
project(FileIndexer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Core library
add_library(fileindexer_core
    src/IndexApp.cpp
    src/Cli.cpp
    src/Index.cpp
    src/Database.cpp
    src/Hasher.cpp
    src/Duplicates.cpp
    src/PlatformPaths.cpp
    external/sqlite/sqlite3.c
)

target_include_directories(fileindexer_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/sqlite
    ${CMAKE_CURRENT_SOURCE_DIR}/external/PicoSHA2
)

add_executable(file-indexer
    src/main.cpp
)

target_link_libraries(file-indexer PRIVATE
    fileindexer_core
)
