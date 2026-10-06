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

struct PhysicsCollisionSegment {
    size_t a = 0, b = 0;
    bool horizontal = false;
};

// A barycentric contact on an existing segment; no mass, bone or skinning influence.
struct PhysicsCollisionSample {
    size_t a = 0, b = 0;
    float t = 0.5f;
    Vector3 position{};
    bool horizontal = false, contacted = false;
    bool Project(const PhysicsCollider& collider, std::vector<PhysicsParticle>& particles, float radius);
};

// World-space Verlet/PBD, with bounded fixed steps. No renderer or rigid-body dependencies.
class ClothSolver {
public:
    struct Settings {
        bool enabled = true;
        Vector3 gravity{0,-9.8f,0};
        float damping = 0.12f; // Fraction of velocity lost per 1/60 second.
        bool dampingRelativeToAnimation = false; // Preserve uniform motion of the animated reference frame.
        float maxSwingAngleDegrees = 180; // Vertical segments may swing this far from their animated direction.
        float stiffness = 0.75f;
        int iterations = 10;
        int collisionSamplesPerSegment = 0; // Interior samples. Zero preserves legacy behavior.
        float collisionSampleRadius = 0.012f; // World units, like PhysicsParticle::radius.
        bool enableHorizontalCollisionSamples = false;
    } settings;
    std::vector<PhysicsParticle> particles;
    std::vector<PhysicsConstraint> constraints;
    std::vector<PhysicsCollisionSegment> collisionSegments;
    std::vector<PhysicsCollisionSample> collisionSamples;
    size_t contacts = 0;
    size_t sampleContacts = 0;
    void RebuildCollisionSamples();
    void Reset();
    void Update(float dt, const std::vector<PhysicsCollider>& colliders);
private:
    std::vector<float> constraintFactors_;
    std::vector<PhysicsCollider> stepColliders_;
    std::vector<Vector3> stepSwingAxes_;
    float accumulator_ = 0;
    bool initialized_ = false, wasEnabled_ = false;
    int sampleCount_ = -1;
    bool horizontalSamples_ = false;
    size_t segmentCount_ = 0;
    void RefreshCollisionSamples();
};
