#pragma once
#include "ClothSolver.h"
#include "Model.h"
#include <string>

// One component per character instance. Profiles use exact bone names; the Model is read-only.
class ClothComponent {
public:
    struct Chain { std::vector<int> joints; std::vector<size_t> particles; Vector3 tipLocal{}; };
    struct Group { std::string name; ClothSolver solver; std::vector<Chain> chains; };
    struct ColliderBinding {
        std::string name;
        int start=-1,end=-1;
        Vector3 startOffset{},endOffset{};
        float modelRadius=1;
        PhysicsCollider collider;
    };
    bool Load(const std::string& profile,const Model::Skeleton& bindSkeleton);
    void Update(Model::Skeleton& animatedPose,const Matrix4x4& world,float dt);
    void Reset();
    void DrawImGui(const Matrix4x4& viewProjection,const Vector2& lo,const Vector2& hi);
    bool enabled=true, showColliders=true, showParticles=false, showConstraints=false;
    std::vector<Group> groups;
    std::vector<ColliderBinding> colliders;
    std::string error;
private:
    bool resetPending_=true;
};
