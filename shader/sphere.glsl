// Simple PBR shader for material preview spheres (no textures)
// Reference: https://learnopengl.com/PBR/IBL/Diffuse-irradiance

@module sphere

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
layout(binding=0) uniform textureCube irradiance_map;
layout(binding=0) uniform sampler irradiance_smp;
layout(binding=1) uniform textureCube prefilter_map;
layout(binding=1) uniform sampler prefilter_smp;
layout(binding=2) uniform texture2D brdf_lut;
layout(binding=2) uniform sampler brdf_lut_smp;

layout(binding=1) uniform fs_params {
    vec3 base_color;
    float metallic;
    float roughness;
    float _pad0;
    float _pad1;
    float _pad2;
    vec3 cam_pos;
    float _pad3;
};

in vec3 v_world_pos;
in vec3 v_normal;
in vec2 v_uv;

out vec4 frag_color;

const float PI = 3.14159265359;
const float MAX_REFLECTION_LOD = 4.0;

// ----------------------------------------------------------------------------
// ACES Filmic Tone Mapping
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// ----------------------------------------------------------------------------
// Accurate sRGB transfer function (linear to sRGB)
vec3 linearToSRGB(vec3 c) {
    vec3 lo = c * 12.92;
    vec3 hi = pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) * 1.055 - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}

// ----------------------------------------------------------------------------
// GGX/Trowbridge-Reitz normal distribution function
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

// ----------------------------------------------------------------------------
// Schlick-GGX geometry function (single direction)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

// ----------------------------------------------------------------------------
// Smith's geometry function
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// ----------------------------------------------------------------------------
// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ----------------------------------------------------------------------------
// Fresnel-Schlick with roughness for IBL
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ----------------------------------------------------------------------------
void main() {
    vec3 N = normalize(v_normal);
    vec3 V = normalize(cam_pos - v_world_pos);
    vec3 R = reflect(-V, N);

    // Material parameters
    vec3 albedo = base_color;
    float metal = metallic;
    float rough = roughness;

    // Calculate F0 (reflectance at normal incidence)
    // For dielectrics (like plastic) use F0 of 0.04
    // For metals, use the albedo color as F0
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metal);

    // Reflectance equation - accumulate direct lighting
    vec3 Lo = vec3(0.0);

    // 4 point lights positioned closer to the spheres for better material visualization
    // Lights are placed at z=10 with high intensity to overcome distance attenuation
    vec3 lightPositions[4];
    lightPositions[0] = vec3(-8.0,  8.0, 12.0);
    lightPositions[1] = vec3( 8.0,  8.0, 12.0);
    lightPositions[2] = vec3(-8.0, -8.0, 12.0);
    lightPositions[3] = vec3( 8.0, -8.0, 12.0);

    // High intensity to compensate for distance attenuation (1/d^2)
    // At distance ~15, attenuation = 1/225, so need 225*desired_intensity
    vec3 lightColors[4];
    lightColors[0] = vec3(500.0, 500.0, 500.0);
    lightColors[1] = vec3(500.0, 500.0, 500.0);
    lightColors[2] = vec3(500.0, 500.0, 500.0);
    lightColors[3] = vec3(500.0, 500.0, 500.0);

    for (int i = 0; i < 4; ++i) {
        // Calculate per-light radiance
        vec3 L = normalize(lightPositions[i] - v_world_pos);
        vec3 H = normalize(V + L);
        float distance = length(lightPositions[i] - v_world_pos);
        float attenuation = 1.0 / (distance * distance);
        vec3 radiance = lightColors[i] * attenuation;

        // Cook-Torrance BRDF
        float NDF = DistributionGGX(N, H, rough);
        float G = GeometrySmith(N, V, L, rough);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;

        // kS is equal to Fresnel
        vec3 kS = F;
        // For energy conservation, diffuse + specular can't exceed 1.0
        // kD should equal 1.0 - kS
        vec3 kD = vec3(1.0) - kS;
        // Multiply kD by inverse metalness so only non-metals have diffuse lighting
        kD *= 1.0 - metal;

        // Scale light by NdotL
        float NdotL = max(dot(N, L), 0.0);

        // Add to outgoing radiance Lo
        // Note: we already multiplied the BRDF by the Fresnel (kS) so we won't multiply by kS again
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    // Ambient lighting using IBL
    float NdotV = max(dot(N, V), 0.0);

    // IBL diffuse
    vec3 kS_ibl = fresnelSchlickRoughness(NdotV, F0, rough);
    vec3 kD_ibl = 1.0 - kS_ibl;
    kD_ibl *= 1.0 - metal;
    vec3 irradiance = texture(samplerCube(irradiance_map, irradiance_smp), N).rgb;
    vec3 diffuse = irradiance * albedo;

    // IBL specular
    vec3 prefilteredColor = textureLod(samplerCube(prefilter_map, prefilter_smp), R, rough * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(sampler2D(brdf_lut, brdf_lut_smp), vec2(NdotV, rough)).rg;
    vec3 specular_ibl = prefilteredColor * (kS_ibl * brdf.x + brdf.y);

    vec3 ambient = kD_ibl * diffuse + specular_ibl;

    // Final color
    vec3 color = ambient + Lo;

    // ACES Filmic tonemapping
    // color = ACESFilm(color);
    color = color / (color + vec3(1.0));
    // Convert linear to sRGB
    //color = linearToSRGB(color);

    frag_color = vec4(color, 1.0);
}
@end

@program sphere vs fs
