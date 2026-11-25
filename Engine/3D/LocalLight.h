#pragma once
#include "DirectXCommon.h"
#include "MathStruct.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

// Pixel shader b6. Static and compute-skinned Object3d use the same layout.
namespace LocalLighting {
    inline constexpr UINT kObjectRootIndex = 11;
    inline constexpr UINT kSkinnedRootIndex = 12;
    inline constexpr UINT kShaderRegister = 6;
    inline constexpr size_t kMaxOccluders = 24;

    enum class Mode : uint32_t { None, Point, Spot, Area };

    struct Occluder {
        // Bounds are relative to the light. Only rotated boxes transform the ray to local space.
        Vector4 inverseX{}, inverseY{}, inverseZ{};
        Vector4 minimum{}, maximum{};
    };

    struct Parameters {
        Vector3 position{};
        Mode mode = Mode::None;
        Vector3 color{};
        float intensity = 0.0f;
        Vector3 direction{};
        float range = 0.0f;
        Vector3 areaRight{}; // Full width on this axis.
        float decay = 0.0f;
        Vector3 areaUp{}; // Full height on this axis.
        float specularStrength = 0.0f;
        float indirectIntensity = 0.0f;
        float indirectRange = 0.0f;
        uint32_t occluderCount = 0;
        float visibilityRange = 0.0f;
        uint32_t axisAlignedOccluderCount = 0;
        uint32_t padding[3]{};
        std::array<Occluder, kMaxOccluders> occluders{};
    };
    static_assert(sizeof(Occluder) == 80);
    static_assert(offsetof(Parameters, occluders) == 112);
    static_assert(sizeof(Parameters) == 112 + 80 * kMaxOccluders);

    class Buffer {
    public:
        void Initialize(DirectXCommon& dx) {
            if (resource_) return;
            resource_ = dx.CreateBufferResource((sizeof(Parameters) + 255) & ~size_t{255});
            if (!resource_ || FAILED(resource_->Map(0, nullptr, reinterpret_cast<void**>(&mapped_)))) {
                throw std::runtime_error("Failed to create the local light constant buffer");
            }
            *mapped_ = {};
        }

        // DirectXCommon waits for the previous frame fence before reusing mapped data.
        void Update(const Parameters& parameters) { *mapped_ = parameters; }
        D3D12_GPU_VIRTUAL_ADDRESS Address() const { return resource_->GetGPUVirtualAddress(); }

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
        Parameters* mapped_ = nullptr;
    };
}
