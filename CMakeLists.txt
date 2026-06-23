cmake_minimum_required(VERSION 3.14)
project(RedisLikeKVStore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags for warnings and performance
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -O3)
endif()

# Directories
include_directories(include)

# Add src directory source files
set(SOURCES
    src/main.cpp
    src/net/Connection.cpp
    src/protocol/RESP.cpp
    src/server/CommandDispatcher.cpp
    src/server/Metrics.cpp
    src/server/Server.cpp
    src/storage/StorageEngine.cpp
    src/storage/AOFLogger.cpp
    src/util/Logger.cpp
)

add_executable(redis_kv_store ${SOURCES})
target_link_libraries(redis_kv_store pthread)

