# sokol
add_library(sokol INTERFACE)
target_include_directories(sokol INTERFACE sokol)

# VRM.h
add_library(vrm_h INTERFACE)
target_include_directories(vrm_h INTERFACE VRM.h/include)

# stb
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE stb)

# hmm
add_library(hmm INTERFACE)
target_include_directories(hmm INTERFACE HandmadeMath)

# parallel-util
add_library(parallel-util INTERFACE)
target_include_directories(parallel-util INTERFACE parallel-util)

# fontstash
add_library(fontstash INTERFACE)
target_include_directories(fontstash INTERFACE fontstash)

# clay
add_library(clay INTERFACE)
target_include_directories(clay INTERFACE clay)
