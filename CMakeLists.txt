cmake_minimum_required(VERSION 3.16)
project(vrm_viewer)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(3rd_party)

# ============================================================================
# Shader compilation with sokol-shdc
# ============================================================================

# Find sokol-shdc executable
if(WIN32)
    set(SOKOL_SHDC_EXECUTABLE ${CMAKE_SOURCE_DIR}/3rd_party/sokol-tools-bin/bin/win32/sokol-shdc.exe)
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        set(SOKOL_SHDC_EXECUTABLE ${CMAKE_SOURCE_DIR}/3rd_party/sokol-tools-bin/bin/osx_arm64/sokol-shdc)
    else()
        set(SOKOL_SHDC_EXECUTABLE ${CMAKE_SOURCE_DIR}/3rd_party/sokol-tools-bin/bin/osx/sokol-shdc)
    endif()
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(SOKOL_SHDC_EXECUTABLE ${CMAKE_SOURCE_DIR}/3rd_party/sokol-tools-bin/bin/linux_arm64/sokol-shdc)
    else()
        set(SOKOL_SHDC_EXECUTABLE ${CMAKE_SOURCE_DIR}/3rd_party/sokol-tools-bin/bin/linux/sokol-shdc)
    endif()
endif()

# Shader language selection based on platform
set(SOKOL_SHDC_SLANG "glsl430:hlsl5:metal_macos")

# Function to compile sokol shaders
function(add_sokol_shader TARGET SHADER_SOURCE)
    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME_WE)
    get_filename_component(SHADER_DIR ${SHADER_SOURCE} DIRECTORY)
    
    set(SHADER_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE})
    set(SHADER_OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_DIR}/${SHADER_NAME}.glsl.h)
    
    if(SOKOL_SHDC_EXECUTABLE)
        add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND ${SOKOL_SHDC_EXECUTABLE}
                --input ${SHADER_INPUT}
                --output ${SHADER_OUTPUT}
                --slang ${SOKOL_SHDC_SLANG}
                --format sokol
            DEPENDS ${SHADER_INPUT}
            COMMENT "Compiling shader: ${SHADER_SOURCE} -> ${SHADER_NAME}.glsl.h"
            VERBATIM
        )
        
        # Create a custom target for this shader
        add_custom_target(${SHADER_NAME}_shader DEPENDS ${SHADER_OUTPUT})
        add_dependencies(${TARGET} ${SHADER_NAME}_shader)
    endif()
endfunction()

# ============================================================================
# Main executable
# ============================================================================

add_executable(vrm_viewer WIN32 main.cpp)
target_include_directories(vrm_viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vrm_viewer PRIVATE vrm_h sokol hmm stb)

# Compile shaders
add_sokol_shader(vrm_viewer shader/mesh.glsl)

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
