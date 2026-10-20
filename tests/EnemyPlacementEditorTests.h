#pragma once
#include "GameApp.h"
#include "scene/Debug/EnemyPlacementEditorScene.h"
#include "ImGuiManagaer.h"
#include "DirectXTex.h"
#include <fstream>

inline void RunEnemyPlacementEditorTests(GameApp& app) {
    const auto check=[](bool value,const char* message) { if (!value) throw std::runtime_error(message); };
    const auto frame=[&]() {
        app.ImGui()->Begin(); app.Update(0,0); app.Draw();
    };
    const auto transition=[&](const char* scene) {
        check(app.Scenes().TransitionTo(scene,.05f,.05f),"fade transition rejected");
        for (int i=0;i<60 && app.Scenes().IsTransitioning();++i) {
            app.ImGui()->Begin(); app.Update(1.f/60,1.f/60); app.Draw();
        }
        check(app.Scenes().CurrentName()==scene && !app.Scenes().IsTransitioning(),"fade transition did not complete");
    };
    transition("EnemyPlacementEditor");
    auto* editor=dynamic_cast<EnemyPlacementEditorScene*>(app.Scenes().Current());
    check(editor && editor->ready_ && !editor->loadError_,"Stage03 editor initialization failed");
    check(editor->metadata_.floors==std::vector<float>({0,6,12}),"Stage03 floor metadata missing");
    const auto original=editor->placements_.Json();
    const auto stageBytes=EnemyPlacements::ReadBytes(editor->StagePath());
    const auto sidecarBytes=EnemyPlacements::ReadBytes(editor->PlacementPath());
    const auto colliders=editor->level_.collision.colliders;
    const auto capture=[&](const wchar_t* path) {
        frame(); auto* dx=app.Dx();
        dx->PreDraw(false); app.Render()->GetOffscreen()->BeginForPostEffect(); app.Render()->GetOffscreen()->End();
        dx->SetBackBufferRenderTarget(); dx->PostDraw(); dx->WaitForGPU();
        DirectX::ScratchImage pixels;
        const auto state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        check(SUCCEEDED(DirectX::CaptureTexture(dx->GetCommandQueue(),app.Render()->GetOffscreen()->GetResource(),false,pixels,state,state)),"editor GPU capture failed");
        check(SUCCEEDED(DirectX::SaveToWICFile(*pixels.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path)),"editor PNG write failed");
    };
    capture(L"generated/placement-tests/free.png");
    editor->SwitchCamera(EnemyPlacementEditorScene::CameraMode::Top);
    editor->camera_.SetTranslate({19,40,11}); editor->zoom_=30; editor->camera_.SetOrthographic(true,30);
    editor->floor_=3; editor->ApplyVisibility(); capture(L"generated/placement-tests/top-3f.png");
    const auto hidden=editor->environment_.GetHiddenMeshes(); check(!hidden.empty(),"roof/floor render mask is empty");
    auto* shared=editor->environment_.GetModel();
    Object3d normal; normal.Initialize(app.ObjCom(),app.Dx()); normal.SetCamera(&editor->camera_); normal.SetModel(shared);
    check(normal.GetHiddenMeshes().empty(),"editor mask leaked to another object sharing the model");
    for (int floor=1;floor<=3;++floor) {
        editor->floor_=floor; editor->ApplyVisibility(); frame();
        check(editor->level_.collision.colliders.size()==colliders.size(),"floor hiding modified collision count");
        for (size_t i=0;i<colliders.size();++i)
            check(editor->level_.collision.colliders[i].id==colliders[i].id &&
                StageLength(editor->level_.collision.colliders[i].position-colliders[i].position)==0,"render filter modified stage collider");
    }
    editor->floor_=3; editor->showRoof_=editor->showCeiling_=editor->showWalls_=editor->showLower_=editor->showUpper_=true;
    editor->ApplyVisibility(); capture(L"generated/placement-tests/top-all-geometry.png");
    check(EnemyPlacements::ReadBytes("generated/placement-tests/top-3f.png")!=EnemyPlacements::ReadBytes("generated/placement-tests/top-all-geometry.png"),"roof/floor visibility did not change rendered pixels");
    editor->showRoof_=editor->showCeiling_=editor->showUpper_=editor->showLower_=false; editor->showWalls_=true; editor->ApplyVisibility();
    editor->SwitchCamera(EnemyPlacementEditorScene::CameraMode::FPS); capture(L"generated/placement-tests/fps.png");
    editor->SwitchCamera(EnemyPlacementEditorScene::CameraMode::Free);
    // Dedicated static previews never consume a gameplay pool and never advance AI.
    check(!editor->test_,"preview unexpectedly created GameScene");
    size_t total=0;
    for (const auto& [_,group]:editor->placements_.groups) total+=group.enemies.size();
    check(editor->previews_.size()==total,"preview instances do not match individual placements");
    for (const auto& [_,preview]:editor->previews_) check(preview.enemy->AI().paused && !preview.enemy->AI().hasLastSeen,"preview AI is active");
    std::string editedSpawn,editedId;
    for (auto& [id,g]:editor->placements_.groups) {
        const auto* point=editor->spawns_.FindPoint(id);
        if (!point || point->activation!=SpawnActivation::StageStart || g.enemies.empty()) continue;
        editedSpawn=id; editedId=g.enemies.front().id;
        auto& p=g.enemies.front(); p.enabled=true; p.offset.x+=.15f; p.yaw+=.37f;
        editor->selectedSpawn_=id; editor->selectedId_=p.id;
        break;
    }
    check(!editedId.empty() && editor->Dirty(),"no editable StageStart placement");
    editor->SyncPreviews(app);
    auto* selected=editor->SelectedEnemy(); const auto* spawn=editor->SelectedSpawn();
    check(StageLength(editor->previews_.at(editedId).enemy->GetPosition()-EnemyPlacements::WorldPosition(*selected,spawn->position,spawn->rotation))<.0001f,"preview position differs from saved transform convention");
    const auto edited=editor->placements_.Json();
    const auto editedPosition=EnemyPlacements::WorldPosition(*selected,spawn->position,spawn->rotation);
    editor->StartTest(app); check(editor->test_ && editor->test_->stageLoaded_,"placement test GameScene failed to load");
    auto& test=*editor->test_;
    test.initialCapturePending_=false; app.GetInput()->SetCameraControlEnabled(false);
    bool matched=false;
    for (auto* enemy:test.enemies_) if (enemy->GetSpawnTriggerId()==editedSpawn && StageLength(enemy->GetPosition()-editedPosition)<.0001f) {
        matched=true; check(std::abs(enemy->GetRotation().y-(spawn->rotation.y+selected->yaw))<.0001f,"runtime rotation differs from preview");
        enemy->SetPosition(enemy->GetPosition()+Vector3{4,0,4});
    }
    check(matched,"unsaved individual placement was not consumed by production StageStart spawn");
    for (int i=0;i<10;++i) { test.player_.ResetHPForDebug(); test.Update(app,1.f/60); }
    capture(L"generated/placement-tests/gameplay.png");
    editor->StopTest(app);
    check(!editor->test_ && editor->placements_.Json()==edited,"test movement contaminated editing data");
    for (const auto& [_,preview]:editor->previews_) check(preview.enemy->AI().paused && !preview.enemy->AI().hasLastSeen,"return from test retained AI history");
    check(editor->Dirty(),"return from test lost unsaved edit");
    editor->RequestAction("title"); check(editor->discardModal_ && editor->pendingAction_.empty(),"unsaved exit did not request confirmation");
    editor->discardModal_=false; editor->confirmAction_.clear();
    check(EnemyPlacements::ReadBytes(editor->StagePath())==stageBytes && EnemyPlacements::ReadBytes(editor->PlacementPath())==sidecarBytes,"editor test wrote exported stage or sidecar");
    // Reload discards the draft and rebuilds stopped previews; other stages use the same path.
    editor->LoadStage(app,"stage03"); check(editor->placements_.Json()==original,"reload failed to restore editing baseline");
    for (const auto* id:{"stage01","stage02"}) { editor->LoadStage(app,id); check(editor->stageId_==id && editor->ready_,"other stage editor failed"); frame(); }
    // Regression for the reported SrvManager.cpp:27 overflow: repeatedly rebuild
    // previews and acquire/retire real GameScene pools in the same process.
    editor->LoadStage(app,"stage03"); frame(); editor->StartTest(app); frame(); editor->StopTest(app); frame();
    const auto warmActive=app.Srv()->ActiveCount();
    const auto warmHigh=app.Srv()->HighWaterMark();
    for (int cycle=0;cycle<8;++cycle) {
        const auto before=app.Srv()->ActiveCount();
        editor->LoadStage(app,"stage03"); frame();
        check(app.Srv()->ActiveCount()==before,"preview reload leaked SRV descriptors");
        editor->StartTest(app); frame(); editor->StopTest(app); frame();
        check(app.Srv()->ActiveCount()==warmActive,"placement test cycle leaked SRV descriptors");
        check(app.Srv()->HighWaterMark()<=warmHigh,"placement test descriptors were not reused");
    }
    std::ofstream("generated/placement-tests/descriptors.txt")<<"8 reload/test cycles: active="<<warmActive<<", high-water="<<warmHigh<<", heap capacity=512; counts stable.\n";
    transition("Title");
    app.Dx()->WaitForGPU();
    std::ofstream("generated/placement-tests/result.txt")<<"PASS: title fades, Stage01-03 load, all camera render paths, orthographic floors/roof/walls mask isolation, immutable collision, static shared-asset previews, unsaved production GameScene/Pool spawn position and yaw, AI test updates, restore without movement/history contamination, unsaved warning, no stage/sidecar writes. Manual mouse/keyboard/gizmo usability remains unverified.\n";
}
