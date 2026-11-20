#include "EnemyEditor.h"
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
#include "GameApp.h"
#include "ImGuiManagaer.h"
#include <cstring>
#include <iterator>
#include <tuple>

namespace {
using Json=nlohmann::json;
constexpr auto kMissingHpGroup=kNoEnemyPart-1;

Json FaceKey(const EnemyModelFaceId& id) { return {id.instance,id.mesh,id.material,id.triangle}; }

void WriteJson(const std::filesystem::path& path,const Json& value,int indent) {
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    auto temporary=path;
    temporary+=L".tmp";
    try {
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
        out<<value.dump(indent)<<'\n';
        out.close();
        if (!out || !MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Could not save "+EnemyPathText(path));
    } catch (...) {
        std::error_code error;
        std::filesystem::remove(temporary,error);
        throw;
    }
}

bool InputText(const char* label,std::string& value) {
    std::vector<char> buffer(std::max<size_t>(1024,value.size()+1),0);
    std::copy(value.begin(),value.end(),buffer.begin());
    if (!ImGui::InputText(label,buffer.data(),buffer.size())) return false;
    value=buffer.data();
    return true;
}

ImVec4 PartColor(size_t part) {
    if (part==kNoEnemyPart) return {.55f,.57f,.62f,1};
    ImVec4 color{1,1,1,1};
    ImGui::ColorConvertHSVtoRGB(std::fmod(static_cast<float>(part)*.618034f+.05f,1.f),.58f,.98f,color.x,color.y,color.z);
    return color;
}

bool Project(const Vector3& position,const Matrix4x4& vp,const ImVec2& min,const ImVec2& max,ImVec2& point) {
    const auto clip=EnemyPartTransformPoint(position,vp);
    const float w=position.x*vp.m[0][3]+position.y*vp.m[1][3]+position.z*vp.m[2][3]+vp.m[3][3];
    if (w<=0 || clip.z<0 || clip.z>w) return false;
    point={min.x+(clip.x/w*.5f+.5f)*(max.x-min.x),min.y+(.5f-clip.y/w*.5f)*(max.y-min.y)};
    return true;
}
}

EnemyEditor::FacePicking::FacePicking(const EnemyModelSurface* model) {
    if (!model || model->faces.empty()) return;
    const auto& bounds=model->bounds;
    const auto size=bounds.max-bounds.min;
    base={(bounds.min.x+bounds.max.x)*.5f,bounds.min.y,(bounds.min.z+bounds.max.z)*.5f};
    scale=2.48f/std::max({size.x,size.y,size.z});
    blocks.reserve((model->faces.size()+facesPerBlock-1)/facesPerBlock);
    centroids.reserve(model->faces.size());
    for (size_t start=0;start<model->faces.size();start+=facesPerBlock) {
        const auto first=Fit(model->faces[start].vertices.front().position);
        AABB block{first,first};
        const size_t end=std::min(start+facesPerBlock,model->faces.size());
        for (size_t i=start;i<end;++i) {
            Vector3 center{};
            for (const auto& vertex:model->faces[i].vertices) {
                const auto p=Fit(vertex.position);
                center+=p;
                block.min={std::min(block.min.x,p.x),std::min(block.min.y,p.y),std::min(block.min.z,p.z)};
                block.max={std::max(block.max.x,p.x),std::max(block.max.y,p.y),std::max(block.max.z,p.z)};
            }
            centroids.push_back(center*(1.f/3));
        }
        blocks.push_back(block);
    }
}

nlohmann::json EnemyEditor::Draft::Document() const {
    Json groups=Json::array(),items=Json::array();
    for (const auto& group:parts.hpGroups)
        groups.push_back({{"id",group.id},{"maxHp",group.maxHp},{"deathOnZero",group.deathOnZero}});
    for (const auto& part:parts) {
        if (part.sharedGroup!=kNoEnemyPart && part.sharedGroup>=parts.hpGroups.size())
            throw std::runtime_error("Choose a shared HP group for "+part.name);
        items.push_back({{"name",part.name},{"role",EnemyPartRoleName(part.role)},
            {"localHp",part.usesLocalHp ? Json(part.maxHp) : Json(nullptr)},{"breakable",part.breakable},
            {"deathOnZero",part.usesLocalHp && part.deathOnZero},{"sharedDamageRate",part.sharedDamageRate},
            {"sharedHpGroup",part.sharedGroup<parts.hpGroups.size() ? parts.hpGroups[part.sharedGroup].id : ""},
            {"faces",Json::array()}});
    }
    for (size_t i=0;i<owners.size();++i)
        if (owners[i]!=kNoEnemyPart) items[owners[i]]["faces"].push_back(FaceKey(model->faces[i].id));
    return {{"version",2},{"coordinateSystem","yanengine"},
        {"model",{{"path",modelPath},{"geometrySignature",model->signature},{"faceCount",owners.size()},{"scale",scale}}},
        {"hpGroups",std::move(groups)},{"parts",std::move(items)}};
}

void EnemyEditor::Open(const std::filesystem::path& current) {
    if (models_.empty()) RefreshModels();
    if (draft_.model || current.empty()) return;
    try {
        std::ifstream input(current);
        const auto json=Json::parse(input);
        if (json.value("version",0)==2) pending_=Request{Action::Load,current};
    } catch (const std::exception& error) { message_=error.what(); }
}

void EnemyEditor::RefreshModels() {
    models_.clear();
    std::error_code error;
    std::filesystem::recursive_directory_iterator it("resources",std::filesystem::directory_options::skip_permission_denied,error),end;
    for (;!error && it!=end;it.increment(error)) {
        std::error_code entryError;
        if (it->is_regular_file(entryError) && IsEnemyModelFile(it->path())) models_.push_back(it->path());
    }
    std::sort(models_.begin(),models_.end(),[](const auto& a,const auto& b) {
        return std::tuple(a.filename(),a)<std::tuple(b.filename(),b);
    });
    if (error) message_="Could not scan all model directories: "+error.message();
}

void EnemyEditor::RequestChange(Action action,const std::filesystem::path& path) {
    if (dirty_) {
        confirmation_=Request{action,path};
        ImGui::OpenPopup("Discard unsaved Enemy Asset edits?");
    } else pending_=Request{action,path};
}

void EnemyEditor::OpenDraft(GameApp& app,const Request& request) {
    Draft next;
    if (request.action==Action::Discard) next=saved_;
    if (request.action==Action::Model) {
        next.modelPath=EnemyPathText(request.path);
        next.model=ReadEnemyModel(next.modelPath);
        next.owners.resize(next.model->faces.size(),kNoEnemyPart);
    }
    if (request.action==Action::Load) {
        std::ifstream input(request.path);
        if (!input) throw std::runtime_error("Could not open "+EnemyPathText(request.path));
        const auto document=Json::parse(input);
        if (document.value("version",0)!=2)
            throw std::runtime_error("This editor opens model-referenced assets (version 2). Version 1 assets can be tested in Showroom or edited in Blender.");
        next.modelPath=document.at("model").at("path").get<std::string>();
        next.model=ReadEnemyModel(next.modelPath);
        const auto asset=EnemyAsset::Parse(document,request.path,next.model);
        next.path=request.path;
        next.scale=asset->modelScale;
        next.parts.hpGroups=asset->defaults.hpGroups;
        next.owners.resize(next.model->faces.size(),kNoEnemyPart);
        for (const auto& item:document.at("parts")) {
            auto part=asset->defaults[next.parts.size()];
            part.geometry.reset();
            for (const auto& key:item.at("faces")) {
                const auto id=EnemyAsset::ReadModelFaceId(key);
                next.owners[next.model->FindFace(id)]=next.parts.size();
            }
            next.parts.push_back(std::move(part));
        }
    }
    auto textures=next.model==draft_.model ? textures_ : BuildEnemyModelTextures(*next.model);
    auto preview=BuildPreview(app,next,textures);
    auto picking=next.model==draft_.model ? facePicking_ : FacePicking(next.model.get());
    Draft snapshot=next;
    std::vector<uint32_t> selectionSlots(next.owners.size(),noSelection);
    std::vector<size_t> partFaceCounts(next.parts.size());
    size_t assignedFaceCount=0;
    for (auto owner:next.owners) if (owner!=kNoEnemyPart) {
        ++partFaceCounts[owner];
        ++assignedFaceCount;
    }
    auto name=next.path.empty() ? "resources/enemy/custom/new.enemy.json" : EnemyPathText(next.path);
    app.Dx()->WaitForGPU();
    draft_=std::move(next);
    saved_=std::move(snapshot);
    textures_=std::move(textures);
    preview_=std::move(preview);
    facePicking_=std::move(picking);
    selectionSlots_=std::move(selectionSlots);
    selectedFaces_.clear();
    selectedAssignedCount_=0;
    partFaceCounts_=std::move(partFaceCounts);
    assignedFaceCount_=assignedFaceCount;
    outlineDirty_=true;
    dragStart_.reset();
    activePart_=draft_.parts.empty() ? kNoEnemyPart : 0;
    activeGroup_=draft_.parts.hpGroups.empty() ? kNoEnemyPart : 0;
    dirty_=previewDirty_=false;
    fileName_=std::move(name);
    pan_={}; yaw_=0; pitch_=.16f; distance_=4.8f*draft_.scale;
    message_.clear();
    for (const auto& face:draft_.model->faces) {
        const auto material=face.id.material;
        if (!draft_.model->materials[material].texture.empty() && !textures_[material]) {
            message_="Some textures were unavailable. Affected materials use white.";
            break;
        }
    }
}

std::vector<EnemyEditor::PreviewPart> EnemyEditor::BuildPreview(GameApp& app,const Draft& draft,const EnemyModelTextures& textures) {
    std::map<size_t,EnemyPartGeometry> geometry;
    AABB unused{};
    for (size_t i=0;i<draft.owners.size();++i)
        EnemyAsset::AppendModelFace(geometry[draft.owners[i]],unused,*draft.model,i,1);
    std::vector<PreviewPart> result;
    for (const auto& [owner,surface]:geometry) {
        auto model=BuildEnemyPartModel(surface,*draft.model,textures);
        auto object=std::make_unique<Object3d>();
        object->Initialize(app.ObjCom(),app.Dx());
        object->SetModel(model.get());
        object->SetCamera(&camera_);
        object->SetDirection({.3f,-1,.5f});
        object->SetIntensity(1);
        object->SetPointLightIntensity(0);
        object->SetSpotLightIntensity(0);
        result.push_back({owner,std::move(model),std::move(object)});
    }
    return result;
}

void EnemyEditor::Update(GameApp& app) {
    try {
        if (const auto request=std::exchange(pending_,std::nullopt)) OpenDraft(app,*request);
        if (previewDirty_) {
            previewDirty_=false;
            auto next=BuildPreview(app,draft_,textures_);
            app.Dx()->WaitForGPU();
            preview_=std::move(next);
        }
    } catch (const std::exception& error) { message_=error.what(); }
    if (!draft_.model) return;
    renderScale_=draft_.scale;
    camera_.SetAspect(1280.f/720.f);
    camera_.SetFovY(.70f);
    camera_.SetNearZ(.002f);
    camera_.SetFarClip(2500);
    const auto rotation=Matrix4x4::RotateXYZ(pitch_,yaw_,0);
    const auto upper=draft_.model->Fit(draft_.model->bounds.max,renderScale_);
    const auto center=pan_+Vector3{0,upper.y*.5f,0};
    camera_.SetTranslate(EnemyPartTransformPoint({0,0,-distance_},rotation)+center);
    camera_.SetRotate({pitch_,yaw_,0});
    camera_.Update();
    for (auto& entry:preview_) {
        const bool tint=(partColors_ && !draft_.parts.empty()) ||
            (activePart_!=kNoEnemyPart && entry.owner==activePart_);
        const auto color=tint ? PartColor(entry.owner) : ImVec4(1,1,1,1);
        const bool dim=dimOthers_ && activePart_!=kNoEnemyPart && entry.owner!=activePart_;
        entry.object->SetMaterialColor({color.x,color.y,color.z,dim ? .17f : 1.f});
        entry.object->SetScale({renderScale_,renderScale_,renderScale_});
        entry.object->Update(0);
    }
}

bool EnemyEditor::Visible(size_t owner) const {
    return (showUnassigned_ || owner!=kNoEnemyPart) && (!isolate_ || activePart_==kNoEnemyPart || owner==activePart_);
}

void EnemyEditor::DrawPreview() {
    for (auto& part:preview_) if (Visible(part.owner) && part.owner==activePart_) part.object->Draw();
    for (auto& part:preview_) if (Visible(part.owner) && part.owner!=activePart_) part.object->Draw();
}

void EnemyEditor::Save(const std::filesystem::path& path) {
    if (!path.filename().u8string().ends_with(u8".enemy.json")) throw std::runtime_error("Use the .enemy.json extension.");
    const auto document=draft_.Document();
    EnemyAsset::Parse(document,path,ReadEnemyModel(draft_.modelPath));
    Draft snapshot=draft_;
    snapshot.path=path;
    WriteJson(path,document,2);
    draft_.path=path;
    saved_=std::move(snapshot);
    fileName_=EnemyPathText(path);
    dirty_=false;
    message_="Saved "+fileName_;
}

void EnemyEditor::ExportBlenderMesh() {
    if (dirty_ || draft_.path.empty()) throw std::runtime_error("Save this Enemy Asset before exporting its Blender mesh.");
    Json faces=Json::array();
    for (const auto& face:draft_.model->faces) {
        Json vertices=Json::array();
        for (const auto& v:face.vertices)
            vertices.push_back({{"p",{v.position.x,v.position.y,v.position.z}},{"n",{v.normal.x,v.normal.y,v.normal.z}},{"uv",{v.uv.x,v.uv.y}}});
        faces.push_back({{"id",FaceKey(face.id)},{"vertices",std::move(vertices)}});
    }
    Json materials=Json::array();
    for (const auto& material:draft_.model->materials) materials.push_back({{"texture",material.texture},{"color",material.color}});
    auto path=draft_.path;
    path+=L".mesh.json";
    WriteJson(path,{{"model",draft_.Document()["model"]},{"faces",std::move(faces)},{"materials",std::move(materials)}},-1);
    message_="Exported "+EnemyPathText(path);
}

void EnemyEditor::DrawFiles() {
    if (ImGui::Button("Rescan resources")) RefreshModels();
    InputText("Filter models",search_);
    const auto label=draft_.modelPath.empty() ? "Select a model" : draft_.modelPath.c_str();
    if (ImGui::BeginCombo("Model",label)) {
        std::string query=search_;
        const auto lower=[](unsigned char c) { return c>='A' && c<='Z' ? static_cast<char>(c-'A'+'a') : static_cast<char>(c); };
        std::transform(query.begin(),query.end(),query.begin(),lower);
        for (const auto& path:models_) {
            const auto reference=EnemyPathText(path);
            const auto title=EnemyPathText(path.filename())+"  |  "+reference;
            auto filter=title;
            std::transform(filter.begin(),filter.end(),filter.begin(),lower);
            if (filter.find(query)!=std::string::npos && ImGui::Selectable(title.c_str(),reference==draft_.modelPath)) {
                // Open the confirmation at window scope, after the combo popup closes.
                confirmation_=Request{Action::Model,path};
            }
        }
        ImGui::EndCombo();
        if (confirmation_ && confirmation_->action==Action::Model) {
            const auto path=confirmation_->path;
            confirmation_.reset();
            RequestChange(Action::Model,path);
        }
    }
    InputText("Enemy Asset path",fileName_);
    if (ImGui::Button("Load JSON")) RequestChange(Action::Load,EnemyUtf8Path(fileName_));
    ImGui::SameLine();
    ImGui::BeginDisabled(draft_.path.empty());
    if (ImGui::Button("Reload")) RequestChange(Action::Load,draft_.path);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty_);
    if (ImGui::Button("Discard edits")) RequestChange(Action::Discard);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!draft_.model);
    try {
        if (ImGui::Button("Save")) Save(draft_.path.empty() ? EnemyUtf8Path(fileName_) : draft_.path);
        ImGui::SameLine();
        if (ImGui::Button("Save As")) Save(EnemyUtf8Path(fileName_));
        ImGui::SameLine();
        if (ImGui::Button("Blender mesh")) ExportBlenderMesh();
        if (ImGui::Button("Test this draft in Showroom"))
            testRequest_=EnemyAsset::Parse(draft_.Document(),draft_.path,draft_.model);
    } catch (const std::exception& error) { message_=error.what(); }
    ImGui::EndDisabled();
    if (dirty_) ImGui::TextUnformatted("Unsaved edits");
    if (!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
    if (ImGui::BeginPopupModal("Discard unsaved Enemy Asset edits?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Continue and discard the current edits?");
        if (ImGui::Button("Discard and continue")) {
            pending_=std::exchange(confirmation_,std::nullopt);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep editing")) {
            confirmation_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EnemyEditor::SetFaceSelected(uint32_t index,bool selected) {
    auto& slot=selectionSlots_[index];
    if ((slot!=noSelection)==selected) return;
    const bool assigned=draft_.owners[index]!=kNoEnemyPart;
    if (selected) {
        selectedFaces_.push_back(index);
        slot=static_cast<uint32_t>(selectedFaces_.size()-1);
        if (assigned) ++selectedAssignedCount_;
    } else {
        const auto last=selectedFaces_.back();
        selectedFaces_[slot]=last;
        selectionSlots_[last]=slot;
        selectedFaces_.pop_back();
        slot=noSelection;
        if (assigned) --selectedAssignedCount_;
    }
    outlineDirty_=true;
}

void EnemyEditor::ClearFaceSelection() {
    for (uint32_t index:selectedFaces_) selectionSlots_[index]=noSelection;
    selectedFaces_.clear();
    selectedAssignedCount_=0;
    outlineDirty_=true;
}

void EnemyEditor::SelectPartFaces() {
    ClearFaceSelection();
    // Explicit inspection can highlight assigned faces even while they are locked.
    for (uint32_t i=0;i<draft_.owners.size();++i)
        if (draft_.owners[i]==activePart_) SetFaceSelected(i,true);
}

void EnemyEditor::SetAssignedFaceLock(bool enabled) {
    lockAssignedFaces_=enabled;
    dragStart_.reset();
    if (enabled) for (size_t slot=selectedFaces_.size();slot>0;--slot) {
        const auto index=selectedFaces_[slot-1];
        if (draft_.owners[index]!=kNoEnemyPart) SetFaceSelected(index,false);
    }
}

void EnemyEditor::ApplyPickedFace(uint32_t index) {
    if (selectionMode_==SelectionMode::Deselect) SetFaceSelected(index,false);
    else if (!lockAssignedFaces_ || draft_.owners[index]==kNoEnemyPart) SetFaceSelected(index,true);
}

void EnemyEditor::ApplyPickedRectangle(const Matrix4x4& vp,const ImVec2& min,const ImVec2& max,const ImVec2& lo,const ImVec2& hi) {
    const auto inside=[&](uint32_t index) {
        if (!Visible(draft_.owners[index])) return false;
        ImVec2 point;
        return Project(facePicking_.centroids[index],vp,min,max,point) &&
            point.x>=lo.x && point.y>=lo.y && point.x<=hi.x && point.y<=hi.y;
    };
    if (selectionMode_==SelectionMode::Deselect) {
        // Iterate backwards because removing a face moves the last entry into its slot.
        for (size_t slot=selectedFaces_.size();slot>0;--slot) {
            const auto index=selectedFaces_[slot-1];
            if (inside(index)) ApplyPickedFace(index);
        }
    } else {
        for (uint32_t index=0;index<draft_.owners.size();++index) {
            if (selectionSlots_[index]!=noSelection ||
                (lockAssignedFaces_ && draft_.owners[index]!=kNoEnemyPart)) continue;
            if (inside(index)) ApplyPickedFace(index);
        }
    }
}

void EnemyEditor::ApplySelection(size_t owner) {
    bool changed=false;
    for (uint32_t index:selectedFaces_) {
        auto& current=draft_.owners[index];
        if (current==owner || (lockAssignedFaces_ && current!=kNoEnemyPart)) continue;
        if (current!=kNoEnemyPart) {
            --partFaceCounts_[current];
            --assignedFaceCount_;
            --selectedAssignedCount_;
        }
        if (owner!=kNoEnemyPart) {
            ++partFaceCounts_[owner];
            ++assignedFaceCount_;
            ++selectedAssignedCount_;
        }
        current=owner;
        changed=true;
    }
    if (!changed) return;
    dirty_=previewDirty_=outlineDirty_=true;
    if (lockAssignedFaces_) ClearFaceSelection();
}

void EnemyEditor::DrawParts() {
    if (!draft_.model) return;
    ImGui::SeparatorText("Parts");
    if (ImGui::Button("Add Part") && draft_.parts.size()<4096) {
        EnemyPart part;
        size_t number=draft_.parts.size()+1;
        do { part.name="Part "+std::to_string(number++); }
        while (std::any_of(draft_.parts.begin(),draft_.parts.end(),[&](const auto& p){return p.name==part.name;}));
        part.maxHp=part.hp=60;
        activePart_=draft_.parts.size();
        draft_.parts.push_back(std::move(part));
        partFaceCounts_.push_back(0);
        dirty_=true;
        outlineDirty_=true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(activePart_>=draft_.parts.size());
    if (ImGui::Button("Delete Part")) {
        draft_.parts.erase(draft_.parts.begin()+activePart_);
        assignedFaceCount_-=partFaceCounts_[activePart_];
        partFaceCounts_.erase(partFaceCounts_.begin()+activePart_);
        for (size_t i=0;i<draft_.owners.size();++i) {
            auto& owner=draft_.owners[i];
            if (owner==activePart_) {
                if (selectionSlots_[i]!=noSelection) --selectedAssignedCount_;
                owner=kNoEnemyPart;
            }
            else if (owner!=kNoEnemyPart && owner>activePart_) --owner;
        }
        activePart_=draft_.parts.empty() ? kNoEnemyPart : std::min(activePart_,draft_.parts.size()-1);
        dirty_=previewDirty_=outlineDirty_=true;
    }
    ImGui::EndDisabled();
    if (ImGui::BeginListBox("##Parts",{-1,120})) {
        for (size_t i=0;i<draft_.parts.size();++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::PushStyleColor(ImGuiCol_Text,PartColor(i));
            const auto title=draft_.parts[i].name+" ("+std::to_string(partFaceCounts_[i])+")";
            if (ImGui::Selectable(title.c_str(),activePart_==i)) {
                activePart_=i;
                outlineDirty_=true;
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
    const size_t editable=selectedFaces_.size()-(lockAssignedFaces_ ? selectedAssignedCount_ : 0);
    ImGui::Text("%zu unassigned / %zu faces | %zu selected",draft_.owners.size()-assignedFaceCount_,
        draft_.owners.size(),selectedFaces_.size());
    ImGui::BeginDisabled(activePart_>=draft_.parts.size() || editable==0);
    if (ImGui::Button("Assign selection")) ApplySelection(activePart_);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(lockAssignedFaces_ || selectedAssignedCount_==0);
    if (ImGui::Button("Unassign selection")) ApplySelection(kNoEnemyPart);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(selectedFaces_.empty());
    if (ImGui::Button("Clear selection")) ClearFaceSelection();
    ImGui::EndDisabled();
    if (activePart_>=draft_.parts.size()) return;
    auto& part=draft_.parts[activePart_];
    if (ImGui::Button("Select Part faces")) SelectPartFaces();
    if (lockAssignedFaces_ && (selectedAssignedCount_ || partFaceCounts_[activePart_]))
        ImGui::TextWrapped("Turn off Lock assigned faces to edit assigned faces.");
    if (InputText("Part name",part.name)) dirty_=true;
    if (ImGui::BeginCombo("Role",EnemyPartRoleName(part.role))) {
        for (auto role:kEnemyPartRoles) if (ImGui::Selectable(EnemyPartRoleName(role),part.role==role)) {
            part.role=role;
            dirty_=true;
        }
        ImGui::EndCombo();
    }
    const bool shared=part.sharedGroup!=kNoEnemyPart;
    int mode=part.usesLocalHp ? (shared ? 2 : 0) : (shared ? 1 : 3);
    if (ImGui::Combo("HP mode",&mode,"Local\0Shared\0LocalAndShared\0Invincible\0")) {
        part.usesLocalHp=mode==0 || mode==2;
        part.sharedGroup=mode==1 || mode==2 ? (shared ? part.sharedGroup : kMissingHpGroup) : kNoEnemyPart;
        if (part.usesLocalHp && part.maxHp<=0) part.maxHp=60;
        if (!part.usesLocalHp) part.deathOnZero=false;
        dirty_=true;
    }
    if (part.usesLocalHp) {
        if (ImGui::DragFloat("Local HP",&part.maxHp,.5f,.001f,1e8f,"%.3f",ImGuiSliderFlags_AlwaysClamp)) dirty_=true;
        if (ImGui::Checkbox("Death on Zero",&part.deathOnZero)) dirty_=true;
    }
    if (ImGui::Checkbox("Breakable",&part.breakable)) dirty_=true;
    if (part.sharedGroup!=kNoEnemyPart) {
        const auto& groups=draft_.parts.hpGroups;
        const auto* group=part.sharedGroup<groups.size() ? groups[part.sharedGroup].id.c_str() : "Choose HP group";
        if (ImGui::BeginCombo("Shared HP group",group)) {
            for (size_t i=0;i<groups.size();++i) {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(groups[i].id.c_str(),part.sharedGroup==i)) { part.sharedGroup=i; dirty_=true; }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::DragFloat("Shared Damage Rate",&part.sharedDamageRate,.01f,0,1e8f,"%.3f",ImGuiSliderFlags_AlwaysClamp)) dirty_=true;
    }
}

void EnemyEditor::DrawGroups() {
    if (!draft_.model) return;
    auto& groups=draft_.parts.hpGroups;
    ImGui::SeparatorText("Shared HP groups");
    if (ImGui::Button("Add Group") && groups.size()<4096) {
        std::string id;
        size_t number=groups.size()+1;
        do { id="Group "+std::to_string(number++); }
        while (std::any_of(groups.begin(),groups.end(),[&](const auto& g){return g.id==id;}));
        activeGroup_=groups.size();
        groups.push_back({std::move(id),500,500,true});
        dirty_=true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(activeGroup_>=groups.size());
    if (ImGui::Button("Delete Group")) {
        groups.erase(groups.begin()+activeGroup_);
        for (auto& part:draft_.parts) {
            if (part.sharedGroup==activeGroup_) part.sharedGroup=kMissingHpGroup;
            else if (part.sharedGroup<kMissingHpGroup && part.sharedGroup>activeGroup_) --part.sharedGroup;
        }
        activeGroup_=groups.empty() ? kNoEnemyPart : std::min(activeGroup_,groups.size()-1);
        dirty_=true;
    }
    ImGui::EndDisabled();
    if (ImGui::BeginCombo("Group",activeGroup_<groups.size() ? groups[activeGroup_].id.c_str() : "Select a group")) {
        for (size_t i=0;i<groups.size();++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(groups[i].id.c_str(),activeGroup_==i)) activeGroup_=i;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (activeGroup_>=groups.size()) return;
    auto& group=groups[activeGroup_];
    if (InputText("Group ID",group.id)) dirty_=true;
    if (ImGui::DragFloat("Group HP",&group.maxHp,1,.001f,1e8f,"%.3f",ImGuiSliderFlags_AlwaysClamp)) dirty_=true;
    if (ImGui::Checkbox("Death on Group Zero",&group.deathOnZero)) dirty_=true;
}

size_t EnemyEditor::PickFace(const Vector3& origin,const Vector3& direction,float range) const {
    float nearest=range;
    size_t hit=kNoEnemyPart;
    for (size_t block=0;block<facePicking_.blocks.size();++block) {
        float entry;
        if (!RaycastAABB(origin,direction,facePicking_.blocks[block],nearest,entry)) continue;
        const size_t start=block*FacePicking::facesPerBlock;
        const size_t end=std::min(start+FacePicking::facesPerBlock,draft_.owners.size());
        for (size_t i=start;i<end;++i) {
            if (!Visible(draft_.owners[i])) continue;
            const auto& v=draft_.model->faces[i].vertices;
            const std::array triangle{facePicking_.Fit(v[0].position),facePicking_.Fit(v[1].position),facePicking_.Fit(v[2].position)};
            if (RaycastEnemyTriangle(origin,direction,triangle,nearest)) hit=i;
        }
    }
    return hit;
}

void EnemyEditor::DrawViewport(GameApp& app) {
    if (!draft_.model) {
        ImGui::TextUnformatted("Choose a model or load an Enemy Asset.");
        return;
    }
    ImGui::TextUnformatted("LMB face / drag box | RMB orbit | MMB pan | Wheel zoom");
    auto mode=selectionMode_;
    if (ImGui::RadioButton("Select",mode==SelectionMode::Select)) mode=SelectionMode::Select;
    ImGui::SameLine();
    if (ImGui::RadioButton("Deselect",mode==SelectionMode::Deselect)) mode=SelectionMode::Deselect;
    if (mode!=selectionMode_) {
        selectionMode_=mode;
        dragStart_.reset();
    }
    ImGui::SameLine();
    bool locked=lockAssignedFaces_;
    if (ImGui::Checkbox("Lock assigned faces",&locked)) SetAssignedFaceLock(locked);
    ImGui::Checkbox("Part colors",&partColors_);
    ImGui::SameLine();
    if (ImGui::Checkbox("Show unassigned",&showUnassigned_)) outlineDirty_=true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Isolate Part",&isolate_)) outlineDirty_=true;
    ImGui::SameLine();
    ImGui::Checkbox("Dim other Parts",&dimOthers_);
    ImGui::SetNextItemWidth(110);
    if (ImGui::DragFloat("Model scale",&draft_.scale,.01f,.01f,100,"%.3f",ImGuiSliderFlags_AlwaysClamp)) dirty_=true;
    ImGui::SameLine();
    if (ImGui::Button("Frame model")) { pan_={}; distance_=4.8f*draft_.scale; }
    app.ImGui()->DrawScenePreview();
    RECT rect{};
    if (!app.ImGui()->GetSceneImageRect(rect)) return;
    const ImVec2 min{static_cast<float>(rect.left),static_cast<float>(rect.top)};
    const ImVec2 max{static_cast<float>(rect.right),static_cast<float>(rect.bottom)};
    const auto& input=ImGui::GetIO();
    const auto mouse=input.MousePos;
    const float height=max.y-min.y;
    const bool hovered=app.ImGui()->IsSceneImageHovered();
    if (hovered) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right,0)) {
            yaw_+=input.MouseDelta.x*.008f;
            pitch_=std::clamp(pitch_+input.MouseDelta.y*.008f,-1.54f,1.54f);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle,0)) {
            const auto rotation=Matrix4x4::RotateXYZ(pitch_,yaw_,0);
            const auto movement=Vector3{-input.MouseDelta.x,input.MouseDelta.y,0}*(distance_/height);
            pan_=pan_+EnemyPartTransformPoint(movement,rotation);
        }
        if (input.MouseWheel)
            distance_=std::clamp(distance_*std::exp(-input.MouseWheel*.15f),.01f,1500.f);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) dragStart_=Vector2{mouse.x,mouse.y};
    }

    const auto vp=Matrix4x4::Scale({renderScale_,renderScale_,renderScale_})*camera_.GetViewProjectionMatrix();
    const auto project=[&](const Vector3& position,ImVec2& p) { return Project(facePicking_.Fit(position),vp,min,max,p); };
    auto* draw=ImGui::GetWindowDrawList();
    draw->PushClipRect(min,max,true);
    if (dragStart_) {
        const ImVec2 end{std::clamp(mouse.x,min.x,max.x),std::clamp(mouse.y,min.y,max.y)};
        const ImVec2 lo{std::min(dragStart_->x,end.x),std::min(dragStart_->y,end.y)};
        const ImVec2 hi{std::max(dragStart_->x,end.x),std::max(dragStart_->y,end.y)};
        const bool box=std::hypot(hi.x-lo.x,hi.y-lo.y)>4;
        const bool deselect=selectionMode_==SelectionMode::Deselect;
        if (box) {
            draw->AddRectFilled(lo,hi,deselect ? IM_COL32(255,116,96,45) : IM_COL32(86,174,255,45));
            draw->AddRect(lo,hi,deselect ? IM_COL32(255,150,134,255) : IM_COL32(140,208,255,255));
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (box) {
                ApplyPickedRectangle(vp,min,max,lo,hi);
            } else if (hovered) {
                const float x=(end.x-min.x)/(max.x-min.x)*2-1;
                const float y=1-(end.y-min.y)/height*2;
                const auto& projection=camera_.GetProjectionMatrix();
                const auto origin=camera_.GetTranslate();
                const auto direction=Matrix4x4::Normalize(EnemyPartTransformPoint(
                    {x/projection.m[0][0],y/projection.m[1][1],1},camera_.GetWorldMatrix())-origin);
                // Keep locked surfaces in the raycast so a click never selects through them.
                const float inverseScale=1.f/renderScale_;
                const auto hit=PickFace(origin*inverseScale,direction,2500*inverseScale);
                if (hit!=kNoEnemyPart) ApplyPickedFace(static_cast<uint32_t>(hit));
            }
            dragStart_.reset();
        }
    }
    if (outlineDirty_) {
        outlineFaces_.clear();
        for (auto index:selectedFaces_) {
            if (Visible(draft_.owners[index])) outlineFaces_.push_back(index);
            if (outlineFaces_.size()==3000) break;
        }
        outlineDirty_=false;
    }
    for (auto index:outlineFaces_) {
        const auto& v=draft_.model->faces[index].vertices;
        ImVec2 a,b,c;
        if (project(v[0].position,a) && project(v[1].position,b) && project(v[2].position,c))
            draw->AddTriangle(a,b,c,IM_COL32(255,222,103,240),1.2f);
    }
    draw->PopClipRect();
}

void EnemyEditor::DrawUI(GameApp& app,bool& visible) {
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::Begin("Enemy Editor",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoDocking);
    ImGui::TextUnformatted("Enemy Editor");
    ImGui::SameLine();
    if (ImGui::Button("Back to Weapon Editor")) { visible=false; dragStart_.reset(); }
    if (ImGui::BeginTable("Enemy Editor layout",2,ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Asset",ImGuiTableColumnFlags_WidthFixed,std::min(410.f,viewport->WorkSize.x*.38f));
        ImGui::TableSetupColumn("Model",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
        ImGui::BeginChild("Asset and parts");
        DrawFiles();
        DrawParts();
        DrawGroups();
        ImGui::EndChild();
        ImGui::TableNextColumn();
        ImGui::BeginChild("Model preview");
        DrawViewport(app);
        ImGui::EndChild();
        ImGui::EndTable();
    }
    ImGui::End();
}
#endif
