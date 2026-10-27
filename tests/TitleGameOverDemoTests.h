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
    const auto startView=capture("00-game-start-mode");
    check(TitleScene::ModeSwitchHit({990,30})==0 && TitleScene::ModeSwitchHit({1150,30})==1 &&
        TitleScene::ModeSwitchHit({640,360})==-1,"Title mode button hit bounds failed");
    const auto targetId=title->startTarget_->GetSpawnId();
    check(title->SetMode(Mode::GameOver) && !title->gameOver_.Active() && !title->start_.Starting(),"Mode selection started an animation");
    const auto demoView=capture("01-game-over-mode");
    check(difference(startView,demoView)>1000,"Mode selection did not replace 3D text");
    AABB demoBounds{}; title->demoText_.GetModel()->GetLocalAABB(demoBounds);
    const auto bounds=title->startExplosion_.source.bounds;
    check(StageLength(demoBounds.min-bounds.min)<.01f && StageLength(demoBounds.max-bounds.max)<.01f,"GAME OVER did not fit GAME START bounds");
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
    fire({0,1,0}); check(!title->gameOver_.Active(),"A missed shot started the demo"); title->bullets_.Clear();
    const auto direction=demoAim();
    StageCollider wall; const auto wallCenter=title->player_.EyePosition()+direction*2;
    wall.local={wallCenter-Vector3{.3f,.3f,.3f},wallCenter+Vector3{.3f,.3f,.3f}}; wall.UpdateMatrices();
    title->level_.collision.colliders.push_back(wall);
    fire(direction); check(!title->gameOver_.Active(),"A shot through a wall started the demo");
    title->level_.collision.colliders.pop_back(); title->bullets_.Clear();
    const auto initialCamera=title->camera_.GetTranslate(), initialRotation=title->camera_.GetRotate();
    const auto capacity=title->pool_.Capacity();
    fire(direction);
    check(title->gameOver_.Phase()==State::Death && title->player_.IsDead() && !title->start_.Starting() &&
        scenes.Current()==title && !scenes.IsTransitioning(),"Actual GAME OVER bullet impact did not start an in-place death");
    check(StageLength(title->gameOver_.Death().camera.position-initialCamera)<.001f &&
        StageLength(title->gameOver_.Death().camera.rotation-initialRotation)<.001f,"Demo lost impact camera pose");
    check(!title->SetMode(Mode::GameStart) && title->bullets_.Count()==0 && !input.IsCameraControlEnabled(),"Demo accepted title controls or retained shots");
    const auto& s=title->gameOver_.settings;
    const float floorAt=s.staggerSeconds+s.collapseSeconds;
    const float opened=floorAt+s.floorHoldSeconds+s.closeSeconds+s.corpseSeconds+s.openSeconds;
    const auto advance=[&](float target) { title->UpdateGameOverDemo(app,std::max(0.0f,target-title->gameOver_.Elapsed())); };
    advance(s.staggerSeconds*.5f); capture("02-death-double-vision");
    check(title->gameOver_.PostEffect().doubleVisionStrength>0 && title->gameOver_.PostEffect().vignette>0,"Demo omitted death post effects");
    advance(floorAt+.01f); capture("03-contact-impact-blur");
    check(title->gameOver_.PostEffect().impactBlurPixels>1,"Demo omitted contact blur");
    advance(floorAt+s.floorHoldSeconds+s.closeSeconds+.1f);
    const auto dark=capture("04-eyes-closed");
    for (size_t i=0;i<dark.GetImage(0,0,0)->height;++i) for (size_t x=0;x<dark.GetImage(0,0,0)->width;++x) {
        const auto* p=dark.GetImage(0,0,0)->pixels+i*dark.GetImage(0,0,0)->rowPitch+x*4;
        check(p[0]==0 && p[1]==0 && p[2]==0,"Closed eyes leaked title/mode controls");
    }
    advance(opened+.2f); capture("05-eyes-open");
    check(title->gameOver_.EnemiesVisible() && title->gameOver_.PostEffect().mosaicStrength>0,"Demo hid enemy before opening or omitted rainbow noise");
    advance(opened+.4499f); capture("06-rainbow-cover-before-cut");
    check(title->gameOver_.EnemiesVisible() && title->gameOver_.PostEffect().mosaicOcclusion==1,"Demo cover-before-cut failed");
    advance(opened+.4501f); capture("07-rainbow-cover-after-cut");
    check(!title->gameOver_.EnemiesVisible() && title->gameOver_.PostEffect().mosaicOcclusion==1,"Demo did not hide ordinary enemy under full noise");
    advance(opened+.55f);
    // Exact GPU comparison: only ordinary enemies/shadows disappear. Stage,
    // background title and GAME OVER text must match a reference without enemies.
    title->gameOver_.settings.post.enabled=false;
    title->camera_.SetTranslate(initialCamera); title->camera_.SetRotate(initialRotation); title->camera_.Update();
    title->environment_.Update(0); title->demoText_.Update(0);
    for (auto* enemy:title->enemies_) enemy->UpdateVisuals(0);
    const auto hidden=capture("08-selective-enemy-hidden");
    const auto actors=title->enemies_; title->enemies_={title->startTarget_.get()};
    const auto reference=capture("08-selective-reference"); title->enemies_=actors;
    check(difference(hidden,reference)==0,"Enemy shadow/geometry remained or title text/stage disappeared");
    title->gameOver_.settings.post.enabled=true;
    input.keys_[DIK_RETURN]=0x80; input.leftMouseDown_=true;
    advance(title->gameOver_.MenuDelay()+.2f); capture("09-game-over-menu");
    check(!title->gameOverOverlay_.Armed() && title->gameOver_.AI().state==EnemyState::Wander,"Demo skipped Wander or accepted held input");
    const auto before=title->gameOver_.Position();
    for (int i=0;i<1200;++i) {
        const auto p=title->gameOver_.Position(); title->UpdateGameOverDemo(app,1.0f/60);
        check(EnemyNavigation::Clear(&title->level_.collision,p,title->gameOver_.Position(),s.collisionRadius,title->player_.Settings().cameraHeight+.18f),"Demo Wander crossed title collision");
        const auto eye=title->camera_.GetTranslate();
        check(StageLength(FirstPersonCamera::ResolveOverlap(title->level_.collision,eye,s.cameraRadius)-eye)<.005f,"Demo camera penetrated title geometry");
    }
    check(StageLength(before-title->gameOver_.Position())>.1f && !title->gameOver_.EnemiesVisible(),"Demo Wander was stuck or restored enemies");
    release(); title->UpdateGameOverDemo(app,0); check(title->gameOverOverlay_.Armed(),"Demo menu did not arm on release");
    for (int retry=0;retry<3;++retry) {
        check(title->ChooseDemoMenu(app,true),"Demo Retry rejected");
        check(title->gameOver_.Phase()==State::Death && title->gameOver_.EnemiesVisible() &&
            title->mode_==Mode::GameOver && title->enemies_.size()==2 && title->pool_.Capacity()==capacity &&
            title->pool_.RuntimeAllocations()==0 && !title->enemies_[0]->IsDead() && !title->startTarget_->IsDead(),"Demo Retry retained actor/visibility state");
        check(StageLength(title->gameOver_.Death().player.translate-title->level_.playerPosition)<.001f &&
            StageLength(title->gameOver_.Death().camera.rotation-title->level_.playerRotation)<.001f,"Demo Retry did not restore title start pose");
        title->UpdateGameOverDemo(app,title->gameOver_.MenuDelay()+.1f);
        check(scenes.Current()==title && !scenes.IsTransitioning() && title->gameOver_.MenuReady(),"Demo Retry created a scene or failed to finish");
    }
    check(title->ChooseDemoMenu(app,false),"Demo Title rejected");
    check(title->mode_==Mode::GameStart && !title->gameOver_.Active() && title->gameOver_.EnemiesVisible() &&
        title->player_.GetHP()==100 && title->player_.IsMovementEnabled() && !app.Render()->DeathPostEffect().Active() &&
        title->environment_.GetModel()==title->environmentFull_ && title->startTarget_->GetSpawnId()==targetId,"Demo Title did not restore normal title");
    capture("10-title-restored");
    // Normal GAME START still uses the original HP target, burst and stage transition.
    const auto origin=title->player_.EyePosition(); Vector3 normalDirection{}; bool found=false;
    for (const auto& face:title->startExplosion_.source.geometry->faces) {
        const auto delta=(face[0]+face[1]+face[2])*(1.0f/3)-origin; const auto d=delta*(1/StageLength(delta));
        EnemyPartHit hit; StageHit block;
        if (title->startTarget_->Raycast(origin,d,100,hit) && !title->level_.collision.Raycast(origin,d,hit.distance,block)) { normalDirection=d; found=true; break; }
    }
    check(found,"Could not aim at normal GAME START after Title"); fire(normalDirection);
    check(title->start_.Starting() && !title->gameOver_.Active() && title->startExplosion_.fragmentCount>0,"Normal GAME START was broken by demo mode");
    title->UpdateWorld(app,title->start_.delay+.01f,false);
    check(scenes.IsTransitioning(),"Normal GAME START did not request the original stage transition");
    for (int i=0;i<200 && scenes.IsTransitioning();++i) { scenes.Update(app,.05f); draw(); }
    check(!scenes.IsTransitioning() && scenes.CurrentName()=="Stage03" && dynamic_cast<GameScene*>(scenes.Current()),"Normal GAME START did not reach Stage03");
    check(!app.Render()->DeathPostEffect().Active(),"Demo post effects leaked to normal gameplay");
    app.Dx()->WaitForGPU();
    std::ofstream(dir/"result.txt")<<"PASS: title GAME START/GAME OVER mode visuals and exact glyph hits, miss/wall rejection, in-place existing GameOverSequence and death post effects, black eyelids, rainbow cover and selective ordinary-enemy/shadow disappearance, title stage/text preservation, safe Wander (20s), held-input guard, 3 retries without scene/pool growth, Title restores start mode/camera/HP/visibility/post, original GAME START explosion and Stage03 transition.\n";
}
