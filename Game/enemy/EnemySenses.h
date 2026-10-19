#pragma once
// Optional Definition fields. Angles are full horizontal FOV in degrees.
struct EnemySenses {
    float viewDistance=20, fieldOfView=110, detectionTime=.35f, loseSightTime=2.0f;
    float arrivalDistance=.6f, wanderRadius=6, wanderSpeedMultiplier=.45f, wanderWaitTime=1.5f;
    float investigateSpeedMultiplier=1.0f, searchDuration=4.0f, searchRadius=5.0f, searchSpeedMultiplier=.85f;
    float investigateUnreachableTime=6.0f; // Continuous failed routing before a reachable local search.
    float memoryRetentionTime=6.0f, memoryRecordInterval=.25f, memoryRecordDistance=.5f;
    float searchDirectionPriority=.85f; // 0: legacy local search, 1: strongest observed-direction preference.
};
