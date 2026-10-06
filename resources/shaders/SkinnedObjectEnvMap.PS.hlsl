#include "Object3d.hlsli"

struct Material
{
    float4 color;
    int enableLighting;
    float3 _pad0;

    float4x4 uvTransform;

    float shininess;
    float environmentCoefficient;
    float2 _pad1;
};

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
};

struct Camera
{
    float3 worldPosition;
    float _pad;
};

struct PointLight
{
    float4 color;
    float3 position;
    float intensity;
    float radius;
    float decay;
    float2 _pad;
};

#include "SpotLighting.hlsli"

Texture2D gTexture : register(t1);
TextureCube gEnvironmentTexture : register(t2);
SamplerState gSampler : register(s0);

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<Camera> gCamera : register(b2);
ConstantBuffer<PointLight> gPointLight : register(b3);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 uv = mul(float4(input.texcoord, 0, 1), gMaterial.uvTransform);
    float4 tex = gTexture.Sample(gSampler, uv.xy);

    output.color = gMaterial.color * tex;

    if (gMaterial.enableLighting == 0)
    {
        output.color.a = gMaterial.color.a * tex.a;
        return output;
    }

    float3 N = normalize(input.normal);
    float3 V = normalize(gCamera.worldPosition - input.worldPosition);

    float3 Ld = normalize(-gDirectionalLight.direction);
    float NdotLd = dot(N, Ld);
    float diffD =
        (gMaterial.enableLighting == 2) ?
        pow(NdotLd * 0.5f + 0.5f, 2.0f) :
        saturate(NdotLd);

    float3 diffuseD =
        gMaterial.color.rgb * tex.rgb *
        gDirectionalLight.color.rgb *
        diffD * gDirectionalLight.intensity;

    float3 Hd = normalize(Ld + V);
    float specD = pow(saturate(dot(N, Hd)), max(gMaterial.shininess, 1.0f));
    float3 specularD = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specD;

    float3 toP = gPointLight.position - input.worldPosition;
    float distP = max(length(toP), 0.001f);
    float3 Lp = toP / distP;

    float t = saturate(1.0f - distP / max(gPointLight.radius, 0.001f));
    float attenP = pow(t, gPointLight.decay);

    float diffP =
        (gMaterial.enableLighting == 2) ?
        pow(dot(N, Lp) * 0.5f + 0.5f, 2.0f) :
        saturate(dot(N, Lp));

    float3 pointCol = gPointLight.color.rgb * gPointLight.intensity * attenP;
    float3 diffuseP = gMaterial.color.rgb * tex.rgb * pointCol * diffP;

    float3 Hp = normalize(Lp + V);
    float specP = pow(saturate(dot(N, Hp)), max(gMaterial.shininess, 1.0f));
    float3 specularP = pointCol * specP;

    float3 diffuseS, specularS;
    CalculateSpotLighting(input.worldPosition, N, V, gMaterial.color.rgb * tex.rgb,
        gMaterial.enableLighting, gMaterial.shininess, diffuseS, specularS);

    float3 cameraToPos = normalize(input.worldPosition - gCamera.worldPosition);
    float3 reflected = reflect(cameraToPos, N);
    float3 envColor = gEnvironmentTexture.Sample(gSampler, reflected).rgb;

    float3 diffuseSum = diffuseD + diffuseP + diffuseS;
    float3 specularSum = specularD + specularP + specularS;

    output.color.rgb =
        diffuseSum +
        specularSum +
        envColor * gMaterial.environmentCoefficient;

    output.color.a = gMaterial.color.a * tex.a;
    return output;
}
