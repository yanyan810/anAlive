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
    inline constexpr size_t kMaxOccluders = 32;
    inline constexpr size_t kMaxBounceLights = 4;

    enum class Mode : uint32_t { None, Point, Spot, Area, Forward };

    struct Occluder {
        // Bounds are relative to the light. Only rotated boxes transform the ray to local space.
        Vector4 inverseX{}, inverseY{}, inverseZ{};
        Vector4 minimum{}, maximum{};
    };

    struct Visibility {
        uint32_t occluderCount = 0;
        uint32_t axisAlignedOccluderCount = 0;
        float nearestOmittedRange = 0.0f;
        uint32_t padding = 0;
        // Conservative range limits per world-space octant when the box budget is exhausted.
        std::array<float, 8> omittedRanges{};
        std::array<Occluder, kMaxOccluders> occluders{};
    };

    struct BounceLight {
        Vector3 position{};
        float range = 0.0f;
        Vector3 normal{};
        float surfaceOffset = 0.0f;
        Vector3 radiance{};
        float decay = 0.0f;
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
        uint32_t bounceCount = 0;
        float forwardBias = 0.0f;
        std::array<BounceLight, kMaxBounceLights> bounces{};
        // Slot zero is shared by direct and indirect light. Each bounce has its own origin.
        std::array<Visibility, 1 + kMaxBounceLights> visibility{};
    };
    static_assert(sizeof(Occluder) == 80);
    static_assert(offsetof(Visibility, omittedRanges) == 16);
    static_assert(offsetof(Visibility, occluders) == 48);
    static_assert(sizeof(Visibility) == 48 + 80 * kMaxOccluders);
    static_assert(sizeof(BounceLight) == 48);
    static_assert(offsetof(Parameters, bounces) == 96);
    static_assert(offsetof(Parameters, visibility) == 96 + 48 * kMaxBounceLights);
    static_assert(sizeof(Parameters) == 96 + 48 * kMaxBounceLights + sizeof(Visibility) * (1 + kMaxBounceLights));

    static_assert(sizeof(Parameters) <= 64 * 1024);

    class Buffer {
    public:
        void Initialize(DirectXCommon& dx) {
            if (resource_) return;
            resource_ = dx.CreateMappedBuffer(mapped_);
            resource_->SetName(L"Local light constant buffer");
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
