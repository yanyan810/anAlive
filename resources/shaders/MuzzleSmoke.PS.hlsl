#include "MuzzleSmoke.hlsli"

cbuffer Material : register(b0) {
    float4 materialColor;
};

float SmokeNoise(float2 p)
{
    float2 cell = floor(p);
    float2 weight = frac(p);
    weight = weight * weight * (3.0f - 2.0f * weight);
    // Hash the four lattice corners together without texture reads or trigonometry.
    float4 n = dot(cell, float2(0.06711056f, 0.00583715f))
        + float4(0.0f, 0.06711056f, 0.00583715f, 0.07294771f);
    n = frac(52.9829189f * frac(n));
    return lerp(lerp(n.x, n.y, weight.x), lerp(n.z, n.w, weight.x), weight.y);
}

float SmokeOpacity(float2 uv, float age, float2 seed, bool vertical, float noiseAmount)
{
    float2 p = uv * 2.0f - 1.0f;
    // Keep the quad edges transparent as the density field moves underneath.
    float edge = 1.0f - smoothstep(0.68f, 0.98f, max(abs(p.x), abs(p.y)));
    float2 offset = seed * 31.0f;
    float2 flow = float2(0.25f, vertical ? 2.2f : 1.5f) * age;
    float broad = SmokeNoise(p * 1.85f + offset + flow);
    float2 bent = p + float2(0.50f, -0.24f) * ((broad - 0.5f) * noiseAmount);
    float folds = SmokeNoise(bent * float2(3.5f, 4.2f) - offset + flow * 1.6f);
    float wisps = SmokeNoise(bent * float2(7.8f, 6.1f) + offset.yx + flow * 2.2f + folds);

    float width = lerp(1.08f, 1.46f, seed.x);
    if (vertical) {
        // Narrow the foot and let the plume open up towards its top.
        width *= lerp(1.20f, 1.60f, saturate(uv.y));
    }
    float2 shape = bent * float2(width, 1.02f);
    float envelope = smoothstep(0.0f, 0.78f, 1.0f - dot(shape, shape) + 0.44f * (folds - 0.5f) * noiseAmount);
    float density = smoothstep(0.34f, 0.74f, 0.40f * broad + 0.43f * folds + 0.17f * wisps);
    return edge * envelope * lerp(0.62f, density, noiseAmount);
}

float4 main(SmokeVertexOutput input) : SV_Target0 {
    float4 color = materialColor * input.color;
    clip(color.a - (1.0f / 255.0f));
    color.a *= SmokeOpacity(input.texcoord, input.age, input.seed, input.shape == kSmokeShapeVertical, input.noiseAmount);
    clip(color.a - (1.0f / 255.0f));
    return color;
}
