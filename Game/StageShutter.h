#pragma once
#include "StageWorld.h"
#include <optional>

enum class ShutterState { Closed, Opening, Open };
enum class ShutterEventType { Started, Progress, Completed, EnemyWave };
struct ShutterProgressEvent {
    float progress=0;
    std::string event, spawnGroup;
    std::optional<size_t> openCollider;
};
struct ShutterDefinition {
    std::string id, model;
    Vector3 closedPosition{}, buttonPosition{}; // Mesh origin is the closed lower edge.
    StageCollider collider; // Position is relative to the lower-edge origin.
    size_t colliderIndex=0;
    double openDuration=30;
    std::string enemyWaveGroup;
    double enemyWaveInterval=10;
    float openDistance=3.2f, interactionDistance=2;
    bool defendBeforeOpening=false;
    std::string switchSound, startSound, motorSound, completeSound;
    std::vector<ShutterProgressEvent> progressEvents;
};
struct ShutterEvent {
    ShutterEventType type;
    std::string shutterId, event;
    float progress;
    std::string spawnGroup{};
    std::optional<size_t> openCollider{};
};
struct StageShutter {
    ShutterDefinition definition;
    ShutterState state=ShutterState::Closed;
    double elapsedTime=0;
    size_t nextProgressEvent=0;
    size_t nextEnemyWave=1;
    float Progress() const { return static_cast<float>(std::clamp(elapsedTime/definition.openDuration,0.0,1.0)); }
    Vector3 Position() const {
        // Defense shutters raise during the final tenth of the countdown.
        const float lift=definition.defendBeforeOpening ? std::clamp((Progress()-.9f)*10,0.f,1.f) : Progress();
        return definition.closedPosition+Vector3{0,definition.openDistance*lift,0};
    }
};

// No input, rendering, audio or enemy dependencies. Consumers drain one-shot events.
class StageShutterSystem {
public:
    std::vector<StageShutter> shutters;
    void Reset(StageWorld& world) {
        events_.clear();
        for (auto& shutter : shutters) {
            shutter.state=ShutterState::Closed; shutter.elapsedTime=0; shutter.nextProgressEvent=0; shutter.nextEnemyWave=1;
            for (const auto& marker : shutter.definition.progressEvents)
                if (marker.openCollider) world.colliders.at(*marker.openCollider).enabled=true;
        }
        Sync(world);
    }
    // A three-dimensional distance from the player's interaction point (chest).
    std::optional<size_t> Nearest(const Vector3& feet,bool closedOnly=true) const {
        std::optional<size_t> nearest;
        float distance=std::numeric_limits<float>::max();
        for (size_t i=0;i<shutters.size();++i) {
            const auto& shutter=shutters[i];
            if (closedOnly && shutter.state!=ShutterState::Closed) continue;
            const float candidate=StageLength(feet+Vector3{0,1.25f,0}-shutter.definition.buttonPosition);
            if (candidate<=shutter.definition.interactionDistance && candidate<distance) { nearest=i; distance=candidate; }
        }
        return nearest;
    }
    bool TryInteract(const Vector3& feet) {
        const auto nearest=Nearest(feet);
        if (!nearest) return false;
        auto& shutter=shutters[*nearest];
        shutter.state=ShutterState::Opening;
        events_.push_back({ShutterEventType::Started,shutter.definition.id,"openingStarted",0});
        return true;
    }
    void Update(float dt,StageWorld& world) {
        const double elapsed=std::isfinite(dt) ? std::max(0.0,static_cast<double>(dt)) : 0;
        for (auto& shutter : shutters) {
            if (shutter.state!=ShutterState::Opening) continue;
            shutter.elapsedTime=std::min(shutter.definition.openDuration,shutter.elapsedTime+elapsed);
            // Float dt at 144 Hz can sum a fraction of a microsecond short of 30 s.
            if (shutter.definition.openDuration-shutter.elapsedTime<1e-6) shutter.elapsedTime=shutter.definition.openDuration;
            while (shutter.nextProgressEvent<shutter.definition.progressEvents.size() &&
                shutter.Progress()>=shutter.definition.progressEvents[shutter.nextProgressEvent].progress) {
                const auto& marker=shutter.definition.progressEvents[shutter.nextProgressEvent++];
                if (marker.openCollider) world.colliders.at(*marker.openCollider).enabled=false;
                events_.push_back({ShutterEventType::Progress,shutter.definition.id,marker.event,marker.progress,
                    marker.spawnGroup,marker.openCollider});
            }
            // Catch up every due wave before completion, including the wave at full open.
            while (!shutter.definition.enemyWaveGroup.empty() &&
                shutter.nextEnemyWave*shutter.definition.enemyWaveInterval<=shutter.elapsedTime+1e-7) {
                events_.push_back({ShutterEventType::EnemyWave,shutter.definition.id,shutter.definition.enemyWaveGroup,
                    static_cast<float>(shutter.nextEnemyWave*shutter.definition.enemyWaveInterval/shutter.definition.openDuration)});
                ++shutter.nextEnemyWave;
            }
            if (shutter.elapsedTime>=shutter.definition.openDuration) {
                shutter.state=ShutterState::Open;
                events_.push_back({ShutterEventType::Completed,shutter.definition.id,"fullyOpened",1});
            }
            auto& collider=world.colliders.at(shutter.definition.colliderIndex);
            const auto position=ColliderPosition(shutter);
            if (position.x!=collider.position.x || position.y!=collider.position.y || position.z!=collider.position.z) {
                collider.position=position;
                collider.UpdateMatrices();
            }
        }
    }
    void Sync(StageWorld& world) const {
        for (const auto& shutter : shutters) {
            auto collider=shutter.definition.collider;
            collider.position=ColliderPosition(shutter);
            collider.UpdateMatrices();
            world.colliders.at(shutter.definition.colliderIndex)=std::move(collider);
        }
    }
    std::vector<ShutterEvent> TakeEvents() { auto events=std::move(events_); events_.clear(); return events; }
private:
    static Vector3 ColliderPosition(const StageShutter& shutter) {
        const auto& def=shutter.definition;
        const auto origin=def.defendBeforeOpening && shutter.state!=ShutterState::Open ? def.closedPosition : shutter.Position();
        return def.collider.position+origin;
    }
    std::vector<ShutterEvent> events_;
};
