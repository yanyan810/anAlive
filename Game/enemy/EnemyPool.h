#pragma once
#include "Enemy.h"

struct EnemyPoolSettings {
    size_t perDefinition=4;
    std::map<std::string,size_t> overrides;
    static EnemyPoolSettings Load(const std::string& path) {
        EnemyPoolSettings result;
        std::ifstream file(path);
        if (!file) return result;
        nlohmann::json data; file>>data;
        const auto count=[](const nlohmann::json& value) -> size_t {
            if (!value.is_number_integer() || value.get<double>()<0 || value.get<double>()>256)
                throw std::runtime_error("Enemy pool capacity must be an integer in [0,256]");
            return value.get<size_t>();
        };
        if (data.contains("perDefinition")) result.perDefinition=count(data.at("perDefinition"));
        if (data.contains("overrides")) {
            if (!data.at("overrides").is_object()) throw std::runtime_error("Enemy pool overrides must be an object");
            for (const auto& [id,value]:data.at("overrides").items()) result.overrides[id]=count(value);
        }
        return result;
    }
};

// Owns every enemy. Scene/BulletManager only borrow stable pointers to active slots.
// Each bucket keeps one immutable spawn definition; changing type selects a different bucket.
class EnemyPool {
public:
    void Initialize(Object3dCommon* common,DirectXCommon* dx,Camera* camera,
        const EnemyDefinitions& definitions,const EnemyPoolSettings& settings={}) {
        Clear(); common_=common; dx_=dx; camera_=camera;
        for (const auto& [id,definition]:definitions.All()) {
            auto& bucket=buckets_[id]; bucket.definition=definition;
            const auto found=settings.overrides.find(id);
            const auto count=found==settings.overrides.end() ? settings.perDefinition : found->second;
            bucket.slots.reserve(count);
            for (size_t i=0;i<count;++i) Add(bucket,false);
        }
    }
    Enemy* Acquire(const std::string& definition,uint64_t id,const std::string& trigger,
        const Vector3& position,const Vector3& rotation) {
        auto found=buckets_.find(definition);
        if (found==buckets_.end()) throw std::runtime_error("Unknown enemy pool definition: "+definition);
        auto& bucket=found->second;
        Slot* available=nullptr;
        for (auto& slot:bucket.slots) if (!slot.active) { available=&slot; break; }
        if (!available) available=&Add(bucket,true);
        available->enemy->ResetForSpawn(id,trigger,position,rotation);
        available->active=true;
        return available->enemy.get();
    }
    bool Release(Enemy* enemy) {
        for (auto& [id,bucket]:buckets_) for (auto& slot:bucket.slots) {
            if (slot.enemy.get()!=enemy) continue;
            if (!slot.active || !enemy->CanReturnToPool()) return false;
            enemy->RetireFromPool(); slot.active=false; return true;
        }
        return false;
    }
    // Explicit debug deletion. Caller waits for GPU and drops all borrowed references first.
    bool ReleaseForReset(Enemy* enemy) {
        for (auto& [id,bucket]:buckets_) for (auto& slot:bucket.slots)
            if (slot.enemy.get()==enemy && slot.active) {
                enemy->RetireFromPool(); slot.active=false; return true;
            }
        return false;
    }
    // Scene reset/rewind only: caller must wait for GPU work before discarding debris.
    void ReleaseAll() {
        for (auto& [id,bucket]:buckets_) for (auto& slot:bucket.slots) {
            slot.enemy->RetireFromPool(); slot.active=false;
        }
    }
    void Clear() { buckets_.clear(); runtimeAllocations_=0; }
    size_t Active() const {
        size_t count=0;
        for (const auto& [id,bucket]:buckets_) for (const auto& slot:bucket.slots) if (slot.active) ++count;
        return count;
    }
    size_t Capacity() const { size_t count=0; for (const auto& [id,bucket]:buckets_) count+=bucket.slots.size(); return count; }
    size_t Inactive() const { return Capacity()-Active(); }
    size_t RuntimeAllocations() const { return runtimeAllocations_; }
private:
    struct Slot { std::unique_ptr<Enemy> enemy; bool active=false; };
    struct Bucket { EnemyDefinition definition; std::vector<Slot> slots; };
    Slot& Add(Bucket& bucket,bool runtime) {
        auto enemy=std::make_unique<Enemy>();
        enemy->PrepareForPool(common_,dx_,camera_,bucket.definition);
        bucket.slots.push_back({std::move(enemy),false});
        if (runtime) {
            ++runtimeAllocations_;
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
            OutputDebugStringA(("Enemy Pool exhausted: "+bucket.definition.id+"; Runtime Allocations: "+std::to_string(runtimeAllocations_)+"\n").c_str());
#endif
        }
        return bucket.slots.back();
    }
    std::map<std::string,Bucket> buckets_;
    Object3dCommon* common_=nullptr;
    DirectXCommon* dx_=nullptr;
    Camera* camera_=nullptr;
    size_t runtimeAllocations_=0;
};
