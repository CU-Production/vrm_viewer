// Stylized Realistic Toon Shader
// A hybrid approach combining PBR lighting with anime-style aesthetics
// Features: Soft ramp shadows, stylized specular, rim light, material-aware shading

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

// IBL textures
layout(binding=3) uniform textureCube irradiance_map;
layout(binding=3) uniform sampler irradiance_smp;
layout(binding=4) uniform textureCube prefilter_map;
layout(binding=4) uniform sampler prefilter_smp;

layout(binding=1) uniform fs_params {
    vec4 base_color_factor;
    float metallic_factor;
    float roughness_factor;
    float toon_ramp_steps;      // Unused, kept for compatibility
    float toon_rim_power;
    float toon_rim_strength;
    vec3 cam_pos;
    float _pad0;
};

in vec3 v_world_pos;
in vec3 v_normal;
in vec3 v_tangent;
in vec3 v_bitangent;
in vec2 v_uv;

out vec4 frag_color;

const float PI = 3.14159265359;

// ============================================================================
// Lighting Configuration
// ============================================================================

// Main directional light
const vec3 LIGHT_DIR = normalize(vec3(0.6, 1.0, 0.4));
const vec3 LIGHT_COLOR = vec3(1.0, 0.98, 0.95);
const float LIGHT_INTENSITY = 1.2;

// Shadow ramp settings (soft transition, not hard cel-shading)
const float SHADOW_RAMP_SMOOTH = 0.15;      // Transition width
const float SHADOW_RAMP_OFFSET = 0.0;       // Shift shadow boundary
const vec3 SHADOW_TINT = vec3(0.85, 0.8, 0.95);  // Subtle purple tint in shadows
const float SHADOW_STRENGTH = 0.45;         // How dark shadows get (0=black, 1=no shadow)

// Specular settings
const float SPEC_INTENSITY = 0.6;
const float SPEC_SHARPNESS = 0.85;          // Higher = sharper specular edge

// Rim light settings  
const vec3 RIM_TINT = vec3(1.0, 0.95, 0.92);
const float RIM_WIDTH = 0.35;
const float RIM_SOFTNESS = 0.3;

// Environment/IBL settings
const float ENV_DIFFUSE_STRENGTH = 0.15;
const float ENV_SPEC_STRENGTH = 0.25;

// ============================================================================
// Utility Functions
// ============================================================================

// sRGB conversion
vec3 linearToSRGB(vec3 c) {
    vec3 lo = c * 12.92;
    vec3 hi = pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) * 1.055 - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}

// Attempt to convert sRGB texture to linear (if texture is in sRGB)
vec3 sRGBToLinear(vec3 c) {
    vec3 lo = c / 12.92;
    vec3 hi = pow(max((c + 0.055) / 1.055, vec3(0.0)), vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

// Soft threshold function for stylized shading
float softStep(float edge, float x, float softness) {
    return smoothstep(edge - softness, edge + softness, x);
}

// Schlick Fresnel
float fresnelSchlick(float cosTheta) {
    return pow(max(1.0 - cosTheta, 0.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

// ============================================================================
// GGX Specular (simplified for stylized look)
// ============================================================================

float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float G_SchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float roughness) {
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

// ============================================================================
// Stylized Diffuse Shading
// ============================================================================

vec3 calculateDiffuse(vec3 baseColor, float NdotL, float ao) {
    // Remap NdotL with offset
    float remappedNdotL = NdotL + SHADOW_RAMP_OFFSET;
    
    // Soft shadow ramp (not hard step)
    float shadowFactor = softStep(0.0, remappedNdotL, SHADOW_RAMP_SMOOTH);
    
    // Shadow color: darken and tint
    vec3 shadowColor = baseColor * SHADOW_TINT * SHADOW_STRENGTH;
    
    // Lit color: full base color with slight boost
    vec3 litColor = baseColor;
    
    // Blend between shadow and lit
    vec3 diffuse = mix(shadowColor, litColor, shadowFactor);
    
    // Apply ambient occlusion
    diffuse *= mix(0.7, 1.0, ao);
    
    return diffuse;
}

// ============================================================================
// Stylized Specular
// ============================================================================

vec3 calculateSpecular(vec3 N, vec3 V, vec3 L, vec3 baseColor, float roughness, float metallic) {
    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotL = max(dot(N, L), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    
    // F0 for Fresnel
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    
    // GGX specular
    float D = D_GGX(NdotH, max(roughness, 0.04));
    float G = G_Smith(NdotV, NdotL, roughness);
    vec3 F = F0 + (1.0 - F0) * fresnelSchlick(VdotH);
    
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
    
    // Stylize: add a soft edge to specular highlight
    float specMask = softStep(SPEC_SHARPNESS, NdotH, 0.05);
    specular *= mix(1.0, specMask * 2.0, 0.3);  // Blend physical and stylized
    
    return specular * SPEC_INTENSITY * NdotL;
}

// ============================================================================
// Rim Light
// ============================================================================

vec3 calculateRim(vec3 N, vec3 V, vec3 baseColor, float NdotL) {
    float NdotV = max(dot(N, V), 0.0);
    
    // Fresnel-based rim
    float rim = 1.0 - NdotV;
    rim = pow(max(rim, 0.0), toon_rim_power);
    
    // Soft threshold
    rim = softStep(1.0 - RIM_WIDTH, rim, RIM_SOFTNESS);
    
    // Reduce rim in deep shadow (looks more natural)
    float shadowMask = softStep(-0.3, NdotL, 0.2);
    rim *= mix(0.2, 1.0, shadowMask);
    
    // Rim color: blend between tint and base color
    vec3 rimColor = mix(RIM_TINT, baseColor * 1.2, 0.25);
    
    return rimColor * rim * toon_rim_strength;
}

// ============================================================================
// Environment Lighting (IBL)
// ============================================================================

vec3 calculateEnvironment(vec3 N, vec3 V, vec3 R, vec3 baseColor, float roughness, float metallic, float ao) {
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    float NdotV = max(dot(N, V), 0.0);
    
    // Fresnel for environment
    vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    
    // Diffuse IBL
    vec3 irradiance = texture(samplerCube(irradiance_map, irradiance_smp), N).rgb;
    vec3 diffuseEnv = irradiance * baseColor * kD * ENV_DIFFUSE_STRENGTH;
    
    // Specular IBL
    const float MAX_LOD = 4.0;
    vec3 prefilteredColor = textureLod(samplerCube(prefilter_map, prefilter_smp), R, roughness * MAX_LOD).rgb;
    vec3 specularEnv = prefilteredColor * F * ENV_SPEC_STRENGTH;
    
    // Metallic surfaces get more environment reflection
    specularEnv *= mix(1.0, 2.0, metallic);
    
    return (diffuseEnv + specularEnv) * ao;
}

// ============================================================================
// Tonemapping and Color Grading
// ============================================================================

// ACES approximation (keeps colors vibrant)
vec3 ACESFilm(vec3 x) {
    float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Subtle color grading for anime aesthetic
vec3 colorGrade(vec3 color) {
    // Slight saturation boost
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, 1.1);
    
    // Subtle contrast curve
    color = color * color * (3.0 - 2.0 * color);  // S-curve
    
    return color;
}

// ============================================================================
// Main
// ============================================================================

void main() {
    // ------------------------------------------------------------------------
    // Sample Textures
    // ------------------------------------------------------------------------
    vec4 baseColorSample = texture(sampler2D(base_color_tex, base_color_smp), v_uv);
    vec3 baseColor = baseColorSample.rgb * base_color_factor.rgb;
    float alpha = baseColorSample.a * base_color_factor.a;
    
    // Assume base color texture is sRGB, convert to linear
    baseColor = sRGBToLinear(baseColor);
    
    vec3 mrSample = texture(sampler2D(metallic_roughness_tex, metallic_roughness_smp), v_uv).rgb;
    float metallic = mrSample.b * metallic_factor;
    float roughness = clamp(mrSample.g * roughness_factor, 0.04, 1.0);
    float ao = mrSample.r;  // Often AO is in R channel
    
    // Normal mapping
    vec3 normalMap = texture(sampler2D(normal_tex, normal_smp), v_uv).rgb;
    normalMap = normalMap * 2.0 - 1.0;
    mat3 TBN = mat3(v_tangent, v_bitangent, v_normal);
    vec3 N = normalize(TBN * normalMap);
    
    // Vectors
    vec3 V = normalize(cam_pos - v_world_pos);
    vec3 L = LIGHT_DIR;
    vec3 R = reflect(-V, N);
    
    float NdotL = dot(N, L);
    float NdotV = max(dot(N, V), 0.0);
    
    // ------------------------------------------------------------------------
    // Lighting Calculation
    // ------------------------------------------------------------------------
    
    // Diffuse (stylized ramp)
    vec3 diffuse = calculateDiffuse(baseColor, NdotL, ao);
    
    // Specular (GGX-based, slightly stylized)
    vec3 specular = calculateSpecular(N, V, L, baseColor, roughness, metallic);
    specular *= LIGHT_COLOR * LIGHT_INTENSITY;
    
    // Only show specular in lit areas
    float specMask = softStep(0.0, NdotL, 0.1);
    specular *= specMask;
    
    // Rim light
    vec3 rim = calculateRim(N, V, baseColor, NdotL);
    
    // Environment/IBL
    vec3 env = calculateEnvironment(N, V, R, baseColor, roughness, metallic, ao);
    
    // ------------------------------------------------------------------------
    // Compose Final Color
    // ------------------------------------------------------------------------
    vec3 color = vec3(0.0);
    
    // Main lighting
    color += diffuse * LIGHT_COLOR * LIGHT_INTENSITY;
    color += specular;
    color += rim;
    color += env;
    
    // ------------------------------------------------------------------------
    // Post Processing
    // ------------------------------------------------------------------------
    
    // Tonemapping
    color = ACESFilm(color);
    
    // Color grading
    color = colorGrade(color);
    
    // Linear to sRGB
    color = linearToSRGB(color);
    
    // Output
    frag_color = vec4(color, alpha);
}
@end

@program toon vs fs
