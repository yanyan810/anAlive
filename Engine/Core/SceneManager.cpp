#include "SceneManager.h"
#include "IScene.h"
#include "GameApp.h"
#include <cmath>
#include <stdexcept>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

void SceneManager::Initialize(GameApp& app) {
    fade_.Initialize(app.SpriteCom(), app.Dx());
}

bool SceneManager::TransitionTo(const std::string& name, float fadeOutSeconds, float fadeInSeconds) {
    return TransitionTo(name, fadeOutSeconds, fadeInSeconds, {0, 0, 0}, FadeEasing::Linear);
}

bool SceneManager::TransitionTo(const std::string& name, float fadeOutSeconds, float fadeInSeconds,
    const Vector3& color, FadeEasing easing) {
    if (IsTransitioning() || changingScene_ || fade_.IsActive() || !HasRegisteredScene(name) ||
        !std::isfinite(fadeOutSeconds) || fadeOutSeconds < 0 ||
        !std::isfinite(fadeInSeconds) || fadeInSeconds < 0 ||
        !std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z) ||
        easing < FadeEasing::Linear || easing > FadeEasing::EaseInOut) return false;
    destination_ = name;
    lastError_.clear();
    debugCycle_ = false;
    fadeInSeconds_ = fadeInSeconds;
    blackFramePresented_ = false;
    fade_.SetColor(color);
    fade_.SetEasing(easing);
    fade_.FadeOut(fadeOutSeconds);
    transition_ = Transition::FadeOut;
    return true;
}

void SceneManager::Register(const std::string& name, Factory factory) {
    factories_[name] = std::move(factory);
}

void SceneManager::Change(GameApp& app, const std::string& name) {
    if (changingScene_) return; // OnEnter/OnExit cannot recursively replace their own scene.
    if (!HasRegisteredScene(name)) { lastError_ = "Unregistered scene: " + name; return; }
    // Immediate/debug changes also cancel any outstanding fade request.
    transition_ = Transition::Idle;
    destination_.clear(); blackFramePresented_ = false; debugCycle_ = false; skipSimulation_ = false; fade_.Reset();
    RetirePrevious_();
    ChangeScene_(app, name);
}

void SceneManager::RetirePrevious_() {
    if (previousScene_) retiredScenes_.push_back(std::move(previousScene_));
    previousName_.clear();
}

void SceneManager::RecoverScene_(GameApp& app, const std::string& error) {
    lastError_ = error;
    if (current_) {
        try { current_->OnExit(app); } catch (...) { /* Preserve the original load error. */ }
        retiredScenes_.push_back(std::move(current_));
    }
    current_ = std::move(previousScene_);
    currentName_ = std::move(previousName_);
    if (current_) {
        try { current_->OnEnter(app); }
        catch (...) {
            lastError_ += "; previous scene initialization also failed";
            retiredScenes_.push_back(std::move(current_));
            currentName_.clear();
        }
    }
    OutputDebugStringA(("Scene transition failed: " + lastError_ + "\n").c_str());
}

bool SceneManager::ChangeScene_(GameApp& app, const std::string& name, bool awaitReady) {
    auto it = factories_.find(name);
    if (it == factories_.end() || !it->second) { lastError_ = "No scene factory: " + name; return false; }
    std::unique_ptr<IScene> next;
    changingScene_ = true;
    try {
        next = it->second();
        if (!next) throw std::runtime_error("Scene factory returned null: " + name);
    } catch (const std::exception& error) {
        lastError_ = error.what(); changingScene_ = false; return false;
    } catch (...) {
        lastError_ = "Scene factory failed: " + name; changingScene_ = false; return false;
    }
    previousScene_ = std::move(current_);
    previousName_ = currentName_;
    try {
        if (previousScene_) previousScene_->OnExit(app);
        if (app.Render()) app.Render()->SetMode(PostEffectMode::FullScreen);
        current_ = std::move(next);
        currentName_ = name;
        current_->OnEnter(app);
        if (awaitReady) {
            const auto error = current_->InitializationError();
            if (!error.empty()) throw std::runtime_error(error);
        } else RetirePrevious_();
        lastError_.clear();
    } catch (const std::exception& error) {
        RecoverScene_(app, error.what()); changingScene_ = false; return false;
    } catch (...) {
        RecoverScene_(app, "Scene initialization failed: " + name); changingScene_ = false; return false;
    }
    changingScene_ = false;
    return true;
}

void SceneManager::Update(GameApp& app, float dt) {
    UpdateTransition(app, dt);
    UpdateScene(app, dt);
}

bool SceneManager::IsWaitingForScene() const {
    return transition_ == Transition::AwaitBlackFrame || transition_ == Transition::AwaitSceneReady ||
        transition_ == Transition::AwaitIncomingFrame;
}

void SceneManager::ReleaseTransitionInput_(GameApp& app) {
    if (!IsTransitioning() && !fade_.IsActive()) return;
    app.GetInput()->SetCameraControlEnabled(false);
#ifdef USE_IMGUI
    ImGui::GetIO().ConfigFlags &= ~(ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange);
#endif
}

void SceneManager::UpdateTransition(GameApp& app, float unscaledDt) {
    skipSimulation_ = IsTransitioning() && transition_ != Transition::FadeOut;
    // A requested restart retires the old scene until the next frame's GPU work is complete.
    // Repeated restarts must not retain every old scene's sprite/model buffers forever.
    if (!retiredScenes_.empty()) {
        app.Dx()->WaitForGPU();
        retiredScenes_.clear();
    }
    if (transition_ == Transition::AwaitBlackFrame && blackFramePresented_) {
        // The outgoing black frame has been submitted. OnEnter/loading cannot
        // reveal the destination: the same overlay stays fully opaque.
        const auto destination = destination_;
        destination_.clear();
        blackFramePresented_ = false;
        transition_ = Transition::AwaitSceneReady;
        if (!ChangeScene_(app, destination, true)) {
            transition_ = current_ && !current_->IsReadyForTransition() ?
                Transition::AwaitSceneReady : Transition::AwaitIncomingFrame;
        }
        // The overlay remains in Hold during OnEnter, including zero-second fades.
    }
    if (transition_ == Transition::AwaitSceneReady) {
        changingScene_ = true;
        try {
            if (current_) {
                current_->UpdateLoading(app, std::isfinite(unscaledDt) && unscaledDt > 0 ? unscaledDt : 0);
                const auto error = current_->InitializationError();
                if (!error.empty()) throw std::runtime_error(error);
                if (current_->IsReadyForTransition()) {
                    RetirePrevious_();
                    transition_ = Transition::AwaitIncomingFrame;
                }
            }
        } catch (const std::exception& error) {
            RecoverScene_(app, error.what());
            transition_ = current_ && !current_->IsReadyForTransition() ?
                Transition::AwaitSceneReady : Transition::AwaitIncomingFrame;
        } catch (...) {
            RecoverScene_(app, "Scene loading failed");
            transition_ = current_ && !current_->IsReadyForTransition() ?
                Transition::AwaitSceneReady : Transition::AwaitIncomingFrame;
        }
        changingScene_ = false;
    } else if (transition_ == Transition::AwaitIncomingFrame && blackFramePresented_) {
        // Start only after the initialized scene has actually drawn opaque.
        // Discard this host delta: it includes blocking loading time.
        blackFramePresented_ = false;
        fade_.FadeIn(fadeInSeconds_);
        transition_ = fade_.IsFinished() ? Transition::Idle : Transition::FadeIn;
    } else if (transition_ == Transition::FadeIn) {
        fade_.Update(unscaledDt);
        if (fade_.IsFinished()) transition_ = Transition::Idle;
    } else if (transition_ == Transition::Idle) {
        if (debugCycle_ && fade_.State() == TransitionState::Hold && blackFramePresented_) {
            debugCycle_ = false; blackFramePresented_ = false; fade_.FadeIn();
        } else fade_.Update(unscaledDt);
    }
    if (transition_ == Transition::FadeOut) {
        fade_.Update(unscaledDt);
        if (fade_.IsFinished()) transition_ = Transition::AwaitBlackFrame;
    }
    ReleaseTransitionInput_(app);
}

void SceneManager::UpdateScene(GameApp& app, float dt) {
    if (!current_ || skipSimulation_ || (IsTransitioning() && transition_ != Transition::FadeOut &&
        transition_ != Transition::AwaitBlackFrame)) return;
    current_->Update(app, dt);
    ReleaseTransitionInput_(app); // Scenes may capture the mouse while updating.

    const std::string next = current_->NextScene();
    if (!next.empty() && !IsTransitioning()) {
        current_->ClearNextScene_();
        Change(app, next);
    }
}

bool SceneManager::CanDrawScene_() const { return current_ && transition_ != Transition::AwaitSceneReady; }

void SceneManager::DrawRender(GameApp& app) {
    if (!CanDrawScene_()) return;
    current_->DrawRender(app);
}

void SceneManager::DrawShadow(GameApp& app) {
    if (CanDrawScene_()) current_->DrawShadow(app);
}

void SceneManager::Draw3D(GameApp& app) {
    if (!CanDrawScene_()) return;
    current_->Draw3D(app);
}

void SceneManager::Draw2D(GameApp& app) {
    if (!CanDrawScene_()) return;
    current_->Draw2D(app);
}

void SceneManager::DrawOverlay2D(GameApp& app) {
    if (CanDrawScene_()) current_->DrawOverlay2D(app);
    fade_.Draw();
    if (fade_.State() == TransitionState::Hold &&
        (transition_ == Transition::AwaitBlackFrame || transition_ == Transition::AwaitIncomingFrame || debugCycle_))
        blackFramePresented_ = true;
}

void SceneManager::Draw(GameApp& app) {
    if (!CanDrawScene_()) return;
    current_->Draw(app);
}

void SceneManager::DrawImGui(GameApp& app) {
    ReleaseTransitionInput_(app);
#ifdef USE_IMGUI
    if (CanDrawScene_()) {
        ImGui::BeginDisabled(IsTransitioning());
        current_->DrawImGui(app);
        ImGui::EndDisabled();
    }
#endif
    DrawTransitionImGui_(app);
}

void SceneManager::DrawPreview(GameApp& app) {
    if (!CanDrawScene_()) return;
    current_->DrawPreview(app);
}

void SceneManager::DrawPostEffectTargets(GameApp& app) {
    if (!CanDrawScene_()) return;
    current_->DrawPostEffectTargets(app);
}

bool SceneManager::HasObjectBloomTargets() const {
    return CanDrawScene_() && current_->HasObjectBloomTargets();
}

bool SceneManager::HasObjectOutlineBloomTargets() const {
    return CanDrawScene_() && current_->HasObjectOutlineBloomTargets();
}

bool SceneManager::HasObjectLuminanceOutlineTargets() const {
    return CanDrawScene_() && current_->HasObjectLuminanceOutlineTargets();
}

void SceneManager::DrawTransitionImGui_([[maybe_unused]] GameApp& app) {
#ifdef USE_IMGUI
    if (ImGui::Begin("Screen Transition")) {
        static const char* states[] = {"Idle", "FadeOut", "Hold", "FadeIn"};
        ImGui::Text("TransitionState: %s", states[static_cast<size_t>(fade_.State())]);
        ImGui::Text("Alpha: %.3f", fade_.Alpha());
        ImGui::Text("Elapsed: %.3f / %.3f s", fade_.Elapsed(), fade_.Duration());
        ImGui::ProgressBar(fade_.Progress());
        ImGui::Text("Scene switch waiting: %s", IsWaitingForScene() ? "Yes" : "No");
        ImGui::Text("Scene transition: %s", IsTransitioning() ? "Active" : "Idle");
        if (!destination_.empty()) ImGui::Text("Destination: %s", destination_.c_str());
        if (!lastError_.empty()) ImGui::TextWrapped("Last error: %s", lastError_.c_str());
        ImGui::BeginDisabled(IsTransitioning());
        float seconds = fade_.Settings().duration;
        if (ImGui::DragFloat("Fade Duration", &seconds, .01f, 0, 10, "%.2f s", ImGuiSliderFlags_AlwaysClamp)) fade_.SetDuration(seconds);
        auto color = fade_.Settings().color;
        if (ImGui::ColorEdit3("Fade Color", &color.x)) fade_.SetColor(color);
        int easing = static_cast<int>(fade_.Settings().easing);
        if (ImGui::Combo("Fade Easing", &easing, "Linear\0EaseIn\0EaseOut\0EaseInOut\0")) fade_.SetEasing(static_cast<FadeEasing>(easing));
        if (ImGui::Button("Fade Out")) { debugCycle_ = false; fade_.FadeOut(); }
        ImGui::SameLine();
        if (ImGui::Button("Fade In")) { debugCycle_ = false; fade_.FadeIn(); }
        if (ImGui::Button("Fade Out -> Fade In")) {
            fade_.Reset(); fade_.FadeOut(); debugCycle_ = true; blackFramePresented_ = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) { debugCycle_ = false; blackFramePresented_ = false; fade_.Reset(); }
        ImGui::EndDisabled();
        ImGui::TextUnformatted("Esc releases the mouse. Hold lasts until Fade In or Reset.");
        ImGui::TextUnformatted("Managed scene transitions own the fade until loading and Fade In finish.");
    }
    ImGui::End();
#endif
}
