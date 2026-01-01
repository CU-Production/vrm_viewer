// Mesh shader for VRM/GLTF/GLB viewer
// Compile with: sokol-shdc --input mesh.glsl --output mesh.glsl.h --slang hlsl5:glsl430:metal_macos

@ctype mat4 HMM_Mat4
@ctype vec4 HMM_Vec4
@ctype vec3 HMM_Vec3

@vs vs
layout(binding=0) uniform vs_params {
    mat4 mvp;
    mat4 model;
    vec3 light_dir;
    float _pad0;
};

in vec3 pos;
in vec3 normal;
in vec2 uv;

out vec3 v_normal;
out vec2 v_uv;
out vec3 v_world_pos;

void main() {
    gl_Position = mvp * vec4(pos, 1.0);
    v_normal = mat3(model) * normal;
    v_uv = uv;
    v_world_pos = (model * vec4(pos, 1.0)).xyz;
}
@end

@fs fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

layout(binding=1) uniform fs_params {
    vec4 base_color;
    vec3 light_dir;
    float _pad0;
    vec3 ambient;
    float _pad1;
};

in vec3 v_normal;
in vec2 v_uv;
in vec3 v_world_pos;

out vec4 frag_color;

void main() {
    vec3 n = normalize(v_normal);
    float ndotl = max(dot(n, normalize(light_dir)), 0.0);
    
    vec4 tex_color = texture(sampler2D(tex, smp), v_uv);
    vec3 color = base_color.rgb * tex_color.rgb;
    
    vec3 lit_color = ambient * color + ndotl * color;
    frag_color = vec4(lit_color, base_color.a * tex_color.a);
}
@end

@program mesh vs fs
