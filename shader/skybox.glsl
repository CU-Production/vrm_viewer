// Skybox shader for displaying environment map with LOD control
// LOD 0: Original environment map
// LOD 1-5: Prefilter map with increasing roughness (mip levels)

@module skybox

@ctype mat4 HMM_Mat4
@ctype vec3 HMM_Vec3

@vs vs
layout(binding=0) uniform vs_params {
    mat4 mvp;
    vec3 cam_pos;
    float _pad0;
};

in vec3 pos;

out vec3 v_world_pos;

void main() {
    v_world_pos = pos;
    gl_Position = (mvp * vec4(pos, 1.0)).xyww;
}
@end

@fs fs
layout(binding=0) uniform textureCube environment_map;
layout(binding=0) uniform sampler env_smp;

layout(binding=1) uniform textureCube prefilter_map;
layout(binding=1) uniform sampler prefilter_smp;

layout(binding=1) uniform fs_params {
    float lod_level;    // 0 = environment, 1-5 = prefilter mip levels
    float exposure;
    float _pad0;
    float _pad1;
};

in vec3 v_world_pos;

out vec4 frag_color;

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

void main() {
    vec3 env_color;
    
    if (lod_level < 0.5) {
        // LOD 0: Use original environment map (no mip, full quality)
        env_color = textureLod(samplerCube(environment_map, env_smp), v_world_pos, 0.0).rgb;
    } else {
        // LOD 1-5: Use prefilter map with corresponding mip level
        // Map lod_level 1-5 to mip level 0-4
        float mip = lod_level - 1.0;
        env_color = textureLod(samplerCube(prefilter_map, prefilter_smp), v_world_pos, mip).rgb;
    }
    
    // Apply exposure
    env_color *= exposure;
    
    // ACES Filmic tonemapping
    env_color = ACESFilm(env_color);
    
    // Convert linear to sRGB for display
    env_color = linearToSRGB(env_color);
    
    frag_color = vec4(env_color, 1.0);
}
@end

@program skybox vs fs
