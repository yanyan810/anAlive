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
        float attenuation = pow(saturate(1 - dist / max(light.distance, 0.001f)), light.decay);
        float3 axis = light.direction / max(length(light.direction), 0.001f);
        float cosTheta = dot(-L, axis);
        float falloff = saturate((cosTheta - light.cosAngle) /
            max(light.cosFalloffStart - light.cosAngle, 1e-6f));
        float NdotL = dot(N, L);
        float diff = lightingMode == 2 ? pow(NdotL * 0.5f + 0.5f, 2.0f) : saturate(NdotL);
        float3 color = light.color.rgb * light.intensity * attenuation * falloff;
        diffuse += baseColor * color * diff;
        float3 H = normalize(L + V);
        specular += color * light.specularStrength * pow(saturate(dot(N, H)), max(shininess, 1.0f));
    }
}
