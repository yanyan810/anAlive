#include "LightingMath.hlsli"

struct LocalLightOccluder
{
    float4 inverseX, inverseY, inverseZ;
    float4 minimum, maximum;
};

struct LocalLight
{
    float3 position;
    uint mode; // None, Point, Spot, Area
    float3 color;
    float intensity;
    float3 direction;
    float range;
    float3 areaRight;
    float decay;
    float3 areaUp;
    float specularStrength;
    float indirectIntensity;
    float indirectRange;
    uint occluderCount;
    float visibilityRange;
    uint axisAlignedOccluderCount;
    uint3 padding;
    LocalLightOccluder occluders[24];
};

ConstantBuffer<LocalLight> gLocalLight : register(b6);

bool LocalLightIntersectsBox(float3 minimum, float3 maximum, float3 inverseDirection,
    bool3 parallel, float segmentLength)
{
    if (any(parallel & ((minimum > 0.0f) | (maximum < 0.0f)))) return false;
    float3 a = minimum * inverseDirection;
    float3 b = maximum * inverseDirection;
    float3 entry = lerp(min(a, b), -1e8f, float3(parallel));
    float3 exit = lerp(max(a, b), 1e8f, float3(parallel));
    float start = max(0.0f, max(entry.x, max(entry.y, entry.z)));
    float end = min(segmentLength, min(exit.x, min(exit.y, exit.z)));
    return start < end;
}

bool LocalLightOccluded(float3 position, float3 normal)
{
    // Offset the receiver into its lit side so it can receive light on its own collider.
    float3 delta = position + normal * 0.035f - gLocalLight.position;
    float distance = length(delta);
    float3 ray = delta / max(distance, 0.001f);
    float segmentLength = distance - 0.025f;

    bool3 parallel = abs(ray) < 1e-8f;
    float3 inverseRay = rcp(ray + float3(parallel));
    uint i = 0;
    [loop]
    for (; i < gLocalLight.axisAlignedOccluderCount; ++i)
    {
        // Most level proxies share world axes. Reuse the same inverse ray for all of them.
        if (LocalLightIntersectsBox(gLocalLight.occluders[i].minimum.xyz,
            gLocalLight.occluders[i].maximum.xyz, inverseRay, parallel, segmentLength)) return true;
    }

    [loop]
    for (; i < gLocalLight.occluderCount; ++i)
    {
        LocalLightOccluder box = gLocalLight.occluders[i];
        float3 direction = float3(dot(ray, box.inverseX.xyz), dot(ray, box.inverseY.xyz),
            dot(ray, box.inverseZ.xyz));
        bool3 localParallel = abs(direction) < 1e-8f;
        float3 inverseDirection = rcp(direction + float3(localParallel));
        if (LocalLightIntersectsBox(box.minimum.xyz, box.maximum.xyz, inverseDirection, localParallel, segmentLength))
            return true;
    }
    return false;
}

float LocalLightRadialFade(float distance, float range)
{
    return 1.0f - smoothstep(0.35f, 1.0f, distance / max(range, 0.001f));
}

float3 LocalLightSample(float3 source, float3 position, float3 N, float3 V,
    float3 albedo, int lightingMode, float shininess)
{
    float3 offset = source - position;
    float distance = length(offset);
    float3 L = offset / max(distance, 0.001f);
    float NoL = saturate(dot(N, L));

    if (gLocalLight.mode == 2)
    {
        // Match the legacy spot's angle and distance response in the comparison slot.
        float cone = SpotConeFalloff(dot(-L, gLocalLight.direction), 0.8660254f, 0.9659258f);
        float falloff = LegacyLightFalloff(distance, gLocalLight.range, gLocalLight.decay);
        float diffuse = DiffuseFactor(dot(N, L), lightingMode);
        float specular = BlinnPhongFactor(N, L, V, shininess);
        return (albedo * diffuse + specular * gLocalLight.specularStrength) * (cone * falloff);
    }

    float attenuation = LocalLightRadialFade(distance, gLocalLight.range) /
        (1.0f + pow(distance / 1.5f, gLocalLight.decay));
    if (gLocalLight.mode == 3)
        attenuation *= abs(dot(-L, gLocalLight.direction)); // Two-sided rectangle.

    float specular = BlinnPhongFactor(N, L, V, max(shininess, 16.0f)) * NoL;
    return (albedo * NoL + specular * gLocalLight.specularStrength) * attenuation;
}

float3 ApplyLocalLighting(float3 existing, float3 position, float3 N, float3 V,
    float3 albedo, int lightingMode, float shininess)
{
    [branch]
    if (gLocalLight.intensity <= 0.0f && gLocalLight.indirectIntensity <= 0.0f) return existing;

    float reach = gLocalLight.range;
    if (gLocalLight.mode == 3)
        reach += 0.25f * (length(gLocalLight.areaRight) + length(gLocalLight.areaUp));

    float3 fromCenter = position - gLocalLight.position;
    float distance = length(fromCenter);
    float indirect = gLocalLight.indirectIntensity * LocalLightRadialFade(distance, gLocalLight.indirectRange);
    bool direct = gLocalLight.mode != 0 && gLocalLight.intensity > 0.0f && distance < reach;
    if (gLocalLight.mode == 2)
        direct = direct && dot(fromCenter, gLocalLight.direction) > distance * 0.8660254f;
    [branch]
    if (!direct && indirect <= 0.0f) return existing;

    float coverage = 1.0f;
    if (gLocalLight.visibilityRange > 0.0f)
        coverage = 1.0f - smoothstep(max(0.0f, gLocalLight.visibilityRange - 0.3f),
            gLocalLight.visibilityRange, distance);
    [branch]
    if (coverage <= 0.0f || LocalLightOccluded(position, N)) return existing;

    float3 extra = albedo * indirect;
    if (direct)
    {
        float3 incident = 0.0f;
        if (gLocalLight.mode == 3)
        {
            // Four strata with equal weights. The center shadow is shared by all four.
            [unroll]
            for (uint i = 0; i < 4; ++i)
            {
                float x = (i & 1) != 0 ? 0.25f : -0.25f;
                float y = (i & 2) != 0 ? 0.25f : -0.25f;
                float3 source = gLocalLight.position + gLocalLight.areaRight * x + gLocalLight.areaUp * y;
                incident += LocalLightSample(source, position, N, V, albedo, lightingMode, shininess);
            }
            incident *= 0.25f;
        }
        else
        {
            incident = LocalLightSample(gLocalLight.position, position, N, V, albedo, lightingMode, shininess);
        }
        extra += incident * gLocalLight.intensity;
    }

    // Reserve headroom in the LDR target without changing the existing light sum.
    extra *= gLocalLight.color * coverage;
    float3 headroom = max(1.0f - existing, 0.0f);
    return existing + headroom * extra / max(headroom + extra, 1e-5f);
}
