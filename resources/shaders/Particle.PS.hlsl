#include "Particle.hlsli"
struct Material
{
    float4 color;
    int enableLighting;
    float4x4 uvTransform;
};

struct PixelSharderOutput
{
    float4 color : SV_TARGET0;
};

struct DirectionalLight
{
    
    float4 color;
    float3 direction;
    float intensity;
   
};

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);
ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

float FlameLobe(float3 eye, float3 ray, float3 center, float3 radii)
{
    // The chord through a soft ellipsoid determines how much light the ray picks up.
    float3 origin = (eye - center) / radii;
    float3 direction = ray / radii;
    float along = max(0.0f, -dot(origin, direction) / max(dot(direction, direction), 0.00001f));
    float3 closest = origin + along * direction;
    return saturate(1.0f - dot(closest, closest));
}

float FlameDensity(float3 eye, float3 ray)
{
    float base = FlameLobe(eye, ray, float3(0.0f, 0.31f, 0.0f), float3(0.66f, 0.31f, 0.61f));
    float tongue = FlameLobe(eye, ray, float3(0.12f, 0.70f, -0.07f), float3(0.34f, 0.60f, 0.30f));
    float side = FlameLobe(eye, ray, float3(-0.31f, 0.56f, 0.19f), float3(0.34f, 0.39f, 0.28f));
    return 1.0f - (1.0f - base) * (1.0f - tongue) * (1.0f - 0.74f * side);
}

float StreakOpacity(float2 uv)
{
    float2 centerDistance = abs(uv * 2.0f - 1.0f);
    float lengthMask = saturate(1.0f - centerDistance.y);
    float width = lerp(0.16f, 1.0f, lengthMask);
    return smoothstep(0.0f, 0.65f, 1.0f - centerDistance.x / width) * smoothstep(0.0f, 0.52f, lengthMask);
}

PixelSharderOutput main(VertexShaderOutput input, bool frontFace : SV_IsFrontFace)
{
    PixelSharderOutput output;

    if (!frontFace && (input.drawFlags & (kParticleFlameFacets | kParticleFlameGlow)) != 0) {
        discard;
    }
    float4 color = gMaterial.color * input.color;
    [branch]
    if ((input.drawFlags & kParticleUseTexture) != 0) {
        float4 uv = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
        color *= gTexture.Sample(gSampler, uv.xy);
    }
    if ((input.drawFlags & kParticleFlameGlow) != 0) {
        float density = FlameDensity(input.localEye, input.localPosition - input.localEye);
        color.a *= smoothstep(0.0f, 0.82f, density);
        color.rgb *= lerp(0.92f, 1.30f, density);
    } else if ((input.drawFlags & kParticleSparkStreak) != 0) {
        color.a *= StreakOpacity(input.texcoord) * smoothstep(0.0f, 0.010f, input.age);
    } else if ((input.drawFlags & kParticleSoftDisc) != 0) {
        float2 fromCenter = input.texcoord * 2.0f - 1.0f;
        float2 warp = 0.10f * float2(sin(5.0f * fromCenter.y + 1.0f), sin(6.0f * fromCenter.x - 0.4f));
        float2 puff = (fromCenter + warp) / 0.88f;
        float wisps = 0.82f + 0.18f * sin(7.0f * fromCenter.x + 4.0f * fromCenter.y)
            * sin(6.0f * fromCenter.y - 3.0f * fromCenter.x + 1.0f);
        color.a *= smoothstep(0.0f, 0.76f, 1.0f - dot(puff, puff)) * wisps;
    }
    if (color.a <= 0.0f) {
        discard;
    }
    if ((input.drawFlags & kParticleEmissiveBlend) != 0) {
        // Remove background coverage before the emitted light fades out.
        float opacity = saturate(color.a);
        float coverage = saturate(2.0f * opacity - 1.0f);
        color = float4(color.rgb * opacity, coverage * coverage);
    }
    output.color = color;

    return output;
}


