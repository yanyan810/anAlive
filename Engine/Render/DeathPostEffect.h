#pragma once
#include "Vector3.h"
#include <algorithm>
#include <cmath>

// Shared CPU/HLSL layout (four 16-byte rows). Zero strengths use the copy PSO.
struct DeathPostEffectParameters {
    Vector4 vignetteColor{0,0,0,1};
    float vignette=0, desaturation=0, blurPixels=0, chromaticPixels=0;
    float edgeInner=.35f, edgeOuter=1.0f, mosaicStrength=0, mosaicCellSize=.006f;
    float mosaicColorStrength=.8f, mosaicTime=0, mosaicChangeRate=.35f, padding=0;
    bool Active() const { return vignette>0 || desaturation>0 || blurPixels>0 || chromaticPixels>0 || (mosaicStrength>0 && mosaicColorStrength>0); }
};
static_assert(sizeof(DeathPostEffectParameters)==64);
