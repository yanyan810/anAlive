#include "EnemyAIDiagnosticsAbi.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

uint32_t __cdecl EnemyAIDiagnosticsGetAbi(EnemyAIDiagnosticsAbi* abi, uint32_t size) {
    if (!abi || size != sizeof(*abi)) return 0;
    *abi = {ENEMY_AI_DIAGNOSTICS_ABI_VERSION, sizeof(EnemyAIDiagnosticsInput), sizeof(EnemyAIDiagnosticsOutput)};
    return 1;
}

uint32_t __cdecl EnemyAIDiagnosticsEvaluate(const EnemyAIDiagnosticsInput* input, EnemyAIDiagnosticsOutput* output) {
    if (!input || !output || input->size != sizeof(*input) || output->size != sizeof(*output) ||
        !std::memchr(input->stateLabel, '\0', sizeof(input->stateLabel)) ||
        !std::isfinite(input->distance) || !std::isfinite(input->sightRange)) return 0;

    *output = {sizeof(*output)};
    const int summaryLength = std::snprintf(output->summary, sizeof(output->summary),
        "現在: %s / 距離: %.2f", input->stateLabel, input->distance);
    if (summaryLength <= 0 || summaryLength >= static_cast<int>(sizeof(output->summary))) return 0;

#ifdef ENEMY_AI_DIAGNOSTICS_VARIANT_B
    if (!input->dead && (input->visible || input->pursuing)) {
        const float proximity = 1.0f - std::clamp(input->distance / std::max(input->sightRange, 1.0f), 0.0f, 1.0f);
        output->threat = .65f * proximity + (input->visible ? .35f : .20f);
    }
    const int detailLength = std::snprintf(output->detail, sizeof(output->detail),
        "脅威度: %.0f%%", output->threat * 100.0f);
    if (detailLength <= 0 || detailLength >= static_cast<int>(sizeof(output->detail))) return 0;
#endif
#if defined(ENEMY_AI_DIAGNOSTICS_VARIANT_C) || defined(ENEMY_AI_DIAGNOSTICS_VARIANT_D)
#if defined(ENEMY_AI_DIAGNOSTICS_VARIANT_C)
    constexpr const char* variant = "C";
#else
    constexpr const char* variant = "D";
#endif
    if (std::snprintf(output->detail, sizeof(output->detail), "Variant %s / 視認距離: %.1f",
        variant, input->sightRange) <= 0) return 0;
#endif

    return 1;
}
