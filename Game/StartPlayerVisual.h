#pragma once
#include "Object3d.h"
#include "ModelManager.h"
#include "EnemyDefinition.h"
#include "PlayerVisualSettings.h"

// Presentation only. Replace this binding with the final player asset later.
class StartPlayerVisual {
public:
    void Initialize(Object3dCommon* common,DirectXCommon* dx,Camera* camera,const EnemyDefinition* normal,float eyeHeight) {
        object_.Initialize(common,dx); object_.SetCamera(camera);
        Model* model=nullptr;
        if (normal && normal->partAsset) {
            const auto& asset=*normal->partAsset;
            const auto key="StartPlayerVisual/"+asset.path;
            model=ModelManager::GetInstance()->FindModel(key);
            if (!model) {
                Model::ModelData data; data.materials.push_back({asset.texture}); Model::MeshData mesh;
                for (const auto& part:asset.defaults) if (part.geometry)
                    for (const auto& triangle:part.geometry->triangles) for (const auto& v:triangle) {
                        mesh.vertices.push_back({{v.position.x,v.position.y,v.position.z,1},v.uv,v.normal});
                        data.indices.push_back(static_cast<uint32_t>(data.indices.size()));
                    }
                mesh.indexCount=static_cast<uint32_t>(data.indices.size());
                data.meshes.push_back(std::move(mesh)); data.rootNode.meshIndices.push_back(0);
                model=ModelManager::GetInstance()->CreatePrimitiveModel(key,data);
            }
        }
        if (model) object_.SetModel(model); else object_.SetModel("enemy/boss/boss.gltf");
        AABB bounds{};
        if (object_.GetModel()->GetLocalAABB(bounds)) {
            scale_=(eyeHeight+.18f)/std::max(.01f,bounds.max.y-bounds.min.y);
            offset_=-bounds.min.y*scale_;
        }
        object_.SetScale({scale_,scale_,scale_}); object_.StopAnimation();
        object_.SetEnableLighting(2); object_.SetDirection({.3f,-1,.5f}); object_.SetIntensity(1);
        object_.SetPointLightIntensity(0); object_.SetSpotLightIntensity(0);
    }
    // Native forward belongs to the model binding. When the final asset replaces
    // Normal, change its binding/native axis here; editor and game use this API.
    static Vector3 LocalForward() { return {-1,0,0}; }
    static Vector3 Forward(float playerYaw,const PlayerVisualSettings& settings) {
        const auto r=Matrix4x4::RotateXYZ(0,settings.WorldYaw(playerYaw),0);
        const auto f=LocalForward();
        return {f.x*r.m[0][0]+f.z*r.m[2][0],0,f.x*r.m[0][2]+f.z*r.m[2][2]};
    }
    void Update(const Transform& player,const PlayerVisualSettings& settings={}) {
        object_.SetTranslate(player.translate+Vector3{0,offset_,0});
        object_.SetRotate({0,settings.WorldYaw(player.rotate.y),0}); object_.Update(0);
    }
    const Vector3& Rotation() const { return object_.GetRotate(); }
    void Draw() { object_.Draw(); }
private:
    Object3d object_;
    float scale_=1,offset_=0;
};
