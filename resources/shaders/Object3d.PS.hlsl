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
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<Camera> gCamera : register(b2);
ConstantBuffer<PointLight> gPointLight : register(b3);
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
    float3 N = normalize(input.normal);
    float3 V = normalize(gCamera.worldPosition - input.worldPosition);

    // =====================
    // Directional Light
    // =====================
    float3 Ld = normalize(-gDirectionalLight.direction);
    float NdotLd = dot(N, Ld);

    float diffD =
        (gMaterial.enableLighting == 2)
        ? pow(NdotLd * 0.5f + 0.5f, 2.0f)
        : saturate(NdotLd);

    float3 diffuseD =
        gMaterial.color.rgb * tex.rgb *
        gDirectionalLight.color.rgb *
        diffD * gDirectionalLight.intensity;

    float3 Hd = normalize(Ld + V);
    float specD = pow(saturate(dot(N, Hd)), max(gMaterial.shininess, 1.0f));
    float3 specularD = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specD;

    // =====================
    // Point Light
    // =====================
    float3 toP = gPointLight.position - input.worldPosition;
    float distP = max(length(toP), 0.001f);
    float3 Lp = toP / distP;

    float t = saturate(1.0f - distP / max(gPointLight.radius, 0.001f));
    float attenP = pow(t, gPointLight.decay);

    float diffP =
        (gMaterial.enableLighting == 2)
        ? pow(dot(N, Lp) * 0.5f + 0.5f, 2.0f)
        : saturate(dot(N, Lp));

    float3 pointCol = gPointLight.color.rgb * gPointLight.intensity * attenP;

    float3 diffuseP = gMaterial.color.rgb * tex.rgb * pointCol * diffP;

    float3 Hp = normalize(Lp + V);
    float specP = pow(saturate(dot(N, Hp)), max(gMaterial.shininess, 1.0f));
    float3 specularP = pointCol * specP;

    // =====================
    // Spot Light（FalloffStart 対応）
    // =====================
    float3 diffuseS, specularS;
    CalculateSpotLighting(input.worldPosition, N, V, gMaterial.color.rgb * tex.rgb,
        gMaterial.enableLighting, gMaterial.shininess, diffuseS, specularS);

    // =====================
    // Debug View
    // =====================
    if (gMaterial.enableLighting == 11)
    {
        output.color.rgb = diffuseD + specularD;
        output.color.a = 1;
        return output;
    }
    if (gMaterial.enableLighting == 12)
    {
        output.color.rgb = diffuseP + specularP;
        output.color.a = 1;
        return output;
    }
    if (gMaterial.enableLighting == 13)
    {
        output.color.rgb = diffuseS + specularS;
        output.color.a = 1;
        return output;
    }

    // =====================
    // Final
    // =====================
    output.color.rgb =
        diffuseD + specularD +
        diffuseP + specularP +
        diffuseS + specularS;

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
