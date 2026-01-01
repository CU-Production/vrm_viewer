// GUI implementation for VRM Viewer using Clay
// This file is compiled as C to use Clay's designated initializer macros

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "util/sokol_gl.h"

// Font rendering
#include "fontstash.h"
#include "util/sokol_fontstash.h"

// Clay UI
#include "clay.h"
#include "sokol_clay.h"

#include "gui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Font ID
#define FONT_ID_BODY 0

// Fonts array
static sclay_font_t fonts[1];

// UI Colors
static Clay_Color COLOR_BG_PANEL = { 30, 30, 35, 230 };
static Clay_Color COLOR_BG_HEADER = { 45, 45, 55, 255 };
static Clay_Color COLOR_BG_SLIDER = { 50, 50, 60, 255 };
static Clay_Color COLOR_TEXT_PRIMARY = { 240, 240, 245, 255 };
static Clay_Color COLOR_TEXT_SECONDARY = { 160, 160, 170, 255 };
static Clay_Color COLOR_ACCENT = { 100, 150, 220, 255 };

// Helper: Create Clay_String from C string
static Clay_String make_string(const char* str) {
    Clay_String result;
    result.length = (int32_t)strlen(str);
    result.chars = str;
    result.isStaticallyAllocated = false;
    return result;
}

// Helper: Render a parameter row with label and value
static void gui_render_param_row(int index, const char* label, float value, const char* format_str) {
    char value_str[32];
    snprintf(value_str, sizeof(value_str), format_str, value);
    
    CLAY(CLAY_IDI("ParamRow", index), {
        .layout = { 
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(28) },
            .padding = CLAY_PADDING_ALL(4),
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childGap = 8
        }
    }) {
        // Label
        CLAY(CLAY_IDI("ParamLabel", index), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(120), .height = CLAY_SIZING_GROW(0) } }
        }) {
            Clay_String labelStr = make_string(label);
            Clay_TextElementConfig* labelCfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 14, .textColor = COLOR_TEXT_SECONDARY });
            CLAY_TEXT(labelStr, labelCfg);
        }
        // Value
        CLAY(CLAY_IDI("ParamValue", index), {
            .layout = { 
                .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
                .padding = CLAY_PADDING_ALL(4)
            },
            .backgroundColor = COLOR_BG_SLIDER,
            .cornerRadius = CLAY_CORNER_RADIUS(4)
        }) {
            Clay_String valueStr = make_string(value_str);
            Clay_TextElementConfig* valueCfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 14, .textColor = COLOR_TEXT_PRIMARY });
            CLAY_TEXT(valueStr, valueCfg);
        }
    }
}

// Helper: Render a text-only parameter row
static void gui_render_text_row(int index, const char* label, const char* value) {
    CLAY(CLAY_IDI("TextRow", index), {
        .layout = { 
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(24) },
            .padding = CLAY_PADDING_ALL(2),
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childGap = 8
        }
    }) {
        Clay_String labelStr = make_string(label);
        Clay_TextElementConfig* labelCfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 13, .textColor = COLOR_TEXT_SECONDARY });
        CLAY_TEXT(labelStr, labelCfg);
        
        Clay_String valueStr = make_string(value);
        Clay_TextElementConfig* valueCfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 13, .textColor = COLOR_TEXT_PRIMARY });
        CLAY_TEXT(valueStr, valueCfg);
    }
}

static Clay_RenderCommandArray create_gui_layout(const GuiState* state) {
    Clay_BeginLayout();
    
    Clay_Sizing layoutExpand = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) };
    
    // Root container
    CLAY(CLAY_ID("Root"), {
        .layout = { 
            .sizing = layoutExpand,
            .layoutDirection = CLAY_LEFT_TO_RIGHT
        }
    }) {
        // Left panel - Settings
        CLAY(CLAY_ID("LeftPanel"), {
            .layout = { 
                .sizing = { .width = CLAY_SIZING_FIXED(280), .height = CLAY_SIZING_GROW(0) },
                .padding = CLAY_PADDING_ALL(12),
                .childGap = 8,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            },
            .backgroundColor = COLOR_BG_PANEL,
            .cornerRadius = { 0, 8, 8, 0 }
        }) {
            // Header
            CLAY(CLAY_ID("Header"), {
                .layout = { 
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(40) },
                    .padding = CLAY_PADDING_ALL(8),
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
                },
                .backgroundColor = COLOR_BG_HEADER,
                .cornerRadius = CLAY_CORNER_RADIUS(6)
            }) {
                Clay_TextElementConfig* cfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 18, .textColor = COLOR_TEXT_PRIMARY });
                CLAY_TEXT(CLAY_STRING("VRM Viewer"), cfg);
            }
            
            // Model info section
            if (state->model_loaded) {
                CLAY(CLAY_ID("ModelInfo"), {
                    .layout = { 
                        .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIT(0) },
                        .padding = CLAY_PADDING_ALL(8),
                        .childGap = 4,
                        .layoutDirection = CLAY_TOP_TO_BOTTOM
                    },
                    .backgroundColor = COLOR_BG_HEADER,
                    .cornerRadius = CLAY_CORNER_RADIUS(6)
                }) {
                    Clay_TextElementConfig* cfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 14, .textColor = COLOR_ACCENT });
                    CLAY_TEXT(CLAY_STRING("Model Info"), cfg);
                    
                    char mesh_info[64];
                    snprintf(mesh_info, sizeof(mesh_info), "%d", state->mesh_count);
                    gui_render_text_row(0, "Meshes:", mesh_info);
                    
                    if (state->is_vrm_model) {
                        gui_render_text_row(1, "Type:", "VRM (Toon)");
                    } else {
                        gui_render_text_row(1, "Type:", "GLTF/GLB (PBR)");
                    }
                }
            }
            
            // Skybox settings
            CLAY(CLAY_ID("SkyboxSettings"), {
                .layout = { 
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIT(0) },
                    .padding = CLAY_PADDING_ALL(8),
                    .childGap = 4,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = COLOR_BG_HEADER,
                .cornerRadius = CLAY_CORNER_RADIUS(6)
            }) {
                Clay_TextElementConfig* cfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 14, .textColor = COLOR_ACCENT });
                CLAY_TEXT(CLAY_STRING("Environment"), cfg);
                
                gui_render_text_row(10, "Skybox:", state->show_skybox ? "ON" : "OFF");
                gui_render_param_row(11, "Exposure", state->skybox_exposure, "%.2f");
                gui_render_param_row(12, "LOD", state->skybox_lod, "%.1f");
            }
            
            // Toon shader settings (only when VRM is loaded)
            if (state->is_vrm_model) {
                CLAY(CLAY_ID("ToonSettings"), {
                    .layout = { 
                        .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIT(0) },
                        .padding = CLAY_PADDING_ALL(8),
                        .childGap = 4,
                        .layoutDirection = CLAY_TOP_TO_BOTTOM
                    },
                    .backgroundColor = COLOR_BG_HEADER,
                    .cornerRadius = CLAY_CORNER_RADIUS(6)
                }) {
                    Clay_TextElementConfig* cfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 14, .textColor = COLOR_ACCENT });
                    CLAY_TEXT(CLAY_STRING("Toon Shading"), cfg);
                    
                    gui_render_param_row(20, "Light Intensity", state->toon_light_intensity, "%.2f");
                    gui_render_param_row(21, "Shade Toony", state->toon_shade_toony, "%.2f");
                    gui_render_param_row(22, "Shade Strength", state->toon_shade_strength, "%.2f");
                    gui_render_param_row(23, "Rim Threshold", state->toon_rim_threshold, "%.2f");
                    gui_render_param_row(24, "Rim Softness", state->toon_rim_softness, "%.2f");
                    gui_render_param_row(25, "Spec Intensity", state->toon_spec_intensity, "%.2f");
                }
            }
            
            // Controls help
            CLAY(CLAY_ID("ControlsHelp"), {
                .layout = { 
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIT(0) },
                    .padding = CLAY_PADDING_ALL(8),
                    .childGap = 2,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = COLOR_BG_HEADER,
                .cornerRadius = CLAY_CORNER_RADIUS(6)
            }) {
                Clay_TextElementConfig* cfgTitle = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 14, .textColor = COLOR_ACCENT });
                Clay_TextElementConfig* cfgHelp = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 12, .textColor = COLOR_TEXT_SECONDARY });
                
                CLAY_TEXT(CLAY_STRING("Controls"), cfgTitle);
                CLAY_TEXT(CLAY_STRING("Drag: Rotate camera"), cfgHelp);
                CLAY_TEXT(CLAY_STRING("Scroll: Zoom"), cfgHelp);
                CLAY_TEXT(CLAY_STRING("G: Toggle GUI"), cfgHelp);
                CLAY_TEXT(CLAY_STRING("S: Toggle Skybox"), cfgHelp);
                CLAY_TEXT(CLAY_STRING("+/-: Adjust Exposure"), cfgHelp);
                CLAY_TEXT(CLAY_STRING("[/]: Adjust LOD"), cfgHelp);
            }
            
            // Spacer
            CLAY(CLAY_ID("Spacer"), { .layout = { .sizing = { .height = CLAY_SIZING_GROW(0) } } }) {}
            
            // Footer
            CLAY(CLAY_ID("Footer"), {
                .layout = { 
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIT(0) },
                    .padding = CLAY_PADDING_ALL(4)
                }
            }) {
                Clay_TextElementConfig* cfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 11, .textColor = COLOR_TEXT_SECONDARY });
                CLAY_TEXT(CLAY_STRING("Drop VRM/GLTF/GLB to load"), cfg);
            }
        }
    }
    
    return Clay_EndLayout();
}

void gui_init(void) {
    // Setup sokol-gl (required for Clay rendering)
    sgl_desc_t sgl_desc = {0};
    sgl_desc.logger.func = slog_func;
    sgl_setup(&sgl_desc);
    
    // Setup Clay UI
    sclay_setup();
    uint64_t clay_memory_size = Clay_MinMemorySize();
    Clay_Arena clay_arena = Clay_CreateArenaWithCapacityAndMemory(clay_memory_size, malloc(clay_memory_size));
    Clay_Initialize(clay_arena, (Clay_Dimensions){ (float)sapp_width(), (float)sapp_height() }, (Clay_ErrorHandler){0});
    
    // Load font for Clay
    fonts[0] = sclay_add_font("assets/font/Roboto-Regular.ttf");
    Clay_SetMeasureTextFunction(sclay_measure_text, fonts);
}

void gui_shutdown(void) {
    sclay_shutdown();
    sgl_shutdown();
}

void gui_handle_event(const sapp_event* ev) {
    sclay_handle_event(ev);
}

void gui_new_frame(void) {
    sclay_new_frame();
}

void gui_render(const GuiState* state) {
    if (!state->show_gui) return;
    
    Clay_RenderCommandArray render_commands = create_gui_layout(state);
    
    // Reset GL matrix for Clay rendering
    sgl_matrix_mode_modelview();
    sgl_load_identity();
    
    // Render Clay commands
    sclay_render(render_commands, fonts);
    
    // Draw GL commands
    sgl_draw();
}

int gui_is_hovered(void) {
    return Clay_PointerOver(Clay__HashString(CLAY_STRING("LeftPanel"), 0));
}
