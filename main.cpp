// VRM/GLTF/GLB Viewer using Sokol
// A simple 3D model viewer for VRM, GLTF, and GLB files

#define HANDMADE_MATH_USE_DEGREES
#include "HandmadeMath.h"

#define SOKOL_IMPL
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "sokol_glue.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <vector>
#include <string>
#include <cstdio>
#include <cmath>

// ============================================================================
// Shader code (HLSL for D3D11)
// ============================================================================

#if defined(SOKOL_D3D11)
static const char* vs_source = R"(
cbuffer vs_params : register(b0) {
    float4x4 mvp;
    float4x4 model;
    float3 light_dir;
    float _pad0;
};

struct vs_in {
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct vs_out {
    float4 pos : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 world_pos : TEXCOORD1;
};

vs_out main(vs_in inp) {
    vs_out outp;
    outp.pos = mul(mvp, float4(inp.pos, 1.0));
    outp.normal = mul((float3x3)model, inp.normal);
    outp.uv = inp.uv;
    outp.world_pos = mul(model, float4(inp.pos, 1.0)).xyz;
    return outp;
}
)";

static const char* fs_source = R"(
cbuffer fs_params : register(b0) {
    float4 base_color;
    float3 light_dir;
    float _pad0;
    float3 ambient;
    float _pad1;
};

Texture2D tex : register(t0);
SamplerState smp : register(s0);

struct fs_in {
    float4 pos : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 world_pos : TEXCOORD1;
};

float4 main(fs_in inp) : SV_Target0 {
    float3 n = normalize(inp.normal);
    float ndotl = max(dot(n, normalize(light_dir)), 0.0);
    
    float4 tex_color = tex.Sample(smp, inp.uv);
    float3 color = base_color.rgb * tex_color.rgb;
    
    float3 lit_color = ambient * color + ndotl * color;
    return float4(lit_color, base_color.a * tex_color.a);
}
)";

#elif defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
static const char* vs_source = R"(
#version 330
uniform mat4 mvp;
uniform mat4 model;
uniform vec3 light_dir;

layout(location=0) in vec3 pos;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;

out vec3 v_normal;
out vec2 v_uv;
out vec3 v_world_pos;

void main() {
    gl_Position = mvp * vec4(pos, 1.0);
    v_normal = mat3(model) * normal;
    v_uv = uv;
    v_world_pos = (model * vec4(pos, 1.0)).xyz;
}
)";

static const char* fs_source = R"(
#version 330
uniform vec4 base_color;
uniform vec3 light_dir;
uniform vec3 ambient;
uniform sampler2D tex;

in vec3 v_normal;
in vec2 v_uv;
in vec3 v_world_pos;

out vec4 frag_color;

void main() {
    vec3 n = normalize(v_normal);
    float ndotl = max(dot(n, normalize(light_dir)), 0.0);
    
    vec4 tex_color = texture(tex, v_uv);
    vec3 color = base_color.rgb * tex_color.rgb;
    
    vec3 lit_color = ambient * color + ndotl * color;
    frag_color = vec4(lit_color, base_color.a * tex_color.a);
}
)";

#elif defined(SOKOL_METAL)
static const char* vs_source = R"(
#include <metal_stdlib>
using namespace metal;

struct vs_params {
    float4x4 mvp;
    float4x4 model;
    float3 light_dir;
};

struct vs_in {
    float3 pos [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float2 uv [[attribute(2)]];
};

struct vs_out {
    float4 pos [[position]];
    float3 normal;
    float2 uv;
    float3 world_pos;
};

vertex vs_out vs_main(vs_in inp [[stage_in]], constant vs_params& params [[buffer(0)]]) {
    vs_out outp;
    outp.pos = params.mvp * float4(inp.pos, 1.0);
    outp.normal = (params.model * float4(inp.normal, 0.0)).xyz;
    outp.uv = inp.uv;
    outp.world_pos = (params.model * float4(inp.pos, 1.0)).xyz;
    return outp;
}
)";

static const char* fs_source = R"(
#include <metal_stdlib>
using namespace metal;

struct fs_params {
    float4 base_color;
    float3 light_dir;
    float3 ambient;
};

struct fs_in {
    float4 pos [[position]];
    float3 normal;
    float2 uv;
    float3 world_pos;
};

fragment float4 fs_main(fs_in inp [[stage_in]], 
                        constant fs_params& params [[buffer(0)]],
                        texture2d<float> tex [[texture(0)]],
                        sampler smp [[sampler(0)]]) {
    float3 n = normalize(inp.normal);
    float ndotl = max(dot(n, normalize(params.light_dir)), 0.0);
    
    float4 tex_color = tex.sample(smp, inp.uv);
    float3 color = params.base_color.rgb * tex_color.rgb;
    
    float3 lit_color = params.ambient * color + ndotl * color;
    return float4(lit_color, params.base_color.a * tex_color.a);
}
)";
#endif

// ============================================================================
// Uniform structs
// ============================================================================

struct vs_params_t {
    HMM_Mat4 mvp;
    HMM_Mat4 model;
    HMM_Vec3 light_dir;
    float _pad0;
};

struct fs_params_t {
    HMM_Vec4 base_color;
    HMM_Vec3 light_dir;
    float _pad0;
    HMM_Vec3 ambient;
    float _pad1;
};

// ============================================================================
// Mesh structure for rendering
// ============================================================================

struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
};

struct RenderMesh {
    sg_buffer vertex_buffer;
    sg_buffer index_buffer;
    int num_indices;
    sg_image texture;
    sg_view texture_view;  // View object for texture binding
    HMM_Vec4 base_color;
    bool has_indices;
    int num_vertices;
};

struct Model {
    std::vector<RenderMesh> meshes;
    HMM_Vec3 center;
    float radius;
};

// ============================================================================
// Global state
// ============================================================================

static struct {
    sg_pipeline pip;
    sg_sampler smp;
    sg_image default_texture;
    sg_view default_texture_view;  // View for default texture
    Model model;
    bool model_loaded;
    
    // Camera
    float cam_distance;
    float cam_azimuth;
    float cam_elevation;
    HMM_Vec3 cam_target;
    
    // Input
    bool mouse_down;
    float last_mouse_x;
    float last_mouse_y;
    
    // Animation
    float time;
} state;

// ============================================================================
// Helper functions
// ============================================================================

static void log_message(const char* msg) {
    printf("[VRM Viewer] %s\n", msg);
}

static sg_view create_texture_view(sg_image img) {
    sg_view_desc view_desc = {};
    view_desc.texture.image = img;
    return sg_make_view(&view_desc);
}

static sg_image create_default_white_texture() {
    uint32_t pixels[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
    sg_image_desc desc = {};
    desc.width = 2;
    desc.height = 2;
    desc.data.mip_levels[0] = { pixels, sizeof(pixels) };
    desc.label = "default-texture";
    return sg_make_image(&desc);
}

static sg_image load_texture_from_buffer(const uint8_t* data, size_t size) {
    int width, height, channels;
    stbi_set_flip_vertically_on_load(0);
    uint8_t* pixels = stbi_load_from_memory(data, (int)size, &width, &height, &channels, 4);
    if (!pixels) {
        log_message("Failed to load texture from buffer");
        return state.default_texture;
    }
    
    sg_image_desc desc = {};
    desc.width = width;
    desc.height = height;
    desc.data.mip_levels[0] = { pixels, (size_t)(width * height * 4) };
    sg_image img = sg_make_image(&desc);
    
    stbi_image_free(pixels);
    return img;
}

static sg_image load_texture_from_file(const char* base_path, const char* uri) {
    // Build full path
    std::string path = base_path;
    size_t last_slash = path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        path = path.substr(0, last_slash + 1);
    } else {
        path = "";
    }
    path += uri;
    
    int width, height, channels;
    stbi_set_flip_vertically_on_load(0);
    uint8_t* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        log_message(("Failed to load texture: " + path).c_str());
        return state.default_texture;
    }
    
    sg_image_desc desc = {};
    desc.width = width;
    desc.height = height;
    desc.data.mip_levels[0] = { pixels, (size_t)(width * height * 4) };
    sg_image img = sg_make_image(&desc);
    
    stbi_image_free(pixels);
    log_message(("Loaded texture: " + path).c_str());
    return img;
}

// ============================================================================
// GLTF/GLB/VRM Loading
// ============================================================================

static bool load_model(const char* filepath) {
    log_message(("Loading model: " + std::string(filepath)).c_str());
    
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    
    cgltf_result result = cgltf_parse_file(&options, filepath, &data);
    if (result != cgltf_result_success) {
        log_message("Failed to parse GLTF file");
        return false;
    }
    
    result = cgltf_load_buffers(&options, data, filepath);
    if (result != cgltf_result_success) {
        log_message("Failed to load GLTF buffers");
        cgltf_free(data);
        return false;
    }
    
    // Clear existing model
    for (auto& mesh : state.model.meshes) {
        sg_destroy_buffer(mesh.vertex_buffer);
        if (mesh.has_indices) {
            sg_destroy_buffer(mesh.index_buffer);
        }
        if (mesh.texture.id != state.default_texture.id) {
            sg_destroy_view(mesh.texture_view);
            sg_destroy_image(mesh.texture);
        }
    }
    state.model.meshes.clear();
    
    // Calculate bounding box for camera positioning
    HMM_Vec3 min_bounds = HMM_V3(1e10f, 1e10f, 1e10f);
    HMM_Vec3 max_bounds = HMM_V3(-1e10f, -1e10f, -1e10f);
    
    // Load textures
    std::vector<sg_image> textures(data->images_count, state.default_texture);
    std::vector<sg_view> texture_views(data->images_count, state.default_texture_view);
    for (size_t i = 0; i < data->images_count; i++) {
        cgltf_image* image = &data->images[i];
        
        if (image->buffer_view) {
            // Embedded texture
            const uint8_t* buffer_data = (const uint8_t*)image->buffer_view->buffer->data;
            buffer_data += image->buffer_view->offset;
            textures[i] = load_texture_from_buffer(buffer_data, image->buffer_view->size);
        } else if (image->uri) {
            // External texture file
            textures[i] = load_texture_from_file(filepath, image->uri);
        }
        
        // Create view for texture if it's not the default
        if (textures[i].id != state.default_texture.id) {
            texture_views[i] = create_texture_view(textures[i]);
        }
    }
    
    // Process all meshes in all nodes
    for (size_t ni = 0; ni < data->nodes_count; ni++) {
        cgltf_node* node = &data->nodes[ni];
        if (!node->mesh) continue;
        
        // Get node transform
        float node_matrix[16];
        cgltf_node_transform_world(node, node_matrix);
        
        cgltf_mesh* mesh = node->mesh;
        
        for (size_t pi = 0; pi < mesh->primitives_count; pi++) {
            cgltf_primitive* prim = &mesh->primitives[pi];
            
            if (prim->type != cgltf_primitive_type_triangles) {
                continue;
            }
            
            // Find position, normal, and texcoord accessors
            cgltf_accessor* pos_accessor = nullptr;
            cgltf_accessor* norm_accessor = nullptr;
            cgltf_accessor* uv_accessor = nullptr;
            
            for (size_t ai = 0; ai < prim->attributes_count; ai++) {
                cgltf_attribute* attr = &prim->attributes[ai];
                switch (attr->type) {
                    case cgltf_attribute_type_position:
                        pos_accessor = attr->data;
                        break;
                    case cgltf_attribute_type_normal:
                        norm_accessor = attr->data;
                        break;
                    case cgltf_attribute_type_texcoord:
                        if (attr->index == 0) {
                            uv_accessor = attr->data;
                        }
                        break;
                    default:
                        break;
                }
            }
            
            if (!pos_accessor) {
                continue;
            }
            
            size_t vertex_count = pos_accessor->count;
            std::vector<Vertex> vertices(vertex_count);
            
            // Read positions
            for (size_t vi = 0; vi < vertex_count; vi++) {
                float pos[3] = {0, 0, 0};
                cgltf_accessor_read_float(pos_accessor, vi, pos, 3);
                
                // Apply node transform
                float tx = node_matrix[0]*pos[0] + node_matrix[4]*pos[1] + node_matrix[8]*pos[2] + node_matrix[12];
                float ty = node_matrix[1]*pos[0] + node_matrix[5]*pos[1] + node_matrix[9]*pos[2] + node_matrix[13];
                float tz = node_matrix[2]*pos[0] + node_matrix[6]*pos[1] + node_matrix[10]*pos[2] + node_matrix[14];
                
                vertices[vi].pos[0] = tx;
                vertices[vi].pos[1] = ty;
                vertices[vi].pos[2] = tz;
                
                // Update bounds
                min_bounds.X = HMM_MIN(min_bounds.X, tx);
                min_bounds.Y = HMM_MIN(min_bounds.Y, ty);
                min_bounds.Z = HMM_MIN(min_bounds.Z, tz);
                max_bounds.X = HMM_MAX(max_bounds.X, tx);
                max_bounds.Y = HMM_MAX(max_bounds.Y, ty);
                max_bounds.Z = HMM_MAX(max_bounds.Z, tz);
            }
            
            // Read normals
            if (norm_accessor) {
                for (size_t vi = 0; vi < vertex_count; vi++) {
                    float norm[3] = {0, 1, 0};
                    cgltf_accessor_read_float(norm_accessor, vi, norm, 3);
                    
                    // Apply node rotation (ignore scale for normals)
                    float nx = node_matrix[0]*norm[0] + node_matrix[4]*norm[1] + node_matrix[8]*norm[2];
                    float ny = node_matrix[1]*norm[0] + node_matrix[5]*norm[1] + node_matrix[9]*norm[2];
                    float nz = node_matrix[2]*norm[0] + node_matrix[6]*norm[1] + node_matrix[10]*norm[2];
                    float len = sqrtf(nx*nx + ny*ny + nz*nz);
                    if (len > 0.0001f) {
                        vertices[vi].normal[0] = nx / len;
                        vertices[vi].normal[1] = ny / len;
                        vertices[vi].normal[2] = nz / len;
                    } else {
                        vertices[vi].normal[0] = 0;
                        vertices[vi].normal[1] = 1;
                        vertices[vi].normal[2] = 0;
                    }
                }
            } else {
                for (size_t vi = 0; vi < vertex_count; vi++) {
                    vertices[vi].normal[0] = 0;
                    vertices[vi].normal[1] = 1;
                    vertices[vi].normal[2] = 0;
                }
            }
            
            // Read UVs
            if (uv_accessor) {
                for (size_t vi = 0; vi < vertex_count; vi++) {
                    float uv[2] = {0, 0};
                    cgltf_accessor_read_float(uv_accessor, vi, uv, 2);
                    vertices[vi].uv[0] = uv[0];
                    vertices[vi].uv[1] = uv[1];
                }
            } else {
                for (size_t vi = 0; vi < vertex_count; vi++) {
                    vertices[vi].uv[0] = 0;
                    vertices[vi].uv[1] = 0;
                }
            }
            
            // Create render mesh
            RenderMesh render_mesh = {};
            
            // Create vertex buffer
            sg_buffer_desc vbuf_desc = {};
            vbuf_desc.data = { vertices.data(), vertices.size() * sizeof(Vertex) };
            vbuf_desc.label = "mesh-vertices";
            render_mesh.vertex_buffer = sg_make_buffer(&vbuf_desc);
            render_mesh.num_vertices = (int)vertex_count;
            
            // Read indices if available
            if (prim->indices) {
                size_t index_count = prim->indices->count;
                std::vector<uint32_t> indices(index_count);
                
                for (size_t ii = 0; ii < index_count; ii++) {
                    indices[ii] = (uint32_t)cgltf_accessor_read_index(prim->indices, ii);
                }
                
                sg_buffer_desc ibuf_desc = {};
                ibuf_desc.usage.index_buffer = true;
                ibuf_desc.data = { indices.data(), indices.size() * sizeof(uint32_t) };
                ibuf_desc.label = "mesh-indices";
                render_mesh.index_buffer = sg_make_buffer(&ibuf_desc);
                render_mesh.num_indices = (int)index_count;
                render_mesh.has_indices = true;
            } else {
                render_mesh.has_indices = false;
            }
            
            // Get material
            render_mesh.base_color = HMM_V4(1.0f, 1.0f, 1.0f, 1.0f);
            render_mesh.texture = state.default_texture;
            render_mesh.texture_view = state.default_texture_view;
            
            if (prim->material) {
                cgltf_material* mat = prim->material;
                
                if (mat->has_pbr_metallic_roughness) {
                    cgltf_pbr_metallic_roughness* pbr = &mat->pbr_metallic_roughness;
                    render_mesh.base_color = HMM_V4(
                        pbr->base_color_factor[0],
                        pbr->base_color_factor[1],
                        pbr->base_color_factor[2],
                        pbr->base_color_factor[3]
                    );
                    
                    if (pbr->base_color_texture.texture) {
                        cgltf_texture* tex = pbr->base_color_texture.texture;
                        if (tex->image) {
                            size_t img_idx = tex->image - data->images;
                            if (img_idx < textures.size()) {
                                render_mesh.texture = textures[img_idx];
                                render_mesh.texture_view = texture_views[img_idx];
                            }
                        }
                    }
                }
            }
            
            state.model.meshes.push_back(render_mesh);
        }
    }
    
    cgltf_free(data);
    
    // Calculate model center and radius
    state.model.center = HMM_MulV3F(HMM_AddV3(min_bounds, max_bounds), 0.5f);
    HMM_Vec3 extent = HMM_SubV3(max_bounds, min_bounds);
    state.model.radius = HMM_LenV3(extent) * 0.5f;
    
    if (state.model.radius < 0.001f) {
        state.model.radius = 1.0f;
    }
    
    // Set up camera to view the model
    state.cam_target = state.model.center;
    state.cam_distance = state.model.radius * 2.5f;
    state.cam_elevation = 15.0f;
    state.cam_azimuth = 45.0f;
    
    log_message(("Loaded " + std::to_string(state.model.meshes.size()) + " mesh(es)").c_str());
    state.model_loaded = true;
    
    return true;
}

// ============================================================================
// Sokol callbacks
// ============================================================================

static void init() {
    log_message("Initializing...");
    
    // Setup sokol-gfx
    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    
    // Create default texture and its view
    state.default_texture = create_default_white_texture();
    state.default_texture_view = create_texture_view(state.default_texture);
    
    // Create sampler
    sg_sampler_desc smp_desc = {};
    smp_desc.min_filter = SG_FILTER_LINEAR;
    smp_desc.mag_filter = SG_FILTER_LINEAR;
    smp_desc.wrap_u = SG_WRAP_REPEAT;
    smp_desc.wrap_v = SG_WRAP_REPEAT;
    state.smp = sg_make_sampler(&smp_desc);
    
    // Create shader
    sg_shader_desc shd_desc = {};
    shd_desc.vertex_func.source = vs_source;
    shd_desc.fragment_func.source = fs_source;
    
    // Vertex attributes (required for D3D11)
    shd_desc.attrs[0].hlsl_sem_name = "POSITION";
    shd_desc.attrs[0].hlsl_sem_index = 0;
    shd_desc.attrs[1].hlsl_sem_name = "NORMAL";
    shd_desc.attrs[1].hlsl_sem_index = 0;
    shd_desc.attrs[2].hlsl_sem_name = "TEXCOORD";
    shd_desc.attrs[2].hlsl_sem_index = 0;
    
    // Vertex shader uniform block
    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = sizeof(vs_params_t);
    shd_desc.uniform_blocks[0].hlsl_register_b_n = 0;
    
    // Fragment shader uniform block
    shd_desc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.uniform_blocks[1].size = sizeof(fs_params_t);
    shd_desc.uniform_blocks[1].hlsl_register_b_n = 0;
    
    // Fragment shader texture view
    shd_desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    shd_desc.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    shd_desc.views[0].texture.hlsl_register_t_n = 0;
    
    // Fragment shader sampler
    shd_desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd_desc.samplers[0].hlsl_register_s_n = 0;
    
    // Texture-sampler pair
    shd_desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.texture_sampler_pairs[0].view_slot = 0;
    shd_desc.texture_sampler_pairs[0].sampler_slot = 0;
    
    sg_shader shd = sg_make_shader(&shd_desc);
    
    // Create pipeline
    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;  // position
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;  // normal
    pip_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT2;  // uv
    pip_desc.index_type = SG_INDEXTYPE_UINT32;
    pip_desc.cull_mode = SG_CULLMODE_BACK;
    pip_desc.depth.write_enabled = true;
    pip_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pip_desc.label = "mesh-pipeline";
    state.pip = sg_make_pipeline(&pip_desc);
    
    // Initialize camera
    state.cam_distance = 5.0f;
    state.cam_azimuth = 45.0f;
    state.cam_elevation = 20.0f;
    state.cam_target = HMM_V3(0, 0, 0);
    
    state.mouse_down = false;
    state.time = 0.0f;
    state.model_loaded = false;
    
    // Try to load a default model if command line argument provided
    // For now, we'll just show an empty scene
    log_message("Ready. Drag and drop a VRM/GLTF/GLB file to load.");
}

static void frame() {
    state.time += (float)sapp_frame_duration();
    
    // Calculate camera position
    float azimuth_rad = HMM_AngleRad(state.cam_azimuth);
    float elevation_rad = HMM_AngleRad(state.cam_elevation);
    
    float cos_elev = cosf(elevation_rad);
    float sin_elev = sinf(elevation_rad);
    float cos_azim = cosf(azimuth_rad);
    float sin_azim = sinf(azimuth_rad);
    
    HMM_Vec3 cam_offset = HMM_V3(
        cos_elev * sin_azim * state.cam_distance,
        sin_elev * state.cam_distance,
        cos_elev * cos_azim * state.cam_distance
    );
    HMM_Vec3 cam_pos = HMM_AddV3(state.cam_target, cam_offset);
    
    // Build view and projection matrices
    float aspect = sapp_widthf() / sapp_heightf();
    HMM_Mat4 proj = HMM_Perspective_RH_ZO(45.0f, aspect, 0.01f, 1000.0f);
    HMM_Mat4 view = HMM_LookAt_RH(cam_pos, state.cam_target, HMM_V3(0, 1, 0));
    HMM_Mat4 model = HMM_M4D(1.0f);
    HMM_Mat4 mvp = HMM_MulM4(proj, HMM_MulM4(view, model));
    
    // Light direction (from camera towards scene)
    HMM_Vec3 light_dir = HMM_NormV3(HMM_V3(0.5f, 1.0f, 0.3f));
    
    // Begin pass
    sg_pass pass = {};
    pass.swapchain = sglue_swapchain();
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = { 0.15f, 0.15f, 0.18f, 1.0f };
    pass.action.depth.load_action = SG_LOADACTION_CLEAR;
    pass.action.depth.clear_value = 1.0f;
    sg_begin_pass(&pass);
    
    if (state.model_loaded) {
        sg_apply_pipeline(state.pip);
        
        for (auto& mesh : state.model.meshes) {
            // Set up bindings
            sg_bindings bind = {};
            bind.vertex_buffers[0] = mesh.vertex_buffer;
            if (mesh.has_indices) {
                bind.index_buffer = mesh.index_buffer;
            }
            bind.views[0] = mesh.texture_view;  // Use view instead of image
            bind.samplers[0] = state.smp;
            sg_apply_bindings(&bind);
            
            // Set up vertex shader uniforms
            vs_params_t vs_params = {};
            vs_params.mvp = mvp;
            vs_params.model = model;
            vs_params.light_dir = light_dir;
            sg_apply_uniforms(0, SG_RANGE(vs_params));
            
            // Set up fragment shader uniforms
            fs_params_t fs_params = {};
            fs_params.base_color = mesh.base_color;
            fs_params.light_dir = light_dir;
            fs_params.ambient = HMM_V3(0.3f, 0.3f, 0.35f);
            sg_apply_uniforms(1, SG_RANGE(fs_params));
            
            // Draw
            if (mesh.has_indices) {
                sg_draw(0, mesh.num_indices, 1);
            } else {
                sg_draw(0, mesh.num_vertices, 1);
            }
        }
    }
    
    sg_end_pass();
    sg_commit();
}

static void cleanup() {
    log_message("Cleaning up...");
    
    // Clean up model resources
    for (auto& mesh : state.model.meshes) {
        sg_destroy_buffer(mesh.vertex_buffer);
        if (mesh.has_indices) {
            sg_destroy_buffer(mesh.index_buffer);
        }
        if (mesh.texture.id != state.default_texture.id) {
            sg_destroy_view(mesh.texture_view);
            sg_destroy_image(mesh.texture);
        }
    }
    
    sg_destroy_view(state.default_texture_view);
    sg_destroy_image(state.default_texture);
    sg_destroy_sampler(state.smp);
    sg_destroy_pipeline(state.pip);
    
    sg_shutdown();
}

static void event(const sapp_event* ev) {
    switch (ev->type) {
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
                state.mouse_down = true;
                state.last_mouse_x = ev->mouse_x;
                state.last_mouse_y = ev->mouse_y;
            }
            break;
            
        case SAPP_EVENTTYPE_MOUSE_UP:
            if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
                state.mouse_down = false;
            }
            break;
            
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            if (state.mouse_down) {
                float dx = ev->mouse_x - state.last_mouse_x;
                float dy = ev->mouse_y - state.last_mouse_y;
                
                state.cam_azimuth -= dx * 0.002f;
                state.cam_elevation += dy * 0.002f;
                
                // Clamp elevation
                state.cam_elevation = HMM_Clamp(-89.0f, state.cam_elevation, 89.0f);
                
                state.last_mouse_x = ev->mouse_x;
                state.last_mouse_y = ev->mouse_y;
            }
            break;
            
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            state.cam_distance -= ev->scroll_y * state.cam_distance * 0.1f;
            state.cam_distance = HMM_MAX(0.1f, state.cam_distance);
            break;
            
        case SAPP_EVENTTYPE_FILES_DROPPED: {
            int num_files = sapp_get_num_dropped_files();
            if (num_files > 0) {
                const char* filepath = sapp_get_dropped_file_path(0);
                load_model(filepath);
            }
            break;
        }
        
        case SAPP_EVENTTYPE_KEY_DOWN:
            if (ev->key_code == SAPP_KEYCODE_ESCAPE) {
                sapp_request_quit();
            } else if (ev->key_code == SAPP_KEYCODE_R) {
                // Reset camera
                if (state.model_loaded) {
                    state.cam_target = state.model.center;
                    state.cam_distance = state.model.radius * 2.5f;
                    state.cam_elevation = 15.0f;
                    state.cam_azimuth = 45.0f;
                }
            }
            break;
            
        default:
            break;
    }
}

sapp_desc sokol_main(int argc, char* argv[]) {
    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup;
    desc.event_cb = event;
    desc.width = 1280;
    desc.height = 720;
    desc.window_title = "VRM/GLTF/GLB Viewer";
    desc.icon.sokol_default = true;
    desc.enable_dragndrop = true;
    desc.max_dropped_files = 1;
    desc.logger.func = slog_func;
    desc.high_dpi = true;
    
    // Try to load model from command line
    if (argc > 1) {
        // Store for later loading in init()
        // We can't load here because sokol isn't initialized yet
    }
    
    return desc;
}
