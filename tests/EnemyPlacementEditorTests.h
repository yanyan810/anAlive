#pragma once
#include "GameApp.h"
#include "scene/Debug/EnemyPlacementEditorScene.h"
#include "ImGuiManagaer.h"
#include "DirectXTex.h"
#include <fstream>
#include "CameraPathEditorTests.h"

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
    const auto captureEditor=[&](const wchar_t* path) {
        frame(); auto* dx=app.Dx(); dx->WaitForGPU();
        auto* resource=dx->swapChainResources[1-dx->swapChain->GetCurrentBackBufferIndex()].Get();
        DirectX::ScratchImage pixels;
        check(SUCCEEDED(DirectX::CaptureTexture(dx->GetCommandQueue(),resource,false,pixels,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT)),"F7 full UI capture failed");
        check(SUCCEEDED(DirectX::SaveToWICFile(*pixels.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path)),"F7 UI PNG failed");
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
    // Player Visual shares the production intro renderer, but owns its own draft
    // and save operation. Keep a dirty enemy edit while saving a stage copy.
    const auto editorPlayer=editor->player_.GetTransform();
    const auto cameraBefore=editor->camera_.GetTransform();
    const auto floorBefore=editor->floor_;
    check(editor->playerVisual_ && editor->playerVisualSettings_.yawOffsetDegrees==90,"Player Visual stage settings missing");
    editor->FocusPlayerVisual(); editor->showPlayerVisual_=true;
    editor->playerVisualSettings_.yawOffsetDegrees=0; editor->SyncPlayerVisual();
    const float playerYaw=editor->level_.playerRotation.y;
    const Vector3 fpsForward{std::sin(playerYaw),0,std::cos(playerYaw)};
    check(std::abs(StageDot(StartPlayerVisual::Forward(playerYaw,editor->playerVisualSettings_),fpsForward))<.0001f,"Normal native forward is not perpendicular before correction");
    capture(L"generated/placement-tests/player-visual-yaw-0.png");
    captureEditor(L"generated/placement-tests/player-visual-editor-yaw-0.png");
    const auto viewingCamera=editor->camera_.GetTransform();
    editor->playerVisualSettings_.yawOffsetDegrees=90; editor->SyncPlayerVisual();
    check(StageDot(StartPlayerVisual::Forward(playerYaw,editor->playerVisualSettings_),fpsForward)>.9999f,"+90 degree Normal correction does not align with FPS forward");
    check(std::abs(editor->playerVisual_->Rotation().y-(playerYaw+1.57079632679f))<.0001f,"preview renderer did not receive degree offset");
    capture(L"generated/placement-tests/player-visual-yaw-90.png");
    captureEditor(L"generated/placement-tests/player-visual-editor-yaw-90.png");
    check(EnemyPlacements::ReadBytes("generated/placement-tests/player-visual-yaw-0.png")!=EnemyPlacements::ReadBytes("generated/placement-tests/player-visual-yaw-90.png"),"Yaw did not change actual preview pixels");
    editor->showPlayerVisual_=false; capture(L"generated/placement-tests/player-visual-hidden.png");
    check(EnemyPlacements::ReadBytes("generated/placement-tests/player-visual-hidden.png")!=EnemyPlacements::ReadBytes("generated/placement-tests/player-visual-yaw-90.png"),"Player Visual visibility did not change actual pixels");
    editor->showPlayerVisual_=true;
    editor->playerVisualSettings_.yawOffsetDegrees=45; editor->SyncPlayerVisual();
    check(editor->PlayerVisualDirty() && editor->PlacementsDirty(),"Player Visual and placement dirty state are not independent");
    const std::string visualFixture="generated/placement-tests/player-visual-stage.json";
    { std::ofstream file(visualFixture,std::ios::binary); file<<stageBytes; }
    check(editor->SavePlayerVisual(visualFixture),"Player Visual-only save failed");
    check(!editor->PlayerVisualDirty() && editor->PlacementsDirty() && editor->placements_.Json()==edited,"Player Visual save changed/marked enemy placements as saved");
    auto savedStage=nlohmann::json::parse(PlayerVisualSettings::ReadBytes(visualFixture));
    auto oldStage=nlohmann::json::parse(stageBytes);
    check(savedStage["playerVisual"]["yawOffsetDegrees"]==45,"Saved Player Visual offset missing");
    savedStage.erase("playerVisual"); oldStage.erase("playerVisual"); check(savedStage==oldStage,"Player Visual save modified other stage fields/camera path");
    editor->playerVisualSettings_.yawOffsetDegrees=-90;
    editor->LoadPlayerVisual(visualFixture); editor->SyncPlayerVisual();
    check(editor->playerVisualSettings_.yawOffsetDegrees==45 && !editor->PlayerVisualDirty(),"F7 Player Visual reload did not restore saved yaw");
    StageLoader visualReload; check(visualReload.Load(visualFixture) && visualReload.playerVisual.yawOffsetDegrees==45,"runtime loader differs from editor reload");
    check(StageLength(editor->player_.GetTransform().rotate-editorPlayer.rotate)==0 && StageLength(editor->player_.GetTransform().translate-editorPlayer.translate)==0,"Visual editing changed Player/FPS pose");
    check(StageLength(editor->camera_.GetRotate()-viewingCamera.rotate)==0,"Yaw editing changed the viewing camera rotation");
    check(EnemyPlacements::ReadBytes(editor->PlacementPath())==sidecarBytes,"Player Visual save changed enemy placement file");
    editor->LoadPlayerVisual(editor->StagePath()); editor->SyncPlayerVisual();
    capture(L"generated/placement-tests/player-visual-reloaded.png");
    check(EnemyPlacements::ReadBytes("generated/placement-tests/player-visual-reloaded.png")==EnemyPlacements::ReadBytes("generated/placement-tests/player-visual-yaw-90.png"),"reloaded Player Visual pose/pixels differ from +90 degree preview");
    // Read back the complete real F7 panel, viewport and projected direction arrows.
    captureEditor(L"generated/placement-tests/player-visual-editor.png");
    editor->camera_.SetTranslate(cameraBefore.translate); editor->camera_.SetRotate(cameraBefore.rotate);
    editor->floor_=floorBefore; editor->ApplyVisibility(); editor->camera_.Update();
    const auto editedPosition=EnemyPlacements::WorldPosition(*selected,spawn->position,spawn->rotation);
    // Exercise the existing F7 scene's camera tools with the real viewport and renderer.
    editor->SetCameraSequenceMode(true);
    check(editor->SelectedCameraSequence() && editor->selectedCameraSequence_=="start","camera mode did not select start");
    const auto cameraOriginal=CameraSequence::ToJson(editor->cameraDrafts_);
    editor->selectedCameraPoint_=editor->SelectedCameraSequence()->points[2].id;
    const auto count=editor->SelectedCameraSequence()->points.size();
    editor->AddCameraPoint(true); check(editor->SelectedCameraSequence()->points.size()==count+1,"camera duplicate failed");
    const auto duplicate=editor->selectedCameraPoint_;
    editor->MoveCameraPoint(-1); check(editor->SelectedCameraSequence()->points[2].id==duplicate,"camera reorder failed");
    editor->DeleteCameraPoint(); check(editor->SelectedCameraSequence()->points.size()==count,"camera delete failed");
    editor->UndoCameraSequence(false); check(editor->SelectedCameraSequence()->points.size()==count+1,"camera undo failed");
    editor->UndoCameraSequence(true); check(editor->SelectedCameraSequence()->points.size()==count,"camera redo failed");
    check(CameraSequence::ToJson(editor->cameraDrafts_)==cameraOriginal,"structural round trip changed route");
    editor->selectedCameraPoint_=editor->SelectedCameraSequence()->points[2].id;
    editor->AddCameraPoint(false); check(editor->SelectedCameraSequence()->points.size()==count+1,"add from view failed");
    editor->DeleteCameraPoint();
    editor->selectedCameraPoint_=editor->SelectedCameraSequence()->points[2].id;
    editor->SwitchCamera(EnemyPlacementEditorScene::CameraMode::Top);
    editor->camera_.SetTranslate({33,40,20}); editor->zoom_=12; editor->camera_.SetOrthographic(true,12); editor->camera_.Update();
    app.GetInput()->SetCameraControlEnabled(false); frame();
    RECT cameraRect{}; check(app.ImGui()->GetSceneImageRect(cameraRect),"camera viewport missing");
    const auto pixel=[&](Vector3 position) {
        const auto vp=editor->camera_.GetViewProjectionMatrix(); const auto clip=StagePoint(position,vp);
        const float w=position.x*vp.m[0][3]+position.y*vp.m[1][3]+position.z*vp.m[2][3]+vp.m[3][3];
        return ImVec2{cameraRect.left+(clip.x/w+1)*.5f*(cameraRect.right-cameraRect.left),cameraRect.top+(1-clip.y/w)*.5f*(cameraRect.bottom-cameraRect.top)};
    };
    const auto mouseFrame=[&](ImVec2 position,bool down) {
        auto& io=ImGui::GetIO(); io.AddMousePosEvent(position.x,position.y); io.AddMouseButtonEvent(0,down); frame();
    };
    const auto beforeMove=editor->SelectedCameraPoint()->position;
    const auto handle=pixel(beforeMove+Vector3{1,0,0});
    mouseFrame(handle,false); mouseFrame(handle,false); mouseFrame(handle,true);
    mouseFrame({handle.x+25,handle.y},true); mouseFrame({handle.x+25,handle.y},false);
    check(editor->SelectedCameraPoint()->position.x>beforeMove.x+.05f,"real viewport XYZ mouse drag failed");
    const auto moved=editor->SelectedCameraPoint()->position;
    editor->cameraGizmo_=1; editor->cameraRotationAxis_=1;
    const auto center=pixel(moved); const auto beforeYaw=editor->SelectedCameraPoint()->rotation.y;
    mouseFrame({center.x+55,center.y},false); mouseFrame({center.x+55,center.y},true);
    mouseFrame({center.x+50,center.y+23},true); mouseFrame({center.x+50,center.y+23},false);
    check(std::abs(editor->SelectedCameraPoint()->rotation.y-beforeYaw)>.05f,"real viewport rotation ring drag failed");
    check(editor->SelectedCameraPoint()->rotationMode=="Manual","rotation ring did not switch to Manual");
    editor->cameraGizmo_=0;
    // Marker picking switches the same selection used by the left point inspector.
    const auto pickId=editor->SelectedCameraSequence()->points[4].id;
    const auto marker=pixel(editor->CameraPointPose(4).position);
    mouseFrame(marker,false); mouseFrame(marker,true); mouseFrame(marker,false);
    check(editor->selectedCameraPoint_==pickId,"viewport point marker picking failed");
    editor->selectedCameraPoint_=editor->SelectedCameraSequence()->points[2].id;
    editor->CommitCameraHistory();
    captureEditor(L"generated/placement-tests/camera-editor-route.png");
    CameraSequence reference; std::string validation;
    check(editor->BuildCameraEvaluator(reference,validation),"edited route validation failed");
    const auto beforePreview=editor->camera_; const auto beforeMask=editor->environment_.GetHiddenMeshes();
    const auto beforeDraft=CameraSequence::ToJson(editor->cameraDrafts_);
    check(editor->StartCameraPreview(true),"camera preview play failed");
    editor->UpdateCameraPreview(.5); check(editor->cameraPreviewTime_==.5,"preview play did not advance");
    editor->HandleCameraAction("cameraPause"); editor->UpdateCameraPreview(1);
    check(editor->cameraPreviewTime_==.5,"paused preview advanced");
    editor->HandleCameraAction("cameraPlay"); editor->UpdateCameraPreview(.25);
    check(editor->cameraPreviewTime_==.75,"preview resume failed");
    editor->SeekCameraPreview(2.8); const auto expected=reference.Evaluate(2.8);
    check(StageLength(editor->camera_.GetTranslate()-expected.position)<.00001f && StageLength(editor->camera_.GetRotate()-expected.rotation)<.00001f && std::abs(editor->camera_.GetFovY()-expected.fov)<.00001f,"preview differs from production evaluator");
    captureEditor(L"generated/placement-tests/camera-editor-preview.png");
    editor->HandleCameraAction("cameraFinal"); const auto eye=editor->CameraEyePose();
    check(StageLength(editor->camera_.GetTranslate()-eye.position)<.00001f && StageLength(editor->camera_.GetRotate()-eye.rotation)<.00001f,"final FPS view differs from PlayerEye");
    editor->HandleCameraAction("cameraRestart"); check(editor->cameraPreviewTime_==0 && editor->cameraPreviewPlaying_,"preview restart failed");
    editor->StopCameraPreview();
    check(!editor->cameraPreviewActive_ && editor->mode_==EnemyPlacementEditorScene::CameraMode::Top,"preview stop failed");
    const auto projection=beforePreview.GetProjectionMatrix(),restoredProjection=editor->camera_.GetProjectionMatrix();
    check(StageLength(editor->camera_.GetTranslate()-beforePreview.GetTranslate())==0 && StageLength(editor->camera_.GetRotate()-beforePreview.GetRotate())==0 && std::memcmp(&projection,&restoredProjection,sizeof(projection))==0,"stop did not restore full editing camera/projection");
    check(editor->environment_.GetHiddenMeshes()==beforeMask && CameraSequence::ToJson(editor->cameraDrafts_)==beforeDraft && !editor->test_,"preview changed stage draft/visibility or started gameplay");
    const std::string cameraFixture="generated/placement-tests/camera-editor-stage03.json";
    const auto cameraSidecar=CameraSequenceOverrides::PathFor(cameraFixture);
    std::filesystem::remove(cameraSidecar);
    { std::ofstream file(cameraFixture,std::ios::binary); file<<stageBytes; }
    editor->LoadCameraSequences(cameraFixture);
    editor->selectedCameraPoint_=editor->SelectedCameraSequence()->points[2].id;
    editor->SelectedCameraPoint()->position=moved; editor->SelectedCameraPoint()->fov=72;
    editor->SelectedCameraPoint()->rotation={.13f,.9f,-.08f};
    editor->SelectedCameraPoint()->rotationMode="LookAtTarget";
    editor->SelectedCameraPoint()->target=editor->level_.playerPosition+Vector3{0,1,0};
    check(editor->SaveCameraSequence(cameraFixture),"F7 camera-only save failed");
    const auto savedCamera=CameraSequence::ToJson(editor->cameraDrafts_);
    check(!editor->CameraSequencesDirty() && editor->PlacementsDirty() && editor->placements_.Json()==edited,"camera save contaminated enemy dirty state");
    check(CameraSequenceOverrides::Read(cameraFixture)==stageBytes,"camera save wrote stage JSON");
    editor->SelectedCameraPoint()->position.x+=100; editor->LoadCameraSequences(cameraFixture);
    check(CameraSequence::ToJson(editor->cameraDrafts_)==savedCamera,"F7 camera reload lost edited pose");
    StageLoader cameraRuntime; check(cameraRuntime.Load(cameraFixture) && CameraSequence::ToJson(cameraRuntime.cameraSequences)==savedCamera,"game loader did not consume F7 override");
    check(editor->RevertCameraSequence(cameraFixture) && CameraSequence::ToJson(editor->cameraDrafts_)==cameraOriginal,"F7 override removal did not restore Blender base");
    check(editor->PlacementsDirty() && editor->placements_.Json()==edited && EnemyPlacements::ReadBytes(editor->PlacementPath())==sidecarBytes,"camera revert changed enemy edits/file");
    editor->LoadCameraSequences(editor->StagePath()); editor->SetCameraSequenceMode(false);
    editor->camera_=beforePreview; editor->SwitchCamera(EnemyPlacementEditorScene::CameraMode::Free);
    check(StageLength(editor->player_.GetTransform().translate-editorPlayer.translate)==0 && StageLength(editor->player_.GetTransform().rotate-editorPlayer.rotate)==0,"camera tools moved FPS player");
    std::ofstream("generated/placement-tests/camera-editor-result.txt")<<"PASS: existing F7 mode, point add/delete/duplicate/reorder, Undo/Redo, injected mouse XYZ translation and rotation ring, marker picking, evaluated curve capture, preview play/pause/resume/seek/restart/final FPS, exact camera/projection and visibility restoration, immutable draft/player/gameplay, camera-only save/reload with dirty enemies retained, runtime loader precedence and Blender revert.\n";
    editor->StartTest(app); check(editor->test_ && editor->test_->stageLoaded_,"placement test GameScene failed to load");
    auto& test=*editor->test_;
    test.initialCapturePending_=false; app.GetInput()->SetCameraControlEnabled(false);
    bool matched=false;
    for (auto* enemy:test.enemies_) if (enemy->GetSpawnTriggerId()==editedSpawn && StageLength(enemy->GetPosition()-editedPosition)<.0001f) {
        matched=true; check(std::abs(enemy->GetRotation().y-(spawn->rotation.y+selected->yaw))<.0001f,"runtime rotation differs from preview");
        enemy->SetPosition(enemy->GetPosition()+Vector3{4,0,4});
    }
    check(matched,"unsaved individual placement was not consumed by production StageStart spawn");
    // Feed the file-loaded F7 definition into the ordinary GameScene intro entry.
    const auto placementTest=test.placementTest_;
    const auto originalSequences=test.level_.cameraSequences;
    test.placementTest_.reset(); test.level_.cameraSequences=cameraRuntime.cameraSequences;
    test.BeginStartSequence(app);
    check(test.sequenceOwnsCamera_ && CameraSequence::ToJson({test.startSequence_.Definition()})==savedCamera,"normal intro did not consume loaded F7 camera definition");
    test.FinishStartSequence(app); test.placementTest_=placementTest; test.level_.cameraSequences=originalSequences;
    for (int i=0;i<10;++i) { test.player_.ResetHPForDebug(); test.Update(app,1.f/60); }
    capture(L"generated/placement-tests/gameplay.png");
    editor->StopTest(app);
    check(!editor->test_ && editor->placements_.Json()==edited,"test movement contaminated editing data");
    for (const auto& [_,preview]:editor->previews_) check(preview.enemy->AI().paused && !preview.enemy->AI().hasLastSeen,"return from test retained AI history");
    check(editor->Dirty(),"return from test lost unsaved edit");
    RunCameraPathEditorTests(app,captureEditor);
    editor->RequestAction("title"); check(editor->discardModal_ && editor->pendingAction_.empty(),"unsaved exit did not request confirmation");
    editor->discardModal_=false; editor->confirmAction_.clear();
    check(EnemyPlacements::ReadBytes(editor->StagePath())==stageBytes && EnemyPlacements::ReadBytes(editor->PlacementPath())==sidecarBytes,"editor test wrote exported stage or sidecar");
    // Reload discards the draft and rebuilds stopped previews; other stages use the same path.
    editor->LoadStage(app,"stage03"); check(editor->placements_.Json()==original,"reload failed to restore editing baseline");
    for (const auto* id:{"stage01","stage02"}) { editor->LoadStage(app,id); check(editor->stageId_==id && editor->ready_ && editor->cameraDrafts_.size()==1,"other stage editor/camera load failed"); frame(); }
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
    std::ofstream("generated/placement-tests/result.txt")<<"PASS: F7 Camera Sequence editing, injected viewport move/rotate/pick, shared evaluator preview, full Stop restoration, isolated camera override save/reload/revert, Undo/Redo; Player Visual +90 Normal correction, yaw/visibility pixels, independent visual save/reload, immutable FPS pose; Stage01-03 load, floor/roof mask isolation, immutable collision, static previews, unsaved production GameScene/Pool spawns, AI test updates, unsaved warning, no source stage/sidecar writes. Physical keyboard/mouse usability remains unverified.\n";
}
