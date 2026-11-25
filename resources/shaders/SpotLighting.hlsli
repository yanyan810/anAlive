#include "LightingMath.hlsli"

// Layout mirrors Object3dLight::SpotLight (64 bytes per light).
struct SpotLight
{
    float4 color;
    float3 position;
    float intensity;
    float3 direction;
    float distance;
    float decay;
    float cosAngle;
    float cosFalloffStart;
    float specularStrength;
};

struct SpotLights
{
    SpotLight lights[3];
};
ConstantBuffer<SpotLights> gSpotLights : register(b4);

void CalculateSpotLighting(float3 worldPosition, float3 N, float3 V,
    float3 baseColor, int lightingMode, float shininess,
    out float3 diffuse, out float3 specular)
{
    diffuse = 0;
    specular = 0;
    [unroll]
    for (int i = 0; i < 3; ++i)
    {
        SpotLight light = gSpotLights.lights[i];
        if (light.intensity <= 0) continue;
        float3 toLight = light.position - worldPosition;
        float dist = max(length(toLight), 0.001f);
        float3 L = toLight / dist;
        float attenuation = LegacyLightFalloff(dist, light.distance, light.decay);
        float3 axis = light.direction / max(length(light.direction), 0.001f);
        float cosTheta = dot(-L, axis);
        float falloff = SpotConeFalloff(cosTheta, light.cosAngle, light.cosFalloffStart);
        float3 color = light.color.rgb * light.intensity * attenuation * falloff;
        diffuse += baseColor * color * DiffuseFactor(dot(N, L), lightingMode);
        specular += color * light.specularStrength * BlinnPhongFactor(N, L, V, shininess);
    }
}
