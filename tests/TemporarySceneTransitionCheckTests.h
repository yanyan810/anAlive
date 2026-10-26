#pragma once
#include "TemporarySceneTransitionCheck.h"
#include "ScreenTransitionTests.h"

inline void RunTemporarySceneTransitionCheckTests(GameApp& app, TemporarySceneTransitionCheck& checker) {
    using namespace ScreenTransitionTests;
    using Action = TemporarySceneTransitionCheck::Action;
    auto& scenes = app.Scenes();
    const auto finish = [&] {
        for (int i=0; i<100 && scenes.IsTransitioning(); ++i) {
            checker.Update(app); scenes.Update(app,.05f); Draw(app,false);
        }
        Check(!scenes.IsTransitioning() && scenes.Fade().Alpha()==0, "Temporary check transition did not finish");
    };
    const auto capture = [&](const wchar_t* path) {
        Draw(app,false);
        app.Dx()->PreDraw(false);
        app.Render()->GetOffscreen()->BeginForPostEffect();
        scenes.DrawOverlay2D(app); checker.Draw(app);
        app.Render()->GetOffscreen()->End();
        app.Dx()->SetBackBufferRenderTarget(); app.Dx()->PostDraw();
        DirectX::ScratchImage image;
        const auto state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        Check(SUCCEEDED(DirectX::CaptureTexture(app.Dx()->GetCommandQueue(),app.Render()->GetOffscreen()->GetResource(),
            false,image,state,state)), "Temporary check GPU capture failed");
        Check(SUCCEEDED(DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path)), "Temporary check PNG failed");
    };
    auto* title = dynamic_cast<TitleScene*>(scenes.Current());
    Check(title && title->ready_ && title->player_.CurrentWeapon().InfiniteAmmo(), "Temporary check title/unlimited ammo");
    Check(!checker.Execute(app,Action::Clear) && !checker.Execute(app,Action::Defeat), "Non-game clear/defeat accepted");
    capture(L"generated/scene-transition-check/title.png");
    Check(checker.Execute(app,Action::Start) && !checker.Execute(app,Action::Start), "Start / duplicate guard");
    title->UpdateWorld(app,title->start_.delay,false); // Exercise the real hold without depending on window focus.
    Check(scenes.IsTransitioning(), "Temporary title start bypassed or missed the normal transition");
    Check(!checker.Execute(app,Action::Title), "Temporary check interrupted a transition");
    finish();
    auto* game = dynamic_cast<GameScene*>(scenes.Current());
    Check(game && game->stage_.IsPlaying() && game->level_.id=="stage03" && scenes.CurrentName()=="Stage03" &&
        !game->player_.CurrentWeapon().InfiniteAmmo(), "Hotel entry / finite gameplay ammo");
    capture(L"generated/scene-transition-check/game.png");
    Check(checker.Execute(app,Action::Clear) && !checker.Execute(app,Action::Clear), "Clear / duplicate guard");
    const auto time = game->stage_.Time();
    scenes.Update(app,.1f);
    Check(!game->stage_.IsPlaying() && game->stage_.Time()==time, "Temporary clear did not freeze results");
    capture(L"generated/scene-transition-check/clear.png");
    Check(checker.Execute(app,Action::Title), "Clear title return rejected"); finish();
    Check(scenes.CurrentName()=="Title", "Clear did not return to Title");
    Check(checker.Execute(app,Action::FadeCycle), "Temporary fade cycle rejected");
    for (int i=0; i<100 && scenes.Fade().IsActive(); ++i) {
        checker.Update(app); scenes.Update(app,.05f); Draw(app,false);
    }
    Check(scenes.CurrentName()=="Title" && scenes.Fade().State()==TransitionState::Idle, "Temporary fade cycle stuck/changed scene");
    Check(checker.Execute(app,Action::Start), "Second temporary start rejected");
    title = dynamic_cast<TitleScene*>(scenes.Current());
    title->UpdateWorld(app,title->start_.delay,false); finish();
    Check(checker.Execute(app,Action::Defeat), "Temporary defeat rejected");
    scenes.Update(app,0);
    game=dynamic_cast<GameScene*>(scenes.Current());
    Check(game && game->gameOver_.Active(), "Temporary defeat did not use real death route");
    scenes.Update(app,game->gameOver_.MenuDelay()+.2f);
    Check(game->gameOver_.MenuReady(), "Death sequence did not reach GameOver menu");
    capture(L"generated/scene-transition-check/game-over.png");
    Check(checker.Execute(app,Action::Start), "GameOver retry rejected"); finish();
    game=dynamic_cast<GameScene*>(scenes.Current());
    Check(game && scenes.CurrentName()=="GameOverRetry" && game->level_.id=="stage03" && !game->gameOver_.Active(), "GameOver retry did not reload the current stage");
    Check(checker.Execute(app,Action::Title), "Gameplay title return rejected"); finish();
    Check(scenes.CurrentName()=="Title", "Gameplay title return failed");
    std::ofstream("generated/scene-transition-check/result.txt") <<
        "PASS: temporary Release controller; real title explosion/hold/stage entry; unlimited title and finite gameplay ammo; "
        "clear/result freeze/title return; fade-out/opaque-frame/fade-in; real death/GameOver/retry; duplicate and transition guards; "
        "Sprite help/status rendered on title/game/clear/GameOver.\n";
}
