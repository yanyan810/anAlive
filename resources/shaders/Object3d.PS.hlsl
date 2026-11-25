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
    float3 _pad1;
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

    // Random
    float enableRandom;
    float randomTime;
    float2 pad2;
};

// =====================
// Resources
// =====================
Texture2D gTexture : register(t1);
Texture2D gEnvTexture : register(t2);
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

// 乱数生成アルゴリズム
float rand2dTo1d(float2 value, float2 dotDir = float2(12.9898, 78.233)) {
    float2 smallValue = sin(value);
    float random = dot(smallValue, dotDir);
    random = frac(sin(random) * 143758.5453);
    return random;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // UV
    float4 uv = mul(float4(input.texcoord, 0, 1), gMaterial.uvTransform);
    float4 tex = gTexture.Sample(gSampler, uv.xy);

    // Dissolve処理
    float edgeFactor = 0.0f;
    if (gEffect.enableDissolve != 0.0f) {
        float mask = gMaskTexture.Sample(gSampler, uv.xy).r;
        if (mask <= gEffect.dissolveThreshold) {
            discard;
        }
        edgeFactor = 1.0f - smoothstep(gEffect.dissolveThreshold, gEffect.dissolveThreshold + gEffect.dissolveEdgeWidth, mask);
    }

    output.color = gMaterial.color * tex;

    if (gMaterial.enableLighting == 0) {
        output.color.rgb += edgeFactor * gEffect.dissolveEdgeColor.rgb;
        return output;
    }

    // =====================
    // Common vectors
    // =====================
    float3 V = normalize(gCamera.worldPosition - input.worldPosition);
    float3 N = ResolveSurfaceNormal(input.normal, V, gMaterial.enableLighting);
    float3 baseColor = gMaterial.color.rgb * tex.rgb;
    int lightingMode = ResolveDiffuseMode(gMaterial.enableLighting);
    LightTerms directional, pointTerms, spot;
    EvaluateObjectLighting(input.worldPosition, N, V, baseColor, lightingMode, gMaterial.shininess,
        directional, pointTerms, spot);

    directional.diffuse += AmbientFill(gDirectionalLight, baseColor);
    if (gMaterial.enableLighting == 11 || gMaterial.enableLighting == 12 || gMaterial.enableLighting == 13)
    {
        float3 selected = gMaterial.enableLighting == 11 ? directional.diffuse + directional.specular :
            (gMaterial.enableLighting == 12 ? pointTerms.diffuse + pointTerms.specular : spot.diffuse + spot.specular);
        output.color = float4(selected, 1.0f);
        return output;
    }

    output.color.rgb = directional.diffuse + directional.specular + pointTerms.diffuse + pointTerms.specular + spot.diffuse + spot.specular;
    output.color.rgb = ApplyLocalLighting(output.color.rgb, input.worldPosition, N, V,
        baseColor, lightingMode, gMaterial.shininess);

    // Dissolveのエッジ色を加算 (自発光のように)
    output.color.rgb += edgeFactor * gEffect.dissolveEdgeColor.rgb;

    // Random
    if (gEffect.enableRandom != 0.0f) {
        float random = rand2dTo1d(input.texcoord * gEffect.randomTime);
        output.color.rgb *= random;
    }

    output.color.a = gMaterial.color.a * tex.a;
    return output;
}
