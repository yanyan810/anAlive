#ifndef OBJECT_LIGHTING_MATH_HLSLI
#define OBJECT_LIGHTING_MATH_HLSLI

float DiffuseFactor(float cosine, int mode)
{
    return mode == 2 ? pow(cosine * 0.5f + 0.5f, 2.0f) : saturate(cosine);
}

float BlinnPhongFactor(float3 normal, float3 toLight, float3 toEye, float shininess)
{
    float3 halfVector = toLight + toEye;
    halfVector *= rsqrt(max(dot(halfVector, halfVector), 1e-6f));
    return pow(saturate(dot(normal, halfVector)), max(shininess, 1.0f));
}

float LegacyLightFalloff(float distance, float range, float decay)
{
    return pow(saturate(1.0f - distance / max(range, 0.001f)), decay);
}

float SpotConeFalloff(float cosine, float outer, float inner)
{
    return saturate((cosine - outer) / max(inner - outer, 1e-6f));
}

#endif
