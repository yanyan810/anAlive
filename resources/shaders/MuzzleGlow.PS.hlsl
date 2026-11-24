#include "Particle.hlsli"

float4 main(VertexShaderOutput input) : SV_Target0
{
    float2 p = input.texcoord * 2.0f - 1.0f;
    float r2 = dot(p, p);
    float edge = 1.0f - smoothstep(0.55f, 1.0f, r2);
    float core = exp(-18.0f * r2);
    float halo = exp(-5.0f * r2);
    float opacity = saturate(input.color.a) * edge;
    float3 rgb = (float3(1.0f, 0.98f, 0.90f) * core +
        input.color.rgb * halo * 0.45f) * opacity;
    // RGB is premultiplied here; use alpha=1 with SRC_ALPHA + ONE.
    // The dedicated PSO does not write destination alpha.
    return float4(rgb, 1.0f);
}
