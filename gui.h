// GUI interface for VRM Viewer (C header for C++ compatibility)
#ifndef GUI_H
#define GUI_H

#include "sokol_app.h"
#include "sokol_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

// GUI state that can be modified from C++
typedef struct {
    // Model info
    int model_loaded;
    int is_vrm_model;
    int mesh_count;
    
    // Skybox settings
    int show_skybox;
    float skybox_exposure;
    float skybox_lod;
    
    // Toon shader parameters
    float toon_light_intensity;
    float toon_shade_toony;
    float toon_shade_strength;
    float toon_rim_threshold;
    float toon_rim_softness;
    float toon_spec_intensity;
    
    // GUI state
    int show_gui;
    int gui_hovered;
} GuiState;

// Initialize GUI system (call after sg_setup)
void gui_init(void);

// Shutdown GUI system
void gui_shutdown(void);

// Handle input events
void gui_handle_event(const sapp_event* ev);

// Begin new frame (call at start of frame)
void gui_new_frame(void);

// Render GUI (call before sg_end_pass)
void gui_render(const GuiState* state);

// Check if mouse is over GUI panel
int gui_is_hovered(void);

#ifdef __cplusplus
}
#endif

#endif // GUI_H
