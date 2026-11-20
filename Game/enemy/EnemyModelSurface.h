#pragma once
#include "EnemyParts.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <compare>
#include <string_view>

struct EnemyModelFaceId {
    uint32_t instance, mesh, material, triangle;
    auto operator<=>(const EnemyModelFaceId&) const = default;
};

struct EnemyModelSurface {
    struct Face {
        EnemyModelFaceId id;
        std::array<EnemyPartVertex,3> vertices;
    };
    struct Material {
        std::string texture;
        std::array<float,4> color;
    };
    std::vector<Face> faces;
    std::vector<Material> materials;
    std::map<std::string,std::vector<uint8_t>> embeddedTextures;
    AABB bounds{};
    std::string signature;

    size_t FindFace(EnemyModelFaceId id) const {
        const auto found=std::lower_bound(faces.begin(),faces.end(),id,
            [](const Face& face,const EnemyModelFaceId& key) { return face.id<key; });
        return found==faces.end() || found->id!=id ? kNoEnemyPart : static_cast<size_t>(found-faces.begin());
    }

    // Fit to the legacy unit scale and stand the model on the spawn floor.
    Vector3 Fit(const Vector3& position,float scale=1) const {
        const auto size=bounds.max-bounds.min;
        const Vector3 base{(bounds.min.x+bounds.max.x)*.5f,bounds.min.y,(bounds.min.z+bounds.max.z)*.5f};
        return (position-base)*(2.48f*scale/std::max({size.x,size.y,size.z}));
    }
};

inline std::string EnemyPathText(const std::filesystem::path& path) {
    const auto utf8=path.generic_u8string();
    return {utf8.begin(),utf8.end()};
}
inline std::filesystem::path EnemyUtf8Path(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(),text.end()));
}

inline void AppendEnemyTriangle(EnemyPartGeometry& geometry,AABB& bounds,
    const std::array<EnemyPartVertex,3>& triangle,uint32_t material=0) {
    std::array<Vector3,3> positions;
    if (geometry.faces.empty()) bounds={triangle[0].position,triangle[0].position};
    for (size_t i=0;i<positions.size();++i) {
        const auto p=positions[i]=triangle[i].position;
        bounds.min={std::min(bounds.min.x,p.x),std::min(bounds.min.y,p.y),std::min(bounds.min.z,p.z)};
        bounds.max={std::max(bounds.max.x,p.x),std::max(bounds.max.y,p.y),std::max(bounds.max.z,p.z)};
    }
    geometry.triangles.push_back(triangle);
    geometry.faces.push_back(positions);
    geometry.materials.push_back(material);
}
