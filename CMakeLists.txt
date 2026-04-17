cmake_minimum_required(VERSION 3.24)
project(FileIndexer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(APP_SOURCES
    src/app/IndexApp.cpp
)

set(CLI_SOURCES
    src/cli/Cli.cpp
)

set(DB_SOURCES
    src/db/Database.cpp
)

set(HASHING_SOURCES
    src/hashing/Hasher.cpp
)

set(MODEL_SOURCES
    src/model/Duplicates.cpp
    src/model/Index.cpp
)

set(UTIL_SOURCES
    src/util/PlatformPaths.cpp
    src/util/FileTime.cpp
)

# Core library
add_library(fileindexer_core
    ${APP_SOURCES}
    ${CLI_SOURCES}
    ${DB_SOURCES}
    ${HASHING_SOURCES}
    ${MODEL_SOURCES}
    ${UTIL_SOURCES}
    external/sqlite/sqlite3.c
)

target_include_directories(fileindexer_core PUBLIC
    include/app
    include/cli
    include/db
    include/hashing
    include/model
    include/scanner
    include/util
    external/sqlite
    external/PicoSHA2
)

add_executable(file-indexer
    src/main.cpp
)

target_link_libraries(file-indexer PRIVATE fileindexer_core)
