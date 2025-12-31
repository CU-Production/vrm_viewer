# sokol
add_library(sokol INTERFACE)
target_include_directories(sokol INTERFACE sokol)

# VRM.h
add_library(vrm_h INTERFACE)
target_include_directories(vrm_h INTERFACE VRM.h/include)
