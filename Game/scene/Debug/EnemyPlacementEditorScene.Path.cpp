#include "EnemyPlacementEditorScene.h"
#include "GameApp.h"
#include "ImGuiManagaer.h"
#include <cstdio>
#ifdef USE_IMGUI
#include "imgui.h"

namespace {
constexpr float degrees=1/CameraSequence::DegreesToRadians;
Vector3 PathForward(const Vector3& rotation) {
    const auto r=Matrix4x4::RotateXYZ(rotation.x,rotation.y,rotation.z); return {r.m[2][0],r.m[2][1],r.m[2][2]};
}
float ScreenDistance(ImVec2 a,ImVec2 b) { return std::hypot(a.x-b.x,a.y-b.y); }
}
#endif

void EnemyPlacementEditorScene::DrawCameraSequenceControls([[maybe_unused]] GameApp& app) {
#ifdef USE_IMGUI
    ImGui::SeparatorText("Camera Sequence");
    ImGui::Checkbox("Player model",&showPlayerVisual_); ImGui::SameLine(); ImGui::Checkbox("Show path",&showCameraPath_);
    ImGui::BeginDisabled(cameraPreviewActive_ || !drawPathPoints_.empty());
    if (ImGui::BeginCombo("Sequence ID",selectedCameraSequence_.empty() ? "(none)" : selectedCameraSequence_.c_str())) {
        for (const auto& d:cameraDrafts_) if (ImGui::Selectable(d.id.c_str(),d.id==selectedCameraSequence_)) {
            selectedCameraSequence_=d.id; selectedCameraPoint_.clear(); selectedCameraSegment_=selectedCameraHandle_=-1; cameraPreviewTime_=0; EnsureCameraSelection();
        }
        ImGui::EndCombo();
    }
    if (ImGui::CollapsingHeader("Create another sequence")) {
        ImGui::InputText("New ID",newCameraSequenceId_,sizeof(newCameraSequenceId_));
        if (ImGui::Button("Create sequence") && newCameraSequenceId_[0] && cameraDrafts_.size()<128) {
            const std::string id=newCameraSequenceId_;
            if (std::none_of(cameraDrafts_.begin(),cameraDrafts_.end(),[&](const auto& d){return d.id==id;})) {
                CommitCameraHistory(); CameraSequenceDefinition d; d.id=id;
                CameraSequencePoint a,b; a.id="CameraPoint_1"; a.position=camera_.GetTranslate(); a.rotation=camera_.GetRotate();
                a.pathType="Linear"; a.interpolation="Linear"; a.duration=3.5;
                b.id="PlayerEye"; b.playerEye=true; b.order=1; b.duration=0; d.points={a,b};
                cameraDrafts_.push_back(d); selectedCameraSequence_=id; selectedCameraPoint_.clear(); selectedCameraSegment_=selectedCameraHandle_=-1;
                EnsureCameraSelection(); CommitCameraHistory();
            } else cameraSequenceStatus_="Sequence ID already exists";
        }
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(cameraPreviewActive_);
    ImGui::SeparatorText("Path Tools");
    if (ImGui::RadioButton("Draw Path",pathTool_==PathTool::Draw)) { SetPathTool(PathTool::Draw); showCameraPath_=true; }
    ImGui::SameLine(); if (ImGui::RadioButton("Edit Path",pathTool_==PathTool::Path)) SetPathTool(PathTool::Path);
    ImGui::SameLine(); if (ImGui::RadioButton("Edit Camera",pathTool_==PathTool::Camera)) SetPathTool(PathTool::Camera);
    if (pathTool_==PathTool::Draw) {
        ImGui::TextWrapped("Top: click to draw at Draw Height. Enter: replace selected path. Esc: cancel. Backspace: remove last. PlayerEye is retained.");
        ImGui::Text("Drawing: %zu points",drawPathPoints_.size());
        if (ImGui::Button("Finish (Enter)")) FinishDrawPath();
        ImGui::SameLine(); if (ImGui::Button("Undo last") && !drawPathPoints_.empty()) drawPathPoints_.pop_back();
        ImGui::SameLine(); if (ImGui::Button("Cancel (Esc)")) CancelDrawPath();
    } else {
        if (ImGui::Button("Add from View")) AddCameraPoint(false);
        ImGui::SameLine(); if (ImGui::Button("Duplicate")) AddCameraPoint(true);
        ImGui::SameLine(); if (ImGui::Button("Delete")) DeleteCameraPoint();
        if (pathTool_==PathTool::Path) {
            ImGui::BeginDisabled(selectedCameraSegment_<0);
            if (ImGui::Button("Insert Point")) InsertCameraPoint();
            ImGui::EndDisabled();
            ImGui::SameLine(); ImGui::TextUnformatted("or double-click a line");
        }
        if (ImGui::Button("Undo (Ctrl+Z)")) UndoCameraSequence(false);
        ImGui::SameLine(); if (ImGui::Button("Redo (Ctrl+Y)")) UndoCameraSequence(true);
    }
    if (ImGui::CollapsingHeader("View Settings",ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& [label,mode]:std::initializer_list<std::pair<const char*,CameraMode>>{{"Free",CameraMode::Free},{"FPS",CameraMode::FPS},{"Top",CameraMode::Top},{"Front",CameraMode::Front},{"Side",CameraMode::Side}}) {
            if (mode!=CameraMode::Free) ImGui::SameLine();
            if (ImGui::RadioButton(label,mode_==mode)) SwitchCamera(mode);
        }
        ImGui::DragFloat("Draw Height (Y)",&drawHeight_,.05f,-1e6f,1e6f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        if (OrthographicView()) { if (ImGui::SliderFloat("View size",&zoom_,3,150)) camera_.SetOrthographic(true,zoom_); }
        else ImGui::SliderFloat("View speed",&speed_,.5f,50,"%.1f m/s");
        if (ImGui::Button("Focus point (F)")) pendingAction_="cameraFocus";
        ImGui::TextWrapped("Tab: view / UI. Ortho: wheel zoom, middle-drag pan. Edit Path: drag points/handles in the view plane. Free: XYZ gizmo. Alt+line: preview here.");
    }
    ImGui::EndDisabled();
    CameraSequence evaluator; std::string validation; const bool valid=BuildCameraEvaluator(evaluator,validation);
    if (ImGui::CollapsingHeader("Preview",ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!valid) ImGui::TextWrapped("Preview unavailable: %s",validation.c_str());
        ImGui::BeginDisabled((!valid && !cameraPreviewActive_) || !drawPathPoints_.empty());
        if (ImGui::Button(cameraPreviewActive_ && !cameraPreviewPlaying_ ? "Resume" : "Play")) pendingAction_="cameraPlay";
        ImGui::SameLine(); if (ImGui::Button("Pause")) pendingAction_="cameraPause";
        ImGui::SameLine(); if (ImGui::Button("Stop")) pendingAction_="cameraStop";
        ImGui::SameLine(); if (ImGui::Button("Restart")) pendingAction_="cameraRestart";
        const double duration=cameraPreviewActive_ ? cameraPreview_.Duration() : valid ? evaluator.Duration() : 0;
        float time=static_cast<float>(cameraPreviewTime_);
        if (ImGui::SliderFloat("Time (s)",&time,0,static_cast<float>(duration),"%.3f",ImGuiSliderFlags_AlwaysClamp)) SeekCameraPreview(time);
        if (ImGui::Button("Final FPS / PlayerEye")) pendingAction_="cameraFinal";
        ImGui::SameLine(); ImGui::Text("%.2f / %.2f s",cameraPreviewTime_,duration);
        ImGui::EndDisabled();
    }
    ImGui::BeginDisabled(cameraPreviewActive_ || !drawPathPoints_.empty());
    auto* d=SelectedCameraSequence();
    if (d && pathTool_!=PathTool::Draw) {
        if (ImGui::BeginListBox("Points",{-1,108})) {
            for (size_t i=0;i<d->points.size();++i) {
                const auto& p=d->points[i]; const auto label=std::to_string(p.order)+": "+p.id+(p.playerEye ? " [FPS]" : "");
                if (ImGui::Selectable(label.c_str(),p.id==selectedCameraPoint_)) { selectedCameraPoint_=p.id; selectedCameraHandle_=-1; selectedCameraSegment_=static_cast<int>(std::min(i,d->points.size()-2)); selectedSegmentParameter_=.5f; }
            }
            ImGui::EndListBox();
        }
        if (ImGui::Button("Move up")) { MoveCameraPoint(-1); selectedCameraSegment_=selectedCameraHandle_=-1; }
        ImGui::SameLine(); if (ImGui::Button("Move down")) { MoveCameraPoint(1); selectedCameraSegment_=selectedCameraHandle_=-1; }
        if (selectedCameraSegment_>=0 && static_cast<size_t>(selectedCameraSegment_)+1<d->points.size() && ImGui::CollapsingHeader("Segment Settings",ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& segment=d->points[static_cast<size_t>(selectedCameraSegment_)];
            ImGui::Text("%s -> %s",segment.id.c_str(),d->points[static_cast<size_t>(selectedCameraSegment_)+1].id.c_str());
            if (ImGui::BeginCombo("Path Type",segment.pathType.empty() ? "Legacy interpolation" : segment.pathType.c_str())) {
                for (const auto* type:{"","Linear","Bezier"}) if (ImGui::Selectable(type[0] ? type : "Legacy interpolation",segment.pathType==type)) SetCameraPathType(type);
                ImGui::EndCombo();
            }
            float duration=static_cast<float>(segment.duration);
            if (ImGui::DragFloat("Duration (s)",&duration,.02f,.001f,1200,"%.3f",ImGuiSliderFlags_AlwaysClamp)) segment.duration=duration;
            if (!segment.pathType.empty()) {
                if (ImGui::BeginCombo("Easing",segment.easing.c_str())) {
                    for (const auto* easing:{"Constant","EaseIn","EaseOut","EaseInOut"}) if (ImGui::Selectable(easing,segment.easing==easing)) segment.easing=easing;
                    ImGui::EndCombo();
                }
            } else if (ImGui::BeginCombo("Legacy interpolation",segment.interpolation.c_str())) {
                for (const auto* name:{"CatmullRom","Linear","SmoothStep","Hold"}) if (ImGui::Selectable(name,segment.interpolation==name)) segment.interpolation=name;
                ImGui::EndCombo();
            }
            ImGui::SliderFloat("Segment parameter",&selectedSegmentParameter_,.001f,.999f,"%.3f");
            if (valid && ImGui::Button("Preview at selection")) { cameraPreviewTime_=evaluator.TimeAtParameter(static_cast<size_t>(selectedCameraSegment_),selectedSegmentParameter_); pendingAction_="cameraSeek"; }
        }
        if (auto* p=SelectedCameraPoint(); p && ImGui::CollapsingHeader("Point Settings",ImGuiTreeNodeFlags_DefaultOpen)) {
            char id[256]; std::snprintf(id,sizeof(id),"%s",p->id.c_str());
            if (ImGui::InputText("Point ID",id,sizeof(id)) && id[0] && std::none_of(d->points.begin(),d->points.end(),[&](const auto& other){return &other!=p && other.id==id;})) { p->id=id; selectedCameraPoint_=p->id; }
            ImGui::BeginDisabled(&d->points.back()!=p); ImGui::Checkbox("PlayerEye",&p->playerEye); ImGui::EndDisabled();
            ImGui::BeginDisabled(p->playerEye);
            ImGui::DragFloat3("Position XYZ",&p->position.x,.03f,-1e6f,1e6f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndDisabled();
            if (pathTool_==PathTool::Path) {
                if (ImGui::BeginCombo("Handle Type",p->handleType.c_str())) {
                    for (const auto* name:{"Auto","Aligned","Free"}) if (ImGui::Selectable(name,p->handleType==name)) SetCameraHandleType(name);
                    ImGui::EndCombo();
                }
                const auto index=static_cast<size_t>(p-d->points.data());
                CameraSequence handles; const bool handlesValid=BuildCameraGeometry(handles);
                auto incoming=handlesValid ? handles.HandleOffset(index,false) : p->handleIn;
                auto outgoing=handlesValid ? handles.HandleOffset(index,true) : p->handleOut;
                ImGui::BeginDisabled(p->handleType=="Auto");
                if (ImGui::DragFloat3("Handle In offset",&incoming.x,.02f,-1e6f,1e6f,"%.3f",ImGuiSliderFlags_AlwaysClamp)) SetCameraHandle(false,incoming);
                if (ImGui::DragFloat3("Handle Out offset",&outgoing.x,.02f,-1e6f,1e6f,"%.3f",ImGuiSliderFlags_AlwaysClamp)) SetCameraHandle(true,outgoing);
                ImGui::EndDisabled();
                ImGui::TextWrapped("Auto handles follow neighbours. Dragging an Auto handle changes it to Aligned. Offsets travel with the point.");
            } else {
                ImGui::BeginDisabled(p->playerEye);
                Vector3 rotation=p->rotation*degrees;
                if (ImGui::DragFloat3("Rotation XYZ (deg)",&rotation.x,.5f,-36000,36000,"%.2f",ImGuiSliderFlags_AlwaysClamp)) p->rotation=rotation*CameraSequence::DegreesToRadians;
                ImGui::RadioButton("Move gizmo",&cameraGizmo_,0); ImGui::SameLine(); ImGui::RadioButton("Rotate gizmo",&cameraGizmo_,1);
                if (cameraGizmo_==1) { ImGui::RadioButton("X",&cameraRotationAxis_,0); ImGui::SameLine(); ImGui::RadioButton("Y",&cameraRotationAxis_,1); ImGui::SameLine(); ImGui::RadioButton("Z",&cameraRotationAxis_,2); }
                if (ImGui::BeginCombo("Rotation Mode",p->rotationMode.c_str())) {
                    for (const auto* name:{"Manual","LookAtPlayer","LookAtTarget","FollowTangent"}) if (ImGui::Selectable(name,p->rotationMode==name)) p->rotationMode=name;
                    ImGui::EndCombo();
                }
                if (p->rotationMode=="LookAtTarget") ImGui::DragFloat3("Look At Target",&p->target.x,.03f,-1e6f,1e6f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
                if (p->rotationMode=="LookAtPlayer") ImGui::DragFloat3("Look At Player offset",&p->lookOffset.x,.03f,-1000,1000,"%.3f",ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("FOV (deg)",&p->fov,10,150,"%.2f",ImGuiSliderFlags_AlwaysClamp);
                ImGui::EndDisabled();
            }
        }
    }
    ImGui::EndDisabled();
    if (!valid) ImGui::TextWrapped("Validation: %s",validation.c_str());
    const bool overridden=std::any_of(cameraOverrides_.sequences.begin(),cameraOverrides_.sequences.end(),[&](const auto& row){return row.id==selectedCameraSequence_;});
    ImGui::Text("%s | %s",overridden ? "F7 override" : "Blender base / draft",CameraSequencesDirty() ? "Unsaved" : "Saved");
    ImGui::BeginDisabled(cameraPreviewActive_ || cameraOverrideLoadError_ || !valid || !drawPathPoints_.empty());
    if (ImGui::Button("Save camera only")) pendingAction_="cameraSave";
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(cameraPreviewActive_ || cameraOverrideLoadError_ || !d || !drawPathPoints_.empty());
    if (ImGui::Button("Use Blender base")) cameraRevertModal_=true;
    ImGui::EndDisabled();
    if (cameraRevertModal_) { ImGui::OpenPopup("Restore Blender camera?"); cameraRevertModal_=false; }
    if (ImGui::BeginPopupModal("Restore Blender camera?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Remove the override for %s? Other IDs, enemy and Player Visual edits are preserved.",selectedCameraSequence_.c_str());
        if (ImGui::Button("Restore")) { pendingAction_="cameraRevert"; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup(); ImGui::EndPopup();
    }
    ImGui::BeginDisabled(cameraPreviewActive_);
    if (ImGui::Button("Reload cameras")) RequestAction("cameraReload");
    ImGui::SameLine(); if (ImGui::Button("Test play (placements)")) pendingAction_="test";
    if (ImGui::Button("Title")) RequestAction("title");
    if (d) ImGui::Checkbox("Sequence enabled",&d->enabled);
    if (ImGui::CollapsingHeader("Stage visibility")) {
        bool changed=ImGui::Checkbox("Roof",&showRoof_); changed=ImGui::Checkbox("Ceiling",&showCeiling_) || changed;
        changed=ImGui::Checkbox("Upper floors",&showUpper_) || changed; changed=ImGui::Checkbox("Lower floors",&showLower_) || changed;
        changed=ImGui::Checkbox("Walls",&showWalls_) || changed; if (changed) ApplyVisibility();
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s",cameraSequenceStatus_.c_str());
    if (!cameraPreviewActive_ && cameraDragAxis_<0 && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(0)) CommitCameraHistory();
#endif
}

void EnemyPlacementEditorScene::DrawCameraSequenceViewport([[maybe_unused]] GameApp& app) {
#ifdef USE_IMGUI
    RECT rect{};
    if (cameraPreviewActive_ || !ready_ || !app.ImGui()->GetSceneImageRect(rect)) return;
    auto* d=SelectedCameraSequence(); if (!d) return;
    auto& io=ImGui::GetIO(); const auto mouse=io.MousePos;
    const bool hover=app.ImGui()->IsSceneImageHovered() && !app.GetInput()->IsCameraControlEnabled();
    if (pathTool_==PathTool::Draw && !io.WantTextInput && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) CancelDrawPath();
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !drawPathPoints_.empty()) drawPathPoints_.pop_back();
        if (ImGui::IsKeyPressed(ImGuiKey_Enter)) FinishDrawPath();
    }
    const ImVec2 lo{static_cast<float>(rect.left),static_cast<float>(rect.top)},hi{static_cast<float>(rect.right),static_cast<float>(rect.bottom)};
    float scale=zoom_/std::max(1.f,hi.y-lo.y);
    const auto world=camera_.GetWorldMatrix();
    const Vector3 right{world.m[0][0],world.m[0][1],world.m[0][2]},up{world.m[1][0],world.m[1][1],world.m[1][2]};
    if (hover && OrthographicView()) {
        if (io.MouseWheel!=0) { zoom_=std::clamp(zoom_*std::pow(.85f,io.MouseWheel),3.f,150.f); scale=zoom_/std::max(1.f,hi.y-lo.y); camera_.SetOrthographic(true,zoom_); camera_.Update(); }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) { camera_.SetTranslate(camera_.GetTranslate()-right*(io.MouseDelta.x*scale)+up*(io.MouseDelta.y*scale)); camera_.Update(); }
    }
    if (!showCameraPath_) { cameraDragAxis_=-1; return; }
    const auto vp=camera_.GetViewProjectionMatrix(); auto* draw=ImGui::GetForegroundDrawList();
    const auto project=[&](const Vector3& p,ImVec2& out) {
        const float w=p.x*vp.m[0][3]+p.y*vp.m[1][3]+p.z*vp.m[2][3]+vp.m[3][3];
        const float z=p.x*vp.m[0][2]+p.y*vp.m[1][2]+p.z*vp.m[2][2]+vp.m[3][2];
        if (w<=.0001f || z<0 || z>w) return false;
        const auto clip=StagePoint(p,vp); out={lo.x+(clip.x/w+1)*.5f*(hi.x-lo.x),lo.y+(1-clip.y/w)*.5f*(hi.y-lo.y)}; return true;
    };
    const auto line=[&](const Vector3& a,const Vector3& b,ImU32 color,float width=2) { ImVec2 x,y; if (project(a,x)&&project(b,y)) draw->AddLine(x,y,color,width); };
    draw->PushClipRect(lo,hi,true);
    if (pathTool_==PathTool::Draw) {
        for (size_t i=0;i<drawPathPoints_.size();++i) {
            ImVec2 p; if (project(drawPathPoints_[i].position,p)) { draw->AddCircleFilled(p,7,IM_COL32(255,210,30,255)); draw->AddText({p.x+9,p.y},IM_COL32_WHITE,std::to_string(i).c_str()); }
            if (i) line(drawPathPoints_[i-1].position,drawPathPoints_[i].position,IM_COL32(255,210,30,255));
        }
        if (hover && mode_==CameraMode::Top) {
            auto position=camera_.GetTranslate()+right*((mouse.x-(lo.x+hi.x)*.5f)*scale)+up*(((lo.y+hi.y)*.5f-mouse.y)*scale);
            position.y=drawHeight_;
            if (!drawPathPoints_.empty()) line(drawPathPoints_.back().position,position,IM_COL32(255,210,30,150));
            if (ImGui::IsMouseClicked(0)) AddDrawPoint(position);
        }
        draw->AddText({lo.x+12,lo.y+12},IM_COL32_WHITE,"Draw Path: Top click | Enter finish | Esc cancel | Backspace undo last");
        draw->PopClipRect(); return;
    }
    CameraSequence evaluator; std::string error; const bool valid=BuildCameraEvaluator(evaluator,error);
    const bool geometryReady=valid || BuildCameraGeometry(evaluator);
    const auto pointPose=[&](size_t index) {
        if (geometryReady) return evaluator.PointPose(index);
        const auto& p=d->points[index]; return p.playerEye ? CameraEyePose() : CameraPose{p.position,p.rotation,p.fov*CameraSequence::DegreesToRadians};
    };
    int hitSegment=-1; float hitParameter=.5f,hitDistance=11;
    const bool clicked=hover && ImGui::IsMouseClicked(0);
    if (geometryReady) for (size_t i=0;i+1<d->points.size();++i) {
        const bool selected=static_cast<int>(i)==selectedCameraSegment_;
        const auto color=selected ? IM_COL32(255,210,25,255) : !valid ? IM_COL32(255,155,70,255) : d->points[i].pathType=="Bezier" ? IM_COL32(215,120,255,255) : IM_COL32(35,220,250,255);
        auto previous=evaluator.SegmentPosition(i,0); ImVec2 previousPixel{}; bool previousVisible=project(previous,previousPixel);
        for (int j=1;j<=64;++j) {
            const float u=static_cast<float>(j)/64; const auto next=evaluator.SegmentPosition(i,u); ImVec2 nextPixel{}; const bool nextVisible=project(next,nextPixel);
            line(previous,next,color,selected ? 3.f : 2.f);
            if (clicked && previousVisible && nextVisible) {
                const float sx=nextPixel.x-previousPixel.x,sy=nextPixel.y-previousPixel.y,n=sx*sx+sy*sy;
                const float f=n>1e-8f ? std::clamp(((mouse.x-previousPixel.x)*sx+(mouse.y-previousPixel.y)*sy)/n,0.f,1.f) : 0;
                const float distance=ScreenDistance(mouse,{previousPixel.x+sx*f,previousPixel.y+sy*f});
                if (distance<hitDistance) { hitDistance=distance; hitSegment=static_cast<int>(i); hitParameter=(static_cast<float>(j-1)+f)/64; }
            }
            previous=next; previousPixel=nextPixel; previousVisible=nextVisible;
        }
        const auto middle=evaluator.SegmentPosition(i,.5f),tangent=evaluator.SegmentTangent(i,.5f); const float length=StageLength(tangent);
        ImVec2 from,to;
        if (length>1e-6f && project(middle,from) && project(middle+tangent*(.4f/length),to)) {
            const float screenLength=ScreenDistance(from,to);
            if (screenLength>3) {
                const float x=(to.x-from.x)/screenLength,y=(to.y-from.y)/screenLength;
                draw->AddLine(from,to,color,2);
                draw->AddLine(to,{to.x-x*7-y*4,to.y-y*7+x*4},color,2);
                draw->AddLine(to,{to.x-x*7+y*4,to.y-y*7-x*4},color,2);
            }
        }
    }
    // Refine the nearest projected curve location around the best sampled span.
    if (hitSegment>=0) {
        float low=std::max(0.f,hitParameter-1.f/64),high=std::min(1.f,hitParameter+1.f/64);
        const auto distance=[&](float u) { ImVec2 p; return project(evaluator.SegmentPosition(static_cast<size_t>(hitSegment),u),p) ? ScreenDistance(mouse,p) : 1e20f; };
        for (int i=0;i<20;++i) { const float a=(2*low+high)/3,b=(low+2*high)/3; if (distance(a)<distance(b)) high=b; else low=a; }
        hitParameter=std::clamp((low+high)*.5f,.00001f,.99999f);
    }
    bool used=false;
    if (pathTool_==PathTool::Path && geometryReady) for (size_t i=0;i<d->points.size();++i) {
        const bool showIn=i>0 && d->points[i-1].pathType=="Bezier",showOut=i+1<d->points.size() && d->points[i].pathType=="Bezier";
        for (int handle=0;handle<2;++handle) {
            if ((handle==0 && !showIn) || (handle==1 && !showOut)) continue;
            const auto position=evaluator.PointPosition(i),end=position+evaluator.HandleOffset(i,handle==1); ImVec2 pixel;
            const bool selected=d->points[i].id==selectedCameraPoint_ && handle==selectedCameraHandle_;
            const auto color=selected ? IM_COL32(255,245,80,255) : IM_COL32(255,150,70,255);
            line(position,end,color,1); if (!project(end,pixel)) continue;
            draw->AddRectFilled({pixel.x-5,pixel.y-5},{pixel.x+5,pixel.y+5},color);
            if (clicked && ScreenDistance(mouse,pixel)<9) {
                CommitCameraHistory(); selectedCameraPoint_=d->points[i].id; selectedCameraHandle_=handle;
                selectedCameraSegment_=static_cast<int>(handle==0 ? i-1 : i); cameraDragAxis_=OrthographicView() ? 4 : -1; used=true;
            }
        }
    }
    for (size_t i=0;i<d->points.size();++i) {
        const auto pose=pointPose(i); const auto& p=d->points[i]; const bool selected=p.id==selectedCameraPoint_;
        const auto color=selected ? IM_COL32(255,215,25,255) : p.playerEye || i+1==d->points.size() ? IM_COL32(100,255,150,255) : i==0 ? IM_COL32(255,170,90,255) : IM_COL32(40,210,255,255);
        ImVec2 pixel;
        if (project(pose.position,pixel)) {
            draw->AddCircleFilled(pixel,selected ? 8.f : 6.f,color);
            const auto label=std::to_string(p.order)+": "+p.id+(p.playerEye ? " [FPS]" : i==0 ? " [Start]" : i+1==d->points.size() ? " [End]" : "");
            draw->AddText({pixel.x+10,pixel.y-8},color,label.c_str());
        }
        const auto forward=PathForward(pose.rotation),tip=pose.position+forward*.7f;
        line(pose.position,tip,color); const Vector3 side{-forward.z*.12f,0,forward.x*.12f};
        line(tip,tip-forward*.18f+side,color); line(tip,tip-forward*.18f-side,color);
    }
    if (valid) { ImVec2 p; if (project(evaluator.Evaluate(std::clamp(cameraPreviewTime_,0.0,evaluator.Duration())).position,p)) {
        draw->AddCircle(p,12,IM_COL32(255,80,100,255),32,2); draw->AddText({p.x+14,p.y+9},IM_COL32(255,80,100,255),"Preview");
    } }
    auto* point=SelectedCameraPoint();
    if (point && (!point->playerEye || selectedCameraHandle_>=0)) {
        auto position=point->playerEye ? CameraEyePose().position : point->position;
        if (selectedCameraHandle_>=0 && geometryReady) position+=evaluator.HandleOffset(static_cast<size_t>(point-d->points.data()),selectedCameraHandle_==1);
        ImVec2 center;
        if (project(position,center)) {
            if (pathTool_==PathTool::Path || cameraGizmo_==0) {
                const Vector3 axes[]={{1,0,0},{0,1,0},{0,0,1}};
                const ImU32 colors[]={IM_COL32(255,65,65,255),IM_COL32(65,255,90,255),IM_COL32(80,130,255,255)};
                for (int axis=0;axis<3;++axis) {
                    ImVec2 end; if (!project(position+axes[axis],end) || ScreenDistance(center,end)<4) continue;
                    draw->AddLine(center,end,colors[axis],3); draw->AddCircleFilled(end,5,colors[axis]);
                    if (!used && clicked && ScreenDistance(mouse,end)<9) { CommitCameraHistory(); cameraDragAxis_=axis; used=true; }
                    if (cameraDragAxis_==axis && ImGui::IsMouseDown(0) && !clicked) {
                        const float sx=end.x-center.x,sy=end.y-center.y,n=sx*sx+sy*sy;
                        if (n>4) MoveCameraPathSelection(axes[axis]*((io.MouseDelta.x*sx+io.MouseDelta.y*sy)/n)); used=true;
                    }
                }
            } else {
                draw->AddCircle(center,55,IM_COL32(255,180,55,255),64,3);
                if (!used && clicked && std::abs(ScreenDistance(mouse,center)-55)<9) { CommitCameraHistory(); cameraDragAxis_=3; used=true; }
                if (cameraDragAxis_==3 && ImGui::IsMouseDown(0) && !clicked) {
                    const float delta=std::remainder(std::atan2(mouse.y-center.y,mouse.x-center.x)-std::atan2(mouse.y-io.MouseDelta.y-center.y,mouse.x-io.MouseDelta.x-center.x),6.283185307f);
                    float* values[]={&point->rotation.x,&point->rotation.y,&point->rotation.z}; *values[cameraRotationAxis_]+=delta; point->rotationMode="Manual"; used=true;
                }
            }
        }
    }
    if (cameraDragAxis_==4 && ImGui::IsMouseDown(0) && !clicked) { MoveCameraPathSelection(right*(io.MouseDelta.x*scale)-up*(io.MouseDelta.y*scale)); used=true; }
    if (!ImGui::IsMouseDown(0) && cameraDragAxis_>=0) { cameraDragAxis_=-1; CommitCameraHistory(); }
    bool insert=false;
    if (clicked && !used) {
        float nearest=13; int index=-1;
        for (size_t i=0;i<d->points.size();++i) { ImVec2 p; if (project(pointPose(i).position,p) && ScreenDistance(mouse,p)<nearest) { nearest=ScreenDistance(mouse,p); index=static_cast<int>(i); } }
        if (index>=0) {
            selectedCameraPoint_=d->points[static_cast<size_t>(index)].id; selectedCameraHandle_=-1;
            selectedCameraSegment_=std::min(index,static_cast<int>(d->points.size())-2); selectedSegmentParameter_=.5f;
            if (pathTool_==PathTool::Path && OrthographicView() && !d->points[static_cast<size_t>(index)].playerEye) { CommitCameraHistory(); cameraDragAxis_=4; }
        } else if (hitSegment>=0) {
            selectedCameraSegment_=hitSegment; selectedSegmentParameter_=hitParameter;
            selectedCameraPoint_=d->points[static_cast<size_t>(hitSegment)].id; selectedCameraHandle_=-1;
            if (io.KeyAlt && valid) { cameraPreviewTime_=evaluator.TimeAtParameter(static_cast<size_t>(hitSegment),hitParameter); pendingAction_="cameraSeek"; }
            else if (pathTool_==PathTool::Path && ImGui::IsMouseDoubleClicked(0)) insert=true;
        }
    }
    if (!valid && geometryReady) {
        draw->AddRectFilled({lo.x+7,lo.y+7},{hi.x-7,lo.y+48},IM_COL32(25,25,25,210),3);
        draw->AddText({lo.x+12,lo.y+11},IM_COL32(255,190,100,255),"Draft path shown. Resolve validation before preview/save.");
        draw->AddText({lo.x+12,lo.y+28},IM_COL32_WHITE,error.c_str());
    }
    draw->PopClipRect();
    if (insert) InsertCameraPoint();
#endif
}
