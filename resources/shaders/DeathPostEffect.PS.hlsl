#include "CopyImage.hlsli"
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);
cbuffer DeathParameters : register(b6) {
    float4 vignetteColor;
    float vignette, desaturation, blurPixels, chromaticPixels;
    float edgeInner, edgeOuter, mosaicStrength, mosaicCellSize;
    float mosaicColorStrength, mosaicTime, mosaicChangeRate, mosaicDensity;
    float mosaicOcclusion, impactBlurPixels, doubleVisionStrength, doubleVisionOpacity;
    float2 doubleVisionOffsetPixels;
    float2 padding;
};
uint NoiseHash(uint value) {
    // Integer avalanche: each epoch replaces the pattern without translating it.
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu;
    return value ^ (value >> 16);
}
float NoiseUnit(uint value) {
    return float(NoiseHash(value) >> 8) * (1.0 / 16777216.0);
}
float4 ReadScene(float2 uv, float2 texel) {
    // The shared post sampler wraps; explicitly keep every tap inside the image.
    return gTexture.Sample(gSampler, clamp(uv, .5 * texel, 1 - .5 * texel));
}
float3 ReadBlurredScene(float2 uv, float2 texel, float radius) {
    float3 color = ReadScene(uv, texel).rgb;
    [branch] if (radius > .001) {
        float2 stepUV = texel * radius;
        // Compact 3x3 Gaussian. Keep the center readable; at most 9 blur taps.
        color *= 4;
        color += (ReadScene(uv + float2(stepUV.x, 0), texel).rgb + ReadScene(uv - float2(stepUV.x, 0), texel).rgb
                + ReadScene(uv + float2(0, stepUV.y), texel).rgb + ReadScene(uv - float2(0, stepUV.y), texel).rgb) * 2;
        color += ReadScene(uv + stepUV, texel).rgb + ReadScene(uv - stepUV, texel).rgb
               + ReadScene(uv + float2(stepUV.x, -stepUV.y), texel).rgb + ReadScene(uv + float2(-stepUV.x, stepUV.y), texel).rgb;
        color *= 1.0 / 16.0;
    }
    return color;
}
float4 main(VertexShaderOutput input) : SV_TARGET0 {
    uint width, height; gTexture.GetDimensions(width, height);
    float2 texel = rcp(float2(width, height));
    float2 uv = input.texcoord;
    float edge = smoothstep(edgeInner, edgeOuter, length((uv - .5) * 2));
    float4 source = ReadScene(uv, texel);
    // Reuse the same 3x3 kernel for peripheral blur and full-frame impact blur.
    float radius = max(blurPixels * edge, impactBlurPixels);
    float3 color = ReadBlurredScene(uv, texel, radius);
    [branch] if (doubleVisionStrength > 0 && doubleVisionOpacity > 0) {
        float2 offset = texel * doubleVisionOffsetPixels * doubleVisionStrength;
        float3 ghost = ReadBlurredScene(uv + offset, texel, radius);
        color = lerp(color, ghost, saturate(doubleVisionOpacity * doubleVisionStrength));
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
    [branch] if (mosaicOcclusion > 0 || (mosaicStrength > 0 && mosaicColorStrength > 0 && mosaicDensity > 0)) {
        // Square blocks, normalized by the short side. No UV displacement or
        // grid outlines: color/coverage vary independently from block to block.
        uint2 cell = uint2(floor(uv * float2(width, height) / (min(width, height) * mosaicCellSize)));
        // Hold exactly one random pattern for each 1/rate interval. The tiny
        // tolerance absorbs float clock rounding at 30/60/144 FPS boundaries.
        uint epoch = uint(floor(mosaicTime * mosaicChangeRate + .0001));
        uint seed = NoiseHash(cell.x ^ NoiseHash(cell.y + 0x9e3779b9u) ^ NoiseHash(epoch + 0x85ebca6bu));
        float hue = NoiseUnit(seed);
        float uneven = NoiseUnit(seed ^ 0x68bc21ebu);
        float densityMask = NoiseUnit(seed ^ 0x02e5be93u) < mosaicDensity ? 1 : 0;
        float coverage = lerp(.25, 1, smoothstep(.1, .9, uneven)) * densityMask;
        float3 rainbow = .5 + .5 * cos(6.2831853 * (hue + float3(0, .3333333, .6666667)));
        float3 palette = lerp(float3(1, 1, 1), .15 + .85 * rainbow, mosaicColorStrength);
        float luminance = dot(color, float3(.2125, .7154, .0721));
        // Independent colored noise, rather than multiplying the scene RGB.
        // A low luminance floor keeps it visible on grey/dark backgrounds;
        // bounded luminance and partial coverage preserve the original scene.
        float noiseLuminance = clamp(luminance, .08, .65) * lerp(.85, 1.15, uneven);
        // At full cover neither the noise's brightness nor its coverage depends
        // on the scene, so silhouettes cannot remain visible underneath it.
        noiseLuminance = mosaicOcclusion >= 1 ? .18 * lerp(.85, 1.15, uneven)
            : lerp(noiseLuminance, .18 * lerp(.85, 1.15, uneven), mosaicOcclusion);
        float3 noiseColor = saturate(palette * noiseLuminance / max(dot(palette, float3(.2125, .7154, .0721)), .1));
        // Ordinary awakening retains scene detail; the disappearance alone
        // overrides partial cell coverage and the ordinary 85% mixing limit.
        float mixAmount = lerp(min(saturate(mosaicStrength), .85) * coverage, 1, mosaicOcclusion);
        color = mosaicOcclusion >= 1 ? noiseColor : lerp(color, noiseColor, mixAmount);
    }
    return float4(saturate(color), source.a);
}
