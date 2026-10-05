#pragma once
#include "EnemyParts.h"

enum class EnemyLocomotionPose { Normal, MissingLeftLeg, MissingRightLeg, Crawl };

// Rendering only. Distances are fractions of the bind-pose model height;
// angles are radians. Boss coordinates: up +Y, left +Z, forward -X.
struct EnemyProceduralAnimationSettings {
    bool enabled = true;
    float walkHz = 1.6f, crawlHz = 1.1f;
    float walkBob = .008f, armSwing = .10f, legSwing = .065f;
    float armLowerAngle = 1.25f; // About 72 degrees down from the horizontal T pose.
    float missingLegLean = .075f, missingLegSway = .025f;
    float remainingLegSwing = .09f;
    float crawlGroundClearance = .015f, crawlForwardLean = 1.48f; // Prone, about 85 degrees.
    float crawlBob = .006f, crawlSway = .025f;
    float crawlArmReach = 1.10f, crawlArmStroke = .30f, crawlArmPlant = .22f;
};

class EnemyProceduralAnimation {
public:
    EnemyProceduralAnimationSettings settings{};
    struct RenderTransform { Vector3 rotate{}, translate{}; };
    struct Pose {
        Matrix4x4 root = Matrix4x4::MakeIdentity4x4(); // After model scale, before world placement.
        float swing = 0;
        bool enabled = false;
        EnemyLocomotionPose mode = EnemyLocomotionPose::Normal;
    };

    void Reset() { phase_ = 0; moving_ = false; }
    static EnemyPartType AnimationPart(const EnemyPart& part) {
        // Generic Arm/Leg roles can share legacy type tags. Only canonical names
        // identify a left/right limb; arbitrary face parts retain the root motion.
        for (auto type : {EnemyPartType::LeftArm, EnemyPartType::RightArm,
                          EnemyPartType::LeftLeg, EnemyPartType::RightLeg})
            if (part.name == EnemyPartName(type)) return type;
        return EnemyPartType::None;
    }
    static EnemyLocomotionPose Classify(const EnemyParts& parts) {
        bool hasLegs = false, left = false, right = false;
        for (const auto& part : parts) {
            const auto type = AnimationPart(part);
            if (type == EnemyPartType::LeftLeg) { hasLegs = true; left |= !part.Destroyed(); }
            if (type == EnemyPartType::RightLeg) { hasLegs = true; right |= !part.Destroyed(); }
        }
        if (!hasLegs || (left && right)) return EnemyLocomotionPose::Normal;
        if (!left && !right) return EnemyLocomotionPose::Crawl;
        return left ? EnemyLocomotionPose::MissingRightLeg : EnemyLocomotionPose::MissingLeftLeg;
    }
    void Advance(float dt, bool moving, const EnemyParts& parts) {
        if (!std::isfinite(dt) || dt <= 0) return; // Zero-time refresh never advances or clears a pose.
        moving_ = moving;
        if (!moving || !settings.enabled) return;
        const float hz = Classify(parts) == EnemyLocomotionPose::Crawl ? settings.crawlHz : settings.walkHz;
        constexpr double tau = 6.283185307179586;
        phase_ = std::fmod(phase_ + static_cast<double>(dt) * std::max(0.0f, hz) * tau, tau);
    }
    Pose Sample(const EnemyParts& parts, const Vector3& scale, bool alive) const {
        Pose pose;
        if (!settings.enabled || !alive || parts.empty()) return pose;
        pose.enabled = true;
        pose.mode = Classify(parts);
        AABB bounds = parts.front().bounds;
        const EnemyPart* body = nullptr;
        for (const auto& part : parts) {
            bounds.min.y = std::min(bounds.min.y, part.bounds.min.y);
            bounds.max.y = std::max(bounds.max.y, part.bounds.max.y);
            if (!body && part.role == EnemyPartRole::Body) body = &part;
        }
        const float height = std::max(0.0f, bounds.max.y - bounds.min.y) * std::abs(scale.y);
        const Vector3 pivot = body ? Vector3{(body->bounds.min.x + body->bounds.max.x) * .5f,
            body->bounds.min.y, (body->bounds.min.z + body->bounds.max.z) * .5f} : Vector3{};
        const float wave = moving_ ? static_cast<float>(std::sin(phase_)) : 0;
        const float bob = moving_ ? static_cast<float>(std::sin(phase_ * 2)) : 0;
        Vector3 tilt{}, offset{0, height * settings.walkBob * bob, 0};
        pose.swing = wave;
        if (pose.mode == EnemyLocomotionPose::Crawl) {
            tilt.z = settings.crawlForwardLean;
            tilt.y = settings.crawlSway * wave;
        } else if (pose.mode != EnemyLocomotionPose::Normal) {
            const float side = pose.mode == EnemyLocomotionPose::MissingLeftLeg ? 1.0f : -1.0f;
            tilt.x = side * settings.missingLegLean + settings.missingLegSway * wave;
        }
        pose.root = AroundPivot(Scaled(pivot, scale), tilt);
        if (pose.mode == EnemyLocomotionPose::Crawl) {
            // Fit the visual pose above the enemy's base plane. This is a bind
            // bounds estimate, with no physics queries or IK. Include surviving
            // arms so their strokes don't drive the hands beneath the floor.
            float lowest = std::numeric_limits<float>::max();
            for (const auto& part : parts) {
                if (part.Destroyed()) continue;
                const auto matrix = Matrix4x4::Multiply(LocalFor(pose, part, scale), pose.root);
                for (int corner = 0; corner < 8; ++corner) {
                    const Vector3 point{(corner & 1) ? part.bounds.max.x : part.bounds.min.x,
                        (corner & 2) ? part.bounds.max.y : part.bounds.min.y,
                        (corner & 4) ? part.bounds.max.z : part.bounds.min.z};
                    lowest = std::min(lowest, EnemyPartTransformPoint(Scaled(point, scale), matrix).y);
                }
            }
            if (lowest != std::numeric_limits<float>::max())
                pose.root.m[3][1] += height * (settings.crawlGroundClearance + settings.crawlBob * (bob + 1) * .5f) - lowest;
        } else pose.root.m[3][1] += offset.y;
        return pose;
    }
    RenderTransform TransformFor(const Pose& pose, const EnemyPart* part, const Vector3& scale,
                                 const Vector3& rotation, const Vector3& position) const {
        const auto local = part ? LocalFor(pose, *part, scale) : Matrix4x4::MakeIdentity4x4();
        // Rotate scaled vertices about their shoulder/hip, then the body pivot.
        // Keeping scale outside the rigid correction also supports nonuniform scale.
        const auto world = Matrix4x4::Multiply(Matrix4x4::Multiply(local, pose.root),
            Matrix4x4::MakeAffineMatrix({1,1,1}, rotation, position));
        const float y = std::asin(std::clamp(-world.m[0][2], -1.0f, 1.0f));
        Vector3 angles{0, y, 0};
        if (std::abs(std::cos(y)) > 1e-5f) {
            angles.x = std::atan2(world.m[1][2], world.m[2][2]);
            angles.z = std::atan2(world.m[0][1], world.m[0][0]);
        } else angles.z = std::atan2(-world.m[1][0], world.m[1][1]);
        return {angles, {world.m[3][0], world.m[3][1], world.m[3][2]}};
    }
private:
    Matrix4x4 LocalFor(const Pose& pose, const EnemyPart& part, const Vector3& scale) const {
        auto local = Matrix4x4::MakeIdentity4x4();
        if (pose.enabled && !part.Destroyed()) {
            const auto type = AnimationPart(part);
            auto pivot = (part.bounds.min + part.bounds.max) * .5f;
            Vector3 swing{};
            if ((type == EnemyPartType::LeftLeg || type == EnemyPartType::RightLeg) && pose.mode != EnemyLocomotionPose::Crawl) {
                pivot.y = part.bounds.max.y;
                const float amount = pose.mode == EnemyLocomotionPose::Normal ? settings.legSwing : settings.remainingLegSwing;
                swing.z = pose.swing * amount * (type == EnemyPartType::LeftLeg ? 1.0f : -1.0f);
                local = AroundPivot(Scaled(pivot, scale), swing);
            } else if (type == EnemyPartType::LeftArm || type == EnemyPartType::RightArm) {
                pivot.z = type == EnemyPartType::LeftArm ? part.bounds.min.z : part.bounds.max.z;
                const float side = type == EnemyPartType::LeftArm ? 1.0f : -1.0f;
                const auto shoulder = Scaled(pivot, scale);
                if (pose.mode == EnemyLocomotionPose::Crawl) {
                    // In a prone body, bind +Y points ahead and bind -X points
                    // down. Reach towards the head, then alternate each arm's
                    // reach/pull and support/lift while keeping its shoulder fixed.
                    const float stroke = side * pose.swing;
                    local = AroundPivot(shoulder, {-side * (settings.crawlArmReach + settings.crawlArmStroke * stroke),
                        -side * (settings.crawlArmPlant - settings.crawlArmStroke * .25f * stroke), 0});
                } else {
                    local = AroundPivot(shoulder, {side * settings.armLowerAngle, 0, 0});
                    local = Matrix4x4::Multiply(local,
                        AroundPivot(shoulder, {0, 0, -side * pose.swing * settings.armSwing}));
                }
            }
        }
        return local;
    }
    static Vector3 Scaled(const Vector3& p, const Vector3& scale) {
        return {p.x * scale.x, p.y * scale.y, p.z * scale.z};
    }
    static Matrix4x4 AroundPivot(const Vector3& pivot, const Vector3& angles) {
        return Matrix4x4::Multiply(Matrix4x4::Multiply(Matrix4x4::Translation(pivot * -1),
            Matrix4x4::RotateXYZ(angles.x, angles.y, angles.z)), Matrix4x4::Translation(pivot));
    }
    double phase_ = 0;
    bool moving_ = false;
};
