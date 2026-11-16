cmake_minimum_required(VERSION 3.22.1)
project(geiss_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(geiss_native SHARED
    geiss_engine.cpp
    geiss_jni.cpp
)

target_compile_options(geiss_native PRIVATE
    -O3
    -Wall
    -Wextra
    -Wno-unused-parameter
)

find_library(log-lib log)
find_library(gles2-lib GLESv2)

target_link_libraries(geiss_native
    ${gles2-lib}
    ${log-lib}
)
