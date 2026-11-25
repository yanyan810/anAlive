#include "Object3d.hlsli"

// =====================
// Constant Buffers
// =====================
struct Material
{
    float4 color;
    int enableLighting;
    float3 _pad0;

    float4x4 uvTransform;

    float shininess;
    float environmentCoefficient; // 追加
    float2 _pad1;
};

#include "ObjectLighting.hlsli"

struct EffectParam {
    // Outline
    float4 outlineColor;
    float outlineThickness;
    float enableOutline;
    float2 pad0;

    // Dissolve
    float4 dissolveEdgeColor;
    float dissolveThreshold;
    float dissolveEdgeWidth;
    float enableDissolve;
    float pad1;
};

// =====================
// Resources
// =====================
Texture2D gTexture : register(t1);
TextureCube gEnvironmentTexture : register(t2);
Texture2D gMaskTexture : register(t3);
SamplerState gSampler : register(s0);

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<EffectParam> gEffect : register(b5);

// =====================
// Pixel Shader
// =====================
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 uv = mul(float4(input.texcoord, 0, 1), gMaterial.uvTransform);
    float4 texColor = gTexture.Sample(gSampler, uv.xy);

    // Dissolve処理
    float edgeFactor = 0.0f;
    if (gEffect.enableDissolve != 0.0f) {
        float mask = gMaskTexture.Sample(gSampler, uv.xy).r;
        if (mask <= gEffect.dissolveThreshold) {
            discard;
        }
        edgeFactor = 1.0f - smoothstep(gEffect.dissolveThreshold, gEffect.dissolveThreshold + gEffect.dissolveEdgeWidth, mask);
    }

    if (gMaterial.enableLighting == 0)
    {
        output.color = gMaterial.color * texColor;
        output.color.rgb += edgeFactor * gEffect.dissolveEdgeColor.rgb;
        output.color.a = gMaterial.color.a * texColor.a;
        return output;
    }

    float3 V = normalize(gCamera.worldPosition - input.worldPosition);
    float3 N = ResolveSurfaceNormal(input.normal, V, gMaterial.enableLighting);
    float3 baseColor = gMaterial.color.rgb * texColor.rgb;
    int lightingMode = ResolveDiffuseMode(gMaterial.enableLighting);
    LightTerms directional, pointTerms, spot;
    EvaluateObjectLighting(input.worldPosition, N, V, baseColor, lightingMode, gMaterial.shininess,
        directional, pointTerms, spot);

    float3 envColor = gEnvironmentTexture.Sample(gSampler, reflect(-V, N)).rgb;
    float3 diffuse = directional.diffuse + pointTerms.diffuse + spot.diffuse;
    float3 specular = directional.specular + pointTerms.specular + spot.specular;
    output.color.rgb = diffuse + specular + AmbientFill(gDirectionalLight, baseColor) + envColor * gMaterial.environmentCoefficient;
    if (gDirectionalLight.diffuseMode == 0)
    {
        // Keep the original environment-map blend for existing objects with material-defined lighting.
        float3 mixedColor = lerp(baseColor, envColor, gMaterial.environmentCoefficient);
        output.color.rgb = mixedColor * diffuse + specular + AmbientFill(gDirectionalLight, mixedColor);
    }
    output.color.rgb = ApplyLocalLighting(output.color.rgb, input.worldPosition, N, V,
        baseColor, lightingMode, gMaterial.shininess);

    // Dissolveのエッジ色を加算
    output.color.rgb += edgeFactor * gEffect.dissolveEdgeColor.rgb;

    output.color.a = gMaterial.color.a * texColor.a;
    
    return output;
}
