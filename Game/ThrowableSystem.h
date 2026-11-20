#pragma once
#include "Matrix4x4.h"
#include "EnemyParts.h"
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

class Enemy;
class StageWorld;

enum class ThrowableType { Corrosive, LureGrenade, Dart };

struct ThrowableDefinition {
    std::string id, displayName;
    ThrowableType type{};
    int maxCount=1, maxBounces=0;
    float speed=12, lift=0, gravityMultiplier=1, flightLifetime=5, bounceRetention=.5f;
    float effectRadius=0, effectDuration=0, slowMultiplier=1;
    float legDamagePerSecond=0, legSharedDamageMultiplier=0, fallbackDamagePerSecond=0;
    float lureRadius=0, fuseSeconds=0;
    float blastRadius=0, blastDamage=0, blastPartMultiplier=1, blastSharedMultiplier=1;
    float directPartDamage=0, directSharedDamage=0;
    EnemyPartDamageMultipliers directPartMultipliers, blastPartMultipliers, corrosionPartMultipliers;
    Vector3 color{1,1,1};
    float visualSize=.15f, flashHz=4, blastVisualSeconds=.3f;
};

class ThrowableSystem {
public:
    struct Pickup { size_t item; Vector3 position; };
    struct Flight { size_t item; Vector3 position, velocity; float remaining; int bounces=0; };
    struct Effect { size_t item; Vector3 position; float remaining; };
    struct State {
        std::optional<size_t> equipped;
        int count=0;
        std::vector<bool> consumed;
        std::vector<Flight> flights;
        std::vector<Effect> effects, flashes;
    };
    struct EnemyEffect { float speedMultiplier=1; std::optional<Vector3> attraction; };
    struct Visual { Vector3 position, scale, color; float opacity=1; };

    bool Load(const std::string& definitionsPath,const std::string& stagePath,
        const nlohmann::json* draft=nullptr);
    void Reset();
    bool Enabled() const { return initialItem_.has_value() || !pickups_.empty(); }
    const std::string& Error() const { return error_; }
    const ThrowableDefinition* Equipped() const {
        return state_.equipped ? &definitions_[*state_.equipped] : nullptr;
    }
    int Count() const { return state_.count; }
    const std::vector<Pickup>& Pickups() const { return pickups_; }
    const ThrowableDefinition* Nearby(const Vector3& player,const StageWorld& world) const;
    bool TryPickup(const Vector3& player,const StageWorld& world);
    bool Throw(const Matrix4x4& cameraWorld);
    void Update(float dt,const StageWorld& world,std::span<Enemy* const> enemies);
    EnemyEffect EffectFor(const Enemy& enemy) const;
    void CollectVisuals(std::vector<Visual>& visuals) const;
    const State& GetState() const { return state_; }
    void Restore(const State& state) { state_=state; }

private:
    std::optional<size_t> FindPickup(const Vector3& player,const StageWorld& world) const;
    bool Advance(Flight& flight,float dt,const StageWorld& world,std::span<Enemy* const> enemies);
    void Explode(size_t item,const Vector3& position,std::span<Enemy* const> enemies,
        Enemy* directEnemy=nullptr,size_t directPart=0);
    static bool InCorrosion(const Vector3& feet,const Vector3& position,float radius);

    std::vector<ThrowableDefinition> definitions_;
    std::vector<Pickup> pickups_;
    std::optional<size_t> initialItem_;
    int initialCount_=0;
    bool reusablePickups_=false;
    State state_;
    std::string error_;
};
