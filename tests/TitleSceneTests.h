#pragma once
#include "GameApp.h"
#include "scene/Flow/TitleScene.h"
#include "ImGuiManagaer.h"
#include "DirectXTex.h"
#include <fstream>
#include <stdexcept>

inline void RunTitleSceneTests(GameApp& app) {
    const auto check=[](bool ok,const char* message) { if (!ok) throw std::runtime_error(message); };
    auto* title = dynamic_cast<TitleScene*>(app.Scenes().Current());
    check(title && title->ready_, "Title layout failed to load");
    const auto capture=[&](const wchar_t* path) {
#ifdef USE_IMGUI
        app.ImGui()->Begin();
#endif
        app.Draw(); app.Dx()->WaitForGPU();
        DirectX::ScratchImage image;
        // RenderManager returns its original offscreen resource to RT after copying.
        check(SUCCEEDED(DirectX::CaptureTexture(app.Dx()->GetCommandQueue(),app.Render()->GetOffscreen()->GetResource(),false,image,
            D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET)),"Title render capture failed");
        check(SUCCEEDED(DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path)),"Title PNG failed");
    };
    capture(L"generated/title-tests/title.png");
    check(title->enemies_.size()==2 && title->startTarget_->CaptureDebug().parts.size()==1,"Title prototype objects");
    check(title->player_.IsMovementEnabled(),"Title WASD movement disabled");
    const float hp = title->player_.GetHP();
    const auto enemyPosition = title->enemies_[0]->GetPosition();
    for (int i=0; i<120; ++i) title->UpdateWorld(app,1.0f/60,false);

    check(StageLength(title->enemies_[0]->GetPosition()-enemyPosition)==0 && title->player_.GetHP()==hp,"Title enemy attacked/moved");
    check(!title->start_.Starting() && title->NextScene().empty(),"Title auto-started");
    auto* enemy = title->enemies_[0];
    const auto state = enemy->CaptureDebug();
    const auto& part = state.parts[0];
    const auto tri = part.geometry->faces[0];
    const auto local = (tri[0]+tri[1]+tri[2])*(1.0f/3);
    const auto point = StagePoint(local,Matrix4x4::MakeAffineMatrix(enemy->Definition().VisualScale(state.scale),state.rotation,state.position));
    auto weapon = *title->weapons_.Find(title->weaponId_);
    weapon.damage = 10;
    const auto gun = Matrix4x4::MakeAffineMatrix({1,1,1},{0,1.57079632679f,0},point-Vector3{5,0,0});
    StageWorld empty;
    int enemyHits = 0;
    title->bullets_.Spawn(weapon,gun,0,title->random_,empty,title->enemies_);
    title->bullets_.Update(.2f,empty,title->enemies_,[&](const BulletEnemyImpact& hit) {
        check(hit.result.damage>0,"Title enemy received no part damage"); ++enemyHits;
    });
    check(enemyHits==1 && !title->start_.Starting(),"Enemy shot started game or missed");
    enemy->Die();
    check(enemy->IsDead() && !enemy->CanReturnToPool(),"Title enemy death/debris");
    const auto allocations = Object3d::debugInitializationCount;
    for (int i=0; i<80; ++i) title->UpdateWorld(app,.1f,false);
    check(title->enemies_.size()==2 && title->enemies_[0]==enemy && !enemy->IsDead(),"Title pool respawn");
    check(Object3d::debugInitializationCount==allocations,"Title respawn allocated renderers");
    check(title->player_.GetHP()==hp,"Title death explosion damaged player");

    const auto startState = title->startTarget_->CaptureDebug();
    check(startState.parts[0].role==EnemyPartRole::Head && startState.parts[0].type==EnemyPartType::Head &&
        startState.parts[0].hp==1 && startState.models.size()==1,"GAME START must be one Head model");
    const auto& geometry = *startState.parts[0].geometry;
    Vector3 firePosition{};
    const std::array<float,9> columns{1,1.7f,2.4f,3.2f,4.4f,5.1f,5.8f,6.5f,7.2f};
    for (size_t i=0; i<columns.size(); ++i) {
        bool tested=false;
        for (const auto& face : geometry.faces) {
            const auto a=face[1]-face[0], b=face[2]-face[0];
            if (std::abs(a.x*b.y-a.y*b.x)<.0001f) continue;
            const auto center=(face[0]+face[1]+face[2])*(1.0f/3);
            const float right=i+1<columns.size()?columns[i+1]:startState.parts[0].bounds.max.x+.01f;
            if (center.x<columns[i] || center.x>=right) continue;
            const auto origin=center-Vector3{0,0,3};
            EnemyPartHit hit;
            if (!title->startTarget_->Raycast(origin,{0,0,1},5,hit)) continue;
            check(hit.part==EnemyPartType::Head && hit.partIndex==0,"Letter is not Head");
            check(!title->start_.Starting(),"Aiming started game");
            firePosition=origin; tested=true; break;
        }
        check(tested,"Merged text letter head raycast failed");
    }
    StageWorld blocked;
    StageCollider wall;
    wall.local={{firePosition.x-2,firePosition.y-2,firePosition.z+1},
                {firePosition.x+2,firePosition.y+2,firePosition.z+1.01f}};
    blocked.colliders.push_back(wall);
    title->bullets_.Spawn(weapon,Matrix4x4::Translation(firePosition),0,title->random_,blocked,title->enemies_);
    title->bullets_.Update(.1f,blocked,title->enemies_,[&](const BulletEnemyImpact& impact) {title->OnBulletImpact(impact);});
    check(!title->start_.Starting() && !title->startTarget_->IsDead(),"Shot passed wall to GAME START");
    const auto gpuCount=Object3d::debugInitializationCount;
    title->bullets_.Spawn(weapon,Matrix4x4::Translation(firePosition),0,title->random_,empty,title->enemies_);
    check(!title->start_.Starting(),"Shot spawned start without flight");
    title->UpdateWorld(app,.1f,false);
    check(title->start_.Starting() && title->start_.Elapsed()==0 && title->NextScene().empty(),"Actual Head impact did not begin delayed start");
    const auto dead=title->startTarget_->CaptureDebug();
    check(title->startTarget_->IsDead() && !dead.faces.empty() && !dead.visible[0],"GAME START did not use Enemy face shatter");
    check(Object3d::debugInitializationCount==gpuCount,"GAME START impact allocated renderers");
    for (const auto& face : dead.faces)
        check(StageLength(face.motion.velocity)>0 && StageLength(face.motion.angularVelocity)>0,"Enemy shards did not launch/spin");
    check(!title->start_.Begin(),"Repeated start reset timer");
    title->UpdateWorld(app,.5f,false);
    check(title->NextScene().empty() && title->startTarget_->CaptureDebug().faces[0].motion.age>0,"Explosion delay/animation");
    capture(L"generated/title-tests/explosion.png");
    title->UpdateWorld(app,.25f,false);
    check(title->NextScene()=="Game","Title did not request Stage01 after .75 seconds");
    app.Scenes().Update(app,0);
    check(app.Scenes().CurrentName()=="Game","SceneManager did not consume title transition");
    StageLoader stage;
    check(stage.Load("resources/levels/stage01/stage01.json") && stage.ValidateAssets(),"Stage01 failed validation");
    std::ofstream("generated/title-tests/result.txt") << "PASS: title assets, WASD enabled, frozen enemy, real enemy damage/death/debris/pool respawn, one combined Head model, all nine letters Head raycasts, wall occlusion, no aiming/spawn start, Enemy face-shatter death/spin without renderer allocation, one-shot .75s delay, SceneManager to Game/Stage01.\n";
}

