#pragma once
#include "StageWorld.h"
#include <optional>

struct StageDoor {
    StageCollider closed;
    size_t colliderIndex = 0;
    float openAngle = 0;
    bool initiallyOpen = false;
    bool targetOpen = false;
    float angle = 0;

    StageCollider BoxAt(float swing) const {
        auto box = closed;
        box.rotation.y += swing;
        box.UpdateMatrices();
        return box;
    }
    static Vector3 Center(const StageCollider& box) {
        return StagePoint((box.local.min + box.local.max) * .5f, box.world);
    }
    static Vector3 HalfSize(const StageCollider& box) {
        const auto half = (box.local.max - box.local.min) * .5f;
        return {half.x * box.scale.x, half.y * box.scale.y, half.z * box.scale.z};
    }
    static bool Occupied(const StageCollider& box, const Vector3& feet, float radius, float height) {
        const auto low = box.position.y + box.local.min.y * box.scale.y;
        const auto high = box.position.y + box.local.max.y * box.scale.y;
        if (feet.y + height <= low || feet.y >= high) return false;
        StageHit hit;
        return StageWorld::CastBox(box, {feet.x, (low + high) * .5f, feet.z}, {1,0,0}, 0,
            {radius / box.scale.x, 0, radius / box.scale.z}, hit);
    }
};

class StageDoorSystem {
public:
    std::vector<StageDoor> doors;

    void Reset(StageWorld& world) {
        for (auto& door : doors) {
            door.targetOpen = door.initiallyOpen;
            door.angle = door.targetOpen ? door.openAngle : 0;
        }
        Sync(world);
    }
    void Sync(StageWorld& world) const {
        for (const auto& door : doors) world.colliders.at(door.colliderIndex) = door.BoxAt(door.angle);
    }
    std::optional<size_t> Nearest(const StageWorld& world, const Vector3& feet) const {
        const auto hand = feet + Vector3{0,1.25f,0};
        std::optional<size_t> nearest;
        float range = 1.55f;
        for (size_t i = 0; i < doors.size(); ++i) {
            const auto direction = StageDoor::Center(doors[i].closed) - hand;
            const float distance = StageLength(direction);
            if (distance > range) continue;
            const auto& blade = world.colliders[doors[i].colliderIndex];
            const float probe = std::max(0.0f, distance - .03f);
            StageHit hit, bladeHit;
            const bool throughBlade = distance > 0 && StageWorld::CastBox(blade, hand, direction * (1 / distance), probe, {}, bladeHit);
            if (world.Raycast(hand, direction, probe, hit) && (!throughBlade || hit.distance < bladeHit.distance - .002f)) continue;
            nearest = i;
            range = distance;
        }
        return nearest;
    }
    bool TryInteract(const StageWorld& world, const Vector3& feet) {
        const auto nearest = Nearest(world, feet);
        if (!nearest) return false;
        auto& door = doors[*nearest];
        door.targetOpen = !door.targetOpen;
        return true;
    }
    template<class IsOccupied>
    void Update(float dt, StageWorld& world, IsOccupied occupied) {
        if (!std::isfinite(dt) || dt <= 0) return;
        for (auto& door : doors) {
            const float target = door.targetOpen ? door.openAngle : 0;
            const float travel = std::clamp(target - door.angle, -dt * 4.0f, dt * 4.0f);
            if (travel == 0) continue;
            const int steps = static_cast<int>(std::ceil(std::abs(travel) / .025f));
            const float start = door.angle;
            for (int step = 1; step <= steps; ++step) {
                const float swing = step == steps && travel == target - start ? target : start + travel * step / steps;
                auto box = door.BoxAt(swing);
                if (occupied(box)) break;
                world.colliders.at(door.colliderIndex) = std::move(box);
                door.angle = swing;
            }
        }
    }
};
