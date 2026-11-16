#include "../Game/LiveDevelopment/EnemyAIDiagnosticsAbi.h"
#include <limits>

uint32_t __cdecl EnemyAIDiagnosticsGetAbi(EnemyAIDiagnosticsAbi* abi, uint32_t size) {
    if (!abi || size != sizeof(*abi)) return 0;
    *abi = {ENEMY_AI_DIAGNOSTICS_ABI_VERSION, sizeof(EnemyAIDiagnosticsInput), sizeof(EnemyAIDiagnosticsOutput)};
#if defined(ENEMY_AI_DIAGNOSTICS_VARIANT_TEST_ABI)
    ++abi->version;
#endif
    return 1;
}

uint32_t __cdecl EnemyAIDiagnosticsEvaluate(const EnemyAIDiagnosticsInput*, EnemyAIDiagnosticsOutput* output) {
    *output = {sizeof(*output), std::numeric_limits<float>::quiet_NaN(), "invalid output", ""};
    return 1;
}
