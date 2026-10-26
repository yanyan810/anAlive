#include "CopyImage.hlsli"
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);
cbuffer DeathParameters : register(b6) {
    float4 vignetteColor;
    float vignette, desaturation, blurPixels, chromaticPixels;
    float edgeInner, edgeOuter, mosaicStrength, mosaicCellSize;
    float mosaicColorStrength, mosaicTime, mosaicChangeRate, padding;
};
float TileHash(float2 cell) {
    // Deterministic spatial variation, also used to seed slow temporal changes.
    float3 p = frac(float3(cell.x, cell.y, cell.x) * .1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}
float4 ReadScene(float2 uv, float2 texel) {
    // The shared post sampler wraps; explicitly keep every tap inside the image.
    return gTexture.Sample(gSampler, clamp(uv, .5 * texel, 1 - .5 * texel));
}
float4 main(VertexShaderOutput input) : SV_TARGET0 {
    uint width, height; gTexture.GetDimensions(width, height);
    float2 texel = rcp(float2(width, height));
    float2 uv = input.texcoord;
    float edge = smoothstep(edgeInner, edgeOuter, length((uv - .5) * 2));
    float4 source = ReadScene(uv, texel);
    float3 color = source.rgb;
    [branch] if (blurPixels > 0 && edge > .001) {
        float2 stepUV = texel * blurPixels * edge;
        // Compact 3x3 Gaussian. Keep the center readable; at most 9 blur taps.
        color = source.rgb * 4;
        color += (ReadScene(uv + float2(stepUV.x, 0), texel).rgb + ReadScene(uv - float2(stepUV.x, 0), texel).rgb
                + ReadScene(uv + float2(0, stepUV.y), texel).rgb + ReadScene(uv - float2(0, stepUV.y), texel).rgb) * 2;
        color += ReadScene(uv + stepUV, texel).rgb + ReadScene(uv - stepUV, texel).rgb
               + ReadScene(uv + float2(stepUV.x, -stepUV.y), texel).rgb + ReadScene(uv + float2(-stepUV.x, stepUV.y), texel).rgb;
        color *= 1.0 / 16.0;
    }
    [branch] if (chromaticPixels > 0 && edge > .001) {
        float2 direction = (uv - .5) / max(length(uv - .5), .0001);
        float2 offset = direction * texel * chromaticPixels * edge;
        color.r += ReadScene(uv + offset, texel).r - source.r;
        color.b += ReadScene(uv - offset, texel).b - source.b;
    }
    // Same luminance weights as the existing Grayscale pass, with a blend amount.
    color = lerp(color, dot(color, float3(.2125, .7154, .0721)).xxx, desaturation);
    color = lerp(color, vignetteColor.rgb, edge * vignette);
    [branch] if (mosaicStrength > 0 && mosaicColorStrength > 0) {
        // Square blocks, normalized by the short side. No UV displacement or
        // grid outlines: color/coverage vary independently from block to block.
        float2 cell = floor(uv * float2(width, height) / (min(width, height) * mosaicCellSize));
        float clock = mosaicTime * mosaicChangeRate;
        float epoch = floor(clock), phase = frac(clock);
        phase = phase * phase * (3 - 2 * phase);
        float2 seed = float2(epoch * 37.1, epoch * 19.7);
        float2 nextSeed = float2((epoch + 1) * 37.1, (epoch + 1) * 19.7);
        float h0 = TileHash(cell + seed), h1 = TileHash(cell + nextSeed);
        // Take the shorter route around the hue circle, continuously at epochs.
        float hue = h0 + (frac(h1 - h0 + .5) - .5) * phase;
        float n0 = TileHash(cell + seed + float2(17.7, 43.1));
        float n1 = TileHash(cell + nextSeed + float2(17.7, 43.1));
        float uneven = lerp(n0, n1, phase);
        float coverage = lerp(.25, 1, smoothstep(.1, .9, uneven));
        float3 rainbow = .5 + .5 * cos(6.2831853 * (hue + float3(0, .3333333, .6666667)));
        float3 palette = lerp(float3(1, 1, 1), .15 + .85 * rainbow, mosaicColorStrength);
        float luminance = dot(color, float3(.2125, .7154, .0721));
        // Independent colored noise, rather than multiplying the scene RGB.
        // A low luminance floor keeps it visible on grey/dark backgrounds;
        // bounded luminance and partial coverage preserve the original scene.
        float noiseLuminance = clamp(luminance, .08, .65) * lerp(.85, 1.15, uneven);
        float3 noiseColor = saturate(palette * noiseLuminance / max(dot(palette, float3(.2125, .7154, .0721)), .1));
        color = lerp(color, noiseColor, saturate(mosaicStrength) * coverage);
    }
    return float4(saturate(color), source.a);
}
