#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>
#include "FadeManager.h"

class GameApp;
class IScene;

class SceneManager {
public:
    using Factory = std::function<std::unique_ptr<IScene>()>;

    void Register(const std::string& name, Factory factory);
    void Initialize(GameApp& app);
    void Change(GameApp& app, const std::string& name);
    // Returns false for an invalid request or while another transition is active.
    bool TransitionTo(const std::string& name, float fadeOutSeconds = .75f, float fadeInSeconds = .75f);
    bool TransitionTo(const std::string& name, float fadeOutSeconds, float fadeInSeconds,
        const Vector3& color, FadeEasing easing);
    bool IsTransitioning() const { return transition_ != Transition::Idle; }
    bool IsWaitingForScene() const;
    const std::string& LastTransitionError() const { return lastError_; }
    FadeManager& Fade() { return fade_; }
    const FadeManager& Fade() const { return fade_; }

    // Convenience for one update per host frame (also used by runtime tests).
    void Update(GameApp& app, float dt);
    // GameApp uses these separately: presentation once, simulation zero/many times.
    void UpdateTransition(GameApp& app, float unscaledDt);
    void UpdateScene(GameApp& app, float dt);

    void DrawRender(GameApp& app);
    void DrawShadow(GameApp& app);
    void Draw3D(GameApp& app);
    void Draw2D(GameApp& app);
    void DrawOverlay2D(GameApp& app);
    void Draw(GameApp& app);
    void DrawImGui(GameApp& app);
    void DrawPreview(GameApp& app);
    void DrawPostEffectTargets(GameApp& app);
    bool HasObjectBloomTargets() const;
    bool HasObjectOutlineBloomTargets() const;
    bool HasObjectLuminanceOutlineTargets() const;

    IScene* Current() { return current_.get(); }
    const std::string& CurrentName() const { return currentName_; }
    bool HasRegisteredScene(const std::string& name) const {
        return factories_.contains(name);
    }

private:
    bool ChangeScene_(GameApp& app, const std::string& name, bool awaitReady = false);
    void RecoverScene_(GameApp& app, const std::string& error);
    void RetirePrevious_();
    void ReleaseTransitionInput_(GameApp& app);
    void DrawTransitionImGui_(GameApp& app);
    bool CanDrawScene_() const;
    enum class Transition { Idle, FadeOut, AwaitBlackFrame, AwaitSceneReady, AwaitIncomingFrame, FadeIn };
    FadeManager fade_;
    Transition transition_ = Transition::Idle;
    std::string destination_;
    float fadeInSeconds_ = .75f;
    bool blackFramePresented_ = false;
    bool changingScene_ = false;
    bool skipSimulation_ = false;
    bool debugCycle_ = false;
    std::string lastError_;
    std::unique_ptr<IScene> previousScene_;
    std::string previousName_;
    std::unordered_map<std::string, Factory> factories_;
    std::unique_ptr<IScene> current_;
    std::vector<std::unique_ptr<IScene>> retiredScenes_;
    std::string currentName_;
};
