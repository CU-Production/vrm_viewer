cmake_minimum_required(VERSION 4.1)
project(vrm_viewer)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(3rd_party)

add_executable(vrm_viewer main.cpp)
target_link_libraries(vrm_viewer PRIVATE vrm_h sokol)
