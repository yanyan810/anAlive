#pragma once
#include <string>

class GameApp;

class IScene {
public:
    virtual ~IScene() = default;

    virtual void OnEnter(GameApp& app) {}
    virtual void OnExit(GameApp& app) {}

    virtual void Update(GameApp& app, float dt) = 0;

    // RenderTexture向け
    virtual void DrawShadow(GameApp& app) {}
    virtual void DrawRender(GameApp& app) {}

    // 3D
    virtual void Draw3D(GameApp& app) {}

    // 2D
    virtual void Draw2D(GameApp& app) {}

    // ポストエフェクト適用後に重ねる2D
    virtual void DrawOverlay2D(GameApp& app) {}

    // 最終描画
    virtual void Draw(GameApp& app) = 0;

    // ImGui描画
    virtual void DrawImGui(GameApp& app) {}

    virtual void DrawPreview(GameApp& app) {}

    virtual void DrawPostEffectTargets(GameApp& app) {}
    virtual bool HasObjectBloomTargets() const { return false; }
    virtual bool HasObjectOutlineBloomTargets() const { return false; }
    virtual bool HasObjectLuminanceOutlineTargets() const { return false; }

    void RequestChangeScene_(const std::string& next) {
        nextScene_ = next;
    }

    const std::string& NextScene() const {
        return nextScene_;
    }

    void ClearNextScene_() {
        nextScene_.clear();
    }

private:
    std::string nextScene_;
};
