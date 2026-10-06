#pragma once
#include "Vector3.h"
#include <vector>
#include <cstddef>

struct PhysicsParticle {
    Vector3 position{}, previous{}, target{}, frameTarget{};
    float inverseMass = 1.0f;
    float radius = 0.015f;
};

struct PhysicsConstraint {
    size_t a = 0, b = 0;
    float restLength = 0;
    float stiffness = 1;
};

struct PhysicsCollider {
    enum class Shape { Sphere, Capsule };
    Shape shape = Shape::Capsule;
    Vector3 a{}, b{}, previousA{}, previousB{};
    float radius = 0.1f;
    bool enabled = true;
    Vector3 ClosestPoint(const Vector3& point) const;
    bool Project(PhysicsParticle& particle) const;
};

// World-space Verlet/PBD, with bounded fixed steps. No renderer or rigid-body dependencies.
class ClothSolver {
public:
    struct Settings {
        bool enabled = true;
        Vector3 gravity{0,-9.8f,0};
        float damping = 0.12f; // Fraction of velocity lost per 1/60 second.
        float stiffness = 0.75f;
        int iterations = 10;
    } settings;
    std::vector<PhysicsParticle> particles;
    std::vector<PhysicsConstraint> constraints;
    size_t contacts = 0;
    void Reset();
    void Update(float dt, const std::vector<PhysicsCollider>& colliders);
private:
    float accumulator_ = 0;
    bool initialized_ = false, wasEnabled_ = false;
};
