#pragma once

#include <stdint.h>

#define ENEMY_AI_DIAGNOSTICS_ABI_VERSION 1u

// Caller-owned, fixed-layout data. Strings are null-terminated UTF-8.
typedef struct EnemyAIDiagnosticsAbi {
    uint32_t version;
    uint32_t inputSize;
    uint32_t outputSize;
} EnemyAIDiagnosticsAbi;

typedef struct EnemyAIDiagnosticsInput {
    uint32_t size;
    uint32_t visible;
    uint32_t pursuing;
    uint32_t dead;
    float distance;
    float sightRange;
    char stateLabel[64];
} EnemyAIDiagnosticsInput;

typedef struct EnemyAIDiagnosticsOutput {
    uint32_t size;
    float threat;
    char summary[160];
    char detail[96];
} EnemyAIDiagnosticsOutput;

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ENEMY_AI_DIAGNOSTICS_EXPORTS
#define ENEMY_AI_DIAGNOSTICS_EXPORT __declspec(dllexport)
#else
#define ENEMY_AI_DIAGNOSTICS_EXPORT
#endif

// Both functions return 1 on success and 0 on failure.
ENEMY_AI_DIAGNOSTICS_EXPORT uint32_t __cdecl EnemyAIDiagnosticsGetAbi(EnemyAIDiagnosticsAbi* abi, uint32_t size);
ENEMY_AI_DIAGNOSTICS_EXPORT uint32_t __cdecl EnemyAIDiagnosticsEvaluate(
    const EnemyAIDiagnosticsInput* input, EnemyAIDiagnosticsOutput* output);

#undef ENEMY_AI_DIAGNOSTICS_EXPORT

#ifdef __cplusplus
}
#endif