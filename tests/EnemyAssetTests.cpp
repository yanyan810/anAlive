#include "EnemyAsset.h"
#include "EnemyAI.h"
#include "EnemyDefinition.h"
#include "DetachedEnemyPart.h"
#include "Bullet.h"
#include <cassert>
#include <iostream>
using Json=nlohmann::json;
static Json Read(const std::string& path) { std::ifstream file(path); Json j; file>>j; return j; }
static void Near(float a,float b) { assert(std::abs(a-b)<.001f); }
int main() {
    auto asset=EnemyAsset::Load("variable.enemy.json"); // Actual Blender export, not hand-made faces.
    auto parts=asset->Instantiate();
    assert(parts.size()==9 && parts.hpGroups.size()==1);
    assert(parts[0].name=="Core0" && parts[1].role==EnemyPartRole::Core);
    for (size_t i=0;i<parts.size();++i) {
        auto& part=parts[i];
        const auto& face=part.geometry->faces[0];
        const auto center=(face[0]+face[1]+face[2])*(1.0f/3);
        EnemyPartHit hit;
        assert(RaycastEnemyParts(parts,Matrix4x4::MakeIdentity4x4(),center+Vector3{0,0,-2},{0,0,1},10,hit));
        assert(hit.partIndex==i); Near(hit.distance,2);
        // Swept bullets must preserve the index even when every role/tag is identical.
        StageWorld emptyWorld;
        const auto trace=TraceBulletPath(center+Vector3{0,0,-2},{0,0,1},10,emptyWorld,1,
            [&](size_t,const Vector3& o,const Vector3& d,float r,EnemyPartHit& h) {
                return RaycastEnemyParts(parts,Matrix4x4::MakeIdentity4x4(),o,d,r,h);
            });
        assert(trace && !trace->wall && trace->partIndex==i);
    }
    // An AABB-only hit outside the triangle must miss.
    EnemyParts single{parts[0]}; EnemyPartHit hit;
    assert(!RaycastEnemyParts(single,Matrix4x4::MakeIdentity4x4(),{-.9f,.9f,-2},{0,0,1},10,hit));
    auto independent=asset->Instantiate(2);
    assert(independent[0].geometry==parts[0].geometry);
    Near(DamageEnemyPart(parts,size_t(0),50),50);
    Near(parts[0].hp,250); Near(parts.hpGroups[0].hp,4950);
    Near(independent[0].hp,600); Near(independent.hpGroups[0].hp,10000);
    DamageEnemyPart(parts,size_t(1),50);
    Near(parts[1].hp,250); Near(parts.hpGroups[0].hp,4925);
    parts[1].sharedDamageRate=0;
    DamageEnemyPart(parts,size_t(1),50);
    Near(parts[1].hp,200); Near(parts.hpGroups[0].hp,4925);
    // Local overkill does not cap shared damage; a broken part then ceases to absorb hits.
    DamageEnemyPart(parts,size_t(0),400);
    assert(parts[0].Destroyed() && !parts[1].Destroyed() && !EnemyPartsDead(parts));
    Near(parts.hpGroups[0].hp,4525);
    Near(DamageEnemyPart(parts,size_t(0),50),0);
    DamageEnemyPart(parts,size_t(2),400);
    assert(parts[2].hp==0 && !parts[2].Destroyed());
    Near(parts.hpGroups[0].hp,4125);
    DamageEnemyPart(parts,size_t(2),50); Near(parts.hpGroups[0].hp,4075);
    const auto& face=parts[2].geometry->faces[0];
    const auto center=(face[0]+face[1]+face[2])*(1.0f/3);
    assert(RaycastEnemyParts(parts,Matrix4x4::MakeIdentity4x4(),center+Vector3{0,0,-2},{0,0,1},10,hit));
    assert(hit.partIndex==2); // Nonbreakable geometry still collides.
    parts[2].usesLocalHp=false;
    DamageEnemyPart(parts,size_t(2),4075);
    assert(EnemyPartsDead(parts) && !parts[2].Destroyed());
    auto copy=parts; copy.hpGroups[0].hp=10; assert(EnemyPartsDead(parts) && !EnemyPartsDead(copy));
    assert(DamageEnemyPart(parts,size_t(8),50)==0 && !parts[8].Destroyed()); // Unassigned is invincible.
    assert(DamageEnemyPart(parts,kNoEnemyPart,50)==0);
    assert(DamageEnemyPart(parts,size_t(2),-1)==0);
    assert(DamageEnemyPart(parts,size_t(2),std::numeric_limits<float>::quiet_NaN())==0);

    auto quads=EnemyAsset::Load("quads.enemy.json")->Instantiate();
    assert(quads.size()==2 && quads[0].geometry->faces.size()==4);
    DamageEnemyPart(quads,size_t(0),1000);
    assert(quads.hpGroups[0].hp==0 && !EnemyPartsDead(quads) && !quads[0].Destroyed());
    auto normal=EnemyAsset::Load("../../resources/enemy/boss/normal.enemy.json");
    const auto normalSource=Read("../../resources/enemy/boss/normal.enemy.json");
    assert(normal->defaults.size()==6);
    for (const auto& part:normal->defaults) assert(part.bulletCollision);
    size_t faces=0;
    const auto legacy=Read("../../resources/enemy/boss/faces/faces.json");
    const char* legacyKeys[]={"head","body","left_arm","right_arm","left_leg","right_leg"};
    const std::array<Transform,3> transforms{{{{1,1,1},{},{0,0,0}},
        {{2,3,1},{.3f,-.7f,.2f},{-8,4,10}},{{-2,1,3},{-.4f,1.2f,.1f},{5,-2,-7}}}};
    for (size_t i=0;i<normal->defaults.size();++i) {
        auto state=normal->Instantiate();
        const auto& part=state[i]; faces+=part.geometry->faces.size();
        assert(part.maxHp==normalSource.at("parts").at(i).at("localHp").get<float>());
        const auto& original=legacy.at("parts").at(legacyKeys[i]);
        assert(original.size()==part.geometry->faces.size());
        for (size_t f=0;f<original.size();++f) for (size_t v=0;v<3;++v) {
            const auto& expected=original[f][2-v]; const auto actual=part.geometry->faces[f][v];
            Near(actual.x,-expected[0].get<float>()); Near(actual.y,expected[1].get<float>()); Near(actual.z,expected[2].get<float>());
        }
        for (const auto& transform:transforms) {
            const auto world=Matrix4x4::MakeAffineMatrix(transform.scale,transform.rotate,transform.translate);
            // Isolate part, then hit a nondegenerate source triangle from its surface normal.
            EnemyParts isolated{part};
            const auto& f=part.geometry->faces[0];
            const auto a=EnemyPartTransformPoint(f[0],world),b=EnemyPartTransformPoint(f[1],world),c=EnemyPartTransformPoint(f[2],world);
            auto ab=b-a,ac=c-a;
            Vector3 n{ab.y*ac.z-ab.z*ac.y,ab.z*ac.x-ab.x*ac.z,ab.x*ac.y-ab.y*ac.x};
            const auto len=std::hypot(n.x,n.y,n.z);
            assert(len>0); n=n*(1/len);
            const auto target=(a+b+c)*(1.0f/3);
            assert(RaycastEnemyParts(isolated,world,target+n*.01f,n*-1,1,hit));
            assert(hit.partIndex==0);
        }
        DamageEnemyPart(state,i,part.hp);
        assert(state[i].Destroyed());
        assert(EnemyPartsDead(state)==(part.role==EnemyPartRole::Head || part.role==EnemyPartRole::Body));
        for (size_t j=0;j<state.size();++j) if (j!=i) assert(!state[j].Destroyed());
        // All authored triangles survive chunk partition exactly once, and are Face Shatter inputs.
        const auto chunks=PartitionEnemyChunks(*part.geometry,part.bounds);
        size_t count=0; for (const auto& chunk:chunks) count+=chunk.size();
        assert(count==part.geometry->faces.size());
        DetachedPartMotion fragment;
        fragment.Initialize(part.bounds,{0,4,0},{},{1,1,1},{0,0,1},LegacyRoleType(part.role),{2,3,4},{});
        const float initialZ=fragment.position.z;
        fragment.Update(.1f); assert(fragment.Active() && fragment.position.z>initialZ);
    }
    assert(faces==712);
    auto boss=EnemyAsset::Load("../../resources/enemy/boss/shared-boss.enemy.json")->Instantiate();
    DamageEnemyPart(boss,size_t(0),50); DamageEnemyPart(boss,size_t(2),50);
    Near(boss.hpGroups[0].hp,4900); Near(boss[0].hp,250); Near(boss[2].hp,250);
    DamageEnemyPart(boss,size_t(1),5000); assert(EnemyPartsDead(boss) && !boss[1].Destroyed());
    auto removed=EnemyAsset::Load("removed-added.enemy.json");
    assert(removed->defaults.size()==9 && removed->defaults[7].name=="WingL");

    const auto source=Read("variable.enemy.json");
    const auto reject=[&](Json data) {
        std::ofstream("invalid.enemy.json")<<data;
        bool failed=false;
        try { EnemyAsset::Load("invalid.enemy.json"); } catch(const std::exception&) { failed=true; }
        assert(failed);
    };
    auto invalid=source; invalid["parts"][1]["faces"].push_back(0); reject(invalid);
    invalid=source; invalid["parts"][0]["sharedHpGroup"]="Missing"; reject(invalid);
    invalid=source; invalid["parts"][0]["name"]=invalid["parts"][1]["name"]; reject(invalid);
    invalid=source; invalid["parts"][0]["sharedDamageRate"]=-1; reject(invalid);
    invalid=source; invalid["hpGroups"][0]["maxHp"]=0; reject(invalid);
    invalid=source; invalid["parts"][0]["role"]="WrongRole"; reject(invalid);
    invalid=source; invalid["parts"][0]["triangles"][0]["face"]=999; reject(invalid);
    invalid=source; invalid["parts"][0]["triangles"]=Json::array(); reject(invalid);
    EnemyDefinitions definitions;
    assert(definitions.Load("../../resources/Data/enemies.json"));
    assert(definitions.Find("normal")->partAsset && !definitions.Find("bomber")->partAsset);
    const auto testAsset=definitions.Find("normal_test")->partAsset;
    auto sharedTest=testAsset->Instantiate();
    const auto initialSharedHp=sharedTest.hpGroups[0].hp;
    DamageEnemyPart(sharedTest,EnemyPartType::LeftArm,50);
    assert(sharedTest.hpGroups[0].hp==initialSharedHp-50 && !EnemyPartsDead(sharedTest));
    DamageEnemyPart(sharedTest,EnemyPartType::RightArm,sharedTest.hpGroups[0].hp);
    assert(sharedTest.hpGroups[0].hp==0 && EnemyPartsDead(sharedTest));
    assert(BeginEnemyDeath(sharedTest));
    for (const auto& part:sharedTest) assert(part.Destroyed());
    assert(!BeginEnemyDeath(sharedTest));
    assert(!EnemyPartsDead(testAsset->Instantiate()));
    std::cout<<"EnemyAsset tests passed: Blender face identity, exact rays / bullet index, arbitrary counts, local/shared/rates, nonbreakable/invincible, group death, instance isolation, six-part compatibility, all 712 face/chunk triangles, fragment motion, validation.\n";
}
