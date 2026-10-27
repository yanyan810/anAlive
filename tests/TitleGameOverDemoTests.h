#pragma once
#include "GameApp.h"
#include "scene/Flow/TitleScene.h"
#include "scene/Main/GameScene.h"
#include "FirstPersonCamera.h"
#include "ImGuiManagaer.h"
#include "DirectXTex.h"
#include <fstream>

inline void RunTitleGameOverDemoTests(GameApp& app) {
    const auto check=[](bool ok,const char* message) { if (!ok) throw std::runtime_error(message); };
    using Mode=TitleScene::Mode; using State=GameOverSequence::State;
    const std::filesystem::path dir="generated/title-demo-tests";
    std::filesystem::create_directories(dir);
    auto& scenes=app.Scenes(); auto& input=*app.GetInput();
    scenes.Change(app,"Title");
    auto* title=dynamic_cast<TitleScene*>(scenes.Current());
    check(title && title->ready_,"Title demo layout failed");
    const auto release=[&] {
        std::fill(std::begin(input.keys_),std::end(input.keys_),0);
        std::fill(std::begin(input.prevKeys_),std::end(input.prevKeys_),0);
        input.leftMouseDown_=input.prevLeftMouseDown_=false; input.mouseDelta_={};
    };
    release();
    for (auto& spot:title->lighting_.spots) spot.flicker.enabled=false;
    title->ResetLightFlicker(); title->ApplyLighting();
    const auto draw=[&] {
#ifdef USE_IMGUI
        app.ImGui()->Begin();
#endif
        app.Draw();
    };
    const auto capture=[&](const char* name) {
        draw();
        app.Dx()->PreDraw(false); app.Render()->RenderPostEffectsForSceneTexture();
        check(app.Render()->BeginSceneTextureOverlay(),"Title demo overlay target missing");
        scenes.DrawOverlay2D(app); app.Render()->EndSceneTextureOverlay();
        app.Dx()->SetBackBufferRenderTarget(); app.Dx()->PostDraw(); app.Dx()->WaitForGPU();
        DirectX::ScratchImage image; constexpr auto state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        check(SUCCEEDED(DirectX::CaptureTexture(app.Dx()->GetCommandQueue(),app.Render()->GetSceneTextureResource(),false,image,state,state)),"Title demo GPU readback failed");
        check(SUCCEEDED(DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),(dir/(std::string(name)+".png")).c_str())),"Title demo capture failed");
        return image;
    };
    const auto difference=[](const DirectX::ScratchImage& a,const DirectX::ScratchImage& b) {
        const auto* x=a.GetImage(0,0,0); const auto* y=b.GetImage(0,0,0); size_t changed=0;
        for (size_t row=0;row<x->height;++row) for (size_t col=0;col<x->width*4;++col)
            if (x->pixels[row*x->rowPitch+col]!=y->pixels[row*y->rowPitch+col]) ++changed;
        return changed;
    };
    check(title->mode_==Mode::GameOver,"This branch must open directly in GAME OVER mode");
    capture("00-default-game-over");
    check(title->SetMode(Mode::GameStart),"Could not select normal mode for regression checks");
    const auto startView=capture("00-game-start-mode");
    check(title->SetMode(Mode::GameOver) && !scenes.IsTransitioning() && !title->start_.Starting(),"Mode selection started an animation");
    const auto demoView=capture("01-game-over-mode");
    check(difference(startView,demoView)>1000,"Mode selection did not replace 3D text");
    AABB demoBounds{}; title->demoText_.GetModel()->GetLocalAABB(demoBounds);
    const auto bounds=title->startExplosion_.source.bounds;
    check(StageLength(demoBounds.min-bounds.min)<.01f && StageLength(demoBounds.max-bounds.max)<.01f,"GAME OVER did not fit GAME START bounds");
    // Moving/turning the view must render the word at its fixed world location.
    // Compare the normal frame against a freshly synchronized camera reference.
    const auto originalPose=title->player_.GetTransform();
    for (int pose=0;pose<3;++pose) {
        if (pose==2) check(title->SetMode(Mode::GameStart),"Could not hide demo text for camera test");
        const auto position=originalPose.translate+Vector3{pose==1 ? 0.0f : .75f,0,0};
        const auto rotation=originalPose.rotate+Vector3{0,pose==0 ? 0.0f : .16f,0};
        title->player_.SetStage(&title->level_.collision,position,rotation);
        title->UpdateWorld(app,0,false);
        if (pose==2) check(title->SetMode(Mode::GameOver),"Could not restore demo mode after camera movement");
        const auto frame=capture(("01-camera-motion-"+std::to_string(pose)).c_str());
        title->demoText_.Update(0);
        const auto reference=capture(("01-camera-reference-"+std::to_string(pose)).c_str());
        check(difference(frame,reference)==0,"GAME OVER retained stale camera projection after movement/turn/mode switch");
    }
    title->player_.SetStage(&title->level_.collision,originalPose.translate,originalPose.rotate);
    title->UpdateWorld(app,0,false);
    const auto demoAim=[&] {
        const auto origin=title->player_.EyePosition();
        for (const auto& face:title->demoTextParts_[0].geometry->faces) {
            const auto center=(face[0]+face[1]+face[2])*(1.0f/3);
            const auto delta=center-origin; const float length=StageLength(delta);
            if (length<.1f) continue;
            const auto direction=delta*(1/length);
            const auto hit=title->TraceDemoText(origin,direction,100);
            StageHit wall;
            if (hit && (!title->level_.collision.Raycast(origin,direction,hit->distance,wall) || wall.distance>hit->distance-.001f)) return direction;
        }
        throw std::runtime_error("Cannot aim at visible GAME OVER geometry");
    };
    const auto fire=[&](const Vector3& direction) {
        Bullet bullet; bullet.position=title->player_.EyePosition(); bullet.direction=direction; bullet.damage=10;
        BulletSimulation simulation; simulation.Spawn(bullet); title->bullets_.Restore(simulation);
        title->UpdateWorld(app,.2f,false);
    };
    fire({0,1,0}); check(!scenes.IsTransitioning(),"A missed shot started the demo"); title->bullets_.Clear();
    const auto direction=demoAim();
    StageCollider wall; const auto wallCenter=title->player_.EyePosition()+direction*2;
    wall.local={wallCenter-Vector3{.3f,.3f,.3f},wallCenter+Vector3{.3f,.3f,.3f}}; wall.UpdateMatrices();
    title->level_.collision.colliders.push_back(wall);
    fire(direction); check(!scenes.IsTransitioning(),"A shot through a wall started the demo");
    title->level_.collision.colliders.pop_back(); title->bullets_.Clear();
    fire(direction);
    check(scenes.IsTransitioning() && title->transitionRequested_ && !title->SetMode(Mode::GameStart),
        "GAME OVER impact did not request/lock the stage transition");
    const auto finishTransition=[&] {
        for (int i=0;i<240 && scenes.IsTransitioning();++i) { scenes.Update(app,.05f); draw(); }
        check(!scenes.IsTransitioning(),"Scene transition did not finish");
        release();
    };
    finishTransition();
    auto* game=dynamic_cast<GameScene*>(scenes.Current());
    check(game && game->IsGameOverDemo() && game->stageLoaded_,"Demo did not reach its dedicated GameScene");
    std::ofstream trace(dir/"trace.txt");
    const auto checkDemoStart=[&] {
        check(game->player_.GetHP()==20 && game->player_.CurrentWeapon().Definition().id.empty() &&
            !game->player_.IsMovementEnabled(),"Demo HP/unarmed/player lock policy failed");
        check(!game->sequenceOwnsCamera_ && game->enemies_.size()==6 && game->weaponVisuals_.empty() &&
            game->stage_.Goals().empty() && !game->gameOver_.Active(),"Demo retained intro/pickups/goals/death state");
        game->BeginStartSequence(app,true);
        check(!game->sequenceOwnsCamera_,"Demo allowed forced start camera");
        check(!game->HandleInteraction(true,true),"Demo allowed a weapon pickup");
        for (size_t i=0;i<game->enemies_.size();++i) {
            const auto* enemy=game->enemies_[i]; const auto& d=enemy->Definition();
            const float range=StageLength(enemy->GetPosition()-game->player_.GetTransform().translate);
            check(d.id=="normal" && range>d.collisionRadius+.4f && range<d.attackRange,
                "Demo enemy overlaps player or starts outside melee range");
            check(EnemyNavigation::Clear(&game->level_.collision,enemy->GetPosition(),game->player_.GetTransform().translate,
                d.collisionRadius,d.collisionHeight),"Demo enemy body route is blocked");
            for (size_t j=0;j<i;++j) check(StageLength(enemy->GetPosition()-game->enemies_[j]->GetPosition())>
                d.collisionRadius+game->enemies_[j]->Definition().collisionRadius,"Demo enemies overlap");
        }
    };
    const auto dieByEnemies=[&](int retry) {
        checkDemoStart();
        // Inputs, including held fire/reload/pickup and movement, cannot arm or move this demo player.
        input.keys_[DIK_W]=input.keys_[DIK_E]=input.keys_[DIK_R]=0x80;
        input.leftMouseDown_=true;
        const auto spawn=game->player_.GetTransform().translate;
        float seconds=0;
        while (!game->gameOver_.Active() && seconds<3) {
            game->Update(app,1.0f/60); seconds+=1.0f/60;
        }
        trace<<"retry="<<retry<<" deathSeconds="<<seconds<<" attacks="<<game->enemyAttackCount_<<'\n';
        check(game->gameOver_.Active() && game->player_.IsDead() && seconds<3 && game->enemyAttackCount_>=2,
            "Ordinary enemies failed to kill demo player quickly");
        check(StageLength(game->player_.GetTransform().translate-spawn)<.001f && game->shotCount_==0 &&
            game->player_.CurrentWeapon().Definition().id.empty(),"Held input moved/armed/fired the demo player");
        for (const auto* enemy:game->enemies_) check(enemy->AI().visible && enemy->AI().inFov &&
            !enemy->AI().occluded && enemy->AI().hasLastSeen,"A demo enemy failed ordinary perception");
        check(game->gameOver_.HasVisual() && StageLength(game->gameOver_.Death().impactDirection)>0,
            "Demo bypassed ordinary impact/death context");
        release();
    };
    checkDemoStart(); capture("02-demo-stage-unarmed");
    dieByEnemies(0);
    const auto& settings=game->gameOver_.settings;
    const float floorAt=settings.staggerSeconds+settings.collapseSeconds;
    const float opened=floorAt+settings.floorHoldSeconds+settings.closeSeconds+settings.corpseSeconds+settings.openSeconds;
    const auto advance=[&](float target) { game->UpdateGameOver(app,std::max(0.0f,target-game->gameOver_.Elapsed())); };
    advance(settings.staggerSeconds*.5f); capture("03-death-double-vision");
    check(game->gameOver_.PostEffect().doubleVisionStrength>0,"Demo omitted double vision");
    advance(floorAt+.01f); capture("04-contact-impact-blur");
    check(game->gameOver_.PostEffect().impactBlurPixels>1,"Demo omitted impact blur");
    advance(floorAt+settings.floorHoldSeconds+settings.closeSeconds+.1f);
    const auto dark=capture("05-eyes-closed");
    const auto* pixels=dark.GetImage(0,0,0);
    for (size_t y=0;y<pixels->height;++y) for (size_t x=0;x<pixels->width;++x) {
        const auto* pixel=pixels->pixels+y*pixels->rowPitch+x*4;
        check(pixel[0]==0 && pixel[1]==0 && pixel[2]==0,"Closed eyes leaked scene pixels");
    }
    advance(opened+.2f); capture("06-eyes-open");
    check(game->gameOver_.EnemiesVisible() && game->gameOver_.PostEffect().mosaicStrength>0,"Demo omitted rainbow noise");
    advance(opened+.4499f); capture("07-noise-before-enemy-cut");
    check(game->gameOver_.EnemiesVisible() && game->gameOver_.PostEffect().mosaicOcclusion==1,"Demo cover-before-cut failed");
    advance(opened+.4501f); capture("08-noise-after-enemy-cut");
    check(!game->gameOver_.EnemiesVisible() && game->gameOver_.PostEffect().mosaicOcclusion==1,"Demo enemy disappearance failed");
    advance(game->gameOver_.MenuDelay()+.2f); capture("09-demo-menu");
    check(game->gameOver_.MenuReady() && game->gameOver_.AI().state==EnemyState::Wander,"Demo did not reach Wander/menu");
    for (int retry=1;retry<=3;++retry) {
        check(game->LeaveGameOver(app,true),"Demo Retry rejected");
        finishTransition(); game=dynamic_cast<GameScene*>(scenes.Current());
        check(game && game->IsGameOverDemo(),"Retry lost dedicated stage ID");
        dieByEnemies(retry);
        advance(game->gameOver_.MenuDelay()+.2f);
    }
    check(game->LeaveGameOver(app,false),"Demo Title rejected");
    finishTransition(); title=dynamic_cast<TitleScene*>(scenes.Current());
    check(title && title->mode_==Mode::GameOver && title->player_.GetHP()==100 &&
        !app.Render()->DeathPostEffect().Active(),"Title did not restore default GAME OVER and clear death post effects");
    capture("10-title-restored");
    // Exercise the normal mode retained in ImGui for debugging.
    check(title->SetMode(Mode::GameStart),"Could not select normal mode after returning to Title");
    const auto origin=title->player_.EyePosition(); Vector3 normalDirection{}; bool found=false;
    for (const auto& face:title->startExplosion_.source.geometry->faces) {
        const auto delta=(face[0]+face[1]+face[2])*(1.0f/3)-origin; const auto d=delta*(1/StageLength(delta));
        EnemyPartHit hit; StageHit block;
        if (title->startTarget_->Raycast(origin,d,100,hit) && !title->level_.collision.Raycast(origin,d,hit.distance,block)) {
            normalDirection=d; found=true; break;
        }
    }
    check(found,"Could not aim at restored GAME START"); fire(normalDirection);
    check(title->start_.Starting() && title->startExplosion_.fragmentCount>0,"Normal GAME START explosion was broken");
    title->UpdateWorld(app,title->start_.delay+.01f,false);
    check(scenes.IsTransitioning(),"Normal GAME START did not request transition");
    finishTransition(); game=dynamic_cast<GameScene*>(scenes.Current());
    check(game && game->stageId_=="stage03" && !game->IsGameOverDemo() && game->player_.GetHP()==100 &&
        !game->player_.CurrentWeapon().Definition().id.empty() && game->player_.IsMovementEnabled(),
        "Demo HP/weapon/movement policy leaked into normal GAME START");
    check(!app.Render()->DeathPostEffect().Active(),"Demo post effects leaked to normal game");
    // Both other normal stages retain their ordinary HP, weapons and spawn data.
    for (const auto* id:{"stage01","stage02"}) {
        scenes.Register("NormalDemoIsolation",[id]{ return std::make_unique<GameScene>(false,id); });
        scenes.Change(app,"NormalDemoIsolation"); game=dynamic_cast<GameScene*>(scenes.Current());
        check(game && game->stageLoaded_ && !game->IsGameOverDemo() && game->player_.GetHP()==100 &&
            !game->player_.CurrentWeapon().Definition().id.empty() && game->player_.IsMovementEnabled(),"Demo policy leaked to normal stage");
        draw();
    }
    app.Dx()->WaitForGPU();
    std::ofstream(dir/"result.txt")<<"PASS: title GAME START/GAME OVER visuals and precise hits, miss/wall rejection, dedicated exported stage, 6 non-overlapping normal enemies with clear sight/routes and natural attacks, HP20 unarmed player, held input suppression, no intro, ordinary death effects (double vision, impact blur, black eyelids, rainbow cover and enemy disappearance), Wander/menu, 3 full stage retries, default/returned title shows GAME OVER without mode buttons, debug GAME START reaches Stage03, no demo policy leakage to stage01/02/03.\n";
}
