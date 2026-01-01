// Skybox shader for displaying prefiltered environment map
// Compile with: sokol-shdc --input skybox.glsl --output skybox.glsl.h --slang hlsl5:glsl430:metal_macos

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

layout(binding=1) uniform fs_params {
    float lod_level;
    float exposure;
    float _pad0;
    float _pad1;
};

in vec3 v_world_pos;

out vec4 frag_color;

void main() {
    vec3 env_color = textureLod(samplerCube(environment_map, env_smp), v_world_pos, lod_level).rgb;
    
    // Apply exposure
    env_color *= exposure;
    
    // HDR tonemapping
    env_color = env_color / (env_color + vec3(1.0));
    // Gamma correction (ensure non-negative for pow)
    env_color = pow(max(env_color, vec3(0.0)), vec3(1.0 / 2.2));
    
    frag_color = vec4(env_color, 1.0);
}
@end

@program skybox vs fs
