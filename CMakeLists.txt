cmake_minimum_required(VERSION 3.16)

project(multiviewpatcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(spirv-tools EXCLUDE_FROM_ALL)
add_subdirectory(src)
add_subdirectory(example)
