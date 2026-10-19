#pragma once
#include "EnemySenses.h"
#include "Vector3.h"
#include <array>
#include <cmath>

// Per-enemy, bounded value state: copied by rewind and cleared by AI/pool reset.
struct EnemyObservationMemory {
    struct Sample { Vector3 position{}; double time=0; unsigned int segment=0; };
    static constexpr size_t kCapacity=64;
    std::array<Sample,kCapacity> samples{};
    size_t count=0;
    double clock=0;
    unsigned int segment=0;
    bool previouslyVisible=false, hasDirection=false;
    Vector3 escapeDirection{};
    void Update(float dt,bool visible,const Vector3& physicalTarget,const EnemySenses& settings) {
        clock+=dt;
        size_t expired=0;
        while (expired<count && clock-samples[expired].time>settings.memoryRetentionTime) ++expired;
        for (size_t i=expired;i<count;++i) samples[i-expired]=samples[i];
        count-=expired;
        if (settings.memoryRetentionTime<=0) { count=0; hasDirection=false; escapeDirection={}; previouslyVisible=visible; return; }
        // Never append or recompute direction from a hidden physical target.
        if (visible && dt>0) {
            if (!previouslyVisible || count==0) ++segment;
            const auto length=[](const Vector3& v) { return std::hypot(v.x,v.z); };
            const bool record=count==0 || samples[count-1].segment!=segment ||
                clock-samples[count-1].time+1e-6>=settings.memoryRecordInterval ||
                length(physicalTarget-samples[count-1].position)>=settings.memoryRecordDistance;
            if (record) {
                if (count==kCapacity) {
                    for (size_t i=1;i<count;++i) samples[i-1]=samples[i];
                    --count;
                }
                samples[count++]={physicalTarget,clock,segment};
                size_t first=count-1;
                // Use only the most recent continuous sighting, never infer a
                // straight escape across a gap in visibility or a teleport.
                while (first>0 && samples[first-1].segment==segment && clock-samples[first-1].time<=1.5) {
                    --first;
                    // Prefer the final movement tangent over an old direction
                    // before a visible turn; average short steps to reject jitter.
                    if (length(physicalTarget-samples[first].position)>=.5f) break;
                }
                const auto delta=physicalTarget-samples[first].position;
                const float distance=length(delta);
                hasDirection=first<count-1 && distance>=.15f;
                escapeDirection=hasDirection ? Vector3{delta.x/distance,0,delta.z/distance} : Vector3{};
            }
        }
        previouslyVisible=visible;
        // On loss, the last observed direction stays fixed for this search even
        // as old coordinate samples expire; it never follows the hidden target.
    }
};
