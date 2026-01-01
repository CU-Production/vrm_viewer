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
static Clay_Color COLOR_BG_SLIDER_TRACK = { 40, 40, 50, 255 };
static Clay_Color COLOR_BG_SLIDER_FILL = { 80, 130, 200, 255 };
static Clay_Color COLOR_BG_SLIDER_HOVER = { 60, 60, 75, 255 };
static Clay_Color COLOR_TEXT_PRIMARY = { 240, 240, 245, 255 };
static Clay_Color COLOR_TEXT_SECONDARY = { 160, 160, 170, 255 };
static Clay_Color COLOR_ACCENT = { 100, 150, 220, 255 };
static Clay_Color COLOR_TOGGLE_ON = { 80, 160, 120, 255 };
static Clay_Color COLOR_TOGGLE_OFF = { 60, 60, 70, 255 };

// Current GUI state pointer for slider interactions
static GuiState* g_current_state = NULL;

// Helper: Create Clay_String from C string
static Clay_String make_string(const char* str) {
    Clay_String result;
    result.length = (int32_t)strlen(str);
    result.chars = str;
    result.isStaticallyAllocated = false;
    return result;
}

// Helper: Check if point is inside a bounding box
static int point_in_box(float px, float py, Clay_BoundingBox box) {
    return px >= box.x && px <= box.x + box.width &&
           py >= box.y && py <= box.y + box.height;
}

// Helper: Update slider value based on mouse position within bounding box
static void update_slider_from_mouse(float* value, float min_val, float max_val, Clay_BoundingBox box, float mouseX) {
    float relativeX = mouseX - box.x;
    float normalized = relativeX / box.width;
    
    // Clamp to 0-1 range
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    
    // Map to value range
    *value = min_val + normalized * (max_val - min_val);
}

// Helper: Render an interactive slider
static void gui_render_slider(int id, const char* label, float* value, float min_val, float max_val, const char* format_str) {
    char value_str[32];
    snprintf(value_str, sizeof(value_str), format_str, *value);
    
    // Calculate fill percentage (0-1 range)
    float normalized = (*value - min_val) / (max_val - min_val);
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    
    // Get track element ID for interaction
    Clay_ElementId trackId = CLAY_IDI("SliderTrack", id);
    
    // Check hover and handle interaction after layout
    bool isHovered = Clay_PointerOver(trackId);
    
    // If mouse is pressed and hovering this slider, update value
    if (g_current_state && g_current_state->mouse_pressed && isHovered) {
        Clay_ElementData elemData = Clay_GetElementData(trackId);
        if (elemData.found) {
            update_slider_from_mouse(value, min_val, max_val, elemData.boundingBox, g_current_state->mouse_x);
        }
    }
    
    Clay_Color trackColor = isHovered ? COLOR_BG_SLIDER_HOVER : COLOR_BG_SLIDER_TRACK;
    
    CLAY(CLAY_IDI("SliderRow", id), {
        .layout = { 
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(26) },
            .padding = { 4, 4, 2, 2 },
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childGap = 6
        }
    }) {
        // Label
        CLAY(CLAY_IDI("SliderLabel", id), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(85), .height = CLAY_SIZING_GROW(0) } }
        }) {
            Clay_String labelStr = make_string(label);
            Clay_TextElementConfig* labelCfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 12, .textColor = COLOR_TEXT_SECONDARY });
            CLAY_TEXT(labelStr, labelCfg);
        }
        
        // Slider track
        CLAY(trackId, {
            .layout = { 
                .sizing = { .width = CLAY_SIZING_FIXED(100), .height = CLAY_SIZING_FIXED(10) },
            },
            .backgroundColor = trackColor,
            .cornerRadius = CLAY_CORNER_RADIUS(5)
        }) {
            // Filled portion
            if (normalized > 0.01f) {
                CLAY(CLAY_IDI("SliderFill", id), {
                    .layout = { 
                        .sizing = { .width = CLAY_SIZING_PERCENT(normalized), .height = CLAY_SIZING_GROW(0) }
                    },
                    .backgroundColor = COLOR_BG_SLIDER_FILL,
                    .cornerRadius = CLAY_CORNER_RADIUS(5)
                }) {}
            }
        }
        
        // Value display
        CLAY(CLAY_IDI("SliderValue", id), {
            .layout = { 
                .sizing = { .width = CLAY_SIZING_FIXED(42), .height = CLAY_SIZING_GROW(0) },
                .childAlignment = { .x = CLAY_ALIGN_X_RIGHT }
            }
        }) {
            Clay_String valueStr = make_string(value_str);
            Clay_TextElementConfig* valueCfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 11, .textColor = COLOR_TEXT_PRIMARY });
            CLAY_TEXT(valueStr, valueCfg);
        }
    }
}

// Helper: Render a text-only info row
static void gui_render_text_row(int index, const char* label, const char* value) {
    CLAY(CLAY_IDI("TextRow", index), {
        .layout = { 
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(22) },
            .padding = CLAY_PADDING_ALL(2),
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childGap = 8
        }
    }) {
        Clay_String labelStr = make_string(label);
        Clay_TextElementConfig* labelCfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 12, .textColor = COLOR_TEXT_SECONDARY });
        CLAY_TEXT(labelStr, labelCfg);
        
        Clay_String valueStr = make_string(value);
        Clay_TextElementConfig* valueCfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 12, .textColor = COLOR_TEXT_PRIMARY });
        CLAY_TEXT(valueStr, valueCfg);
    }
}

// Helper: Render a toggle button
static void gui_render_toggle(int id, const char* label, int* value) {
    Clay_ElementId toggleId = CLAY_IDI("ToggleButton", id);
    bool isHovered = Clay_PointerOver(toggleId);
    
    // Handle click
    if (g_current_state && g_current_state->mouse_pressed && isHovered) {
        Clay_ElementData elemData = Clay_GetElementData(toggleId);
        if (elemData.found) {
            // Toggle on click (only on first frame of press)
            static int last_toggle_id = -1;
            if (last_toggle_id != id) {
                *value = !(*value);
                last_toggle_id = id;
            }
        }
    } else if (!g_current_state || !g_current_state->mouse_pressed) {
        // Reset when mouse released
        static int last_toggle_id = -1;
        last_toggle_id = -1;
    }
    
    Clay_Color toggleColor = *value ? COLOR_TOGGLE_ON : COLOR_TOGGLE_OFF;
    if (isHovered) {
        toggleColor.r += 20;
        toggleColor.g += 20;
        toggleColor.b += 20;
    }
    const char* stateText = *value ? "ON" : "OFF";
    
    CLAY(CLAY_IDI("ToggleRow", id), {
        .layout = { 
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(26) },
            .padding = { 4, 4, 2, 2 },
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childGap = 8
        }
    }) {
        // Label
        CLAY(CLAY_IDI("ToggleLabel", id), {
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(85), .height = CLAY_SIZING_GROW(0) } }
        }) {
            Clay_String labelStr = make_string(label);
            Clay_TextElementConfig* labelCfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 12, .textColor = COLOR_TEXT_SECONDARY });
            CLAY_TEXT(labelStr, labelCfg);
        }
        
        // Toggle button
        CLAY(toggleId, {
            .layout = { 
                .sizing = { .width = CLAY_SIZING_FIXED(50), .height = CLAY_SIZING_FIXED(20) },
                .padding = CLAY_PADDING_ALL(2),
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
            },
            .backgroundColor = toggleColor,
            .cornerRadius = CLAY_CORNER_RADIUS(4)
        }) {
            Clay_String stateStr = make_string(stateText);
            Clay_TextElementConfig* cfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 11, .textColor = COLOR_TEXT_PRIMARY });
            CLAY_TEXT(stateStr, cfg);
        }
    }
}

static Clay_RenderCommandArray create_gui_layout(GuiState* state) {
    g_current_state = state;
    
    Clay_BeginLayout();
    
    Clay_Sizing layoutExpand = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) };
    
    // Root container
    CLAY(CLAY_ID("Root"), {
        .layout = { 
            .sizing = layoutExpand,
            .layoutDirection = CLAY_LEFT_TO_RIGHT
        }
    }) {
        // Left panel
        CLAY(CLAY_ID("LeftPanel"), {
            .layout = { 
                .sizing = { .width = CLAY_SIZING_FIXED(280), .height = CLAY_SIZING_GROW(0) },
                .padding = CLAY_PADDING_ALL(10),
                .childGap = 6,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            },
            .backgroundColor = COLOR_BG_PANEL,
            .cornerRadius = { 0, 8, 8, 0 }
        }) {
            // Header
            CLAY(CLAY_ID("Header"), {
                .layout = { 
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(36) },
                    .padding = CLAY_PADDING_ALL(8),
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
                },
                .backgroundColor = COLOR_BG_HEADER,
                .cornerRadius = CLAY_CORNER_RADIUS(6)
            }) {
                Clay_TextElementConfig* cfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 16, .textColor = COLOR_TEXT_PRIMARY });
                CLAY_TEXT(CLAY_STRING("VRM/GLTF/GLB Viewer"), cfg);
            }
            
            // Model info
            if (state->model_loaded) {
                CLAY(CLAY_ID("ModelInfo"), {
                    .layout = { 
                        .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIT(0) },
                        .padding = CLAY_PADDING_ALL(6),
                        .childGap = 2,
                        .layoutDirection = CLAY_TOP_TO_BOTTOM
                    },
                    .backgroundColor = COLOR_BG_HEADER,
                    .cornerRadius = CLAY_CORNER_RADIUS(6)
                }) {
                    Clay_TextElementConfig* cfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 13, .textColor = COLOR_ACCENT });
                    CLAY_TEXT(CLAY_STRING("Model"), cfg);
                    
                    char mesh_info[64];
                    snprintf(mesh_info, sizeof(mesh_info), "%d", state->mesh_count);
                    gui_render_text_row(0, "Meshes:", mesh_info);
                    gui_render_text_row(1, "Type:", state->is_vrm_model ? "VRM (Toon)" : "GLTF (PBR)");
                }
            }
            
            // Environment
            CLAY(CLAY_ID("EnvSettings"), {
                .layout = { 
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIT(0) },
                    .padding = CLAY_PADDING_ALL(6),
                    .childGap = 2,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = COLOR_BG_HEADER,
                .cornerRadius = CLAY_CORNER_RADIUS(6)
            }) {
                Clay_TextElementConfig* cfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 13, .textColor = COLOR_ACCENT });
                CLAY_TEXT(CLAY_STRING("Environment"), cfg);
                
                gui_render_toggle(100, "Skybox", &state->show_skybox);
                gui_render_slider(101, "Exposure", &state->skybox_exposure, 0.1f, 5.0f, "%.2f");
                gui_render_slider(102, "LOD", &state->skybox_lod, 0.0f, 4.0f, "%.1f");
            }
            
            // Toon settings
            if (state->is_vrm_model) {
                CLAY(CLAY_ID("ToonSettings"), {
                    .layout = { 
                        .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIT(0) },
                        .padding = CLAY_PADDING_ALL(6),
                        .childGap = 2,
                        .layoutDirection = CLAY_TOP_TO_BOTTOM
                    },
                    .backgroundColor = COLOR_BG_HEADER,
                    .cornerRadius = CLAY_CORNER_RADIUS(6)
                }) {
                    Clay_TextElementConfig* cfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 13, .textColor = COLOR_ACCENT });
                    CLAY_TEXT(CLAY_STRING("Toon Shading"), cfg);
                    
                    gui_render_slider(200, "Light", &state->toon_light_intensity, 0.0f, 3.0f, "%.2f");
                    gui_render_slider(201, "Toony", &state->toon_shade_toony, 0.0f, 1.0f, "%.2f");
                    gui_render_slider(202, "Shadow", &state->toon_shade_strength, 0.0f, 1.0f, "%.2f");
                    gui_render_slider(203, "Rim Thr", &state->toon_rim_threshold, 0.0f, 1.0f, "%.2f");
                    gui_render_slider(204, "Rim Soft", &state->toon_rim_softness, 0.0f, 1.0f, "%.2f");
                    gui_render_slider(205, "Specular", &state->toon_spec_intensity, 0.0f, 1.0f, "%.2f");
                }
            }
            
            // Controls
            CLAY(CLAY_ID("ControlsHelp"), {
                .layout = { 
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIT(0) },
                    .padding = CLAY_PADDING_ALL(6),
                    .childGap = 1,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = COLOR_BG_HEADER,
                .cornerRadius = CLAY_CORNER_RADIUS(6)
            }) {
                Clay_TextElementConfig* cfgTitle = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 13, .textColor = COLOR_ACCENT });
                Clay_TextElementConfig* cfgHelp = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 10, .textColor = COLOR_TEXT_SECONDARY });
                
                CLAY_TEXT(CLAY_STRING("Controls"), cfgTitle);
                CLAY_TEXT(CLAY_STRING("Drag: Rotate | Scroll: Zoom | R: Reset"), cfgHelp);
                CLAY_TEXT(CLAY_STRING("G: Toggle GUI | S: Toggle Skybox"), cfgHelp);
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
                Clay_TextElementConfig* cfg = CLAY_TEXT_CONFIG({ .fontId = FONT_ID_BODY, .fontSize = 10, .textColor = COLOR_TEXT_SECONDARY });
                CLAY_TEXT(CLAY_STRING("Drop VRM/GLTF/GLB file to load"), cfg);
            }
        }
    }
    
    return Clay_EndLayout();
}

void gui_init(void) {
    sgl_desc_t sgl_desc = {0};
    sgl_desc.logger.func = slog_func;
    sgl_setup(&sgl_desc);
    
    sclay_setup();
    uint64_t clay_memory_size = Clay_MinMemorySize();
    Clay_Arena clay_arena = Clay_CreateArenaWithCapacityAndMemory(clay_memory_size, malloc(clay_memory_size));
    Clay_Initialize(clay_arena, (Clay_Dimensions){ (float)sapp_width(), (float)sapp_height() }, (Clay_ErrorHandler){0});
    
    fonts[0] = sclay_add_font("assets/font/Roboto-Regular.ttf");
    Clay_SetMeasureTextFunction(sclay_measure_text, fonts);
}

void gui_shutdown(void) {
    sclay_shutdown();
    sgl_shutdown();
}

int gui_handle_event(const sapp_event* ev) {
    sclay_handle_event(ev);
    return 0;
}

void gui_new_frame(void) {
    sclay_new_frame();
}

int gui_render(GuiState* state) {
    if (!state->show_gui) return 0;
    
    Clay_RenderCommandArray render_commands = create_gui_layout(state);
    
    sgl_matrix_mode_modelview();
    sgl_load_identity();
    
    sclay_render(render_commands, fonts);
    sgl_draw();
    
    return 0;
}

int gui_is_hovered(void) {
    return Clay_PointerOver(Clay__HashString(CLAY_STRING("LeftPanel"), 0));
}
