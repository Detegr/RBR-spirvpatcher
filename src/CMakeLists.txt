include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/spirv-tools/include)

add_library(multiviewpatcher SHARED multiviewpatcher.cpp)
target_link_libraries(multiviewpatcher PRIVATE SPIRV-Tools SPIRV-Tools-opt)
