#include "EnemyPlacementEditorScene.h"
#include "GameApp.h"
#include "ImGuiManagaer.h"
#include <cstdio>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
constexpr float toDegrees=1/CameraSequence::DegreesToRadians;
void ReplaceDefinition(std::vector<CameraSequenceDefinition>& rows,const CameraSequenceDefinition& definition) {
    const auto it=std::find_if(rows.begin(),rows.end(),[&](const auto& d){return d.id==definition.id;});
    if (it==rows.end()) rows.push_back(definition); else *it=definition;
}

}

void EnemyPlacementEditorScene::LoadCameraSequences(const std::string& path) {
    StopCameraPreview();
    CancelDrawPath(); selectedCameraSegment_=selectedCameraHandle_=-1; cameraEvaluatorKey_=cameraGeometryKey_=nullptr;
    StageLoader loaded;
    if (!loaded.Load(path) || loaded.id!=stageId_) {
        cameraSequenceStatus_="Camera reload failed: "+loaded.Error(); return;
    }
    cameraDrafts_=loaded.cameraSequences; blenderCameraDrafts_=loaded.blenderCameraSequences;
    cameraOverrides_=loaded.cameraOverrides; cameraOverrideLoadError_=!loaded.cameraOverrideWarning.empty();
    cameraBaseline_=CameraSequence::ToJson(cameraDrafts_);
    cameraSequenceStatus_=loaded.cameraWarning+" "+loaded.cameraOverrideWarning;
    if (cameraSequenceStatus_==" ") cameraSequenceStatus_="Camera sequences loaded. Blender base + F7 overrides.";
    cameraHistory_={cameraDrafts_}; cameraHistoryIndex_=0;
    cameraPreviewTime_=0; drawHeight_=level_.playerPosition.y+player_.Settings().cameraHeight; EnsureCameraSelection();
}

CameraSequenceDefinition* EnemyPlacementEditorScene::SelectedCameraSequence() {
    for (auto& d:cameraDrafts_) if (d.id==selectedCameraSequence_) return &d;
    return nullptr;
}
CameraSequencePoint* EnemyPlacementEditorScene::SelectedCameraPoint() {
    if (auto* d=SelectedCameraSequence()) for (auto& p:d->points) if (p.id==selectedCameraPoint_) return &p;
    return nullptr;
}
void EnemyPlacementEditorScene::EnsureCameraSelection() {
    auto* d=SelectedCameraSequence();
    if (!d && !cameraDrafts_.empty()) { selectedCameraSequence_=cameraDrafts_.front().id; d=&cameraDrafts_.front(); }
    if (!d) { selectedCameraSequence_.clear(); selectedCameraPoint_.clear(); return; }
    if (!SelectedCameraPoint()) selectedCameraPoint_=d->points.empty() ? std::string{} : d->points.front().id;
}
CameraPose EnemyPlacementEditorScene::CameraEyePose() const {
    return {level_.playerPosition+Vector3{0,player_.Settings().cameraHeight,0},level_.playerRotation,60*CameraSequence::DegreesToRadians};
}
bool EnemyPlacementEditorScene::BuildCameraEvaluator(CameraSequence& evaluator,std::string& error) const {
    try {
        for (const auto& d:cameraDrafts_) if (d.id==selectedCameraSequence_) {
            const auto eye=CameraEyePose();
            const nlohmann::json key={CameraSequence::ToJson({d}),eye.position.x,eye.position.y,eye.position.z,eye.rotation.x,eye.rotation.y,eye.rotation.z,eye.fov,
                level_.playerPosition.x,level_.playerPosition.y,level_.playerPosition.z};
            if (key!=cameraEvaluatorKey_) {
                cameraEvaluatorKey_=key; cameraEvaluatorValid_=false;
                try {
                    auto checked=CameraSequence::Parse({{"cameraSequences",key[0]}});
                    checked.front().enabled=true;
                    cameraEvaluatorValid_=cameraEvaluatorCache_.Start(checked.front(),eye,level_.playerPosition);
                    cameraEvaluatorError_=cameraEvaluatorValid_ ? "" : "Cannot evaluate sequence";
                } catch (const std::exception& e) { cameraEvaluatorError_=e.what(); }
            }
            error=cameraEvaluatorError_; if (cameraEvaluatorValid_) evaluator=cameraEvaluatorCache_;
            return cameraEvaluatorValid_;
        }
        throw std::runtime_error("Select or create a Sequence ID");
    } catch (const std::exception& e) { error=e.what(); return false; }
}
CameraPose EnemyPlacementEditorScene::CameraPointPose(size_t index) const {
    CameraSequence evaluator;
    if (BuildCameraGeometry(evaluator)) return evaluator.PointPose(index);
    for (const auto& d:cameraDrafts_) if (d.id==selectedCameraSequence_ && index<d.points.size()) {
        const auto& p=d.points[index];
        return p.playerEye ? CameraEyePose() : CameraPose{p.position,p.rotation,p.fov*CameraSequence::DegreesToRadians};
    }
    return CameraEyePose();
}
bool EnemyPlacementEditorScene::BuildCameraGeometry(CameraSequence& geometry) const {
    std::string error;
    if (BuildCameraEvaluator(geometry,error)) return true;
    for (auto draft:cameraDrafts_) if (draft.id==selectedCameraSequence_) {
        if (draft.points.size()<2) return false;
        const auto finite=[](const Vector3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
        // Spatial guides remain editable even while timing or Hold constraints are
        // incomplete. Only this display snapshot is relaxed; never the saved draft.
        draft.enabled=true;
        for (size_t i=0;i<draft.points.size();++i) {
            auto& p=draft.points[i];
            if (!finite(p.position) || !finite(p.rotation) || !finite(p.target) || !finite(p.lookOffset) || !finite(p.handleIn) || !finite(p.handleOut)) return false;
            p.duration=i+1<draft.points.size() ? 1 : 0;
            if (!std::isfinite(p.fov)) p.fov=60;
            if (p.pathType.empty() && p.interpolation=="Hold") p.pathType="Linear";
        }
        const auto eye=CameraEyePose();
        const nlohmann::json key={CameraSequence::ToJson({draft}),eye.position.x,eye.position.y,eye.position.z,eye.rotation.x,eye.rotation.y,eye.rotation.z,eye.fov,
            level_.playerPosition.x,level_.playerPosition.y,level_.playerPosition.z};
        if (key!=cameraGeometryKey_) {
            cameraGeometryKey_=key;
            cameraGeometryValid_=cameraGeometryCache_.Start(draft,eye,level_.playerPosition);
        }
        if (cameraGeometryValid_) geometry=cameraGeometryCache_;
        return cameraGeometryValid_;
    }
    return false;
}

void EnemyPlacementEditorScene::CommitCameraHistory() {
    if (cameraHistory_.empty()) { cameraHistory_={cameraDrafts_}; cameraHistoryIndex_=0; return; }
    if (CameraSequence::ToJson(cameraHistory_[cameraHistoryIndex_])==CameraSequence::ToJson(cameraDrafts_)) return;
    cameraHistory_.resize(cameraHistoryIndex_+1); cameraHistory_.push_back(cameraDrafts_);
    if (cameraHistory_.size()>100) cameraHistory_.erase(cameraHistory_.begin());
    cameraHistoryIndex_=cameraHistory_.size()-1;
}
void EnemyPlacementEditorScene::UndoCameraSequence(bool redo) {
    if (cameraPreviewActive_) return;
    CommitCameraHistory();
    if (redo) { if (cameraHistoryIndex_+1<cameraHistory_.size()) ++cameraHistoryIndex_; }
    else if (cameraHistoryIndex_>0) --cameraHistoryIndex_;
    cameraDrafts_=cameraHistory_[cameraHistoryIndex_]; EnsureCameraSelection();
    selectedCameraSegment_=selectedCameraHandle_=-1;
}
void EnemyPlacementEditorScene::ReindexCameraPoints(CameraSequenceDefinition& definition) {
    for (size_t i=0;i<definition.points.size();++i) {
        auto& p=definition.points[i]; p.order=static_cast<int>(i);
        if (i+1==definition.points.size()) p.duration=0;
        else if (p.duration<.001) p.duration=1;
    }
}
void EnemyPlacementEditorScene::AddCameraPoint(bool duplicate) {
    auto* d=SelectedCameraSequence(); if (!d || d->points.size()>=1024 || cameraPreviewActive_) return;
    CommitCameraHistory();
    size_t selected=0;
    for (size_t i=0;i<d->points.size();++i) if (d->points[i].id==selectedCameraPoint_) selected=i;
    CameraSequencePoint point;
    if (duplicate && !d->points.empty()) point=d->points[selected];
    else { point.position=camera_.GetTranslate(); point.rotation=camera_.GetRotate(); point.rotationMode="Manual"; point.fov=camera_.GetFovY()*toDegrees; }
    if (point.playerEye) { const auto eye=CameraEyePose(); point.position=eye.position; point.rotation=eye.rotation; point.rotationMode="Manual"; }
    point.playerEye=false; if (point.duration<.001) point.duration=1;
    point.id=NewCameraPointId();
    if (!duplicate) { point.pathType="Linear"; point.interpolation="Linear"; }
    const size_t insert=std::min(selected+1,d->points.size()-1);
    d->points.insert(d->points.begin()+static_cast<ptrdiff_t>(insert),point);
    ReindexCameraPoints(*d); selectedCameraPoint_=point.id; CommitCameraHistory();
    selectedCameraSegment_=selectedCameraHandle_=-1;
}
void EnemyPlacementEditorScene::DeleteCameraPoint() {
    auto* d=SelectedCameraSequence(); auto* p=SelectedCameraPoint();
    if (!d || !p || d->points.size()<=2 || p->playerEye || cameraPreviewActive_) return;
    CommitCameraHistory();
    std::erase_if(d->points,[&](const auto& point){return point.id==selectedCameraPoint_;});
    ReindexCameraPoints(*d); EnsureCameraSelection(); CommitCameraHistory();
    selectedCameraSegment_=selectedCameraHandle_=-1;
}
void EnemyPlacementEditorScene::MoveCameraPoint(int direction) {
    auto* d=SelectedCameraSequence(); if (!d || cameraPreviewActive_) return;
    for (size_t i=0;i<d->points.size();++i) if (d->points[i].id==selectedCameraPoint_) {
        const int target=static_cast<int>(i)+direction;
        if (target<0 || target>=static_cast<int>(d->points.size()) || d->points[i].playerEye || d->points[static_cast<size_t>(target)].playerEye) return;
        CommitCameraHistory(); std::swap(d->points[i],d->points[static_cast<size_t>(target)]);
        ReindexCameraPoints(*d); CommitCameraHistory(); return;
    }
}

bool EnemyPlacementEditorScene::SaveCameraSequence(const std::string& path) {
    auto* d=SelectedCameraSequence(); if (!d || cameraOverrideLoadError_ || cameraPreviewActive_ || !drawPathPoints_.empty()) return false;
    CameraSequence checked; std::string error;
    if (!BuildCameraEvaluator(checked,error)) { cameraSequenceStatus_="Validation: "+error; return false; }
    auto saved=cameraOverrides_; saved.stage=stageId_; ReplaceDefinition(saved.sequences,*d);
    if (!saved.Save(CameraSequenceOverrides::PathFor(path))) { cameraSequenceStatus_=saved.error; return false; }
    cameraOverrides_=std::move(saved);
    auto baseline=CameraSequence::Parse({{"cameraSequences",cameraBaseline_}});
    ReplaceDefinition(baseline,*d); cameraBaseline_=CameraSequence::ToJson(baseline);
    CommitCameraHistory(); cameraSequenceStatus_="Saved selected camera sequence only: "+CameraSequenceOverrides::PathFor(path); return true;
}
bool EnemyPlacementEditorScene::RevertCameraSequence(const std::string& path) {
    if (cameraPreviewActive_ || cameraOverrideLoadError_) return false;
    const auto id=selectedCameraSequence_;
    auto saved=cameraOverrides_;
    const auto count=saved.sequences.size(); std::erase_if(saved.sequences,[&](const auto& d){return d.id==id;});
    if (saved.sequences.size()!=count) {
        if (!saved.Save(CameraSequenceOverrides::PathFor(path))) { cameraSequenceStatus_=saved.error; return false; }
        cameraOverrides_=std::move(saved);
    }
    CommitCameraHistory();
    auto baseline=CameraSequence::Parse({{"cameraSequences",cameraBaseline_}});
    std::erase_if(cameraDrafts_,[&](const auto& d){return d.id==id;});
    std::erase_if(baseline,[&](const auto& d){return d.id==id;});
    for (const auto& d:blenderCameraDrafts_) if (d.id==id) { ReplaceDefinition(cameraDrafts_,d); ReplaceDefinition(baseline,d); }
    cameraBaseline_=CameraSequence::ToJson(baseline); EnsureCameraSelection(); CommitCameraHistory();
    cameraSequenceStatus_="F7 override removed for "+id+". Blender base is active."; return true;
}
void EnemyPlacementEditorScene::SetCameraSequenceMode(bool enabled) {
    CancelDrawPath();
    StopCameraPreview(); cameraSequenceMode_=enabled; cameraDragAxis_=-1; dragAxis_=-1; selectingBox_=false;
    if (!enabled && (mode_==CameraMode::Front || mode_==CameraMode::Side)) SwitchCamera(CameraMode::Free);
    if (enabled) { CommitCameraHistory(); EnsureCameraSelection(); }
}
void EnemyPlacementEditorScene::FocusCameraPoint() {
    if (cameraPreviewActive_) return;
    const auto target=SelectedPosition();
    if (mode_==CameraMode::Top) camera_.SetTranslate({target.x,target.y+topHeight_,target.z});
    else if (mode_==CameraMode::Front) camera_.SetTranslate({target.x,target.y,target.z-topHeight_});
    else if (mode_==CameraMode::Side) camera_.SetTranslate({target.x-topHeight_,target.y,target.z});
    else { mode_=CameraMode::Free; camera_.SetOrthographic(false); camera_.SetTranslate(target+Vector3{2,2,-4}); camera_.SetRotate({.42f,-.46f,0}); }
    camera_.Update();
}
std::string EnemyPlacementEditorScene::NewCameraPointId() const {
    for (unsigned int n=1;;++n) {
        const auto id="CameraPoint_"+std::to_string(n); bool used=false;
        for (const auto& d:cameraDrafts_) if (d.id==selectedCameraSequence_)
            for (const auto& p:d.points) used|=p.id==id;
        for (const auto& p:drawPathPoints_) used|=p.id==id;
        if (!used) return id;
    }
}
void EnemyPlacementEditorScene::SetPathTool(PathTool tool) {
    if (cameraPreviewActive_) return;
    CancelDrawPath(); pathTool_=tool; selectedCameraHandle_=-1;
    if (tool==PathTool::Draw) SwitchCamera(CameraMode::Top);
}
void EnemyPlacementEditorScene::CancelDrawPath() { drawPathPoints_.clear(); cameraDragAxis_=-1; }
void EnemyPlacementEditorScene::AddDrawPoint(const Vector3& position) {
    if (cameraPreviewActive_ || pathTool_!=PathTool::Draw || mode_!=CameraMode::Top || !SelectedCameraSequence() || drawPathPoints_.size()>=1023) return;
    CameraSequencePoint p; p.id=NewCameraPointId(); p.position=position; p.position.y=drawHeight_;
    p.pathType="Linear"; p.interpolation="Linear"; p.rotationMode="FollowTangent"; p.order=static_cast<int>(drawPathPoints_.size());
    drawPathPoints_.push_back(p);
}
bool EnemyPlacementEditorScene::FinishDrawPath() {
    auto* d=SelectedCameraSequence(); if (!d || cameraPreviewActive_) return false;
    auto result=*d; result.points=drawPathPoints_;
    if (d->id=="start" || (!d->points.empty() && d->points.back().playerEye)) {
        CameraSequencePoint eye;
        if (!d->points.empty() && d->points.back().playerEye) eye=d->points.back();
        else { eye.id="PlayerEye"; eye.playerEye=true; }
        result.points.push_back(eye);
    }
    if (result.points.size()<2) { cameraSequenceStatus_="Draw at least two points (one plus PlayerEye for start)."; return false; }
    ReindexCameraPoints(result);
    try { (void)CameraSequence::Parse({{"cameraSequences",CameraSequence::ToJson({result})}}); }
    catch (const std::exception& e) { cameraSequenceStatus_=e.what(); return false; }
    CommitCameraHistory(); *d=std::move(result); drawPathPoints_.clear(); pathTool_=PathTool::Path;
    selectedCameraPoint_=d->points.front().id; selectedCameraSegment_=0; selectedCameraHandle_=-1;
    cameraPreviewTime_=0; CommitCameraHistory(); cameraSequenceStatus_="Path created. Select a line to edit or insert a point."; return true;
}
bool EnemyPlacementEditorScene::InsertCameraPoint() {
    if (cameraPreviewActive_ || !drawPathPoints_.empty()) return false;
    auto* d=SelectedCameraSequence(); if (!d || selectedCameraSegment_<0 || static_cast<size_t>(selectedCameraSegment_)+1>=d->points.size()) return false;
    CameraSequence evaluator; std::string error;
    if (!BuildCameraEvaluator(evaluator,error)) { cameraSequenceStatus_=error; return false; }
    try {
        const auto id=NewCameraPointId(); auto result=evaluator.SplitSegment(static_cast<size_t>(selectedCameraSegment_),selectedSegmentParameter_,id);
        // Inspection enables disabled sequences temporarily; preserve the author flag.
        result.enabled=d->enabled;
        CommitCameraHistory(); *d=std::move(result); selectedCameraPoint_=id; ++selectedCameraSegment_; selectedCameraHandle_=-1;
        CommitCameraHistory(); cameraSequenceStatus_="Inserted point; original path shape retained."; return true;
    } catch (const std::exception& e) { cameraSequenceStatus_=e.what(); return false; }
}
void EnemyPlacementEditorScene::SetCameraHandle(bool outgoing,const Vector3& offset) {
    auto* p=SelectedCameraPoint(); auto* d=SelectedCameraSequence(); if (!p || !d || cameraPreviewActive_) return;
    const auto index=static_cast<size_t>(p-d->points.data());
    if (p->handleType=="Auto") {
        CameraSequence evaluator;
        if (!BuildCameraGeometry(evaluator)) return;
        p->handleIn=evaluator.HandleOffset(index,false); p->handleOut=evaluator.HandleOffset(index,true); p->handleType="Aligned";
    }
    auto& own=outgoing ? p->handleOut : p->handleIn;
    auto& other=outgoing ? p->handleIn : p->handleOut;
    const float length=StageLength(other),nextLength=StageLength(offset); own=offset;
    if (p->handleType=="Aligned" && nextLength>1e-6f) other=offset*(-length/nextLength);
}
void EnemyPlacementEditorScene::SetCameraHandleType(const std::string& type) {
    auto* p=SelectedCameraPoint(); auto* d=SelectedCameraSequence(); if (!p || !d || cameraPreviewActive_ || p->handleType==type) return;
    CameraSequence evaluator;
    if (!BuildCameraGeometry(evaluator)) return;
    const auto index=static_cast<size_t>(p-d->points.data());
    CommitCameraHistory(); p->handleIn=evaluator.HandleOffset(index,false); p->handleOut=evaluator.HandleOffset(index,true); p->handleType=type;
    if (type=="Aligned") {
        if (StageLength(p->handleOut)>1e-6f) SetCameraHandle(true,p->handleOut);
        else SetCameraHandle(false,p->handleIn);
    }
    CommitCameraHistory();
}
void EnemyPlacementEditorScene::SetCameraPathType(const std::string& type) {
    auto* d=SelectedCameraSequence(); if (!d || cameraPreviewActive_) return;
    if (selectedCameraSegment_<0) for (size_t i=0;i<d->points.size();++i) if (d->points[i].id==selectedCameraPoint_) selectedCameraSegment_=static_cast<int>(std::min(i,d->points.size()-2));
    if (selectedCameraSegment_<0 || static_cast<size_t>(selectedCameraSegment_)+1>=d->points.size()) return;
    auto& p=d->points[static_cast<size_t>(selectedCameraSegment_)]; if (p.pathType==type) return;
    CameraSequence evaluator; const bool valid=BuildCameraGeometry(evaluator);
    CommitCameraHistory();
    if (type=="Bezier" && valid) {
        const auto index=static_cast<size_t>(selectedCameraSegment_); const auto controls=evaluator.SegmentControls(index);
        for (size_t i=index;i<=index+1;++i) {
            d->points[i].handleIn=evaluator.HandleOffset(i,false); d->points[i].handleOut=evaluator.HandleOffset(i,true); d->points[i].handleType="Free";
        }
        p.handleOut=controls[1]-controls[0]; d->points[index+1].handleIn=controls[2]-controls[3];
    }
    p.pathType=type; if (!type.empty()) { if (p.interpolation=="SmoothStep") p.easing="EaseInOut"; p.interpolation="Linear"; }
    CommitCameraHistory();
}
void EnemyPlacementEditorScene::MoveCameraPathSelection(const Vector3& delta) {
    auto* p=SelectedCameraPoint(); if (!p || cameraPreviewActive_) return;
    if (selectedCameraHandle_>=0) {
        auto* d=SelectedCameraSequence(); CameraSequence evaluator;
        if (!BuildCameraGeometry(evaluator)) return;
        const bool outgoing=selectedCameraHandle_==1;
        SetCameraHandle(outgoing,evaluator.HandleOffset(static_cast<size_t>(p-d->points.data()),outgoing)+delta);
    } else if (!p->playerEye) p->position+=delta; // relative handles travel with the point
}
void EnemyPlacementEditorScene::ApplyCameraPreviewPose(const CameraPose& pose) {
    camera_.SetNearZ(.1f); camera_.SetFarClip(1000);
    camera_.SetOrthographic(false); camera_.SetTranslate(pose.position); camera_.SetRotate(pose.rotation); camera_.SetFovY(pose.fov); camera_.Update();
}
bool EnemyPlacementEditorScene::StartCameraPreview(bool playing) {
    if (!drawPathPoints_.empty()) { cameraSequenceStatus_="Finish Draw Path with Enter, or cancel with Esc."; return false; }
    if (!cameraPreviewActive_) {
        std::string error;
        if (!BuildCameraEvaluator(cameraPreview_,error)) { cameraSequenceStatus_=error; return false; }
        cameraEditingView_=camera_; cameraEditingMode_=mode_; cameraPreviewTime_=0; cameraPreviewActive_=true;
        environment_.SetHiddenMeshes({}); for (auto& shutter:shutters_) shutter->SetIsVisible(true);
    }
    cameraPreviewPlaying_=playing; ApplyCameraPreviewPose(cameraPreview_.Evaluate(cameraPreviewTime_)); return true;
}
void EnemyPlacementEditorScene::StopCameraPreview() {
    if (!cameraPreviewActive_) return;
    if (cameraEditingView_) camera_=*cameraEditingView_;
    mode_=cameraEditingMode_; cameraEditingView_.reset(); cameraPreviewActive_=cameraPreviewPlaying_=false;
    ApplyVisibility();
}
void EnemyPlacementEditorScene::SeekCameraPreview(double time) {
    if (!cameraPreviewActive_ && !StartCameraPreview(false)) return;
    cameraPreviewPlaying_=false; cameraPreviewTime_=std::isfinite(time) ? std::clamp(time,0.0,cameraPreview_.Duration()) : 0;
    ApplyCameraPreviewPose(cameraPreview_.Evaluate(cameraPreviewTime_));
}
void EnemyPlacementEditorScene::UpdateCameraPreview(double dt) {
    if (cameraPreviewPlaying_ && std::isfinite(dt) && dt>0) {
        cameraPreviewTime_=std::min(cameraPreview_.Duration(),cameraPreviewTime_+dt);
        if (cameraPreviewTime_>=cameraPreview_.Duration()) cameraPreviewPlaying_=false;
    }
    ApplyCameraPreviewPose(cameraPreview_.Evaluate(cameraPreviewTime_));
}
bool EnemyPlacementEditorScene::HandleCameraAction(const std::string& action) {
    if (action=="cameraMode") SetCameraSequenceMode(true);
    else if (action=="enemyMode") SetCameraSequenceMode(false);
    else if (action=="cameraSave") SaveCameraSequence(StagePath());
    else if (action=="cameraReload") LoadCameraSequences(StagePath());
    else if (action=="cameraRevert") RevertCameraSequence(StagePath());
    else if (action=="cameraPlay") StartCameraPreview(true);
    else if (action=="cameraPause") cameraPreviewPlaying_=false;
    else if (action=="cameraStop") StopCameraPreview();
    else if (action=="cameraRestart") { if (StartCameraPreview(true)) { cameraPreviewTime_=0; ApplyCameraPreviewPose(cameraPreview_.Evaluate(0)); } }
    else if (action=="cameraFinal") { if (StartCameraPreview(false)) SeekCameraPreview(cameraPreview_.Duration()); }
    else if (action=="cameraFocus") FocusCameraPoint();
    else if (action=="cameraSeek") SeekCameraPreview(cameraPreviewTime_);
    else return false;
    return true;
}

