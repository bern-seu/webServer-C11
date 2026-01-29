# 设置cmake的最低版本和项目名称
cmake_minimum_required(VERSION 3.22)
project(WebServer)
set(CMAKE_BUILD_TYPE "Debug")

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

include_directories(${PROJECT_SOURCE_DIR}/code/buffer)
include_directories(${PROJECT_SOURCE_DIR}/code/http)
include_directories(${PROJECT_SOURCE_DIR}/code/log)
include_directories(${PROJECT_SOURCE_DIR}/code/pool)
include_directories(${PROJECT_SOURCE_DIR}/code/server)
include_directories(${PROJECT_SOURCE_DIR}/code/timer)

link_directories(${PROJECT_BINARY_DIR}/lib)

add_subdirectory(code)


