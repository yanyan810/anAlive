#pragma once
#include "GameApp.h"
#include "scene/Flow/TitleScene.h"
#include "scene/Main/GameScene.h"
#include "UIFont.h"
#include <iomanip>
#include <sstream>

// Temporary, opt-in Release check. Remove this file and the GameApp hooks
// after checking transitions; normal launches never create this controller.
class TemporarySceneTransitionCheck {
public:
    enum class Action { Title, Start, Clear, Defeat, FadeCycle };

    void Initialize(GameApp& app) {
        panel_.Initialize(app.SpriteCom(), app.Dx(), "resources/white1x1.png");
        panel_.SetPosition({0,0});
        panel_.SetColor({.015f,.025f,.045f,.94f});
        const auto& white = TextureManager::GetInstance()->GetMetaData("resources/white1x1.png");
        panel_.SetScale({static_cast<float>(WinApp::kClientWidth)/static_cast<float>(white.width),
            88.0f/static_cast<float>(white.height),1});
        font_.Initialize(app.SpriteCom(), app.Dx(), 320);
        view_ = Matrix4x4::MakeIdentity4x4();
        projection_ = Matrix4x4::MakeOrthographicMatrix(0,0,
            static_cast<float>(WinApp::kClientWidth),static_cast<float>(WinApp::kClientHeight),0,100);
    }

    void Update(GameApp& app) {
        auto& fade = app.Scenes().Fade();
        if (fadeCycle_ && blackFrameDrawn_ && fade.State()==TransitionState::Hold) {
            fadeCycle_ = false;
            blackFrameDrawn_ = false;
            fade.FadeIn(.75f);
        } else if (fadeCycle_ && !fade.IsActive()) {
            fadeCycle_ = false; // A scene's immediate change can cancel a manual fade.
            blackFrameDrawn_ = false;
        }
        const auto& input = *app.GetInput();
        if (!input.HasFocus()) return;
        if (input.IsKeyTrigger(DIK_F1)) Execute(app,Action::Title);
        else if (input.IsKeyTrigger(DIK_F2)) Execute(app,Action::Start);
        else if (input.IsKeyTrigger(DIK_F3)) Execute(app,Action::Clear);
        else if (input.IsKeyTrigger(DIK_F4)) Execute(app,Action::Defeat);
        else if (input.IsKeyTrigger(DIK_F5)) Execute(app,Action::FadeCycle);
    }

    bool Execute(GameApp& app, Action action) {
        auto& scenes = app.Scenes();
        auto* title = dynamic_cast<TitleScene*>(scenes.Current());
        auto* game = dynamic_cast<GameScene*>(scenes.Current());
        if (scenes.IsTransitioning() || scenes.Fade().IsActive() ||
            (title && title->StartExplosionActive())) return false;
        switch (action) {
        case Action::Title:
            return game && !game->stage_.IsPlaying() ? game->ReturnToTitle(app) : scenes.TransitionTo("Title");
        case Action::Start:
            if (title) {
                if (!title->ready_ || !title->start_.Begin()) return false;
                title->BeginStartExplosion(); // Same explosion, hold and fade as shooting GAME START.
                return true;
            }
            return scenes.TransitionTo("Game");
        case Action::Clear:
            if (!game || !game->stageLoaded_ || !game->stage_.IsPlaying() || game->stage_.Goals().empty()) return false;
            if (!game->stage_.Update(0,game->stage_.Goals().front().position)) return false;
            game->OnStageClear(app); // Real result overlay and frozen gameplay.
            return true;
        case Action::Defeat:
            if (!game || !game->stageLoaded_ || !game->stage_.IsPlaying() || game->player_.IsDead()) return false;
            game->player_.ApplyDamage(game->player_.GetHP()); // Normal death route on the next scene update.
            return true;
        case Action::FadeCycle:
            scenes.Fade().SetColor({0,0,0});
            scenes.Fade().SetEasing(FadeEasing::Linear);
            scenes.Fade().FadeOut(.75f);
            app.GetInput()->SetCameraControlEnabled(false);
            fadeCycle_ = true;
            blackFrameDrawn_ = false;
            return true;
        }
        return false;
    }

    void Draw(GameApp& app) {
        auto& scenes = app.Scenes();
        const auto& fade = scenes.Fade();
        if (fadeCycle_ && fade.State()==TransitionState::Hold) blackFrameDrawn_ = true;
        const char* state = "IDLE";
        switch (fade.State()) {
        case TransitionState::FadeOut: state="FADE OUT"; break;
        case TransitionState::Hold: state="HOLD"; break;
        case TransitionState::FadeIn: state="FADE IN"; break;
        default: break;
        }
        auto* game = dynamic_cast<GameScene*>(scenes.Current());
        auto* title = dynamic_cast<TitleScene*>(scenes.Current());
        const std::string scene = game && !game->stage_.IsPlaying() ? "STAGE CLEAR" : scenes.CurrentName();
        std::ostringstream status;
        status << "SCENE: " << scene << " - " << state << " - ALPHA: " << std::fixed << std::setprecision(2) << fade.Alpha();
        if (scenes.IsWaitingForScene()) status << " - LOADING";
        else if (scenes.IsTransitioning() || fade.IsActive() || (title && title->StartExplosionActive())) status << " - PLEASE WAIT";
        else if (game && game->stage_.IsPlaying()) status << " - F3/F4 READY";
        else status << " - F2 TO PLAY";
        if (!scenes.LastTransitionError().empty()) status << " - LOAD ERROR";
        // The supplied GameOver logo reaches the top edge; keep it unobscured.
        const float panelY = scenes.CurrentName()=="GameOver" ? static_cast<float>(WinApp::kClientHeight)-88 : 0;
        panel_.SetPosition({0,panelY});
        font_.Reset();
        font_.DrawText("TEMPORARY TRANSITION CHECK",16,panelY+9,12,{1,.8f,.3f,1});
        font_.DrawText("F1 TITLE - F2 START/RETRY - F3 CLEAR - F4 DEFEAT - F5 FADE OUT/IN",16,panelY+34,12,{1,1,1,1});
        font_.DrawText(status.str(),16,panelY+59,11,{.7f,.85f,1,1});
        panel_.Update(view_,projection_);
        panel_.Draw();
        font_.Draw(view_,projection_);
    }

private:
    Sprite panel_;
    UIFont font_;
    Matrix4x4 view_{}, projection_{};
    bool fadeCycle_ = false, blackFrameDrawn_ = false;
};
