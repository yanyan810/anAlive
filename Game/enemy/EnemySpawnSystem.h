#pragma once
#include "Vector3.h"
#include "EnemyDefinition.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <vector>

enum class SpawnMode { Sequential, Simultaneous };
enum class SpawnActivation { Legacy, StageStart, OnTrigger, Manual };
struct EnemyPoolEntry { std::string id; double weight = 1; };
struct EnemySpawnCount { std::string enemyId; int count = 1; int endCount = 1; };
struct EnemySpawnPoint {
    std::string id;
    Vector3 position{};
    std::vector<EnemyPoolEntry> enemyPool;
    Vector3 rotation{}; // radians, same convention as Enemy / Object3d
    // Missing activation in old maps means location/lottery only; never auto-spawn.
    SpawnActivation activation = SpawnActivation::Legacy;
    std::string enemyId, groupId;
    std::vector<EnemySpawnCount> enemyComposition; // ordered, exact counts (not weights)
    int spawnCount = 1, maxAlive = 1;
    SpawnMode mode = SpawnMode::Sequential;
    double spawnInterval = .5, initialDelay = 0;
    bool activated = false, active = false;
    int spawned = 0;
    double nextSpawn = 0; // absolute gameplay time
    std::vector<uint64_t> livingEnemies;
    const std::string& NextEnemyId() const {
        const auto found = std::upper_bound(enemyComposition.begin(), enemyComposition.end(), spawned,
            [](int index, const EnemySpawnCount& entry) { return index < entry.endCount; });
        return found == enemyComposition.end() ? enemyId : found->enemyId;
    }
};
enum class SpawnPointSelection { Random, RoundRobin };
struct SpawnGroupEnemy {
    std::string enemyId;
    std::string spawnPointId;
};
// Stage-time schedule, independent of the legacy player-entry triggers.
struct SpawnGroup {
    std::string id;
    SpawnMode mode = SpawnMode::Sequential;
    std::vector<SpawnGroupEnemy> enemies;
    double startTime = 0;
    double interval = .5;
    size_t spawned = 0;
};
struct EnemySpawnTrigger {
    std::string id;
    Vector3 position{}; // AABB center
    Vector3 size{};     // full extents
    std::vector<std::string> spawnPointIds;
    int spawnCount = 1;
    double spawnInterval = 0;
    double initialDelay = 0;
    int maxAlive = 1;
    SpawnPointSelection selection = SpawnPointSelection::Random;
    bool oneShot = true;
    // Empty for legacy shared waves. New triggers only activate matching points.
    std::string groupId;

    bool activated = false;
    bool active = false;
    bool wasInside = false;
    int spawned = 0;
    size_t nextPoint = 0;
    double nextSpawn = 0;
    std::vector<uint64_t> livingEnemies;
};

class EnemySpawnSystem {
public:
    using Spawn = std::function<uint64_t(const EnemySpawnPoint&, const std::string&)>;
    // Empty enemyId means the legacy point's weighted Enemy Pool is used.
    using SpawnEnemy = std::function<uint64_t(const EnemySpawnPoint&, const std::string&, const std::string&)>;
    using IsAlive = std::function<bool(uint64_t)>;
    // Transactional validation: bad data never leaves a partially configured map.
    bool Load(const std::string& path, const EnemyDefinitions& definitions = EnemyDefinitions{});
    void SetSeed(uint32_t seed) { random_.seed(seed); initialRandom_ = random_; }
    void Reset() {
        elapsedTime_ = 0;
        random_ = initialRandom_;
        for (auto& group : groups_) group.spawned = 0;
        for (auto& point : points_) {
            point.activated = point.active = false;
            point.spawned = 0; point.nextSpawn = 0; point.livingEnemies.clear();
            if (point.activation == SpawnActivation::StageStart) Activate(point, 0);
        }
        for (auto& trigger : triggers_) {
            trigger.activated = trigger.active = trigger.wasInside = false;
            trigger.spawned = 0; trigger.nextPoint = 0; trigger.nextSpawn = 0;
            trigger.livingEnemies.clear();
        }
    }
    std::string SelectEnemyId(const EnemySpawnPoint& point) {
        if (point.enemyPool.empty()) return "normal";
        std::vector<double> weights;
        for (const auto& entry : point.enemyPool) weights.push_back(entry.weight);
        return point.enemyPool[std::discrete_distribution<size_t>(weights.begin(),weights.end())(random_)].id;
    }
    const std::string& Error() const { return error_; }
    const std::vector<EnemySpawnPoint>& Points() const { return points_; }
    const std::vector<EnemySpawnTrigger>& Triggers() const { return triggers_; }
    const std::vector<SpawnGroup>& Groups() const { return groups_; }
    double ElapsedTime() const { return elapsedTime_; }
    // Starts one wave; active waves are never restarted or silently queued.
    // Completed waves can be activated again, retaining their living-enemy limit.
    bool ActivateSpawn(const std::string& id) {
        for (auto& point : points_) if (point.id == id) return Activate(point, elapsedTime_);
        return false;
    }
    size_t ActivateGroup(const std::string& id) {
        return ActivateGroupAt(id, elapsedTime_);
    }
    const EnemySpawnPoint* FindPoint(const std::string& id) const {
        for (const auto& point : points_) if (point.id == id) return &point;
        return nullptr;
    }
    void Update(float dt, const Vector3& player, const Spawn& spawn, const IsAlive& isAlive) {
        // Preserve callers that select definitions from the point's Enemy Pool.
        Update(dt, player, SpawnEnemy{[&](const EnemySpawnPoint& point, const std::string& source, const std::string& enemyId) {
            if (enemyId.empty()) return spawn(point, source);
            auto explicitPoint = point;
            explicitPoint.enemyPool = {{enemyId, 1}};
            return spawn(explicitPoint, source);
        }}, isAlive);
    }
    // processTriggers=false prepares Stage Start enemies during scene initialization,
    // including FadeIn, without detecting player entry or advancing gameplay time.
    void Update(float dt, const Vector3& player, const SpawnEnemy& spawn, const IsAlive& isAlive, bool processTriggers = true) {
        const double elapsed = std::isfinite(dt) ? std::max(0.0, static_cast<double>(dt)) : 0.0;
        elapsedTime_ += elapsed;
        for (auto& group : groups_) {
            if (!processTriggers) continue;
            // Every simultaneous member is acquired/activated in this Update call,
            // before GameScene advances any AI, collisions, bullets or rendering.
            while (group.spawned < group.enemies.size()) {
                const double due = group.startTime + (group.mode == SpawnMode::Sequential ? group.interval * group.spawned : 0);
                if (elapsedTime_ + 1e-7 < due) break;
                const auto& entry = group.enemies[group.spawned];
                spawn(*FindPoint(entry.spawnPointId), group.id, entry.enemyId);
                ++group.spawned;
            }
        }
        for (auto& trigger : triggers_) {
            if (!processTriggers) continue;
            auto& living = trigger.livingEnemies;
            living.erase(std::remove_if(living.begin(), living.end(), [&](uint64_t id) { return !isAlive(id); }), living.end());
            const auto delta = player - trigger.position;
            const bool inside = std::abs(delta.x) <= trigger.size.x * .5f &&
                std::abs(delta.y) <= trigger.size.y * .5f && std::abs(delta.z) <= trigger.size.z * .5f;
            const bool start = inside && !trigger.wasInside && !trigger.active && (!trigger.oneShot || !trigger.activated);
            trigger.wasInside = inside;
            if (start) {
                trigger.activated = true;
                trigger.active = true;
                trigger.spawned = 0;
                trigger.nextPoint = 0;
                trigger.nextSpawn = trigger.initialDelay;
            }
            if (!trigger.active) continue;
            // Activation is observed at the end of this frame; do not charge its preceding dt.
            if (!start) trigger.nextSpawn -= elapsed;
            if (!trigger.groupId.empty()) {
                if (trigger.nextSpawn <= 1e-7) {
                    ActivateGroupAt(trigger.groupId, elapsedTime_ + trigger.nextSpawn);
                    trigger.active = false;
                    trigger.nextSpawn = 0;
                }
                continue;
            }
            while (trigger.nextSpawn <= 1e-7 && trigger.spawned < trigger.spawnCount) {
                if (living.size() >= static_cast<size_t>(trigger.maxAlive)) {
                    trigger.nextSpawn = 0; // no accumulated burst debt while capacity is full
                    break;
                }
                size_t index = trigger.nextPoint % trigger.spawnPointIds.size();
                if (trigger.selection == SpawnPointSelection::Random)
                    index = std::uniform_int_distribution<size_t>(0, trigger.spawnPointIds.size() - 1)(random_);
                const auto* point = FindPoint(trigger.spawnPointIds[index]);
                living.push_back(spawn(*point, trigger.id, {}));
                ++trigger.nextPoint;
                ++trigger.spawned;
                trigger.nextSpawn += trigger.spawnInterval;
            }
            if (trigger.spawned == trigger.spawnCount) {
                trigger.active = false;
                trigger.nextSpawn = 0;
            }
        }
        // Stage Start, trigger groups and external events all drain this same wave.
        for (auto& point : points_) {
            auto& living = point.livingEnemies;
            living.erase(std::remove_if(living.begin(), living.end(), [&](uint64_t id) { return !isAlive(id); }), living.end());
            if (!point.active) continue;
            const double interval = point.mode == SpawnMode::Sequential ? point.spawnInterval : 0;
            while (point.spawned < point.spawnCount && point.nextSpawn <= elapsedTime_ + 1e-7) {
                if (living.size() >= static_cast<size_t>(point.maxAlive)) {
                    point.nextSpawn = elapsedTime_; // no burst debt while full
                    break;
                }
                living.push_back(spawn(point, point.id, point.NextEnemyId()));
                ++point.spawned;
                point.nextSpawn += interval;
            }
            if (point.spawned == point.spawnCount) point.active = false;
        }
    }
private:
    size_t ActivateGroupAt(const std::string& id, double time) {
        size_t count = 0;
        for (auto& point : points_)
            if (point.activation == SpawnActivation::OnTrigger && point.groupId == id && Activate(point, time)) ++count;
        return count;
    }
    bool Activate(EnemySpawnPoint& point, double time) {
        if (point.activation == SpawnActivation::Legacy || point.active) return false;
        point.activated = point.active = true;
        point.spawned = 0;
        point.nextSpawn = time + point.initialDelay;
        return true;
    }
    std::vector<EnemySpawnPoint> points_;
    std::vector<EnemySpawnTrigger> triggers_;
    std::vector<SpawnGroup> groups_;
    double elapsedTime_ = 0;
    std::string error_;
    std::mt19937 random_{std::random_device{}()};
    std::mt19937 initialRandom_ = random_;
};
