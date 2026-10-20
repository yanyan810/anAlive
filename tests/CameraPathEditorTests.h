#pragma once
#include "GameApp.h"
#include "scene/Debug/EnemyPlacementEditorScene.h"
#include "ImGuiManagaer.h"
#include <fstream>

inline void RunCameraPathEditorTests(GameApp& app,const std::function<void(const wchar_t*)>& capture) {
    const auto check=[](bool ok,const char* message) { if (!ok) throw std::runtime_error(message); };
    auto* editor=dynamic_cast<EnemyPlacementEditorScene*>(app.Scenes().Current()); check(editor && editor->ready_,"path editor not active");
    const auto frame=[&] { app.ImGui()->Begin(); app.Update(0,0); app.Draw(); };
    const auto oldCamera=editor->camera_; const auto oldMode=editor->mode_; const auto oldDraft=editor->cameraDrafts_;
    const auto originalStage=CameraSequenceOverrides::Read(editor->StagePath()),originalEnemies=CameraSequenceOverrides::Read(editor->PlacementPath());
    const auto oldPlacements=editor->placements_.Json(); const auto oldVisual=editor->playerVisualSettings_.yawOffsetDegrees;
    editor->SetCameraSequenceMode(true); editor->SetPathTool(EnemyPlacementEditorScene::PathTool::Draw);
    editor->drawHeight_=14; editor->zoom_=18; editor->camera_.SetOrthographic(true,18); editor->camera_.SetTranslate({31,42,17}); editor->camera_.Update(); frame();
    const auto pixel=[&](Vector3 position) {
        RECT rect{}; check(app.ImGui()->GetSceneImageRect(rect),"path viewport missing");
        const auto vp=editor->camera_.GetViewProjectionMatrix(); const auto p=StagePoint(position,vp);
        const float w=position.x*vp.m[0][3]+position.y*vp.m[1][3]+position.z*vp.m[2][3]+vp.m[3][3];
        return ImVec2{rect.left+(p.x/w+1)*.5f*(rect.right-rect.left),rect.top+(1-p.y/w)*.5f*(rect.bottom-rect.top)};
    };
    const auto mouse=[&](ImVec2 p,bool down) { auto& io=ImGui::GetIO(); io.AddMousePosEvent(p.x,p.y); io.AddMouseButtonEvent(0,down); frame(); };
    const auto click=[&](ImVec2 p) { mouse(p,false); mouse(p,true); mouse(p,false); };
    const auto key=[&](ImGuiKey k) { ImGui::GetIO().AddKeyEvent(k,true); frame(); ImGui::GetIO().AddKeyEvent(k,false); frame(); };
    // Regression: moving the second point breaks the initial Hold's identical-pose
    // constraint. That must block playback/save, without hiding the editable path.
    editor->SetPathTool(EnemyPlacementEditorScene::PathTool::Camera);
    editor->selectedCameraPoint_=editor->SelectedCameraSequence()->points[1].id;
    editor->cameraGizmo_=0; editor->selectedCameraHandle_=-1;
    const auto holdStart=editor->SelectedCameraSequence()->points[0].position;
    const auto moveHandle=pixel(editor->SelectedCameraPoint()->position+Vector3{1,0,0});
    mouse(moveHandle,false); mouse(moveHandle,true); mouse({moveHandle.x+25,moveHandle.y},true); mouse({moveHandle.x+25,moveHandle.y},false);
    check(StageLength(editor->SelectedCameraPoint()->position-holdStart)>.1f,"Hold endpoint drag did not move independently");
    CameraSequence invalid,guide; std::string holdError;
    check(!editor->BuildCameraEvaluator(invalid,holdError) && holdError.find("Hold")!=std::string::npos,"broken Hold was accepted for playback");
    const auto invalidDraft=CameraSequence::ToJson(editor->cameraDrafts_);
    check(editor->BuildCameraGeometry(guide),"invalid Hold hid the spatial guide");
    const auto expectedMiddle=(holdStart+editor->SelectedCameraPoint()->position)*.5f;
    check(StageLength(guide.SegmentPosition(0,.5f)-expectedMiddle)<.0001f,"moved Hold points are not connected by the draft guide");
    const auto revision=guide.ArcCacheRevision();
    capture(L"generated/placement-tests/camera-path-moved-hold.png");
    size_t guideVertices=0;
    for (const auto* list:ImGui::GetDrawData()->CmdLists) for (const auto& vertex:list->VtxBuffer)
        if (vertex.col==IM_COL32(255,155,70,255)) ++guideVertices;
    check(guideVertices>200,"invalid Hold path curves were not submitted to the actual viewport");
    check(editor->BuildCameraGeometry(guide) && guide.ArcCacheRevision()==revision,"draft geometry rebuilt while idle");
    check(!editor->StartCameraPreview(false) && !editor->SaveCameraSequence("generated/placement-tests/invalid-hold-stage03.json"),"display fallback enabled invalid preview/save");
    check(CameraSequence::ToJson(editor->cameraDrafts_)==invalidDraft && editor->SelectedCameraSequence()->points[0].interpolation=="Hold","display fallback rewrote author data");
    editor->LoadCameraSequences(editor->StagePath()); editor->cameraDrafts_=oldDraft; editor->EnsureCameraSelection();
    editor->SetPathTool(EnemyPlacementEditorScene::PathTool::Draw);
    editor->drawHeight_=14;
    const auto beforeDraw=CameraSequence::ToJson(editor->cameraDrafts_);
    click(pixel({29,14,16})); check(editor->drawPathPoints_.size()==1,"Top click did not add a draw point");
    key(ImGuiKey_Escape); check(editor->drawPathPoints_.empty() && CameraSequence::ToJson(editor->cameraDrafts_)==beforeDraw,"Draw cancel changed existing path");
    for (auto p:{Vector3{29,14,16},Vector3{31,14,18},Vector3{34,14,17}}) click(pixel(p));
    check(editor->drawPathPoints_.size()==3,"Top draw clicks failed");
    key(ImGuiKey_Backspace); check(editor->drawPathPoints_.size()==2,"draw Backspace failed");
    click(pixel({34,14,17})); key(ImGuiKey_Enter);
    check(editor->drawPathPoints_.empty() && editor->SelectedCameraSequence()->points.size()==4 && editor->SelectedCameraSequence()->points.back().playerEye,"Draw Enter/PlayerEye failed");
    check(editor->SelectedCameraSequence()->points[1].pathType=="Linear","draw initial segment is not Linear");
    editor->UndoCameraSequence(false); check(CameraSequence::ToJson(editor->cameraDrafts_)==beforeDraw,"draw did not undo as one edit");
    editor->UndoCameraSequence(true); check(editor->SelectedCameraSequence()->points.size()==4,"draw redo failed");
    const auto b=editor->SelectedCameraSequence()->points[1].position;
    click(pixel(b)); const auto historyBefore=editor->cameraHistoryIndex_;
    auto center=pixel(b); mouse(center,true); mouse({center.x+10,center.y-5},true); mouse({center.x+20,center.y-10},true); mouse({center.x+20,center.y-10},false);
    check(editor->SelectedCameraPoint()->position.y==14 && editor->SelectedCameraPoint()->position.x>b.x,"Top planar point drag failed");
    check(editor->cameraHistoryIndex_==historyBefore+1,"one point drag created multiple Undo records");
    const auto moved=editor->SelectedCameraPoint()->position; editor->UndoCameraSequence(false);
    check(StageLength(editor->SelectedCameraSequence()->points[1].position-b)<.0001f,"point drag Undo failed"); editor->UndoCameraSequence(true);
    check(StageLength(editor->SelectedCameraSequence()->points[1].position-moved)<.0001f,"point drag Redo failed");
    auto* d=editor->SelectedCameraSequence(); const auto midpoint=d->points[0].position+(d->points[1].position-d->points[0].position)*.4f;
    click(pixel(midpoint)); check(editor->selectedCameraSegment_==0 && std::abs(editor->selectedSegmentParameter_-.4f)<.015f,"nearest projected Linear segment selection failed");
    check(editor->InsertCameraPoint() && editor->SelectedCameraSequence()->points.size()==5,"Linear insert failed");
    editor->selectedCameraSegment_=2; editor->selectedCameraPoint_=editor->SelectedCameraSequence()->points[2].id;
    editor->SetCameraPathType("Bezier"); editor->SetCameraHandleType("Aligned");
    CameraSequence evaluator; std::string error; check(editor->BuildCameraEvaluator(evaluator,error),"mixed path failed validation");
    const auto handle=evaluator.PointPosition(2)+evaluator.HandleOffset(2,true); const auto handlePixel=pixel(handle);
    const auto beforeHandle=editor->SelectedCameraPoint()->handleOut;
    mouse(handlePixel,false); mouse(handlePixel,true); mouse({handlePixel.x+10,handlePixel.y-18},true); mouse({handlePixel.x+20,handlePixel.y-28},true); mouse({handlePixel.x+20,handlePixel.y-28},false);
    check(editor->selectedCameraHandle_==1 && StageLength(editor->SelectedCameraPoint()->handleOut-beforeHandle)>.1f,"Bezier handle viewport drag failed");
    const auto hIn=editor->SelectedCameraPoint()->handleIn,hOut=editor->SelectedCameraPoint()->handleOut;
    check(StageDot(hIn,hOut)<0 && std::abs(StageDot(hIn,hOut)/(StageLength(hIn)*StageLength(hOut))+1)<.0001f,"Aligned handles are not collinear");
    editor->SetCameraHandleType("Free"); const auto opposite=editor->SelectedCameraPoint()->handleIn;
    editor->SetCameraHandle(true,{1,1,3}); editor->CommitCameraHistory();
    check(StageLength(editor->SelectedCameraPoint()->handleIn-opposite)==0,"Free handle changed the opposite handle");
    editor->SetCameraHandleType("Auto"); editor->SetCameraHandle(true,{1,1,3}); editor->CommitCameraHistory();
    check(editor->SelectedCameraPoint()->handleType=="Aligned","dragging Auto did not create editable Aligned handles");
    editor->SetCameraHandleType("Free"); editor->SetCameraHandle(true,{1,1,3}); editor->CommitCameraHistory();
    check(editor->BuildCameraEvaluator(evaluator,error),"Bezier handle edit failed validation");
    const auto curvePixel=pixel(evaluator.SegmentPosition(2,.62f)); const auto pointCount=editor->SelectedCameraSequence()->points.size();
    const float doubleClickTime=ImGui::GetIO().MouseDoubleClickTime; ImGui::GetIO().MouseDoubleClickTime=2;
    click(curvePixel); click(curvePixel); ImGui::GetIO().MouseDoubleClickTime=doubleClickTime;
    check(editor->SelectedCameraSequence()->points.size()==pointCount+1,"Bezier projected double-click insertion failed");
    const auto splitParameter=editor->selectedSegmentParameter_; CameraSequence divided; check(editor->BuildCameraEvaluator(divided,error),"split path invalid");
    for (int j=0;j<=30;++j) {
        const float u=static_cast<float>(j)/30;
        check(StageLength(divided.SegmentPosition(2,u)-evaluator.SegmentPosition(2,u*splitParameter))<.0002f,"viewport Bezier split changed left curve");
        check(StageLength(divided.SegmentPosition(3,u)-evaluator.SegmentPosition(2,splitParameter+(1-splitParameter)*u))<.0002f,"viewport Bezier split changed right curve");
    }
    capture(L"generated/placement-tests/camera-path-top.png");
    editor->selectedCameraPoint_=editor->SelectedCameraSequence()->points.front().id; editor->selectedCameraHandle_=-1;
    for (const auto view:{EnemyPlacementEditorScene::CameraMode::Front,EnemyPlacementEditorScene::CameraMode::Side}) {
        editor->SwitchCamera(view); frame(); const auto before=editor->SelectedCameraPoint()->position; center=pixel(before);
        mouse(center,false); mouse(center,true); mouse({center.x+14,center.y-18},true); mouse({center.x+14,center.y-18},false);
        const auto after=editor->SelectedCameraPoint()->position;
        check(after.y>before.y+.1f,"Front/Side failed to edit height");
        check(view==EnemyPlacementEditorScene::CameraMode::Front ? std::abs(after.z-before.z)<.00001f : std::abs(after.x-before.x)<.00001f,"Front/Side changed its fixed coordinate");
        capture(view==EnemyPlacementEditorScene::CameraMode::Front ? L"generated/placement-tests/camera-path-front.png" : L"generated/placement-tests/camera-path-side.png");
    }
    editor->SwitchCamera(EnemyPlacementEditorScene::CameraMode::Free); const auto fromView=editor->camera_.GetTransform();
    editor->AddCameraPoint(false); check(StageLength(editor->SelectedCameraPoint()->position-fromView.translate)==0 && StageLength(editor->SelectedCameraPoint()->rotation-fromView.rotate)==0,"Add from View pose differs");
    editor->DeleteCameraPoint();
    editor->SwitchCamera(EnemyPlacementEditorScene::CameraMode::Top); editor->camera_.SetTranslate({31,42,17}); editor->camera_.Update();
    check(editor->BuildCameraEvaluator(evaluator,error),"path preview validation failed"); const auto editingCamera=editor->camera_;
    check(editor->StartCameraPreview(true),"new path preview failed"); editor->SeekCameraPreview(2.3);
    const auto expected=evaluator.Evaluate(2.3);
    check(StageLength(editor->camera_.GetTranslate()-expected.position)<.0001f && StageLength(editor->camera_.GetRotate()-expected.rotation)<.0001f,"new path preview differs from common evaluator");
    capture(L"generated/placement-tests/camera-path-preview.png"); editor->StopCameraPreview();
    check(StageLength(editor->camera_.GetTranslate()-editingCamera.GetTranslate())==0,"new path Stop did not restore editor");
    const auto authored=editor->cameraDrafts_; const std::string fixture="generated/placement-tests/bezier-stage03.json";
    std::filesystem::remove(CameraSequenceOverrides::PathFor(fixture)); { std::ofstream file(fixture,std::ios::binary); file<<originalStage; }
    editor->LoadCameraSequences(fixture); editor->cameraDrafts_=authored; editor->EnsureCameraSelection();
    check(editor->SaveCameraSequence(fixture),"Bezier override save failed"); const auto saved=CameraSequence::ToJson(editor->cameraDrafts_);
    editor->LoadCameraSequences(fixture); check(CameraSequence::ToJson(editor->cameraDrafts_)==saved,"Bezier override reload changed path/handles");
    StageLoader runtime; check(runtime.Load(fixture) && CameraSequence::ToJson(runtime.cameraSequences)==saved,"runtime did not load Bezier override");
    CameraSequence reloaded; check(reloaded.Start(runtime.cameraSequences.front(),editor->CameraEyePose(),editor->level_.playerPosition),"runtime Bezier start failed");
    for (int j=0;j<=100;++j) check(StageLength(reloaded.Evaluate(reloaded.Duration()*j/100).position-evaluator.Evaluate(evaluator.Duration()*j/100).position)<.0001f,"saved runtime and F7 curve differ");
    check(editor->RevertCameraSequence(fixture),"Bezier override revert failed");
    check(CameraSequenceOverrides::Read(editor->StagePath())==originalStage && CameraSequenceOverrides::Read(editor->PlacementPath())==originalEnemies && editor->placements_.Json()==oldPlacements && editor->playerVisualSettings_.yawOffsetDegrees==oldVisual,"path edits contaminated stage/enemies/Player Visual");
    editor->LoadCameraSequences(editor->StagePath()); editor->cameraDrafts_=oldDraft; editor->SetCameraSequenceMode(false); editor->mode_=oldMode; editor->camera_=oldCamera;
    std::ofstream("generated/placement-tests/camera-path-result.txt")<<"PASS: moved Hold endpoint keeps visible connected draft curves, cached geometry without author mutation, invalid preview/save still rejected; real F7 mouse Top draw, Enter/Esc/Backspace, PlayerEye, one-operation Undo/Redo, planar point and handle dragging, projected Linear selection/insertion, mixed Bezier, Auto/Aligned/Free, projected double-click de Casteljau shape preservation, Front/Side fixed-plane height, Add from View, common-evaluator preview/Stop, isolated override save/reload/revert and runtime parity, unchanged stage/enemies/Player Visual.\n";
}
