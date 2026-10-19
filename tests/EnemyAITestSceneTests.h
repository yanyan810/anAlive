#pragma once
#include "GameApp.h"
#include "scene/Debug/EnemyAITestScene.h"
#include "scene/Flow/TitleScene.h"
#include "ImGuiManagaer.h"
#include "SrvManager.h"
#include "DirectXTex.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>

inline void RunEnemyAITestSceneTests(GameApp& app) {
    const auto check=[](bool ok,const char* text) { if (!ok) throw std::runtime_error(text); };
    const auto frame=[&]() {
#ifdef USE_IMGUI
        app.ImGui()->Begin();
#endif
        app.Update(1.0f/60,1.0f/60); app.Draw();
    };
    const auto transition=[&](const char* name) {
        check(app.Scenes().TransitionTo(name,.05f,.05f),"transition rejected");
        for (int i=0;i<60 && app.Scenes().IsTransitioning();++i) frame();
        check(!app.Scenes().IsTransitioning() && app.Scenes().CurrentName()==name,"fade transition did not finish");
    };
    check(dynamic_cast<TitleScene*>(app.Scenes().Current())!=nullptr,"initial title missing");
    transition("EnemyAITest");
    auto* scene=dynamic_cast<EnemyAITestScene*>(app.Scenes().Current());
    check(scene && scene->enemies_.size()==8 && scene->pool_.Active()==8,"AI scene initial enemies/pool");
    check(scene->world_.colliders.size()==10,"AI scene collision layout missing");
    // Capture only environment geometry: enemies/debug overlays already used the
    // live camera and would hide a stale background WVP in a whole-scene comparison.
    const auto captureEnvironment=[&](const wchar_t* path) {
        auto* dx=app.Dx(); auto* target=app.Render()->GetOffscreen();
        app.Srv()->PreDraw(); dx->PreDraw(); target->Begin();
        for (const auto& object:scene->geometry_) object->Draw();
        target->End(); dx->SetBackBufferRenderTarget(); dx->PostDraw(); dx->WaitForGPU();
        DirectX::ScratchImage image;
        const auto state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        check(SUCCEEDED(DirectX::CaptureTexture(dx->GetCommandQueue(),target->GetResource(),false,image,state,state)),"environment capture failed");
        const auto* pixels=image.GetImage(0,0,0);
        check(SUCCEEDED(DirectX::SaveToWICFile(*pixels,DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path)),"environment PNG failed");
        std::vector<uint8_t> result;
        for (size_t y=0;y<pixels->height;++y)
            result.insert(result.end(),pixels->pixels+y*pixels->rowPitch,pixels->pixels+y*pixels->rowPitch+pixels->width*4);
        return result;
    };
    app.GetInput()->SetCameraControlEnabled(false);
    const auto initialPlayer=scene->player_.GetTransform();
    scene->Update(app,0);
    const auto initialPixels=captureEnvironment(L"generated/enemy-ai-tests/camera-initial.png");
    scene->player_.SetStage(&scene->world_,initialPlayer.translate,initialPlayer.rotate+Vector3{.12f,.45f,0});
    scene->Update(app,0);
    const auto rotatedPixels=captureEnvironment(L"generated/enemy-ai-tests/camera-rotated.png");
    check(initialPixels!=rotatedPixels,"camera rotation left environment frozen (stale object WVP)");
    scene->player_.SetStage(&scene->world_,initialPlayer.translate+Vector3{3,0,0},initialPlayer.rotate);
    scene->Update(app,0);
    const auto movedPixels=captureEnvironment(L"generated/enemy-ai-tests/camera-moved.png");
    check(initialPixels!=movedPixels,"camera translation left environment frozen (stale object WVP)");
    scene->player_.SetStage(&scene->world_,initialPlayer.translate,initialPlayer.rotate);
    scene->Update(app,0);
    check(initialPixels==captureEnvironment(L"generated/enemy-ai-tests/camera-restored.png"),"restoring camera did not restore environment pixels");
    // Use the actual pooled enemy and the same corner colliders as the playable
    // chase/lost-sight area. The north-east hiding point is occluded by the L wall.
    auto* corner=scene->enemies_[3];
    corner->Senses().detectionTime=0; corner->Senses().fieldOfView=360;
    const Vector3 observed{-24,0,20}, hidden{-14,0,40};
    for (int i=0;i<5;++i) corner->Update(.1f,observed+Vector3{static_cast<float>(i-4),0,0},&scene->world_);
    check(corner->AI().visible && corner->GetState()==EnemyState::Chase,"corner area cannot initially detect player");
    check(corner->AI().memory.count>=2 && corner->AI().memory.hasDirection && corner->AI().memory.escapeDirection.x>.9f,"lateral visible history did not infer east escape");
    const double lastVisibleTime=corner->AI().memory.samples[corner->AI().memory.count-1].time;
    bool investigated=false;
    for (int i=0;i<200 && corner->GetState()!=EnemyState::Search;++i) {
        corner->Update(.1f,hidden,&scene->world_);
        const auto& ai=corner->AI();
        check(!ai.visible && ai.occluded && StageLength(ai.lastSeen-observed)<.001f,"corner hiding leaked current player position");
        for (size_t j=0;j<ai.memory.count;++j)
            check(ai.memory.samples[j].time<=lastVisibleTime && ai.memory.samples[j].position.z==observed.z,"corner history recorded a hidden position");
        if (corner->GetState()==EnemyState::Investigate) investigated=true;
        check(ai.searchElapsed==0,"corner search clock advanced on approach");
    }
    check(investigated && corner->GetState()==EnemyState::Search && corner->AI().lastSeenReached,"corner pursuit did not reach last seen location");
    const auto searchStart=corner->GetPosition();
    corner->Update(.1f,hidden,&scene->world_);
    check(corner->GetState()==EnemyState::Search && corner->AI().moveTarget.x>searchStart.x+.5f &&
        EnemyNavigation::Clear(&scene->world_,corner->AI().moveTarget,corner->AI().moveTarget,corner->Definition().collisionRadius,corner->Definition().collisionHeight),"corner search did not prefer a reachable east escape goal");
    for (int i=0;i<100 && corner->GetState()==EnemyState::Search;++i) corner->Update(.1f,hidden,&scene->world_);
    check(corner->GetState()==EnemyState::Wander && corner->AI().searchPointsVisited>=2,"corner search failed to inspect multiple locations then wander");
    corner->Update(.1f,corner->GetPosition()+Vector3{0,0,-4},&scene->world_);
    check(corner->GetState()==EnemyState::Chase,"corner reappearance did not resume pursuit");
    const auto afterSearch=corner->AI().wanderCenter;
    check(StageLength(afterSearch-searchStart)>.1f,"Wander did not center on post-search position");
    corner->ResetAI(); corner->Senses()=corner->Definition().senses;
    check(corner->AI().memory.count==0 && !corner->AI().memory.hasDirection && corner->AI().memory.clock==0,"AI reset retained observed history");
    auto* enemy=scene->enemies_[0]; scene->selected_=0;
    const auto at=enemy->GetPosition();
    enemy->ResetAI(); enemy->Senses().viewDistance=0;
    enemy->Update(1,at+Vector3{0,0,5},&scene->world_);
    check(enemy->GetState()==EnemyState::Idle && !enemy->AI().hasLastSeen,"live distance adjustment ignored");
    enemy->Senses().viewDistance=100; enemy->Senses().fieldOfView=360; enemy->Senses().detectionTime=0;
    enemy->Update(.1f,at+Vector3{0,0,5},&scene->world_);
    check(enemy->GetState()==EnemyState::Chase && enemy->AI().hasLastSeen,"live detection adjustment ignored");
    enemy->SetAIPaused(true); const auto stopped=enemy->GetPosition();
    enemy->Update(1,at+Vector3{0,0,6},&scene->world_);
    check(StageLength(enemy->GetPosition()-stopped)==0 && enemy->PendingAttackCount()==0,"selected AI pause failed");
    const auto primeSearch=[&]() {
        enemy->Senses().detectionTime=0; enemy->Senses().fieldOfView=360;
        enemy->Senses().loseSightTime=0; // This fixture isolates arrival/reset, not the grace clock.
        enemy->SetAIPaused(false);
        enemy->Update(.1f,enemy->GetPosition()+Vector3{0,0,5},&scene->world_);
        enemy->SetPosition(enemy->AI().lastSeen);
        enemy->Update(.1f,{1000,0,1000},&scene->world_);
        check(enemy->GetState()==EnemyState::Search,"search lifecycle fixture did not arrive");
        enemy->Senses().searchSpeedMultiplier=.7f;
        enemy->Update(.1f,{1000,0,1000},&scene->world_);
        check(std::abs(enemy->AI().currentSpeed-enemy->Definition().moveSpeed*.7f)<.01f,"live search speed multiplier ignored");
        enemy->Senses().searchDuration=.05f;
        check(enemy->AI().SearchRemaining()==0,"live search duration ignored");
    };
    primeSearch();
    const auto beforeReset=enemy->GetPosition();
    enemy->ResetAI();
    check(!enemy->AI().hasLastSeen && !enemy->AI().paused && enemy->AI().navigation.path.empty() &&
        enemy->AI().searchElapsed==0 && enemy->AI().searchPointsVisited==0 && !enemy->AI().searchFinishing &&
        !enemy->AI().lastSeenReached && !enemy->AI().investigateUnreachable && enemy->AI().investigateBlockedTime==0 &&
        enemy->AI().memory.count==0 && !enemy->AI().memory.hasDirection && enemy->AI().memory.clock==0 &&
        StageLength(enemy->GetPosition()-beforeReset)==0,"AI reset teleported or retained search memory");
    enemy->Senses()=enemy->Definition().senses;
    primeSearch(); // Recycle a slot while its search clock/path/live tuning are dirty.
    enemy->Senses().memoryRetentionTime=.3f; enemy->Senses().memoryRecordInterval=0; enemy->Senses().searchDirectionPriority=.2f;
    const auto definition=enemy->Definition().id;
    const auto spawnId=enemy->GetSpawnId();
    scene->Remove(app,false); check(scene->pool_.Active()==7 && scene->enemies_.size()==7,"selected deletion failed");
    scene->definition_=definition;
    scene->Spawn(app,{-24,0,2});
    auto* reused=scene->enemies_.back();
    check(reused==enemy && reused->GetSpawnId()!=spawnId && !reused->AI().hasLastSeen &&
        reused->Senses().detectionTime==reused->Definition().senses.detectionTime && reused->AI().searchElapsed==0 &&
        reused->AI().searchPointsVisited==0 && reused->AI().SearchRemaining()==0 && !reused->AI().searchFinishing &&
        !reused->AI().investigateUnreachable && reused->AI().navigation.path.empty() &&
        reused->AI().memory.count==0 && !reused->AI().memory.hasDirection && reused->AI().memory.clock==0 &&
        reused->Senses().searchSpeedMultiplier==reused->Definition().senses.searchSpeedMultiplier &&
        reused->Senses().memoryRetentionTime==reused->Definition().senses.memoryRetentionTime &&
        reused->Senses().memoryRecordInterval==reused->Definition().senses.memoryRecordInterval &&
        reused->Senses().searchDirectionPriority==reused->Definition().senses.searchDirectionPriority,"pool reuse leaked AI search tuning/memory");
    scene->pauseAll_=true; frame();
    const auto frozen=reused->GetPosition(); frame();
    check(StageLength(frozen-reused->GetPosition())==0,"global pause moved enemy");
    scene->Remove(app,true); check(scene->pool_.Active()==0 && scene->enemies_.empty(),"delete all left active slots");
    for (const auto& [id,d]:scene->definitions_.All()) {
        (void)d; scene->definition_=id; scene->Spawn(app,{0,0,2});
        check(scene->enemies_.size()==1 && scene->enemies_[0]->Definition().id==id,"definition selector/spawn failed");
        scene->Remove(app,true);
    }
    scene->definition_="normal"; scene->Spawn(app,{24,0,-2});
    check(scene->enemies_.empty() && !scene->status_.empty(),"spawn inside wall accepted");
    transition("Title");
    check(dynamic_cast<TitleScene*>(app.Scenes().Current())!=nullptr && !app.GetInput()->IsCameraControlEnabled(),"return to title/input cleanup failed");
    std::ofstream("generated/enemy-ai-tests/scene-result.txt")<<"PASS: real D3D title fades/camera restoration, actual L-wall visible lateral history -> east escape estimate -> blind pursuit -> direction-prioritized reachable Search -> current-area Wander -> reacquire; no hidden samples/xray; live search tuning; pause/reset; same-slot pool reuse clears history/direction/search state; all definitions; wall spawn rejection.\n";
}
