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

#include "ObjectLighting.hlsli"

Texture2D gTexture : register(t1);
TextureCube gEnvironmentTexture : register(t2);
SamplerState gSampler : register(s0);

ConstantBuffer<Material> gMaterial : register(b0);

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

    float3 V = normalize(gCamera.worldPosition - input.worldPosition);
    float3 N = ResolveSurfaceNormal(input.normal, V, gMaterial.enableLighting);
    float3 baseColor = gMaterial.color.rgb * tex.rgb;
    int lightingMode = ResolveDiffuseMode(gMaterial.enableLighting);
    LightTerms directional, pointTerms, spot;
    EvaluateObjectLighting(input.worldPosition, N, V, baseColor, lightingMode, gMaterial.shininess,
        directional, pointTerms, spot);

    float3 envColor = gEnvironmentTexture.Sample(gSampler, reflect(-V, N)).rgb;
    float3 diffuse = directional.diffuse + pointTerms.diffuse + spot.diffuse;
    float3 specular = directional.specular + pointTerms.specular + spot.specular;
    output.color.rgb = diffuse + specular + AmbientFill(gDirectionalLight, baseColor) + envColor * gMaterial.environmentCoefficient;
    output.color.rgb = ApplyLocalLighting(output.color.rgb, input.worldPosition, N, V,
        baseColor, lightingMode, gMaterial.shininess);

    output.color.a = gMaterial.color.a * tex.a;
    return output;
}
