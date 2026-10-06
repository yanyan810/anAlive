#pragma once
#include "GameApp.h"
#include "scene/Flow/TitleScene.h"
#include "scene/Main/GameScene.h"
#include "ImGuiManagaer.h"
#include "DirectXTex.h"
#include "DebugJsonEditor.h"
#include <fstream>
#include <cstring>
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
    auto legacyLight=lighting.ToJson();
    legacyLight["directional"].erase("ambientIntensity");
    legacyLight["shadow"].erase("roomCastsShadows");
    const auto legacy=TitleLighting::FromJson(legacyLight);
    check(legacy.ambientIntensity==TitleLighting{}.ambientIntensity && !legacy.shadow.roomCastsShadows,
        "Legacy lighting settings lost minimum fill/default room shadow selection");
    auto invalidLight=lighting.ToJson();
    invalidLight["directional"]["ambientIntensity"]=-.01f;
    bool rejected=false;
    try { (void)TitleLighting::FromJson(invalidLight); } catch (const std::exception&) { rejected=true; }
    check(rejected,"Negative ambient fill accepted");
    invalidLight=lighting.ToJson();
    invalidLight["spots"]["Enemy"]["direction"]={0,0,0};
    rejected=false;
    try { (void)TitleLighting::FromJson(invalidLight); } catch (const std::exception&) { rejected=true; }
    check(rejected,"Zero spot direction accepted");
    invalidLight=lighting.ToJson();
    invalidLight["spots"]["GAME START"]["innerAngleDegrees"]=90;
    rejected=false;
    try { (void)TitleLighting::FromJson(invalidLight); } catch (const std::exception&) { rejected=true; }
    check(rejected,"Invalid spot cone accepted");
    invalidLight=lighting.ToJson();
    invalidLight["shadow"]["farClip"]=invalidLight["shadow"]["nearClip"];
    rejected=false;
    try { (void)TitleLighting::FromJson(invalidLight); } catch (const std::exception&) { rejected=true; }
    check(rejected,"Invalid shadow depth range accepted");
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
    uint64_t floorEnergy=0, floorSamples=0;
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
        floorEnergy=0; floorSamples=0;
        const auto* pixels=image.GetImage(0,0,0);
        for (size_t y=0;y<pixels->height;++y) for (size_t x=0;x<pixels->width;++x) {
            const auto* p=pixels->pixels+y*pixels->rowPitch+x*4;
            energy+=static_cast<uint64_t>(p[0])+p[1]+p[2];
            // Foreground floor, outside every spotlight and the crosshair.
            if (x>=pixels->width*2/5 && x<pixels->width*3/5 &&
                y>=pixels->height*4/5 && y<pixels->height*9/10) {
                floorEnergy+=static_cast<uint64_t>(p[0])+p[1]+p[2]; ++floorSamples;
            }
        }
        return energy;
    };
    check(!title->roomShadowMeshes_.empty(),"Room shadow meshes were not identified from exported nodes");
    auto roomCheck=lighting;
    roomCheck.direction={.25f,-.23f,.5f}; // Reproduce the shallow light angle from the reported screenshot.
    roomCheck.intensity=.22f;
    roomCheck.ambientIntensity=.08f;
    for (auto& spot : roomCheck.spots) spot.intensity=0;
    roomCheck.shadow.enabled=true;
    roomCheck.shadow.strength=1;
    roomCheck.shadow.roomCastsShadows=true;
    title->lighting_=roomCheck; roomCheck.Apply(title->sceneLight_);
    const auto roomShadowEnergy=capture(L"generated/title-tests/room-shadows-on.png");
    const auto fullShadowDraws=title->shadowMap_.DrawCount();
    title->lighting_.shadow.roomCastsShadows=false;
    const auto filteredEnergy=capture(L"generated/title-tests/room-shadows-off.png");
    check(fullShadowDraws==title->shadowMap_.DrawCount()+title->roomShadowMeshes_.size() &&
        title->shadowMap_.DrawCount()>0 && filteredEnergy>roomShadowEnergy+100,
        "Room shadow exclusion failed or removed all object shadows");
    roomCheck.intensity=0;
    roomCheck.ambientIntensity=0;
    title->lighting_=roomCheck; roomCheck.Apply(title->sceneLight_);
    capture(L"generated/title-tests/ambient-fill-off.png");
    check(floorEnergy==0,"Ambient regression fixture still lights the foreground floor");
    roomCheck.ambientIntensity=.08f;
    title->lighting_=roomCheck; roomCheck.Apply(title->sceneLight_);
    const auto ambientShadowed=capture(L"generated/title-tests/ambient-fill-on.png");
    check(floorSamples>0 && floorEnergy>=floorSamples*18,"Ambient fill failed to preserve floor visibility");
    title->lighting_.shadow.enabled=false;
    check(capture(L"generated/title-tests/ambient-fill-without-shadow.png")==ambientShadowed,
        "Directional shadows darkened the ambient fill");
    title->lighting_=lighting; lighting.Apply(title->sceneLight_);
    auto shadowCheck=lighting;
    shadowCheck.intensity=1;
    for (auto& spot : shadowCheck.spots) spot.intensity=0;
    shadowCheck.Apply(title->sceneLight_);
    title->lighting_.shadow=TitleLighting::Shadow{};
    title->lighting_.shadow.enabled=false;
    const auto unshadowed=capture(L"generated/title-tests/directional-without-shadow.png");
    title->lighting_.shadow.enabled=true;
    const auto shadowed=capture(L"generated/title-tests/directional-with-shadow.png");
    check(shadowed+100<unshadowed && title->shadowMap_.DrawCount()>0,"Directional shadow had no rendered effect");
    DirectX::ScratchImage depth;
    const auto depthState=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    check(SUCCEEDED(DirectX::CaptureTexture(app.Dx()->GetCommandQueue(),title->shadowMap_.Resource(),false,
        depth,depthState,depthState)),"Shadow depth readback failed");
    const auto* pixels=depth.GetImage(0,0,0);
    check(pixels->width==DirectionalShadowMap::kResolution && pixels->height==DirectionalShadowMap::kResolution,
        "Shadow texture size");
    size_t writtenDepth=0;
    DirectX::ScratchImage preview;
    check(SUCCEEDED(preview.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM,pixels->width,pixels->height,1,1)),"Depth preview allocation");
    const auto* gray=preview.GetImage(0,0,0);
    for (size_t y=0;y<pixels->height;++y) for (size_t x=0;x<pixels->width;++x) {
        float value=0;
        std::memcpy(&value,pixels->pixels+y*pixels->rowPitch+x*sizeof(float),sizeof(float));
        check(std::isfinite(value) && value>=0 && value<=1,"Invalid shadow depth");
        if (value<1) ++writtenDepth;
        auto* p=gray->pixels+y*gray->rowPitch+x*4;
        p[0]=p[1]=p[2]=static_cast<uint8_t>(value*255); p[3]=255;
    }
    check(writtenDepth>1000,"Shadow depth pass drew no geometry");
    check(SUCCEEDED(DirectX::SaveToWICFile(*gray,DirectX::WIC_FLAGS_NONE,
        DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),L"generated/title-tests/shadow-depth.png")),"Depth PNG failed");
    title->lighting_.shadow=lighting.shadow;
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

    check(title->startLetters_.size()==9 && title->startFragments_.size()==40,"Whole-letter/fragment renderers were not prepared");
    size_t letterVertices=0;
    for (const auto& letter : title->startLetters_) letterVertices+=letter.object->GetModel()->GetSourceVertexCount();
    check(letterVertices==title->startDefinition_.partAsset->defaults[0].geometry->triangles.size()*3,
        "Whole-letter extraction lost or duplicated GAME START geometry");
    const auto previewAllocations=Object3d::debugInitializationCount;
    title->BeginStartExplosion(true);
    title->UpdateWorld(app,0,false);
    check(title->explosionPreview_ && !title->start_.Starting() && !title->startTarget_->IsDead() &&
        !app.Scenes().IsTransitioning() && title->activeStartFragments_==30,"Preview damaged target or started transition");
    capture(L"generated/title-tests/explosion-preview-flash.png");
    title->UpdateWorld(app,2,false);
    check(!title->explosionPreview_ && !title->startTarget_->IsDead() && !app.Scenes().IsTransitioning() &&
        Object3d::debugInitializationCount==previewAllocations,"Preview failed to restore title or allocated renderers");
    const auto settings=title->explosionSettings_;
    title->explosionSettings_.fragmentCount=40;
    title->explosionSettings_.flashDuration=0;
    title->explosionSettings_.shakeDuration=0;
    title->BeginStartExplosion(true);
    title->UpdateWorld(app,0,false);
    check(title->activeStartFragments_==40 && title->startLetters_[0].object->GetEnableLighting()==2,
        "Maximum fragment count or disabled flash failed");
    title->UpdateWorld(app,2,false);
    title->explosionSettings_=settings;

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
    check(title->startTarget_->IsDead() && dead.faces.empty() && !dead.visible[0] && title->activeStartFragments_==30,
        "GAME START failed to replace hidden generic shards with title burst");
    check(Object3d::debugInitializationCount==gpuCount,"GAME START impact allocated renderers");
    const auto blastCenter=(startState.parts[0].bounds.min+startState.parts[0].bounds.max)*.5f;
    for (const auto& letter : title->startLetters_) {
        const auto outward=letter.motion.position-blastCenter;
        check(outward.x*letter.motion.velocity.x>0 && StageLength(letter.motion.angularVelocity)>0 && letter.motion.age==0,
            "Letters failed to launch radially/spin, or consumed preceding frame time");
        check(letter.object->GetEnableLighting()==0 && letter.object->GetMaterialColor().x>1,"Impact flash missing");
        check(StageLength(letter.motion.Translation())<1e-5f,"Impact moved whole letters before the first simulation frame");
    }
    const auto baseCamera=title->player_.GetTransform().translate+Vector3{0,title->player_.Settings().cameraHeight,0};
    check(StageLength(title->camera_.GetTranslate()-baseCamera)>0 &&
        StageLength(title->camera_.GetTranslate()-baseCamera)<.06f,"Short camera shake missing or too strong");
    capture(L"generated/title-tests/explosion-flash.png");
    size_t shadowFragments=0;
    for (size_t i=0;i<title->activeStartFragments_;++i) {
        const auto& fragment=title->startFragments_[i];
        check(StageLength(fragment.motion.velocity)>0 && StageLength(fragment.motion.angularVelocity)>0 &&
            fragment.motion.age==0,"Small fragments failed to launch/spin");
        if (title->fragmentCastsShadow_[i]) ++shadowFragments;
        if (i<3) check(fragment.motion.velocity.z<0 && !title->fragmentCastsShadow_[i],"Lens pass missing or casts a tiny shadow");
    }
    const auto burstShadowDraws=title->shadowMap_.DrawCount();
    const auto shadowFlags=title->fragmentCastsShadow_;
    for (size_t i=0;i<title->activeStartFragments_;++i) title->fragmentCastsShadow_[i]=false;
    capture(L"generated/title-tests/explosion-letter-shadows.png");
    check(shadowFragments>0 && shadowFragments<title->activeStartFragments_ &&
        burstShadowDraws==title->shadowMap_.DrawCount()+shadowFragments,"Fragment shadow size filtering failed");
    title->fragmentCastsShadow_=shadowFlags;
    check(!title->start_.Begin(),"Repeated start reset timer");
    title->UpdateWorld(app,.08f,false);
    check(title->startLetters_[0].object->GetEnableLighting()==2 &&
        title->startLetters_[0].object->GetMaterialColor().x==1,"Flash did not restore lit letter material");
    title->UpdateWorld(app,.10f,false);
    check(StageLength(title->camera_.GetTranslate()-baseCamera)<1e-5f,"Camera shake drifted after duration");
    title->UpdateWorld(app,.2f,false);
    for (size_t i=0;i<3;++i)
        check(StageLength(title->startFragments_[i].motion.position-baseCamera)<2.5f,"Perspective fragments did not pass near camera");
    capture(L"generated/title-tests/explosion-near-pass.png");
    title->UpdateWorld(app,.02f,false);
    check(title->NextScene().empty() && title->startLetters_[0].motion.age>0,"Explosion delay/animation");
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
    std::ofstream("generated/title-tests/result.txt") << "PASS: nine whole glyphs with original geometry and impact positions preserved, 30 title fragments / 40 preallocated slots, preview without damage/transition and automatic restore, maximum count / zero flash and shake durations, radial letter launch / XYZ spin / deferred impact-frame time, short flash and lit-material restoration, camera shake and no residual offset, small fragments near camera, shadow size filtering, no impact/preview renderer allocations, foreground floor ambient visibility under full shadows, room shadow exclusion with object shadows retained, legacy lighting defaults, directional shadow depth readback/range/geometry, shadow ON/OFF render comparison, shadow frustum validation, lighting JSON round trip, validation/save/backup/external-edit protection, all three spot GPU slots, scene background restoration, title assets and Enemy destruction, one Head model/all letters/wall occlusion, reusable FadeOut/FadeIn duration/completion/clamp, .45s explosion hold, .75s out/in, duplicate request protection, actual all-black frames after HUD/post effects, black-frame presentation before Stage01 load, frozen gameplay during FadeIn, resume and immediate-change cancellation.\n";
}

