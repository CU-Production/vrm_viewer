// PBR shader with IBL support for GLTF models
// Compile with: sokol-shdc --input pbr.glsl --output pbr.glsl.h --slang hlsl5:glsl430:metal_macos

@module pbr

@ctype mat4 HMM_Mat4
@ctype vec4 HMM_Vec4
@ctype vec3 HMM_Vec3

@vs vs
layout(binding=0) uniform vs_params {
    mat4 mvp;
    mat4 model;
    mat4 normal_matrix;
    vec3 cam_pos;
    float _pad0;
};

in vec3 pos;
in vec3 normal;
in vec2 uv;
in vec4 tangent;

out vec3 v_world_pos;
out vec3 v_normal;
out vec3 v_tangent;
out vec3 v_bitangent;
out vec2 v_uv;

void main() {
    v_world_pos = (model * vec4(pos, 1.0)).xyz;
    v_normal = normalize((normal_matrix * vec4(normal, 0.0)).xyz);
    v_tangent = normalize((normal_matrix * vec4(tangent.xyz, 0.0)).xyz);
    v_bitangent = cross(v_normal, v_tangent) * tangent.w;
    v_uv = uv;
    gl_Position = mvp * vec4(pos, 1.0);
}
@end

@fs fs
layout(binding=0) uniform texture2D base_color_tex;
layout(binding=0) uniform sampler base_color_smp;
layout(binding=1) uniform texture2D metallic_roughness_tex;
layout(binding=1) uniform sampler metallic_roughness_smp;
layout(binding=2) uniform texture2D normal_tex;
layout(binding=2) uniform sampler normal_smp;
layout(binding=3) uniform texture2D occlusion_tex;
layout(binding=3) uniform sampler occlusion_smp;
layout(binding=4) uniform texture2D emissive_tex;
layout(binding=4) uniform sampler emissive_smp;

// IBL textures
layout(binding=5) uniform textureCube irradiance_map;
layout(binding=5) uniform sampler irradiance_smp;
layout(binding=6) uniform textureCube prefilter_map;
layout(binding=6) uniform sampler prefilter_smp;
layout(binding=7) uniform texture2D brdf_lut;
layout(binding=7) uniform sampler brdf_lut_smp;

layout(binding=1) uniform fs_params {
    vec4 base_color_factor;
    float metallic_factor;
    float roughness_factor;
    vec3 emissive_factor;
    float _pad0;
    vec3 cam_pos;
    float _pad1;
};

in vec3 v_world_pos;
in vec3 v_normal;
in vec3 v_tangent;
in vec3 v_bitangent;
in vec2 v_uv;

out vec4 frag_color;

const float PI = 3.14159265359;

// ============================================================================
// Utility Functions
// ============================================================================

// ACES Filmic Tone Mapping
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Accurate sRGB transfer function (linear to sRGB)
vec3 linearToSRGB(vec3 c) {
    vec3 lo = c * 12.92;
    vec3 hi = pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) * 1.055 - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}

// Schlick Fresnel approximation: (1 - cosTheta)^5
float SchlickFresnel(float u) {
    float m = clamp(1.0 - u, 0.0, 1.0);
    float m2 = m * m;
    return m2 * m2 * m;  // pow(m, 5)
}

// ============================================================================
// GGX Specular BRDF (Cook-Torrance with GGX)
// ============================================================================

// Normal Distribution Function - GGX/Trowbridge-Reitz
float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Geometry Function - Smith GGX (height-correlated)
float G_SmithGGX(float NdotV, float NdotL, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    
    float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    
    return 0.5 / max(GGXV + GGXL, 0.0001);
}

// Fresnel-Schlick approximation
vec3 F_Schlick(float VdotH, vec3 F0) {
    return F0 + (1.0 - F0) * SchlickFresnel(VdotH);
}

// Fresnel-Schlick with roughness (for IBL)
vec3 F_SchlickRoughness(float NdotV, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * SchlickFresnel(NdotV);
}

// ============================================================================
// Disney Diffuse BRDF
// Based on Burley's physically-based diffuse model from Disney
// Accounts for roughness, Fresnel, and retro-reflection
// ============================================================================

float Fd_DisneyDiffuse(float NdotV, float NdotL, float LdotH, float roughness) {
    // Energy factor for retro-reflection at grazing angles
    float energyBias = mix(0.0, 0.5, roughness);
    float energyFactor = mix(1.0, 1.0 / 1.51, roughness);
    
    // Fresnel terms for view and light
    float FD90 = energyBias + 2.0 * LdotH * LdotH * roughness;
    float FdV = 1.0 + (FD90 - 1.0) * SchlickFresnel(NdotV);
    float FdL = 1.0 + (FD90 - 1.0) * SchlickFresnel(NdotL);
    
    return FdV * FdL * energyFactor;
}

// Alternative: Lambert diffuse (simpler, for comparison)
float Fd_Lambert() {
    return 1.0 / PI;
}

void main() {
    // ========================================================================
    // Sample Material Textures
    // ========================================================================
    vec4 base_color = texture(sampler2D(base_color_tex, base_color_smp), v_uv) * base_color_factor;
    vec3 metallic_roughness = texture(sampler2D(metallic_roughness_tex, metallic_roughness_smp), v_uv).rgb;
    float metallic = metallic_roughness.b * metallic_factor;
    float roughness = clamp(metallic_roughness.g * roughness_factor, 0.04, 1.0);  // Clamp to avoid singularities
    float ao = texture(sampler2D(occlusion_tex, occlusion_smp), v_uv).r;
    vec3 emissive = texture(sampler2D(emissive_tex, emissive_smp), v_uv).rgb * emissive_factor;
    
    // Sample and transform normal map
    vec3 normal_map = texture(sampler2D(normal_tex, normal_smp), v_uv).rgb;
    normal_map = normal_map * 2.0 - 1.0;
    mat3 TBN = mat3(v_tangent, v_bitangent, v_normal);
    vec3 N = normalize(TBN * normal_map);
    
    // View and reflection vectors
    vec3 V = normalize(cam_pos - v_world_pos);
    vec3 R = reflect(-V, N);
    float NdotV = max(dot(N, V), 0.0001);
    
    // F0: reflectance at normal incidence
    // Dielectrics: 0.04, Metals: base color
    vec3 F0 = mix(vec3(0.04), base_color.rgb, metallic);
    
    // Diffuse color (metals have no diffuse)
    vec3 diffuseColor = base_color.rgb * (1.0 - metallic);
    
    // ========================================================================
    // Direct Lighting (Directional Light)
    // ========================================================================
    vec3 L = normalize(vec3(1.0, 1.0, 1.0));
    vec3 H = normalize(V + L);
    
    float NdotL = max(dot(N, L), 0.0001);
    float NdotH = max(dot(N, H), 0.0);
    float LdotH = max(dot(L, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    
    // GGX Specular BRDF
    float D = D_GGX(NdotH, roughness);
    float G = G_SmithGGX(NdotV, NdotL, roughness);
    vec3 F = F_Schlick(VdotH, F0);
    vec3 specular = D * G * F;
    
    // Disney Diffuse BRDF
    float Fd = Fd_DisneyDiffuse(NdotV, NdotL, LdotH, roughness);
    vec3 diffuse = diffuseColor * Fd;
    
    // Energy conservation: what's not reflected is diffused
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    
    // Direct lighting contribution
    vec3 lightColor = vec3(1.0);  // White directional light
    vec3 Lo = (kD * diffuse + specular) * lightColor * NdotL;
    
    // ========================================================================
    // Image-Based Lighting (IBL)
    // ========================================================================
    
    // Fresnel for IBL (accounts for roughness)
    vec3 F_ibl = F_SchlickRoughness(NdotV, F0, roughness);
    vec3 kD_ibl = (1.0 - F_ibl) * (1.0 - metallic);
    
    // Diffuse IBL (from irradiance map)
    vec3 irradiance = texture(samplerCube(irradiance_map, irradiance_smp), N).rgb;
    vec3 diffuse_ibl = irradiance * diffuseColor;
    
    // Specular IBL (from prefilter map + BRDF LUT)
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(samplerCube(prefilter_map, prefilter_smp), R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(sampler2D(brdf_lut, brdf_lut_smp), vec2(NdotV, roughness)).rg;
    vec3 specular_ibl = prefilteredColor * (F_ibl * brdf.x + brdf.y);
    
    // Combined ambient (IBL)
    vec3 ambient = (kD_ibl * diffuse_ibl + specular_ibl) * ao;
    
    // ========================================================================
    // Final Color
    // ========================================================================
    vec3 color = ambient + Lo + emissive;
    
    // Tone mapping and gamma correction
    color = ACESFilm(color);
    color = linearToSRGB(color);
    
    frag_color = vec4(color, base_color.a);
}
@end

@program pbr vs fs
