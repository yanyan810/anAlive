#include "ThrowableSystem.h"
#include "Enemy.h"
#include "StageWorld.h"
#include "WeaponSystem.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
constexpr float kGroundNormal = .55f;
constexpr float kSurfaceOffset = .005f;
constexpr float kCorrosionHeight = .65f;
constexpr float kFlightStep = 1.0f/120;
constexpr size_t kMaxActiveThrowables = 128;

Json ReadJson(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open " + path);
    return Json::parse(input);
}
const Json& Object(const Json& json,const char* key) {
    const auto& value=json.at(key);
    if (!value.is_object()) throw std::runtime_error(std::string("Expected object: ")+key);
    return value;
}

float Number(const Json& json,const char* key,float fallback,float min,float max) {
    if (!json.contains(key)) return fallback;
    const auto& field=json.at(key);
    if (!field.is_number()) throw std::runtime_error(std::string("Expected number: ")+key);
    const float value=field.get<float>();
    if (!std::isfinite(value) || value<min || value>max)
        throw std::runtime_error(std::string("Out of range: ")+key);
    return value;
}

EnemyPartDamageMultipliers ReadPartMultipliers(const Json& effect) {
    EnemyPartDamageMultipliers result;
    if (!effect.contains("partMultipliers")) return result;
    const auto& values=Object(effect,"partMultipliers");
    for (auto entry=values.begin();entry!=values.end();++entry) {
        const auto match=std::find_if(kEnemyPartRoles.begin(),kEnemyPartRoles.end(),[&](EnemyPartRole role) {
            return entry.key()==EnemyPartRoleName(role);
        });
        if (match==kEnemyPartRoles.end()) throw std::runtime_error("Unknown throwable part role: "+entry.key());
        result.Set(*match,Number(values,entry.key().c_str(),1,0,100));
    }
    return result;
}

int Integer(const Json& json,const char* key,int fallback,int min,int max) {
    if (json.contains(key) && !json.at(key).is_number_integer())
        throw std::runtime_error(std::string("Expected integer: ")+key);
    return static_cast<int>(Number(json,key,static_cast<float>(fallback),static_cast<float>(min),static_cast<float>(max)));
}

Vector3 ReadVector(const Json& value,float min,float max) {
    if (!value.is_array() || value.size()!=3) throw std::runtime_error("Expected 3-vector");
    Vector3 result{value.at(0).get<float>(),value.at(1).get<float>(),value.at(2).get<float>()};
    for (float n:{result.x,result.y,result.z})
        if (!std::isfinite(n) || n<min || n>max) throw std::runtime_error("Invalid vector component");
    return result;
}

ThrowableDefinition ReadDefinition(const Json& json) {
    ThrowableDefinition d;
    d.id=json.at("id").get<std::string>();
    d.displayName=json.at("displayName").get<std::string>();
    if (d.id.empty() || d.displayName.empty()) throw std::runtime_error("Empty throwable ID or displayName");
    const auto type=json.at("type").get<std::string>();
    const auto& flight=Object(json,"flight");
    d.maxCount=Integer(json,"maxCount",1,1,999);
    d.speed=Number(flight,"speed",d.speed,1,300);
    d.lift=Number(flight,"lift",d.lift,0,30);
    d.gravityMultiplier=Number(flight,"gravityMultiplier",d.gravityMultiplier,0,10);
    d.flightLifetime=Number(flight,"lifetime",d.flightLifetime,.01f,30);
    d.maxBounces=Integer(flight,"maxBounces",d.maxBounces,0,8);
    d.bounceRetention=Number(flight,"bounceRetention",d.bounceRetention,0,1);
    if (type=="Corrosive") {
        d.type=ThrowableType::Corrosive;
        const auto& area=Object(json,"corrosion");
        d.effectRadius=Number(area,"radius",2,.01f,100);
        d.effectDuration=Number(area,"duration",8,.01f,120);
        d.slowMultiplier=Number(area,"slowMultiplier",.5f,0,1);
        d.legDamagePerSecond=Number(area,"legDamagePerSecond",10,0,10000);
        d.legSharedDamageMultiplier=Number(area,"legSharedDamageMultiplier",0,0,100);
        d.fallbackDamagePerSecond=Number(area,"fallbackDamagePerSecond",d.legDamagePerSecond,0,10000);
        d.corrosionPartMultipliers=ReadPartMultipliers(area);
    } else if (type=="LureGrenade" || type=="Dart") {
        d.type=type=="LureGrenade" ? ThrowableType::LureGrenade : ThrowableType::Dart;
        if (d.type==ThrowableType::LureGrenade) {
            const auto& lure=Object(json,"lure");
            d.lureRadius=Number(lure,"radius",8,0,100);
            d.fuseSeconds=Number(lure,"fuseSeconds",4,.01f,120);
        } else {
            const auto& direct=Object(json,"directHit");
            d.directPartDamage=Number(direct,"partDamage",80,0,10000);
            d.directSharedDamage=Number(direct,"sharedDamage",25,0,10000);
            d.directPartMultipliers=ReadPartMultipliers(direct);
        }
        const auto& blast=Object(json,"blast");
        const bool grenade=d.type==ThrowableType::LureGrenade;
        d.blastRadius=Number(blast,"radius",grenade ? 2.5f : 1.0f,.01f,100);
        d.blastDamage=Number(blast,"damage",grenade ? 35.0f : 20.0f,0,10000);
        d.blastPartMultiplier=Number(blast,"partMultiplier",1,0,100);
        d.blastSharedMultiplier=Number(blast,"sharedMultiplier",1,0,100);
        d.blastPartMultipliers=ReadPartMultipliers(blast);
    } else throw std::runtime_error("Unknown throwable type: "+type);

    const auto& visual=Object(json,"visual");
    d.color=ReadVector(visual.at("color"),0,1);
    d.visualSize=Number(visual,"size",d.visualSize,.01f,2);
    d.flashHz=Number(visual,"flashHz",d.flashHz,.1f,30);
    d.blastVisualSeconds=Number(visual,"blastSeconds",d.blastVisualSeconds,.01f,5);
    return d;
}
}

bool ThrowableSystem::Load(const std::string& definitionsPath,const std::string& stagePath,const Json* draft) {
    try {
        ThrowableSystem next;
        const auto stage=draft ? *draft : ReadJson(stagePath);
        if (stage.contains("throwables")) {
            const auto source=ReadJson(definitionsPath);
            if (source.at("version")!=1 || !source.at("items").is_array())
                throw std::runtime_error("Unsupported throwable definitions");
            const auto find=[&](const std::string& id) -> std::optional<size_t> {
                for (size_t i=0;i<next.definitions_.size();++i)
                    if (next.definitions_[i].id==id) return i;
                return std::nullopt;
            };
            for (const auto& item:source.at("items")) {
                auto d=ReadDefinition(item);
                if (find(d.id)) throw std::runtime_error("Duplicate throwable: "+d.id);
                next.definitions_.push_back(std::move(d));
            }
            const auto resolve=[&](const std::string& id) {
                const auto item=find(id);
                if (!item) throw std::runtime_error("Unknown throwable: "+id);
                return *item;
            };
            const auto& config=Object(stage,"throwables");
            next.reusablePickups_=config.value("reusablePickups",false);
            if (config.contains("initial")) {
                const auto& initial=Object(config,"initial");
                const auto item=resolve(initial.at("id").get<std::string>());
                const int maxCount=next.definitions_[item].maxCount;
                next.initialItem_=item;
                next.initialCount_=Integer(initial,"count",maxCount,0,maxCount);
            }
            if (config.contains("pickups")) {
                const auto& pickups=config.at("pickups");
                if (!pickups.is_array()) throw std::runtime_error("Throwable pickups must be an array");
                for (const auto& pickup:pickups) {
                    const auto item=resolve(pickup.at("id").get<std::string>());
                    next.pickups_.push_back({item,ReadVector(pickup.at("position"),-1e6f,1e6f)});
                }
            }
        }
        next.Reset();
        *this=std::move(next);
        return true;
    } catch (const std::exception& error) {
        error_=error.what();
        return false;
    }
}

void ThrowableSystem::Reset() {
    state_={};
    state_.equipped=initialItem_;
    state_.count=initialCount_;
    state_.consumed.resize(pickups_.size());
}

std::optional<size_t> ThrowableSystem::FindPickup(const Vector3& player,const StageWorld& world) const {
    float best=WeaponSystem::kPickupRadius;
    std::optional<size_t> nearest;
    for (size_t i=0;i<pickups_.size();++i) {
        const auto& pickup=pickups_[i];
        if (state_.consumed[i] || (state_.equipped==pickup.item && state_.count==definitions_[pickup.item].maxCount)) continue;
        const float distance=StageLength(pickup.position-player);
        if (distance>best || (nearest && distance==best)) continue;
        const auto origin=player+Vector3{0,.75f,0};
        const auto toPickup=pickup.position-origin;
        StageHit hit;
        if (world.Raycast(origin,toPickup,StageLength(toPickup),hit)) continue;
        best=distance;
        nearest=i;
    }
    return nearest;
}

const ThrowableDefinition* ThrowableSystem::Nearby(const Vector3& player,const StageWorld& world) const {
    const auto nearest=FindPickup(player,world);
    return nearest ? &definitions_[pickups_[*nearest].item] : nullptr;
}

bool ThrowableSystem::TryPickup(const Vector3& player,const StageWorld& world) {
    const auto nearest=FindPickup(player,world);
    if (!nearest) return false;
    const auto item=pickups_[*nearest].item;
    state_.equipped=item;
    state_.count=definitions_[item].maxCount;
    state_.consumed[*nearest]=!reusablePickups_;
    return true;
}

bool ThrowableSystem::Throw(const Matrix4x4& cameraWorld) {
    if (state_.count<=0 || state_.flights.size()+state_.effects.size()>=kMaxActiveThrowables) return false;
    const auto& d=*Equipped();
    const Vector3 origin{cameraWorld.m[3][0],cameraWorld.m[3][1],cameraWorld.m[3][2]};
    const Vector3 direction{cameraWorld.m[2][0],cameraWorld.m[2][1],cameraWorld.m[2][2]};
    state_.flights.push_back({*state_.equipped,origin,direction*d.speed+Vector3{0,d.lift,0},d.flightLifetime});
    --state_.count;
    return true;
}

bool ThrowableSystem::Advance(Flight& f,float dt,const StageWorld& world,std::span<Enemy* const> enemies) {
    const auto& d=definitions_[f.item];
    const Vector3 gravity{0,-9.81f*d.gravityMultiplier,0};
    float remaining=std::min(dt,f.remaining);
    f.remaining-=remaining;
    while (remaining>0) {
        const float step=std::min(remaining,kFlightStep);
        remaining-=step;
        const auto motion=(f.velocity+gravity*(step*.5f))*step;
        f.velocity+=gravity*step;
        const float length=StageLength(motion);
        if (length<1e-6f) continue;
        const auto direction=motion*(1/length);
        StageHit wall;
        const bool hitWorld=world.Raycast(f.position,direction,length,wall);
        float distance=hitWorld ? wall.distance : length;
        Enemy* directEnemy=nullptr;
        Enemy::RaycastHit part;
        if (d.type!=ThrowableType::LureGrenade) for (auto* enemy:enemies) {
            Enemy::RaycastHit candidate;
            if (enemy->Raycast(f.position,direction,distance,candidate) &&
                ((!hitWorld && !directEnemy) || candidate.distance<distance)) {
                directEnemy=enemy;
                part=candidate;
                distance=part.distance;
            }
        }
        f.position+=direction*distance;
        if (directEnemy) {
            if (d.type==ThrowableType::Dart) Explode(f.item,f.position,enemies,directEnemy,part.partIndex);
            else {
                const auto origin=directEnemy->GetPosition()+Vector3{0,.35f,0};
                StageHit floor;
                if (world.Raycast(origin,{0,-1,0},1.5f,floor) && floor.normal.y>=kGroundNormal)
                    state_.effects.push_back({f.item,origin-Vector3{0,floor.distance,0},d.effectDuration});
            }
            return false;
        }
        if (!hitWorld) continue;
        if (d.type==ThrowableType::Dart) {
            Explode(f.item,f.position,enemies);
            return false;
        }
        if (wall.normal.y>=kGroundNormal) {
            const float duration=d.type==ThrowableType::Corrosive ? d.effectDuration : d.fuseSeconds;
            state_.effects.push_back({f.item,f.position,duration});
            return false;
        }
        f.position+=wall.normal*kSurfaceOffset;
        if (f.bounces<d.maxBounces && StageDot(f.velocity,wall.normal)<0) {
            f.velocity=(f.velocity-wall.normal*(2*StageDot(f.velocity,wall.normal)))*d.bounceRetention;
            ++f.bounces;
        } else {
            // A second wall or ceiling clips lateral motion so the object can drop.
            f.velocity={0,std::min(0.0f,f.velocity.y),0};
        }
    }
    return f.remaining>0;
}

bool ThrowableSystem::InCorrosion(const Vector3& feet,const Vector3& position,float radius) {
    const auto delta=feet-position;
    return std::abs(delta.y)<=kCorrosionHeight && std::hypot(delta.x,delta.z)<=radius;
}

void ThrowableSystem::Explode(size_t item,const Vector3& position,std::span<Enemy* const> enemies,
    Enemy* directEnemy,size_t directPart) {
    const auto& d=definitions_[item];
    if (state_.flashes.size()<kMaxActiveThrowables) state_.flashes.push_back({item,position,d.blastVisualSeconds});
    const Enemy::AreaDamage blast{position,d.blastRadius,d.blastDamage*d.blastPartMultiplier,d.blastDamage*d.blastSharedMultiplier};
    for (auto* enemy:enemies) {
        const auto direct=enemy==directEnemy ? Enemy::PartDamage{directPart,d.directPartDamage,d.directSharedDamage}
            : Enemy::PartDamage{};
        enemy->ApplyThrowableBlast(blast,d.blastPartMultipliers,direct,d.directPartMultipliers);
    }
}

void ThrowableSystem::Update(float dt,const StageWorld& world,std::span<Enemy* const> enemies) {
    if (dt<=0 || !std::isfinite(dt)) return;
    for (auto& flash:state_.flashes) flash.remaining-=dt;
    std::erase_if(state_.flashes,[](const Effect& flash) { return flash.remaining<=0; });
    for (auto& effect:state_.effects) {
        const auto& d=definitions_[effect.item];
        const float elapsed=std::min(dt,effect.remaining);
        effect.remaining-=elapsed;
        if (d.type==ThrowableType::Corrosive) {
            const float damage=d.legDamagePerSecond*elapsed;
            for (auto* enemy:enemies)
                if (!enemy->IsDead() && InCorrosion(enemy->GetPosition(),effect.position,d.effectRadius))
                    enemy->ApplyCorrosionDamage(damage,damage*d.legSharedDamageMultiplier,
                        d.fallbackDamagePerSecond*elapsed,d.corrosionPartMultipliers);
        } else if (effect.remaining<=0) Explode(effect.item,effect.position,enemies);
    }
    std::erase_if(state_.effects,[](const Effect& effect) { return effect.remaining<=0; });
    for (auto& flight:state_.flights)
        if (!Advance(flight,dt,world,enemies)) flight.remaining=0;
    std::erase_if(state_.flights,[](const Flight& flight) { return flight.remaining<=0; });
}

ThrowableSystem::EnemyEffect ThrowableSystem::EffectFor(const Enemy& enemy) const {
    EnemyEffect result;
    if (enemy.IsDead()) return result;
    const auto feet=enemy.GetPosition();
    float nearest=std::numeric_limits<float>::infinity();
    for (const auto& effect:state_.effects) {
        const auto& d=definitions_[effect.item];
        if (d.type==ThrowableType::Corrosive) {
            if (InCorrosion(feet,effect.position,d.effectRadius))
                result.speedMultiplier=std::min(result.speedMultiplier,d.slowMultiplier);
        } else if (d.lureRadius>0) {
            const float distance=StageLength(effect.position-feet);
            if (distance<=d.lureRadius && distance<nearest) {
                nearest=distance;
                result.attraction=effect.position;
            }
        }
    }
    return result;
}

void ThrowableSystem::CollectVisuals(std::vector<Visual>& visuals) const {
    visuals.clear();
    const auto marker=[&](size_t item,const Vector3& point,float brightness=1) {
        const auto& d=definitions_[item];
        const float r=d.visualSize;
        const auto scale=d.type==ThrowableType::Dart ? Vector3{r,r*1.8f,r} : Vector3{r,r,r};
        visuals.push_back({point,scale,d.color*brightness});
    };
    for (size_t i=0;i<pickups_.size();++i)
        if (!state_.consumed[i]) marker(pickups_[i].item,pickups_[i].position);
    for (const auto& f:state_.flights) marker(f.item,f.position);
    for (const auto& effect:state_.effects) {
        const auto& d=definitions_[effect.item];
        if (d.type==ThrowableType::Corrosive) {
            const float r=d.effectRadius;
            visuals.push_back({effect.position+Vector3{0,.035f,0},{r,.025f,r},d.color,.55f});
        } else {
            const auto phase=std::fmod((d.fuseSeconds-effect.remaining)*d.flashHz,1.0f);
            marker(effect.item,effect.position+Vector3{0,d.visualSize,0},phase<.5f ? 1.0f : .25f);
        }
    }
    for (const auto& flash:state_.flashes) {
        const auto& d=definitions_[flash.item];
        const float r=d.blastRadius;
        visuals.push_back({flash.position,{r,r,r},{1,.4f,.08f},.4f*flash.remaining/d.blastVisualSeconds});
    }
}
