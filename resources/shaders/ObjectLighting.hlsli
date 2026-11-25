#include "LightingMath.hlsli"
#include "DirectionalLighting.hlsli"
#include "SpotLighting.hlsli"
#include "LocalLighting.hlsli"

struct LightingCamera
{
    float3 worldPosition;
    float padding;
};

struct PointLight
{
    float4 color;
    float3 position;
    float intensity;
    float radius;
    float decay;
    float2 padding;
};

ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<LightingCamera> gCamera : register(b2);
ConstantBuffer<PointLight> gPointLight : register(b3);

struct LightTerms
{
    float3 diffuse;
    float3 specular;
};

float3 ResolveSurfaceNormal(float3 vertexNormal, float3 toEye, int materialMode)
{
    float3 N = normalize(vertexNormal);
    // Material mode 3 is used by the two-sided fracture triangles.
    return materialMode == 3 && dot(N, toEye) < 0.0f ? -N : N;
}

int ResolveDiffuseMode(int materialMode)
{
    return gDirectionalLight.diffuseMode != 0 ? gDirectionalLight.diffuseMode : materialMode;
}

void EvaluateObjectLighting(float3 position, float3 N, float3 V, float3 baseColor,
    int mode, float shininess, out LightTerms directional, out LightTerms pointTerms, out LightTerms spot)
{
    directional = (LightTerms)0;
    pointTerms = (LightTerms)0;
    if (gDirectionalLight.intensity != 0.0f)
    {
        float3 L = normalize(-gDirectionalLight.direction);
        float3 radiance = gDirectionalLight.color.rgb * gDirectionalLight.intensity *
            DirectionalVisibility(gDirectionalLight, position, N);
        directional.diffuse = baseColor * radiance * DiffuseFactor(dot(N, L), mode);
        directional.specular = radiance * BlinnPhongFactor(N, L, V, shininess);
    }
    if (gPointLight.intensity != 0.0f)
    {
        float3 delta = gPointLight.position - position;
        float distance = max(length(delta), 0.001f);
        float3 L = delta / distance;
        float3 radiance = gPointLight.color.rgb * gPointLight.intensity *
            LegacyLightFalloff(distance, gPointLight.radius, gPointLight.decay);
        pointTerms.diffuse = baseColor * radiance * DiffuseFactor(dot(N, L), mode);
        pointTerms.specular = radiance * BlinnPhongFactor(N, L, V, shininess);
    }

    CalculateSpotLighting(position, N, V, baseColor, mode, shininess, spot.diffuse, spot.specular);
    directional.specular *= gDirectionalLight.specularStrength;
    pointTerms.specular *= gDirectionalLight.specularStrength;
    spot.specular *= gDirectionalLight.specularStrength;
}
