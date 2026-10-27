#pragma once
#include "DeathPostEffect.h"

// Reusable camera effects. Callers supply exposure/contact time; no scene or AI dependency.
struct DoubleVisionSettings {
    bool enabled=true;
    float strength=1;
    Vector2 offsetPixels{10,3};
    float opacity=.32f;
    float swayPixels=12, swaySpeed=1.4f; // Moving ghost, in pixels and cycles/second.
};
struct ImpactBlurSettings {
    bool enabled=true;
    float strength=1.5f, duration=.2f;
};
struct ViewPostEffectParameters {
    float doubleVisionStrength=0;
    Vector2 doubleVisionOffsetPixels{10,3};
    float doubleVisionOpacity=.32f;
    float impactBlurPixels=0;
};
inline float ViewPostLimit(float value,float low,float high,float fallback=0) {
    return std::isfinite(value) ? std::clamp(value,low,high) : fallback;
}
inline float ViewPostSmooth(float x) { x=std::clamp(x,0.0f,1.0f); return x*x*(3-2*x); }
inline float ImpactBlurEnvelope(float contactSeconds,float duration) {
    duration=ViewPostLimit(duration,.02f,2,.2f);
    if (!std::isfinite(contactSeconds) || contactSeconds<0 || contactSeconds>=duration) return 0;
    // A 10ms attack avoids a hard brightness/detail discontinuity at contact.
    return ViewPostSmooth(contactSeconds/(std::min)(.01f,duration*.2f))*(1-ViewPostSmooth(contactSeconds/duration));
}
inline ViewPostEffectParameters EvaluateViewPostEffects(const DoubleVisionSettings& vision,float exposure,
    const ImpactBlurSettings& impact,float contactSeconds,double presentationSeconds=0) {
    ViewPostEffectParameters result;
    result.doubleVisionStrength=vision.enabled ? ViewPostLimit(vision.strength,0,2)*ViewPostLimit(exposure,0,1) : 0;
    result.doubleVisionOffsetPixels={ViewPostLimit(vision.offsetPixels.x,-64,64),ViewPostLimit(vision.offsetPixels.y,-64,64)};
    const float amplitude=ViewPostLimit(vision.swayPixels,0,32)*ViewPostLimit(exposure,0,1);
    const float speed=ViewPostLimit(vision.swaySpeed,0,5);
    if (result.doubleVisionStrength>0 && amplitude>0 && speed>0) {
        const double time=std::isfinite(presentationSeconds) ? std::clamp(presentationSeconds,0.0,86400.0) : 0;
        const auto wave=[&](double frequency) {
            return static_cast<float>(std::sin(std::remainder(time*speed*6.283185307179586*frequency,6.283185307179586)));
        };
        // Unequal frequencies drift in two directions without jumps or camera motion.
        result.doubleVisionOffsetPixels.x+=amplitude*(.7f*wave(1)+.3f*wave(.43));
        result.doubleVisionOffsetPixels.y+=amplitude*(.65f*wave(.71)-.2f*wave(1.17));
    }
    result.doubleVisionOpacity=ViewPostLimit(vision.opacity,0,.8f);
    result.impactBlurPixels=impact.enabled ? ViewPostLimit(impact.strength,0,3)*ImpactBlurEnvelope(contactSeconds,impact.duration) : 0;
    return result;
}
inline void ApplyViewPostEffects(DeathPostEffectParameters& target,const ViewPostEffectParameters& effects) {
    target.doubleVisionStrength=ViewPostLimit(effects.doubleVisionStrength,0,2);
    target.doubleVisionOffsetPixels={ViewPostLimit(effects.doubleVisionOffsetPixels.x,-64,64),ViewPostLimit(effects.doubleVisionOffsetPixels.y,-64,64)};
    target.doubleVisionOpacity=ViewPostLimit(effects.doubleVisionOpacity,0,.8f);
    target.impactBlurPixels=ViewPostLimit(effects.impactBlurPixels,0,3);
}
