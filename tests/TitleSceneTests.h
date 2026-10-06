#pragma once
#include "GameApp.h"
#include "scene/Flow/TitleScene.h"
#include "scene/Main/GameScene.h"
#include "ImGuiManagaer.h"
#include "DirectXTex.h"
#include "DebugJsonEditor.h"
#include <fstream>
#include <stdexcept>

inline void RunTitleSceneTests(GameApp& app) {
    const auto check=[](bool ok,const char* message) { if (!ok) throw std::runtime_error(message); };
    FadeManager fade;
    fade.FadeOut(.75f); fade.Update(.375f);
    check(fade.Alpha()==.5f && !fade.IsFinished(),"FadeOut midpoint");
    fade.Update(-1); fade.Update(std::numeric_limits<float>::quiet_NaN());
    check(fade.Alpha()==.5f,"Invalid dt advanced fade");
    fade.Update(1); check(fade.Alpha()==1 && fade.IsFinished(),"FadeOut clamp/completion");
    fade.FadeIn(.75f); check(fade.Alpha()==1,"FadeIn must start black");
    fade.Update(.375f); check(fade.Alpha()==.5f,"FadeIn midpoint");
    fade.Update(.375f); check(fade.Alpha()==0 && fade.IsFinished(),"FadeIn completion");
    fade.FadeOut(0); check(fade.Alpha()==1 && fade.IsFinished(),"Instant fade");
    fade.Reset(); check(fade.Alpha()==0,"Fade reset");
    auto* title = dynamic_cast<TitleScene*>(app.Scenes().Current());
    check(title && title->ready_, "Title layout failed to load");
    const auto lighting = title->lighting_;
    check(TitleLighting::FromJson(lighting.ToJson()).ToJson()==lighting.ToJson(),"Lighting JSON round trip");
    auto invalidLight=lighting.ToJson();
    invalidLight["spots"]["Enemy"]["direction"]={0,0,0};
    bool rejected=false;
    try { (void)TitleLighting::FromJson(invalidLight); } catch (const std::exception&) { rejected=true; }
    check(rejected,"Zero spot direction accepted");
    invalidLight=lighting.ToJson();
    invalidLight["spots"]["GAME START"]["innerAngleDegrees"]=90;
    rejected=false;
    try { (void)TitleLighting::FromJson(invalidLight); } catch (const std::exception&) { rejected=true; }
    check(rejected,"Invalid spot cone accepted");
    const std::string tuningPath="generated/title-tests/lighting-save.json";
    { std::ofstream file(tuningPath); file<<lighting.ToJson().dump(2); }
    DebugJsonEditor editor;
    check(editor.Open(tuningPath),"Lighting editor open");
    editor.document["spots"]["GAME START"]["intensity"]=.75f;
    const auto validate=[](const std::string& path) -> std::string {
        try { (void)TitleLighting::Load(path); return {}; }
        catch (const std::exception& e) { return e.what(); }
    };
    check(editor.Save(validate) && TitleLighting::Load(tuningPath).spots[2].intensity==.75f &&
        std::filesystem::exists(tuningPath+".debug-backup"),"Lighting save/reload/backup");
    editor.document["spots"]["Enemy"]["distance"]=-1;
    check(!editor.Save(validate) && TitleLighting::Load(tuningPath).spots[1].distance==lighting.spots[1].distance,
        "Invalid lighting save replaced valid settings");
    editor.document=lighting.ToJson();
    { std::ofstream file(tuningPath,std::ios::app); file<<'\n'; }
    check(!editor.Save(validate),"Lighting save overwrote external edit");
    const auto capture=[&](const wchar_t* path, bool expectBlack=false) {
#ifdef USE_IMGUI
        app.ImGui()->Begin();
#endif
        app.Draw(); app.Dx()->WaitForGPU();
        DirectX::ScratchImage image;
        auto* resource=app.Render()->GetOffscreen()->GetResource();
        auto state=D3D12_RESOURCE_STATE_RENDER_TARGET;
#ifdef USE_IMGUI
        // Capture the actual composited Scene image, after post effects, HUD and fade.
        auto* render=app.Render();
        for (auto* pass : {render->postBuffers_[0].get(),render->postBuffers_[1].get(),
            render->compositeBuffer_.get(),render->compositeBuffer2_.get(),render->previewBuffer_.get()}) {
            if (pass->GetSrvIndex()==render->previewSrvIndex_) {
                resource=pass->GetResource(); state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; break;
            }
        }
#endif
        check(SUCCEEDED(DirectX::CaptureTexture(app.Dx()->GetCommandQueue(),resource,false,image,state,state)),"Title render capture failed");
        if (expectBlack) {
            const auto* pixels=image.GetImage(0,0,0);
            for (size_t y=0; y<pixels->height; ++y) for (size_t x=0; x<pixels->width; ++x) {
                const auto* p=pixels->pixels+y*pixels->rowPitch+x*4;
                check(p[0]==0 && p[1]==0 && p[2]==0,"Fully black frame leaked 3D/HUD pixels");
            }
        }
        check(SUCCEEDED(DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path)),"Title PNG failed");
        uint64_t energy=0;
        const auto* pixels=image.GetImage(0,0,0);
        for (size_t y=0;y<pixels->height;++y) for (size_t x=0;x<pixels->width;++x) {
            const auto* p=pixels->pixels+y*pixels->rowPitch+x*4;
            energy+=static_cast<uint64_t>(p[0])+p[1]+p[2];
        }
        return energy;
    };
    auto fillOnly=lighting;
    for (auto& spot : fillOnly.spots) spot.intensity=0;
    fillOnly.Apply(title->sceneLight_);
    const auto fillEnergy=capture(L"generated/title-tests/title-fill-only.png");
    const std::array<const wchar_t*,3> lightImages{L"generated/title-tests/title-unalive-light.png",
        L"generated/title-tests/title-enemy-light.png",L"generated/title-tests/title-start-light.png"};
    for (size_t i=0;i<lighting.spots.size();++i) {
        auto single=fillOnly;
        single.spots[i]=lighting.spots[i];
        single.spots[i].intensity=1; // Test each GPU slot even if user tuning has disabled it.
        single.Apply(title->sceneLight_);
        check(capture(lightImages[i])>fillEnergy+100,"Spot GPU slot did not illuminate the scene");
    }
    lighting.Apply(title->sceneLight_);
    capture(L"generated/title-tests/title.png");
    check(title->enemies_.size()==2 && title->startTarget_->CaptureDebug().parts.size()==1,"Title prototype objects");
    check(title->player_.IsMovementEnabled(),"Title WASD movement disabled");
    const float hp = title->player_.GetHP();
    const auto enemyPosition = title->enemies_[0]->GetPosition();
    for (int i=0; i<120; ++i) title->UpdateWorld(app,1.0f/60,false);

    check(StageLength(title->enemies_[0]->GetPosition()-enemyPosition)==0 && title->player_.GetHP()==hp,"Title enemy attacked/moved");
    check(!title->start_.Starting() && title->NextScene().empty(),"Title auto-started");
    auto* enemy = title->enemies_[0];
    const auto state = enemy->CaptureDebug();
    const auto& part = state.parts[0];
    const auto tri = part.geometry->faces[0];
    const auto local = (tri[0]+tri[1]+tri[2])*(1.0f/3);
    const auto point = StagePoint(local,Matrix4x4::MakeAffineMatrix(enemy->Definition().VisualScale(state.scale),state.rotation,state.position));
    auto weapon = *title->weapons_.Find(title->weaponId_);
    weapon.damage = 10;
    const auto gun = Matrix4x4::MakeAffineMatrix({1,1,1},{0,1.57079632679f,0},point-Vector3{5,0,0});
    StageWorld empty;
    int enemyHits = 0;
    title->bullets_.Spawn(weapon,gun,0,title->random_,empty,title->enemies_);
    title->bullets_.Update(.2f,empty,title->enemies_,[&](const BulletEnemyImpact& hit) {
        check(hit.result.damage>0,"Title enemy received no part damage"); ++enemyHits;
    });
    check(enemyHits==1 && !title->start_.Starting(),"Enemy shot started game or missed");
    enemy->Die();
    check(enemy->IsDead() && !enemy->CanReturnToPool(),"Title enemy death/debris");
    const auto allocations = Object3d::debugInitializationCount;
    for (int i=0; i<80; ++i) title->UpdateWorld(app,.1f,false);
    check(title->enemies_.size()==2 && title->enemies_[0]==enemy && !enemy->IsDead(),"Title pool respawn");
    check(Object3d::debugInitializationCount==allocations,"Title respawn allocated renderers");
    check(title->player_.GetHP()==hp,"Title death explosion damaged player");

    const auto startState = title->startTarget_->CaptureDebug();
    check(startState.parts[0].role==EnemyPartRole::Head && startState.parts[0].type==EnemyPartType::Head &&
        startState.parts[0].hp==1 && startState.models.size()==1,"GAME START must be one Head model");
    const auto& geometry = *startState.parts[0].geometry;
    Vector3 firePosition{};
    const std::array<float,9> columns{1,1.7f,2.4f,3.2f,4.4f,5.1f,5.8f,6.5f,7.2f};
    for (size_t i=0; i<columns.size(); ++i) {
        bool tested=false;
        for (const auto& face : geometry.faces) {
            const auto a=face[1]-face[0], b=face[2]-face[0];
            if (std::abs(a.x*b.y-a.y*b.x)<.0001f) continue;
            const auto center=(face[0]+face[1]+face[2])*(1.0f/3);
            const float right=i+1<columns.size()?columns[i+1]:startState.parts[0].bounds.max.x+.01f;
            if (center.x<columns[i] || center.x>=right) continue;
            const auto origin=center-Vector3{0,0,3};
            EnemyPartHit hit;
            if (!title->startTarget_->Raycast(origin,{0,0,1},5,hit)) continue;
            check(hit.part==EnemyPartType::Head && hit.partIndex==0,"Letter is not Head");
            check(!title->start_.Starting(),"Aiming started game");
            firePosition=origin; tested=true; break;
        }
        check(tested,"Merged text letter head raycast failed");
    }
    StageWorld blocked;
    StageCollider wall;
    wall.local={{firePosition.x-2,firePosition.y-2,firePosition.z+1},
                {firePosition.x+2,firePosition.y+2,firePosition.z+1.01f}};
    blocked.colliders.push_back(wall);
    title->bullets_.Spawn(weapon,Matrix4x4::Translation(firePosition),0,title->random_,blocked,title->enemies_);
    title->bullets_.Update(.1f,blocked,title->enemies_,[&](const BulletEnemyImpact& impact) {title->OnBulletImpact(impact);});
    check(!title->start_.Starting() && !title->startTarget_->IsDead(),"Shot passed wall to GAME START");
    const auto gpuCount=Object3d::debugInitializationCount;
    title->bullets_.Spawn(weapon,Matrix4x4::Translation(firePosition),0,title->random_,empty,title->enemies_);
    check(!title->start_.Starting(),"Shot spawned start without flight");
    title->UpdateWorld(app,.1f,false);
    check(title->start_.Starting() && title->start_.Elapsed()==0 && title->NextScene().empty(),"Actual Head impact did not begin delayed start");
    const auto dead=title->startTarget_->CaptureDebug();
    check(title->startTarget_->IsDead() && !dead.faces.empty() && !dead.visible[0],"GAME START did not use Enemy face shatter");
    check(Object3d::debugInitializationCount==gpuCount,"GAME START impact allocated renderers");
    for (const auto& face : dead.faces)
        check(StageLength(face.motion.velocity)>0 && StageLength(face.motion.angularVelocity)>0,"Enemy shards did not launch/spin");
    check(!title->start_.Begin(),"Repeated start reset timer");
    title->UpdateWorld(app,.4f,false);
    check(title->NextScene().empty() && title->startTarget_->CaptureDebug().faces[0].motion.age>0,"Explosion delay/animation");
    check(!app.Scenes().IsTransitioning() && app.Scenes().Fade().Alpha()==0,"Fade started before explosion hold");
    capture(L"generated/title-tests/explosion.png");
    title->UpdateWorld(app,.05f,false);
    check(title->transitionRequested_ && app.Scenes().IsTransitioning() &&
        app.Scenes().Fade().Alpha()==0 && title->NextScene().empty(),"Title failed to start common FadeOut after .45 seconds");
    check(!app.Scenes().TransitionTo("Game") && !title->start_.Begin(),"Repeated request restarted fade/hold");
    app.Scenes().Update(app,.375f);
    check(app.Scenes().CurrentName()=="Title" && app.Scenes().Fade().Alpha()==.5f,"FadeOut midpoint changed scene");
    capture(L"generated/title-tests/fadeout-half.png");
    app.Scenes().Update(app,.375f);
    check(app.Scenes().CurrentName()=="Title" && app.Scenes().Fade().Alpha()==1,"FadeOut full black");
    app.Scenes().Update(app,1);
    check(app.Scenes().CurrentName()=="Title","Scene changed before black frame was presented");
    capture(L"generated/title-tests/title-black.png",true);
    app.Scenes().Update(app,0);
    auto* game=dynamic_cast<GameScene*>(app.Scenes().Current());
    check(game && game->stageLoaded_ && app.Scenes().CurrentName()=="Game" && app.Scenes().Fade().Alpha()==1,"Stage01 did not enter fully black");
    check(app.Render()->GetOffscreen()->GetClearColor().x==title->savedClearColor_.x &&
        app.Render()->GetOffscreen()->GetClearColor().y==title->savedClearColor_.y &&
        app.Render()->GetOffscreen()->GetClearColor().z==title->savedClearColor_.z,"Title background color leaked into GameScene");
    app.Scenes().Update(app,1);
    check(app.Scenes().Fade().Alpha()==1 && game->stage_.Time()==0,"Multiple updates skipped incoming black frame");
    capture(L"generated/title-tests/game-black.png",true);
    app.Scenes().Update(app,1); // Simulate the host delta containing the stage load.
    check(app.Scenes().Fade().Alpha()==1,"Stage loading time advanced FadeIn");
    const auto initial=game->player_.GetTransform();
    check(game->stage_.Time()==0 && game->bullets_.Count()==0,"Gameplay ran during black initialization");
    app.Scenes().Update(app,.375f);
    check(app.Scenes().Fade().Alpha()==.5f && game->stage_.Time()==0 && game->debugFrame_==0 &&
        StageLength(game->player_.GetTransform().translate-initial.translate)==0 && game->enemies_.empty(),"FadeIn did not freeze gameplay");
    capture(L"generated/title-tests/fadein-half.png");
    app.Scenes().Update(app,.375f);
    check(!app.Scenes().IsTransitioning() && app.Scenes().Fade().Alpha()==0 && game->stage_.Time()==0,"FadeIn did not finish transparently");
    capture(L"generated/title-tests/game.png");
    app.Scenes().Update(app,.016f);
    check(game->stage_.Time()>0,"Gameplay did not resume after FadeIn");
    check(!app.Scenes().TransitionTo("Missing") && !app.Scenes().TransitionTo("Title",-1,1),"Invalid transition accepted");
    check(app.Scenes().TransitionTo("Title",0,0),"Common transition cannot target another scene");
    app.Scenes().Change(app,"Title");
    check(!app.Scenes().IsTransitioning() && app.Scenes().Fade().Alpha()==0,"Immediate Change left stale fade");
    std::ofstream("generated/title-tests/result.txt") << "PASS: lighting JSON round trip, validation/save/backup/external-edit protection, all three spot GPU slots, scene background restoration, title assets and Enemy destruction, one Head model/all letters/wall occlusion, reusable FadeOut/FadeIn duration/completion/clamp, .45s explosion hold, .75s out/in, duplicate request protection, actual all-black frames after HUD/post effects, black-frame presentation before Stage01 load, frozen gameplay during FadeIn, resume and immediate-change cancellation.\n";
}

