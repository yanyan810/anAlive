#pragma once
#include "Vector3.h"
#include <algorithm>
#include <cmath>

// Shared CPU/HLSL layout (six 16-byte rows). Zero strengths use the copy PSO.
struct DeathPostEffectParameters {
    Vector4 vignetteColor{0,0,0,1};
    float vignette=0, desaturation=0, blurPixels=0, chromaticPixels=0;
    float edgeInner=.35f, edgeOuter=1.0f, mosaicStrength=0, mosaicCellSize=.006f;
    float mosaicColorStrength=.8f, mosaicTime=0, mosaicChangeRate=20, mosaicDensity=1;
    float mosaicOcclusion=0, impactBlurPixels=0, doubleVisionStrength=0, doubleVisionOpacity=.32f;
    Vector2 doubleVisionOffsetPixels{10,3};
    float padding[2]{};
    bool Active() const { return vignette>0 || desaturation>0 || blurPixels>0 || chromaticPixels>0 || impactBlurPixels>0 ||
        (doubleVisionStrength>0 && doubleVisionOpacity>0 && (doubleVisionOffsetPixels.x!=0 || doubleVisionOffsetPixels.y!=0)) ||
        mosaicOcclusion>0 || (mosaicStrength>0 && mosaicColorStrength>0 && mosaicDensity>0); }
};
static_assert(sizeof(DeathPostEffectParameters)==96);
