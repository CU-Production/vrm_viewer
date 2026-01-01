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
// Lighting Configuration (MToon-inspired, VRChat compatible)
// ============================================================================

// Main directional light
const vec3 LIGHT_DIR = normalize(vec3(0.5, 1.0, 0.3));
const vec3 LIGHT_COLOR = vec3(1.0, 1.0, 1.0);
const float LIGHT_INTENSITY = 1.0;          // Neutral intensity

// Shade settings (MToon style - softer than cel-shading)
const float SHADE_SHIFT = 0.0;              // Shift shade boundary (-1 to 1)
const float SHADE_TOONY = 0.5;              // 0 = smooth gradient, 1 = hard edge
const float SHADE_STRENGTH = 0.65;          // How much darker shade is (0 = same as lit)

// Specular settings (subtle for VRM)
const float SPEC_INTENSITY = 0.3;           // Lower specular
const float SPEC_SHARPNESS = 0.9;

// Rim light settings (subtle)
const vec3 RIM_TINT = vec3(1.0, 1.0, 1.0);
const float RIM_WIDTH = 0.4;
const float RIM_SOFTNESS = 0.25;
const float RIM_LIFT = 0.0;                 // Lift rim into shadow areas

// Environment/IBL settings (minimal for VRM)
const float ENV_DIFFUSE_STRENGTH = 0.05;    // Very subtle
const float ENV_SPEC_STRENGTH = 0.1;

// Global intensity control
const float GLOBAL_ILLUMINATION = 0.85;     // Overall brightness multiplier

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
// MToon-style Diffuse Shading
// ============================================================================

vec3 calculateDiffuse(vec3 baseColor, float NdotL, float ao) {
    // MToon-style shading: shift and toony parameters
    // Shift moves the boundary, toony controls the hardness
    float shadeShift = SHADE_SHIFT;
    float shadeToony = SHADE_TOONY;
    
    // Calculate shade factor with shift
    float halfLambert = NdotL * 0.5 + 0.5;  // Remap -1..1 to 0..1
    float shadeValue = halfLambert + shadeShift;
    
    // Apply toony factor (0 = linear gradient, 1 = step function)
    float shadeWidth = 1.0 - shadeToony;
    float shadeFactor = smoothstep(0.5 - shadeWidth * 0.5, 0.5 + shadeWidth * 0.5, shadeValue);
    
    // Shade color is darkened base color (no tint, keeps original hue)
    vec3 shadeColor = baseColor * (1.0 - SHADE_STRENGTH);
    
    // Lit color: base color as-is
    vec3 litColor = baseColor;
    
    // Blend between shade and lit
    vec3 diffuse = mix(shadeColor, litColor, shadeFactor);
    
    // Apply ambient occlusion (subtle)
    diffuse *= mix(0.85, 1.0, ao);
    
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
// Rim Light (MToon style)
// ============================================================================

vec3 calculateRim(vec3 N, vec3 V, vec3 baseColor, float NdotL) {
    float NdotV = max(dot(N, V), 0.0);
    
    // Fresnel-based rim
    float rim = 1.0 - NdotV;
    rim = pow(max(rim, 0.0), toon_rim_power);
    
    // Soft threshold with width control
    float rimThreshold = 1.0 - RIM_WIDTH;
    rim = softStep(rimThreshold, rim, RIM_SOFTNESS);
    
    // MToon style: rim can be lifted into shadow or only in lit areas
    float halfLambert = NdotL * 0.5 + 0.5;
    float rimMask = smoothstep(0.0, 0.5, halfLambert + RIM_LIFT);
    rim *= rimMask;
    
    // Rim color: white-ish, subtle influence from base
    vec3 rimColor = RIM_TINT;
    
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

// Simple Reinhard tonemapping (gentler than ACES, good for VRM)
vec3 tonemapReinhard(vec3 x) {
    return x / (x + vec3(1.0));
}

// Minimal color grading (preserve VRM texture colors)
vec3 colorGrade(vec3 color) {
    // Very subtle saturation adjustment
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, 1.05);  // +5% saturation
    
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
    
    // Main diffuse (already includes shading)
    color = diffuse * LIGHT_COLOR * LIGHT_INTENSITY;
    
    // Add specular (subtle)
    color += specular;
    
    // Add rim light
    color += rim;
    
    // Add environment (very subtle for VRM)
    color += env;
    
    // Apply global illumination control
    color *= GLOBAL_ILLUMINATION;
    
    // ------------------------------------------------------------------------
    // Post Processing
    // ------------------------------------------------------------------------
    
    // Gentle tonemapping (Reinhard preserves colors better)
    color = tonemapReinhard(color);
    
    // Minimal color grading
    color = colorGrade(color);
    
    // Linear to sRGB
    color = linearToSRGB(color);
    
    // Output
    frag_color = vec4(color, alpha);
}
@end

@program toon vs fs
