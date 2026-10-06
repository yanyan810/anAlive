struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
    float4x4 shadowViewProjection;
    float4 shadowParameters; // strength, depth bias, inverse resolution, ambient fill intensity
};
Texture2D<float> gDirectionalShadow : register(t4);
SamplerState gShadowSampler : register(s1);

float3 AmbientFill(DirectionalLight light, float3 baseColor)
{
    return baseColor * light.color.rgb * max(light.shadowParameters.w, 0);
}

float DirectionalVisibility(DirectionalLight light, float3 worldPosition, float3 normal)
{
    if (light.shadowParameters.x <= 0) return 1;
    float4 clip = mul(float4(worldPosition, 1), light.shadowViewProjection);
    float3 ndc = clip.xyz / max(clip.w, 0.00001f);
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    if (any(uv < 0) || any(uv > 1) || ndc.z < 0 || ndc.z > 1) return 1;
    float bias = light.shadowParameters.y * (2 - saturate(dot(normal, -normalize(light.direction))));
    float visibility = 0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float depth = gDirectionalShadow.SampleLevel(gShadowSampler,
                uv + float2(x, y) * light.shadowParameters.z, 0);
            visibility += ndc.z - bias <= depth ? 1 : 0;
        }
    }
    return lerp(1, visibility / 9, saturate(light.shadowParameters.x));
}
