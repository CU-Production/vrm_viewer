// Toon/Cel shader for VRM models with PBR base
// Compile with: sokol-shdc --input toon.glsl --output toon.glsl.h --slang hlsl5:glsl430:metal_macos

@module toon

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
out vec2 v_uv;

void main() {
    v_world_pos = (model * vec4(pos, 1.0)).xyz;
    v_normal = normalize((normal_matrix * vec4(normal, 0.0)).xyz);
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

// IBL textures
layout(binding=3) uniform textureCube irradiance_map;
layout(binding=3) uniform sampler irradiance_smp;
layout(binding=4) uniform textureCube prefilter_map;
layout(binding=4) uniform sampler prefilter_smp;
layout(binding=5) uniform texture2D brdf_lut;
layout(binding=5) uniform sampler brdf_lut_smp;

layout(binding=1) uniform fs_params {
    vec4 base_color_factor;
    float metallic_factor;
    float roughness_factor;
    float toon_ramp_steps;
    float toon_rim_power;
    float toon_rim_strength;
    vec3 cam_pos;
    float _pad0;
};

in vec3 v_world_pos;
in vec3 v_normal;
in vec2 v_uv;

out vec4 frag_color;

const float PI = 3.14159265359;

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

// Toon shading functions
float toon_ramp(float ndotl, float steps) {
    return floor(ndotl * steps) / steps;
}

float toon_rim(vec3 N, vec3 V, float power, float strength) {
    float rim = 1.0 - max(dot(N, V), 0.0);
    return pow(max(rim, 0.0), power) * strength;
}

// Simplified PBR for toon
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // Sample textures
    vec4 base_color = texture(sampler2D(base_color_tex, base_color_smp), v_uv) * base_color_factor;
    vec3 metallic_roughness = texture(sampler2D(metallic_roughness_tex, metallic_roughness_smp), v_uv).rgb;
    float metallic = metallic_roughness.b * metallic_factor;
    float roughness = metallic_roughness.g * roughness_factor;
    
    vec3 N = normalize(v_normal);
    vec3 V = normalize(cam_pos - v_world_pos);
    vec3 R = reflect(-V, N);
    
    // Calculate reflectance
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, base_color.rgb, metallic);
    
    // Toon lighting
    vec3 L = normalize(vec3(1.0, 1.0, 1.0));
    float NdotL = dot(N, L);
    float toon_diffuse = toon_ramp(max(NdotL, 0.0), toon_ramp_steps);
    
    // Rim lighting
    float rim = toon_rim(N, V, toon_rim_power, toon_rim_strength);
    
    // IBL (simplified)
    vec3 F_ibl = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kS_ibl = F_ibl;
    vec3 kD_ibl = 1.0 - kS_ibl;
    kD_ibl *= 1.0 - metallic;
    
    vec3 irradiance = texture(samplerCube(irradiance_map, irradiance_smp), N).rgb;
    vec3 diffuse_ibl = irradiance * base_color.rgb;
    
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(samplerCube(prefilter_map, prefilter_smp), R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(sampler2D(brdf_lut, brdf_lut_smp), vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular_ibl = prefilteredColor * (F_ibl * brdf.x + brdf.y);
    
    vec3 ambient = kD_ibl * diffuse_ibl * 0.3;
    vec3 direct = base_color.rgb * toon_diffuse;
    vec3 specular = specular_ibl * metallic;
    vec3 rim_color = vec3(1.0, 1.0, 1.0) * rim;
    
    vec3 color = ambient + direct + specular + rim_color;
    
    // ACES Filmic tonemapping
    color = ACESFilm(color);
    // Convert linear to sRGB for display
    color = linearToSRGB(color);
    
    frag_color = vec4(color, base_color.a);
}
@end

@program toon vs fs
