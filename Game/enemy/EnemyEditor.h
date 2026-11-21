#pragma once
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
#include "Camera.h"
#include "EnemyAsset.h"
#include "EnemyModelSource.h"
#include "Object3d.h"
#include <deque>
#include <optional>

class GameApp;
struct ImVec2;

class EnemyEditor {
public:
    void Open(const std::filesystem::path& current);
    void Update(GameApp& app);
    void DrawPreview();
    void DrawUI(GameApp& app,bool& visible);
    void ReportError(std::string error) { message_=std::move(error); }
    std::shared_ptr<const EnemyAsset> TakeTestRequest() { return std::exchange(testRequest_,{}); }

private:
    friend void RunEnemyPoolTests(GameApp& app);

    struct Draft {
        std::shared_ptr<const EnemyModelSurface> model;
        std::string modelPath;
        std::filesystem::path path;
        float scale=1;
        EnemyParts parts;
        std::vector<size_t> owners;
        nlohmann::json Document() const;
    };
    struct PreviewPart {
        size_t owner;
        std::unique_ptr<Model> model;
        std::unique_ptr<Object3d> object;
        std::vector<uint32_t> instances;
    };
    struct FaceGroup {
        std::string label;
        std::vector<uint32_t> faces;
        bool hidden=false,locked=false;
    };
    struct Components {
        std::vector<uint32_t> byFace;
        std::vector<std::vector<uint32_t>> faces;
        Components()=default;
        Components(const EnemyModelSurface& model,std::span<const std::array<uint32_t,3>> vertices,
            std::span<const FaceGroup> materials,bool withinMaterial);
    };
    struct FacePicking {
        static constexpr size_t facesPerBlock=128;
        Vector3 base{};
        float scale=1;
        std::vector<AABB> blocks;
        std::vector<Vector3> centroids;
        std::vector<FaceGroup> meshes,materials;
        Components connected,connectedInMaterial;

        explicit FacePicking(const EnemyModelSurface* model=nullptr,const EnemyModelEditingData& editing={});
        Vector3 Fit(const Vector3& point) const { return (point-base)*scale; }
    };
    enum class SelectionMode { Select, Deselect };
    enum class FaceScope { Triangle, Connected, Material, Mesh, ConnectedInMaterial };
    enum class Action { Model, Load, Discard };
    struct Request { Action action; std::filesystem::path path; };
    struct AssignmentEdit {
        struct Face { uint32_t index,previous; };
        std::vector<Face> faces;
        size_t owner;

        void Execute(EnemyEditor& editor) const;
        void Undo(EnemyEditor& editor) const;
    };

    void RequestChange(Action action,const std::filesystem::path& path={});
    void OpenDraft(GameApp& app,const Request& request);
    std::vector<PreviewPart> BuildPreview(GameApp& app,const Draft& draft,const EnemyModelTextures& textures);
    void DrawFiles();
    void DrawParts();
    void DrawGroups();
    void DrawAssetPanel();
    void DrawFacePanel();
    void DrawSurfaceGroups();
    void DrawSurfaceGroupList(std::vector<FaceGroup>& groups,size_t& active);
    void DrawBoneSelection();
    void DrawViewport(GameApp& app);
    void RefreshModels();
    void SetFaceSelected(uint32_t index,bool selected);
    void ClearFaceSelection();
    void SelectPartFaces();
    void SetAssignedFaceLock(bool enabled);
    bool CanSelect(uint32_t index) const;
    bool SurfaceVisible(uint32_t index) const;
    bool FaceVisible(uint32_t index) const;
    void PruneSelection();
    void UpdatePreviewVisibility();
    void ShowAllSurfaces();
    void IsolateSurfaceGroup(std::vector<FaceGroup>& groups,size_t active);
    void ApplyFaceCandidates(std::span<const uint32_t> faces);
    void ApplySeeds(std::span<const uint32_t> seeds,FaceScope scope);
    void SelectLinked();
    void SelectBoneCandidates();
    void ApplyPickedFace(uint32_t index);
    void ApplyPickedRectangle(const Matrix4x4& vp,const ImVec2& min,const ImVec2& max,const ImVec2& lo,const ImVec2& hi);
    void ApplySelection(size_t owner);
    void ChangeOwner(uint32_t index,size_t owner);
    void StepAssignmentHistory(bool redo);
    size_t PickFace(const Vector3& origin,const Vector3& direction,float range) const;
    bool Visible(size_t owner) const;
    void Save(const std::filesystem::path& path);
    void ExportBlenderMesh();

    Draft draft_,saved_;
    EnemyModelTextures textures_;
    std::vector<PreviewPart> preview_;
    std::vector<std::filesystem::path> models_;
    FacePicking facePicking_;
    EnemyModelEditingData editingData_;
    // A packed list keeps selection edits and overlays proportional to the selection.
    static constexpr uint32_t noSelection=UINT32_MAX;
    std::vector<uint32_t> selectionSlots_,selectedFaces_,outlineFaces_;
    std::vector<size_t> partFaceCounts_;
    size_t assignedFaceCount_=0,selectedAssignedCount_=0;
    SelectionMode selectionMode_=SelectionMode::Select;
    FaceScope faceScope_=FaceScope::Triangle;
    std::deque<AssignmentEdit> assignmentHistory_;
    size_t assignmentCursor_=0;
    bool lockAssignedFaces_=true,outlineDirty_=true;
    std::optional<Request> pending_,confirmation_;
    std::shared_ptr<const EnemyAsset> testRequest_;
    std::string search_,fileName_,message_;
    size_t activePart_=kNoEnemyPart,activeGroup_=kNoEnemyPart;
    size_t activeMesh_=kNoEnemyPart,activeMaterial_=kNoEnemyPart;
    std::string activeBone_;
    float boneThreshold_=.5f;
    bool dirty_=false,otherDirty_=false,previewDirty_=false,showUnassigned_=true,partColors_=true,isolate_=false,dimOthers_=false;
    Camera camera_;
    float yaw_=0,pitch_=.16f,distance_=4.8f,renderScale_=1;
    Vector3 pan_{};
    std::optional<Vector2> dragStart_;
};
#endif
