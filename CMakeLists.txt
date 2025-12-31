cmake_minimum_required(VERSION 3.16)
project(vrm_viewer)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(3rd_party)

add_executable(vrm_viewer WIN32 main.cpp)
target_link_libraries(vrm_viewer PRIVATE vrm_h sokol hmm stb)

# Platform-specific settings
if(WIN32)
    # For D3D11 backend on Windows
    target_compile_definitions(vrm_viewer PRIVATE SOKOL_D3D11)
    target_link_libraries(vrm_viewer PRIVATE
        d3d11
        dxgi
    )
elseif(APPLE)
    # For Metal backend on macOS
    target_compile_definitions(vrm_viewer PRIVATE SOKOL_METAL)
    target_link_libraries(vrm_viewer PRIVATE
        "-framework Metal"
        "-framework MetalKit"
        "-framework Cocoa"
        "-framework QuartzCore"
    )
else()
    # For OpenGL backend on Linux
    target_compile_definitions(vrm_viewer PRIVATE SOKOL_GLCORE)
    target_link_libraries(vrm_viewer PRIVATE
        GL
        X11
        Xi
        Xcursor
        dl
        pthread
        m
    )
endif()
