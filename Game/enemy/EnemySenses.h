#pragma once
#include <cmath>
#include <initializer_list>
// Optional Definition fields. Angles are full horizontal FOV in degrees.
struct EnemySenses {
    float viewDistance=20, fieldOfView=110, detectionTime=.35f, loseSightTime=2.0f;
    float arrivalDistance=.6f, wanderRadius=6, wanderSpeedMultiplier=.45f, wanderWaitTime=1.5f;
    float investigateSpeedMultiplier=1.0f, searchDuration=4.0f, searchRadius=5.0f, searchSpeedMultiplier=.85f;
    float investigateUnreachableTime=6.0f; // Continuous failed routing before a reachable local search.
    float memoryRetentionTime=6.0f, memoryRecordInterval=.25f, memoryRecordDistance=.5f;
    float searchDirectionPriority=.85f; // 0: legacy local search, 1: strongest observed-direction preference.
    bool IsValid() const {
        for (float value : {viewDistance, fieldOfView, detectionTime, loseSightTime,
            arrivalDistance, wanderRadius, wanderSpeedMultiplier, wanderWaitTime,
            investigateSpeedMultiplier, searchDuration, searchRadius, searchSpeedMultiplier,
            investigateUnreachableTime, memoryRetentionTime, memoryRecordInterval,
            memoryRecordDistance, searchDirectionPriority})
            if (!std::isfinite(value) || value < 0) return false;
        return fieldOfView <= 360 && searchDirectionPriority <= 1;
    }
};
