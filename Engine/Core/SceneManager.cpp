#include "SceneManager.h"
#include "IScene.h"
#include "GameApp.h"
#include <cassert>
#include <cmath>

void SceneManager::Initialize(GameApp& app) {
    fade_.Initialize(app.SpriteCom(), app.Dx());
}

bool SceneManager::TransitionTo(const std::string& name, float fadeOutSeconds, float fadeInSeconds) {
    if (IsTransitioning() || !current_ || !HasRegisteredScene(name) ||
        !std::isfinite(fadeOutSeconds) || fadeOutSeconds < 0 ||
        !std::isfinite(fadeInSeconds) || fadeInSeconds < 0) return false;
    destination_ = name;
    fadeInSeconds_ = fadeInSeconds;
    blackFramePresented_ = false;
    fade_.FadeOut(fadeOutSeconds);
    transition_ = Transition::FadeOut;
    return true;
}

void SceneManager::Register(const std::string& name, Factory factory) {
    factories_[name] = std::move(factory);
}

void SceneManager::Change(GameApp& app, const std::string& name) {
    // Immediate/debug changes also cancel any outstanding fade request.
    transition_ = Transition::Idle;
    destination_.clear(); blackFramePresented_ = false; discardLoadingDelta_ = false; fade_.Reset();
    ChangeScene_(app, name);
}

void SceneManager::ChangeScene_(GameApp& app, const std::string& name) {
    auto it = factories_.find(name);
    assert(it != factories_.end());

    if (current_) {
        current_->OnExit(app);
        retiredScenes_.push_back(std::move(current_));
    }

    if (app.Render()) {
        app.Render()->SetMode(PostEffectMode::FullScreen);
    }

    current_ = it->second();
    currentName_ = name;
    current_->OnEnter(app);
}

void SceneManager::Update(GameApp& app, float dt) {
    // A requested restart retires the old scene until the next frame's GPU work is complete.
    // Repeated restarts must not retain every old scene's sprite/model buffers forever.
    if (!retiredScenes_.empty()) {
        app.Dx()->WaitForGPU();
        retiredScenes_.clear();
    }
    if (!current_) return;

    if (transition_ == Transition::AwaitBlackFrame && blackFramePresented_) {
        // The outgoing black frame has been submitted. OnEnter/loading cannot
        // reveal the destination: the same overlay stays fully opaque.
        const auto destination = destination_;
        destination_.clear();
        blackFramePresented_ = false;
        discardLoadingDelta_ = true;
        transition_ = Transition::FadeIn;
        fade_.FadeIn(fadeInSeconds_);
        ChangeScene_(app, destination);
        return; // Do not charge loading time/this frame's dt to FadeIn.
    }
    if (transition_ == Transition::FadeIn) {
        // Multiple simulation ticks before Draw must not skip the incoming black frame.
        if (!blackFramePresented_) return;
        // GameApp's next dt includes the blocking OnEnter load. Start the fade
        // clock with the following frame instead of spending that time on it.
        if (discardLoadingDelta_) { discardLoadingDelta_ = false; return; }
        fade_.Update(dt);
        if (fade_.IsFinished()) transition_ = Transition::Idle;
        return; // Freeze input, AI and stage time until the next visible frame.
    }
    if (transition_ == Transition::FadeOut) {
        fade_.Update(dt);
        if (fade_.IsFinished()) transition_ = Transition::AwaitBlackFrame;
    }

    current_->Update(app, dt);

    const std::string next = current_->NextScene();
    if (!next.empty() && !IsTransitioning()) {
        current_->ClearNextScene_();
        Change(app, next);
    }
}

void SceneManager::DrawRender(GameApp& app) {
    if (!current_) return;
    current_->DrawRender(app);
}

void SceneManager::Draw3D(GameApp& app) {
    if (!current_) return;
    current_->Draw3D(app);
}

void SceneManager::Draw2D(GameApp& app) {
    if (!current_) return;
    current_->Draw2D(app);
}

void SceneManager::DrawOverlay2D(GameApp& app) {
    if (!current_) return;
    current_->DrawOverlay2D(app);
    fade_.Draw();
    if (transition_ == Transition::AwaitBlackFrame || transition_ == Transition::FadeIn)
        blackFramePresented_ = true;
}

void SceneManager::Draw(GameApp& app) {
    if (!current_) return;
    current_->Draw(app);
}

void SceneManager::DrawImGui(GameApp& app) {
    if (!current_ || IsTransitioning()) return; // No debug markers/edits over the black game view.
    current_->DrawImGui(app);
}

void SceneManager::DrawPreview(GameApp& app) {
    if (!current_) return;
    current_->DrawPreview(app);
}

void SceneManager::DrawPostEffectTargets(GameApp& app) {
    if (!current_) return;
    current_->DrawPostEffectTargets(app);
}

bool SceneManager::HasObjectBloomTargets() const {
    return current_ && current_->HasObjectBloomTargets();
}

bool SceneManager::HasObjectOutlineBloomTargets() const {
    return current_ && current_->HasObjectOutlineBloomTargets();
}

bool SceneManager::HasObjectLuminanceOutlineTargets() const {
    return current_ && current_->HasObjectLuminanceOutlineTargets();
}
