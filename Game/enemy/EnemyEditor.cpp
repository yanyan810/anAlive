#include "EnemyEditor.h"
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
#include "GameApp.h"
#include "ImGuiManagaer.h"
#include <cstring>
#include <iterator>
#include <numeric>
#include <tuple>
#include <unordered_map>

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

EnemyEditor::Components::Components(const EnemyModelSurface& model,std::span<const std::array<uint32_t,3>> vertices,
    std::span<const FaceGroup> materials,bool withinMaterial) {
    const auto count=static_cast<uint32_t>(model.faces.size());
    std::vector<uint32_t> parent(count),sizes(count,1);
    std::iota(parent.begin(),parent.end(),0u);
    const auto root=[&](uint32_t face) {
        while (parent[face]!=face) {
            parent[face]=parent[parent[face]];
            face=parent[face];
        }
        return face;
    };
    std::unordered_map<uint64_t,uint32_t> edges;
    const auto addFace=[&](uint32_t face) {
        const auto& v=vertices[face];
        for (size_t corner=0;corner<v.size();++corner) {
            const auto [a,b]=std::minmax(v[corner],v[(corner+1)%v.size()]);
            if (a==b) continue;
            const auto [edge,inserted]=edges.try_emplace((uint64_t{a}<<32)|b,face);
            if (inserted) continue;
            auto x=root(face),y=root(edge->second);
            if (x==y) continue;
            if (sizes[x]<sizes[y]) std::swap(x,y);
            parent[y]=x;
            sizes[x]+=sizes[y];
        }
    };
    if (!vertices.empty()) {
        if (withinMaterial) {
            for (const auto& material:materials) {
                edges.clear();
                edges.reserve(material.faces.size()*2);
                for (auto face:material.faces) addFace(face);
            }
        } else {
            edges.reserve(count*2);
            for (uint32_t face=0;face<count;++face) addFace(face);
        }
    }
    byFace.resize(count);
    std::vector<uint32_t> groupForRoot(count,UINT32_MAX);
    for (uint32_t face=0;face<count;++face) {
        const auto representative=root(face);
        auto& group=groupForRoot[representative];
        if (group==UINT32_MAX) {
            group=static_cast<uint32_t>(faces.size());
            faces.emplace_back().reserve(sizes[representative]);
        }
        byFace[face]=group;
        faces[group].push_back(face);
    }
}

EnemyEditor::FacePicking::FacePicking(const EnemyModelSurface* model,const EnemyModelEditingData& editing) {
    if (!model || model->faces.empty()) return;
    meshes.resize(model->faces.back().id.instance+1);
    materials.resize(std::max<size_t>(1,model->materials.size()));
    for (uint32_t index=0;index<model->faces.size();++index) {
        const auto& id=model->faces[index].id;
        meshes[id.instance].faces.push_back(index);
        materials[id.material].faces.push_back(index);
    }
    const auto labelGroups=[](auto& groups,const auto& names,const char* kind) {
        for (size_t i=0;i<groups.size();++i) {
            const auto id=std::string(kind)+" "+std::to_string(i);
            groups[i].label=(i<names.size() && !names[i].empty() ? names[i]+" | "+id : id) +
                " ("+std::to_string(groups[i].faces.size())+")";
        }
    };
    labelGroups(meshes,editing.meshNames,"Mesh");
    labelGroups(materials,editing.materialNames,"Material");
    connected=Components(*model,editing.topologyVertices,materials,false);
    connectedInMaterial=Components(*model,editing.topologyVertices,materials,true);

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
    EnemyModelEditingData editing;
    if (request.action==Action::Discard) next=saved_;
    if (request.action==Action::Model) {
        next.modelPath=EnemyPathText(request.path);
        next.model=ReadEnemyModelForEditing(next.modelPath,editing);
        next.owners.resize(next.model->faces.size(),kNoEnemyPart);
    }
    if (request.action==Action::Load) {
        std::ifstream input(request.path);
        if (!input) throw std::runtime_error("Could not open "+EnemyPathText(request.path));
        const auto document=Json::parse(input);
        if (document.value("version",0)!=2)
            throw std::runtime_error("This editor opens model-referenced assets (version 2). Version 1 assets can be tested in Showroom or edited in Blender.");
        next.modelPath=document.at("model").at("path").get<std::string>();
        next.model=ReadEnemyModelForEditing(next.modelPath,editing);
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
    const bool newModel=next.model!=draft_.model;
    auto picking=newModel ? FacePicking(next.model.get(),editing) : FacePicking{};
    if (newModel) std::vector<std::array<uint32_t,3>>{}.swap(editing.topologyVertices);
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
    if (newModel) {
        facePicking_=std::move(picking);
        editingData_=std::move(editing);
        activeMesh_=activeMaterial_=kNoEnemyPart;
        activeBone_.clear();
    }
    selectionSlots_=std::move(selectionSlots);
    selectedFaces_.clear();
    selectedAssignedCount_=0;
    partFaceCounts_=std::move(partFaceCounts);
    assignedFaceCount_=assignedFaceCount;
    outlineDirty_=true;
    dragStart_.reset();
    activePart_=draft_.parts.empty() ? kNoEnemyPart : 0;
    activeGroup_=draft_.parts.hpGroups.empty() ? kNoEnemyPart : 0;
    assignmentHistory_.clear();
    assignmentCursor_=0;
    dirty_=otherDirty_=previewDirty_=false;
    UpdatePreviewVisibility();
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
    struct PreviewGeometry {
        EnemyPartGeometry surface;
        std::vector<uint32_t> faceInstances,instances;
    };
    std::map<size_t,PreviewGeometry> geometry;
    AABB unused{};
    for (size_t i=0;i<draft.owners.size();++i) {
        auto& group=geometry[draft.owners[i]];
        const auto instance=draft.model->faces[i].id.instance;
        group.faceInstances.push_back(instance);
        if (group.instances.empty() || group.instances.back()!=instance) group.instances.push_back(instance);
        EnemyAsset::AppendModelFace(group.surface,unused,*draft.model,i,1);
    }
    std::vector<PreviewPart> result;
    for (auto& [owner,group]:geometry) {
        auto model=BuildEnemyPartModel(group.surface,*draft.model,textures,group.faceInstances);
        auto object=std::make_unique<Object3d>();
        object->Initialize(app.ObjCom(),app.Dx());
        object->SetModel(model.get());
        object->SetCamera(&camera_);
        object->SetDirection({.3f,-1,.5f});
        object->SetIntensity(1);
        object->SetPointLightIntensity(0);
        object->SetSpotLightIntensity(0);
        result.push_back({owner,std::move(model),std::move(object),std::move(group.instances)});
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
            UpdatePreviewVisibility();
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
    otherDirty_=false;
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
    ImGui::TextUnformatted("Filter models");
    ImGui::SetNextItemWidth(-1);
    InputText("##Filter models",search_);
    ImGui::TextUnformatted("Model");
    ImGui::SetNextItemWidth(-1);
    const auto label=draft_.modelPath.empty() ? "Select a model" : draft_.modelPath.c_str();
    if (ImGui::BeginCombo("##Model",label)) {
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
    ImGui::SetItemTooltip("%s",label);
    ImGui::BeginDisabled(!draft_.model);
    ImGui::SetNextItemWidth(110);
    if (ImGui::DragFloat("Model scale",&draft_.scale,.01f,.01f,100,"%.3f",ImGuiSliderFlags_AlwaysClamp)) dirty_=otherDirty_=true;
    ImGui::EndDisabled();
    ImGui::SeparatorText("Enemy Asset");
    ImGui::TextUnformatted("Enemy Asset path");
    ImGui::SetNextItemWidth(-1);
    InputText("##Enemy Asset path",fileName_);
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
    for (uint32_t i=0;i<draft_.owners.size();++i)
        if (draft_.owners[i]==activePart_ && CanSelect(i)) SetFaceSelected(i,true);
}

void EnemyEditor::SetAssignedFaceLock(bool enabled) {
    lockAssignedFaces_=enabled;
    PruneSelection();
}

bool EnemyEditor::SurfaceVisible(uint32_t index) const {
    const auto& id=draft_.model->faces[index].id;
    return !facePicking_.meshes[id.instance].hidden && !facePicking_.materials[id.material].hidden;
}

bool EnemyEditor::FaceVisible(uint32_t index) const {
    return Visible(draft_.owners[index]) && SurfaceVisible(index);
}

bool EnemyEditor::CanSelect(uint32_t index) const {
    const auto& id=draft_.model->faces[index].id;
    return FaceVisible(index) && !facePicking_.meshes[id.instance].locked && !facePicking_.materials[id.material].locked &&
        (!lockAssignedFaces_ || draft_.owners[index]==kNoEnemyPart);
}

void EnemyEditor::PruneSelection() {
    dragStart_.reset();
    outlineDirty_=true;
    for (size_t slot=selectedFaces_.size();slot>0;--slot) {
        const auto index=selectedFaces_[slot-1];
        if (!CanSelect(index)) SetFaceSelected(index,false);
    }
}

void EnemyEditor::UpdatePreviewVisibility() {
    for (auto& part:preview_) {
        std::vector<uint32_t> hidden;
        for (uint32_t mesh=0;mesh<part.instances.size();++mesh) {
            const auto instance=part.instances[mesh];
            if (!SurfaceVisible(facePicking_.meshes[instance].faces.front())) hidden.push_back(mesh);
        }
        part.object->SetHiddenMeshes(std::move(hidden));
    }
}

void EnemyEditor::ShowAllSurfaces() {
    for (auto& group:facePicking_.meshes) group.hidden=false;
    for (auto& group:facePicking_.materials) group.hidden=false;
    showUnassigned_=true;
    isolate_=false;
    UpdatePreviewVisibility();
    PruneSelection();
}

void EnemyEditor::IsolateSurfaceGroup(std::vector<FaceGroup>& groups,size_t active) {
    for (auto& group:facePicking_.meshes) group.hidden=false;
    for (auto& group:facePicking_.materials) group.hidden=false;
    for (size_t i=0;i<groups.size();++i) groups[i].hidden=i!=active;
    showUnassigned_=true;
    isolate_=false;
    UpdatePreviewVisibility();
    PruneSelection();
}

void EnemyEditor::ApplyFaceCandidates(std::span<const uint32_t> faces) {
    const bool select=selectionMode_==SelectionMode::Select;
    for (auto face:faces)
        if ((selectionSlots_[face]!=noSelection)!=select && CanSelect(face)) SetFaceSelected(face,select);
}

void EnemyEditor::ApplySeeds(std::span<const uint32_t> seeds,FaceScope scope) {
    if (seeds.empty()) return;
    if (scope==FaceScope::Triangle) {
        ApplyFaceCandidates(seeds);
        return;
    }
    const auto applyGroups=[&](const auto& groups,auto groupOf,auto facesOf) {
        std::vector<bool> visited(seeds.size()>1 ? groups.size() : 0);
        for (auto seed:seeds) {
            if (!CanSelect(seed)) continue;
            const auto group=groupOf(seed);
            if (!visited.empty()) {
                if (visited[group]) continue;
                visited[group]=true;
            }
            ApplyFaceCandidates(facesOf(groups[group]));
        }
    };
    const auto itemFaces=[](const FaceGroup& group) -> const auto& { return group.faces; };
    if (scope==FaceScope::Mesh) {
        applyGroups(facePicking_.meshes,[&](auto seed) { return draft_.model->faces[seed].id.instance; },itemFaces);
    } else if (scope==FaceScope::Material) {
        applyGroups(facePicking_.materials,[&](auto seed) { return draft_.model->faces[seed].id.material; },itemFaces);
    } else {
        const auto& connected=scope==FaceScope::Connected ? facePicking_.connected : facePicking_.connectedInMaterial;
        applyGroups(connected.faces,[&](auto seed) { return connected.byFace[seed]; },
            [](const auto& faces) -> const auto& { return faces; });
    }
}

void EnemyEditor::SelectLinked() {
    const auto seeds=selectedFaces_;
    // PMX meshes each contain one material, and other importers scope edges to an instance.
    const auto scope=faceScope_==FaceScope::Triangle || faceScope_==FaceScope::Connected ?
        FaceScope::Connected : FaceScope::ConnectedInMaterial;
    ApplySeeds(seeds,scope);
}

void EnemyEditor::SelectBoneCandidates() {
    const auto bone=editingData_.skinWeights.find(activeBone_);
    if (bone==editingData_.skinWeights.end()) return;
    std::vector<float> weights(editingData_.skinVertexCount);
    for (const auto& weight:bone->second.vertexWeights) {
        if (weight.vertexIndex<weights.size() && std::isfinite(weight.weight)) weights[weight.vertexIndex]+=weight.weight;
    }
    const float minimum=boneThreshold_*3;
    for (uint32_t face=0;face<editingData_.skinVertices.size();++face) {
        const auto& v=editingData_.skinVertices[face];
        if (weights[v[0]]+weights[v[1]]+weights[v[2]]>=minimum) ApplyFaceCandidates(std::span(&face,1));
    }
}

void EnemyEditor::ApplyPickedFace(uint32_t index) {
    ApplySeeds(std::span(&index,1),faceScope_);
}

void EnemyEditor::ApplyPickedRectangle(const Matrix4x4& vp,const ImVec2& min,const ImVec2& max,const ImVec2& lo,const ImVec2& hi) {
    std::vector<uint32_t> seeds;
    const auto addSeed=[&](uint32_t index) {
        ImVec2 point;
        if (CanSelect(index) && Project(facePicking_.centroids[index],vp,min,max,point) &&
            point.x>=lo.x && point.y>=lo.y && point.x<=hi.x && point.y<=hi.y) seeds.push_back(index);
    };
    if (faceScope_==FaceScope::Triangle && selectionMode_==SelectionMode::Deselect) {
        for (auto index:selectedFaces_) addSeed(index);
    } else {
        for (uint32_t index=0;index<draft_.owners.size();++index) addSeed(index);
    }
    ApplySeeds(seeds,faceScope_);
}

void EnemyEditor::ChangeOwner(uint32_t index,size_t owner) {
    auto& current=draft_.owners[index];
    const bool selected=selectionSlots_[index]!=noSelection;
    if (current!=kNoEnemyPart) {
        --partFaceCounts_[current];
        --assignedFaceCount_;
        if (selected) --selectedAssignedCount_;
    }
    if (owner!=kNoEnemyPart) {
        ++partFaceCounts_[owner];
        ++assignedFaceCount_;
        if (selected) ++selectedAssignedCount_;
    }
    current=owner;
}

void EnemyEditor::ApplySelection(size_t owner) {
    AssignmentEdit edit{{},owner};
    for (uint32_t index:selectedFaces_) {
        const auto current=draft_.owners[index];
        if (current!=owner && CanSelect(index))
            edit.faces.push_back({index,current==kNoEnemyPart ? UINT32_MAX : static_cast<uint32_t>(current)});
    }
    if (!edit.faces.empty()) {
        assignmentHistory_.erase(assignmentHistory_.begin()+assignmentCursor_,assignmentHistory_.end());
        assignmentHistory_.push_back(std::move(edit));
        if (assignmentHistory_.size()>32) assignmentHistory_.pop_front();
        assignmentCursor_=assignmentHistory_.size();
        for (const auto& face:assignmentHistory_.back().faces) ChangeOwner(face.index,owner);
        dirty_=otherDirty_ || draft_.owners!=saved_.owners;
        previewDirty_=outlineDirty_=true;
    }
    if (owner!=kNoEnemyPart) ClearFaceSelection();
    else PruneSelection();
}

void EnemyEditor::StepAssignmentHistory(bool redo) {
    if ((redo && assignmentCursor_==assignmentHistory_.size()) || (!redo && !assignmentCursor_)) return;
    const auto& edit=assignmentHistory_[redo ? assignmentCursor_++ : --assignmentCursor_];
    ClearFaceSelection();
    for (const auto& face:edit.faces) {
        const size_t previous=face.previous==UINT32_MAX ? kNoEnemyPart : face.previous;
        ChangeOwner(face.index,redo ? edit.owner : previous);
    }
    dirty_=otherDirty_ || draft_.owners!=saved_.owners;
    previewDirty_=true;
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
        dirty_=otherDirty_=true;
        if (isolate_) PruneSelection();
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
        assignmentHistory_.clear();
        assignmentCursor_=0;
        dirty_=otherDirty_=previewDirty_=true;
        PruneSelection();
    }
    ImGui::EndDisabled();
    if (ImGui::BeginListBox("##Parts",{-1,120})) {
        for (size_t i=0;i<draft_.parts.size();++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::PushStyleColor(ImGuiCol_Text,PartColor(i));
            const auto title=draft_.parts[i].name+" ("+std::to_string(partFaceCounts_[i])+")";
            if (ImGui::Selectable(title.c_str(),activePart_==i)) {
                activePart_=i;
                if (isolate_) PruneSelection();
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
    const size_t editable=selectedFaces_.size()-(lockAssignedFaces_ ? selectedAssignedCount_ : 0);
    ImGui::Text("%zu unassigned / %zu faces",draft_.owners.size()-assignedFaceCount_,draft_.owners.size());
    ImGui::BeginDisabled(activePart_>=draft_.parts.size() || editable==0);
    if (ImGui::Button("Assign selection")) ApplySelection(activePart_);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(lockAssignedFaces_ || selectedAssignedCount_==0);
    if (ImGui::Button("Unassign selection")) ApplySelection(kNoEnemyPart);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(selectedFaces_.empty());
    if (ImGui::Button("Clear selection")) ClearFaceSelection();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(assignmentCursor_==0);
    if (ImGui::Button("Undo")) StepAssignmentHistory(false);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Undo face assignment");
    ImGui::SameLine();
    ImGui::BeginDisabled(assignmentCursor_==assignmentHistory_.size());
    if (ImGui::Button("Redo")) StepAssignmentHistory(true);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Redo face assignment");
    if (activePart_>=draft_.parts.size()) return;
    auto& part=draft_.parts[activePart_];
    ImGui::BeginDisabled(lockAssignedFaces_);
    if (ImGui::Button("Select Part faces")) SelectPartFaces();
    ImGui::EndDisabled();
    if (lockAssignedFaces_ && partFaceCounts_[activePart_])
        ImGui::TextWrapped("Turn off Lock assigned faces to edit assigned faces.");
    if (InputText("Part name",part.name)) dirty_=otherDirty_=true;
    if (ImGui::BeginCombo("Role",EnemyPartRoleName(part.role))) {
        for (auto role:kEnemyPartRoles) if (ImGui::Selectable(EnemyPartRoleName(role),part.role==role)) {
            part.role=role;
            dirty_=otherDirty_=true;
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
        dirty_=otherDirty_=true;
    }
    if (part.usesLocalHp) {
        if (ImGui::DragFloat("Local HP",&part.maxHp,.5f,.001f,1e8f,"%.3f",ImGuiSliderFlags_AlwaysClamp)) dirty_=otherDirty_=true;
        if (ImGui::Checkbox("Death on Zero",&part.deathOnZero)) dirty_=otherDirty_=true;
    }
    if (ImGui::Checkbox("Breakable",&part.breakable)) dirty_=otherDirty_=true;
    if (part.sharedGroup!=kNoEnemyPart) {
        const auto& groups=draft_.parts.hpGroups;
        const auto* group=part.sharedGroup<groups.size() ? groups[part.sharedGroup].id.c_str() : "Choose HP group";
        if (ImGui::BeginCombo("Shared HP group",group)) {
            for (size_t i=0;i<groups.size();++i) {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(groups[i].id.c_str(),part.sharedGroup==i)) { part.sharedGroup=i; dirty_=otherDirty_=true; }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::DragFloat("Shared Damage Rate",&part.sharedDamageRate,.01f,0,1e8f,"%.3f",ImGuiSliderFlags_AlwaysClamp)) dirty_=otherDirty_=true;
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
        dirty_=otherDirty_=true;
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
        dirty_=otherDirty_=true;
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
    if (InputText("Group ID",group.id)) dirty_=otherDirty_=true;
    if (ImGui::DragFloat("Group HP",&group.maxHp,1,.001f,1e8f,"%.3f",ImGuiSliderFlags_AlwaysClamp)) dirty_=otherDirty_=true;
    if (ImGui::Checkbox("Death on Group Zero",&group.deathOnZero)) dirty_=otherDirty_=true;
}

void EnemyEditor::DrawSurfaceGroupList(std::vector<FaceGroup>& groups,size_t& active) {
    if (groups.empty()) {
        ImGui::TextDisabled("No surfaces loaded.");
        return;
    }
    if (active>=groups.size()) {
        const auto first=std::find_if(groups.begin(),groups.end(),[](const auto& group) { return !group.faces.empty(); });
        active=static_cast<size_t>(first-groups.begin());
    }
    if (ImGui::BeginListBox("##Surfaces",{-1,ImGui::GetTextLineHeightWithSpacing()*8})) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(groups.size()));
        while (clipper.Step()) for (int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i) {
            const auto& item=groups[i];
            std::string label=item.label;
            if (item.hidden) label+=" [Hidden]";
            if (item.locked) label+=" [Locked]";
            ImGui::PushID(i);
            if (ImGui::Selectable(label.c_str(),active==static_cast<size_t>(i))) active=i;
            ImGui::SetItemTooltip("%s",label.c_str());
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
    if (active>=groups.size()) return;
    auto& item=groups[active];
    ImGui::BeginDisabled(item.faces.empty());
    if (ImGui::Button("Select All Faces")) ApplyFaceCandidates(item.faces);
    ImGui::SetItemTooltip("%s",selectionMode_==SelectionMode::Select ? "Add eligible faces in this item" : "Remove eligible faces in this item");
    ImGui::SameLine();
    if (ImGui::Button(item.hidden ? "Show" : "Hide")) {
        item.hidden=!item.hidden;
        UpdatePreviewVisibility();
        PruneSelection();
    }
    ImGui::SameLine();
    if (ImGui::Button("Isolate")) IsolateSurfaceGroup(groups,active);
    if (ImGui::Checkbox("Lock Selection",&item.locked)) PruneSelection();
    ImGui::EndDisabled();
}

void EnemyEditor::DrawSurfaceGroups() {
    ImGui::SeparatorText("Meshes and Materials");
    ImGui::BeginDisabled(!draft_.model);
    if (ImGui::Button("Show All")) ShowAllSurfaces();
    ImGui::EndDisabled();
    if (ImGui::BeginTabBar("Surface groups")) {
        if (ImGui::BeginTabItem("Meshes")) {
            ImGui::PushID("Meshes");
            DrawSurfaceGroupList(facePicking_.meshes,activeMesh_);
            ImGui::PopID();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Materials")) {
            ImGui::PushID("Materials");
            DrawSurfaceGroupList(facePicking_.materials,activeMaterial_);
            ImGui::PopID();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void EnemyEditor::DrawBoneSelection() {
    if (!ImGui::CollapsingHeader("Bone weight candidates")) return;
    if (editingData_.skinWeights.empty()) {
        ImGui::TextDisabled("%s",draft_.model ? "This model has no skin weights." : "Load a model with skin weights.");
        return;
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##Bone",activeBone_.empty() ? "Choose a bone" : activeBone_.c_str())) {
        for (const auto& [name,weights]:editingData_.skinWeights)
            if (ImGui::Selectable(name.c_str(),activeBone_==name)) activeBone_=name;
        ImGui::EndCombo();
    }
    ImGui::TextUnformatted("Minimum mean weight");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Minimum mean weight",&boneThreshold_,.01f,1.f,"%.2f");
    ImGui::BeginDisabled(activeBone_.empty());
    if (ImGui::Button("Apply Bone Candidates")) SelectBoneCandidates();
    ImGui::EndDisabled();
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
            if (!FaceVisible(static_cast<uint32_t>(i))) continue;
            const auto& v=draft_.model->faces[i].vertices;
            const std::array triangle{facePicking_.Fit(v[0].position),facePicking_.Fit(v[1].position),facePicking_.Fit(v[2].position)};
            if (RaycastEnemyTriangle(origin,direction,triangle,nearest)) hit=i;
        }
    }
    return hit;
}

void EnemyEditor::DrawAssetPanel() {
    if (ImGui::BeginTabBar("Model and part settings")) {
        if (ImGui::BeginTabItem("Model")) {
            DrawFiles();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Parts")) {
            if (draft_.model) DrawParts();
            else ImGui::TextDisabled("Load a model to edit parts.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("HP Groups")) {
            if (draft_.model) DrawGroups();
            else ImGui::TextDisabled("Load a model to edit HP groups.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void EnemyEditor::DrawFacePanel() {
    ImGui::BeginDisabled(!draft_.model);
    auto mode=selectionMode_;
    if (ImGui::RadioButton("Select",mode==SelectionMode::Select)) mode=SelectionMode::Select;
    ImGui::SameLine();
    if (ImGui::RadioButton("Deselect",mode==SelectionMode::Deselect)) mode=SelectionMode::Deselect;
    if (mode!=selectionMode_) {
        selectionMode_=mode;
        dragStart_.reset();
    }
    bool locked=lockAssignedFaces_;
    if (ImGui::Checkbox("Lock assigned faces",&locked)) SetAssignedFaceLock(locked);
    int scope=static_cast<int>(faceScope_);
    ImGui::TextUnformatted("Face scope");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##Face scope",&scope,"Triangle\0Connected\0Material\0Mesh\0Connected in Material\0")) {
        faceScope_=static_cast<FaceScope>(scope);
        dragStart_.reset();
    }
    ImGui::BeginDisabled(selectedFaces_.empty());
    if (ImGui::Button("Select Linked")) SelectLinked();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Expand the current selection by shared edges within the current scope");
    ImGui::SameLine();
    ImGui::Text("%zu selected",selectedFaces_.size());
    ImGui::EndDisabled();
    DrawSurfaceGroups();
    DrawBoneSelection();
}

void EnemyEditor::DrawViewport(GameApp& app) {
    if (!draft_.model) {
        ImGui::TextUnformatted("Choose a model or load an Enemy Asset.");
        return;
    }
    if (ImGui::Button("Frame model")) { pan_={}; distance_=4.8f*draft_.scale; }
    ImGui::SameLine();
    ImGui::Checkbox("Part colors",&partColors_);
    ImGui::SameLine();
    if (ImGui::Checkbox("Show unassigned",&showUnassigned_)) PruneSelection();
    ImGui::SameLine();
    if (ImGui::Checkbox("Isolate Part",&isolate_)) PruneSelection();
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x<ImGui::CalcTextSize("Dim other Parts").x+ImGui::GetFrameHeight()+ImGui::GetStyle().ItemInnerSpacing.x)
        ImGui::NewLine();
    ImGui::Checkbox("Dim other Parts",&dimOthers_);
    ImGui::TextWrapped("LMB face / drag box | RMB orbit | MMB pan | Wheel zoom");
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
        // Sample across the selection so large components stay visible throughout the model.
        const size_t samples=std::min<size_t>(selectedFaces_.size(),3000);
        for (size_t i=0;i<samples;++i) {
            const auto index=selectedFaces_[i*selectedFaces_.size()/samples];
            if (FaceVisible(index)) outlineFaces_.push_back(index);
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
    if (draft_.model && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiKey_Z,ImGuiInputFlags_RouteGlobal)) StepAssignmentHistory(false);
        if (ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiKey_Y,ImGuiInputFlags_RouteGlobal) ||
            ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiMod_Shift|ImGuiKey_Z,ImGuiInputFlags_RouteGlobal)) StepAssignmentHistory(true);
    }
    ImGui::TextUnformatted("Enemy Editor");
    ImGui::SameLine();
    if (ImGui::Button("Back to Weapon Editor")) { visible=false; dragStart_.reset(); }
    if (dirty_) {
        ImGui::SameLine();
        ImGui::TextUnformatted("Unsaved edits");
    }
    if (!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
    const auto available=ImGui::GetContentRegionAvail();
    const bool wide=available.x>=1100;
    const auto tableFlags=ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable(wide ? "Enemy Editor workspace" : "Enemy Editor compact workspace",wide ? 3 : 2,
        tableFlags,{0,available.y})) {
        ImGui::TableSetupColumn(wide ? "Model & Parts" : "Tools",ImGuiTableColumnFlags_WidthStretch,1.f);
        ImGui::TableSetupColumn("Viewport",ImGuiTableColumnFlags_WidthStretch,wide ? 2.15f : 1.7f);
        if (wide) ImGui::TableSetupColumn("Face Selection",ImGuiTableColumnFlags_WidthStretch,1.05f);
        ImGui::TableNextColumn();
        ImGui::SeparatorText(wide ? "Model & Parts" : "Tools");
        ImGui::BeginChild("Enemy Editor tools");
        if (wide) DrawAssetPanel();
        else if (ImGui::BeginTabBar("Compact tools")) {
            if (ImGui::BeginTabItem("Model & Parts")) {
                DrawAssetPanel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Face Selection")) {
                DrawFacePanel();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
        ImGui::TableNextColumn();
        ImGui::SeparatorText("Viewport");
        ImGui::BeginChild("Model preview");
        DrawViewport(app);
        ImGui::EndChild();
        if (wide) {
            ImGui::TableNextColumn();
            ImGui::SeparatorText("Face Selection");
            ImGui::BeginChild("Enemy Editor face tools");
            DrawFacePanel();
            ImGui::EndChild();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}
#endif
