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
    const auto spotIntensity=[&](size_t index) {
        void* mapped=nullptr;
        const D3D12_RANGE readRange{0,sizeof(Object3dLight::SpotLights)};
        auto* resource=title->sceneLight_.GetSpotLightResource();
        check(SUCCEEDED(resource->Map(0,&readRange,&mapped)),"Spot light constant buffer read failed");
        const float intensity=static_cast<const Object3dLight::SpotLights*>(mapped)->at(index).intensity;
        const D3D12_RANGE writtenRange{0,0};
        resource->Unmap(0,&writtenRange);
        return intensity;
    };
    const auto lighting = title->lighting_;
    check(TitleLighting::FromJson(lighting.ToJson()).ToJson()==lighting.ToJson(),"Lighting JSON round trip");
    auto legacyLight=lighting.ToJson();
    legacyLight["directional"].erase("ambientIntensity");
    legacyLight["shadow"].erase("roomCastsShadows");
    for (auto& spot : legacyLight["spots"]) spot.erase("flicker");
    const auto legacy=TitleLighting::FromJson(legacyLight);
    check(legacy.ambientIntensity==TitleLighting{}.ambientIntensity && !legacy.shadow.roomCastsShadows,
        "Legacy lighting settings lost minimum fill/default room shadow selection");
    for (const auto& spot : legacy.spots) check(!spot.flicker.enabled,"Legacy lighting unexpectedly enabled lamp flicker");
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
    invalidLight=lighting.ToJson();
    invalidLight["spots"]["UNALIVE"]["flicker"]["minOffTime"]=0;
    rejected=false;
    try { (void)TitleLighting::FromJson(invalidLight); } catch (const std::exception&) { rejected=true; }
    check(rejected,"Zero flicker duration accepted");
    invalidLight=lighting.ToJson();
    invalidLight["spots"]["UNALIVE"]["flicker"]["maxInterval"]=.1f;
    rejected=false;
    try { (void)TitleLighting::FromJson(invalidLight); } catch (const std::exception&) { rejected=true; }
    check(rejected,"Reversed flicker interval accepted");
    invalidLight=lighting.ToJson();
    invalidLight["spots"]["UNALIVE"]["flicker"]["flashes"]=0;
    rejected=false;
    try { (void)TitleLighting::FromJson(invalidLight); } catch (const std::exception&) { rejected=true; }
    check(rejected,"Empty flicker burst accepted");
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
    const auto savedFlickerRandom=title->flickerRandom_;
    const auto gameplayRandom=title->random_;
    auto flickerFixture=lighting;
    for (auto& spot : flickerFixture.spots) spot.flicker.enabled=false;
    flickerFixture.spots[0].flicker={true,.25f,.25f,.1f,.1f,2,0};
    flickerFixture.spots[2].flicker={true,.6f,.6f,.1f,.1f,1,0};
    title->lighting_=flickerFixture;
    for (size_t i=0;i<title->flickerRandom_.size();++i) title->flickerRandom_[i].seed(static_cast<uint32_t>(2026+i));
    title->ResetLightFlicker();
    title->UpdateLightFlicker(0);
    const auto flickerOnEnergy=capture(L"generated/title-tests/lamp-flicker-on.png");
    title->UpdateLightFlicker(.26f);
    check(title->lightFlicker_[0].off && !title->lightFlicker_[1].off && !title->lightFlicker_[2].off,
        "Lamp flicker failed to turn off independently");
    check(capture(L"generated/title-tests/lamp-flicker-unalive-off.png")+100<flickerOnEnergy,
        "UNALIVE lamp off had no rendered effect");
    check(title->lighting_.ToJson()==flickerFixture.ToJson(),"Lamp flicker changed saved base intensities");
    title->UpdateLightFlicker(.1f);
    check(!title->lightFlicker_[0].off && title->lightFlicker_[0].flashesRemaining==1,
        "Lamp did not relight between flashes");
    check(capture(L"generated/title-tests/lamp-flicker-relit.png")==flickerOnEnergy,"Relit lamp failed to restore brightness");
    title->UpdateLightFlicker(.3f);
    check(!title->lightFlicker_[0].off && title->lightFlicker_[2].off && title->lightFlicker_[0].flashesRemaining==0,
        "Flicker burst did not finish or lamps flashed in sync");
    check(capture(L"generated/title-tests/lamp-flicker-start-off.png")+100<flickerOnEnergy,
        "GAME START / instruction lamp off had no rendered effect");
    title->UpdateLightFlicker(.051f);
    check(!title->lightFlicker_[2].off,"GAME START lamp did not relight");
    title->ResetLightFlicker();
    const auto initialFlicker=title->lightFlicker_;
    const auto initialRandom=title->flickerRandom_;
    title->UpdateLightFlicker(1.2f);
    const auto steppedFlicker=title->lightFlicker_;
    const auto steppedRandom=title->flickerRandom_;
    title->lightFlicker_=initialFlicker; title->flickerRandom_=initialRandom;
    for (int i=0;i<120;++i) title->UpdateLightFlicker(.01f);
    for (size_t i=0;i<steppedFlicker.size();++i) {
        check(title->lightFlicker_[i].off==steppedFlicker[i].off &&
            title->lightFlicker_[i].flashesRemaining==steppedFlicker[i].flashesRemaining &&
            std::abs(title->lightFlicker_[i].remaining-steppedFlicker[i].remaining)<.0001f,
            "Lamp sequence changed with frame rate");
    }
    check(title->flickerRandom_==steppedRandom && title->random_==gameplayRandom,"Lamp flicker changed gameplay randomness or frame-rate sequence");
    title->lightFlicker_[0].off=true;
    title->lighting_.spots[0].flicker.enabled=false;
    title->UpdateLightFlicker(0);
    check(!title->lightFlicker_[0].off,"Disabling lamp flicker left it dark");
    check(capture(L"generated/title-tests/lamp-flicker-disabled.png")==flickerOnEnergy,"Disabled flicker failed to restore base light");
    title->lighting_=lighting; title->flickerRandom_=savedFlickerRandom;
    title->ResetLightFlicker(); title->ApplyLighting();
    auto steadyLighting=lighting;
    for (auto& spot : steadyLighting.spots) spot.flicker.enabled=false;
    title->lighting_=steadyLighting; title->ResetLightFlicker(); title->ApplyLighting();
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
    title->ApplyLighting();
    check(enemyHits==1 && !title->start_.Starting(),"Enemy shot started game or missed");
    check(!enemy->IsDead() && spotIntensity(1)==steadyLighting.spots[1].intensity,
        "Nonfatal enemy damage extinguished its spotlight");
    enemy->Die();
    check(enemy->IsDead() && !enemy->CanReturnToPool(),"Title enemy death/debris");
    title->UpdateWorld(app,0,false);
    check(spotIntensity(1)==0 && spotIntensity(0)==steadyLighting.spots[0].intensity &&
        spotIntensity(2)==steadyLighting.spots[2].intensity,"Enemy death failed to extinguish only its spotlight");
    const auto allocations = Object3d::debugInitializationCount;
    for (int i=0; i<80; ++i) title->UpdateWorld(app,.1f,false);
    check(title->enemies_.size()==2 && title->enemies_[0]==enemy && !enemy->IsDead(),"Title pool respawn");
    check(Object3d::debugInitializationCount==allocations,"Title respawn allocated renderers");
    check(spotIntensity(1)==steadyLighting.spots[1].intensity,"Enemy respawn failed to restore its spotlight");
    check(title->player_.GetHP()==hp,"Title death explosion damaged player");

    check(title->unaliveExplosion_.letters.size()==7 && title->unaliveExplosion_.fragments.size()==40,
        "UNALIVE glyph/fragment renderers were not prepared");
    Vector3 unaliveFirePosition{};
    bool foundUnaliveFace=false;
    for (const auto& face : title->unaliveExplosion_.source.geometry->faces) {
        const auto a=face[1]-face[0], b=face[2]-face[0];
        if (std::abs(a.x*b.y-a.y*b.x)<.0001f) continue;
        const auto origin=(face[0]+face[1]+face[2])*(1.0f/3)-Vector3{0,0,3};
        if (title->TraceUnalive(origin,{0,0,1},5)) {
            unaliveFirePosition=origin; foundUnaliveFace=true; break;
        }
    }
    check(foundUnaliveFace && !title->unaliveExplosion_.destroyed,"UNALIVE triangle raycast failed or aiming destroyed it");
    // A ray through a gap between letters must continue to the background.
    const auto& firstBounds=title->unaliveExplosion_.letters[0].motion.bounds;
    const auto& nextBounds=title->unaliveExplosion_.letters[1].motion.bounds;
    const Vector3 gap{(firstBounds.max.x+nextBounds.min.x)*.5f,
        (firstBounds.min.y+firstBounds.max.y)*.5f,firstBounds.min.z-3};
    check(!title->TraceUnalive(gap,{0,0,1},5),"UNALIVE hitbox filled the gap between glyphs");
    const BulletTrace unaliveTrace=[&](const Vector3& origin,const Vector3& direction,float distance) {
        return title->TraceUnalive(origin,direction,distance);
    };
    StageWorld unaliveBlocked;
    StageCollider unaliveWall;
    unaliveWall.local={unaliveFirePosition+Vector3{-2,-2,1},unaliveFirePosition+Vector3{2,2,1.01f}};
    unaliveBlocked.colliders.push_back(unaliveWall);
    title->bullets_.Spawn(weapon,Matrix4x4::Translation(unaliveFirePosition),0,title->random_,unaliveBlocked,title->enemies_,unaliveTrace);
    title->bullets_.Update(.1f,unaliveBlocked,title->enemies_,{},unaliveTrace,
        [&](const Bullet&,const BulletHit&) { title->BeginUnaliveExplosion(); });
    check(!title->unaliveExplosion_.destroyed,"UNALIVE exploded through an occluding wall");
    const auto unaliveAllocations=Object3d::debugInitializationCount;
    title->bullets_.Spawn(weapon,Matrix4x4::Translation(unaliveFirePosition),0,title->random_,empty,title->enemies_,unaliveTrace);
    check(!title->unaliveExplosion_.destroyed,"UNALIVE exploded before bullet flight");
    title->UpdateWorld(app,.1f,false);
    check(title->unaliveExplosion_.destroyed && title->unaliveExplosion_.active && title->unaliveExplosion_.elapsed==0 &&
        title->unaliveExplosion_.fragmentCount==30 && !title->start_.Starting() && !title->transitionRequested_ &&
        title->environment_.GetModel()==title->environmentTextVariants_[1] && title->enemies_.size()==2,
        "UNALIVE impact did not explode independently from GAME START");
    check(!title->TraceUnalive(unaliveFirePosition,{0,0,1},5),"Destroyed UNALIVE still blocks shots");
    check(spotIntensity(0)==0 && spotIntensity(1)==steadyLighting.spots[1].intensity &&
        spotIntensity(2)==steadyLighting.spots[2].intensity,"UNALIVE impact failed to extinguish only its spotlight immediately");
    title->lighting_.spots[0].flicker.enabled=true;
    title->lighting_.spots[0].flicker.offBrightness=1;
    title->ResetLightFlicker(); title->UpdateLightFlicker(1);
    check(spotIntensity(0)==0,"Lamp flicker relit destroyed UNALIVE");
    title->lighting_=steadyLighting; title->ResetLightFlicker(); title->ApplyLighting();
    for (const auto& letter : title->unaliveExplosion_.letters) {
        check(StageLength(letter.motion.velocity)>0 && StageLength(letter.motion.angularVelocity)>0 && letter.motion.age==0 &&
            letter.object->GetEnableLighting()==0,"UNALIVE glyph launch/spin/impact flash failed");
    }
    title->BeginUnaliveExplosion();
    check(title->unaliveExplosion_.elapsed==0 && Object3d::debugInitializationCount==unaliveAllocations,
        "UNALIVE impact allocated renderers");
    capture(L"generated/title-tests/unalive-flash.png");
    title->UpdateWorld(app,.3f,false);
    capture(L"generated/title-tests/unalive-explosion.png");
    title->BeginUnaliveExplosion();
    check(title->unaliveExplosion_.elapsed>=.3f,"Repeated UNALIVE hit restarted burst");
    title->UpdateWorld(app,2,false);
    check(title->unaliveExplosion_.destroyed && !title->unaliveExplosion_.active && !title->start_.Starting() && !app.Scenes().IsTransitioning() &&
        title->environment_.GetModel()==title->environmentTextVariants_[1] && title->startTarget_->CaptureDebug().parts[0].hp==1,
        "UNALIVE restored itself, began fading, or damaged GAME START");
    check(spotIntensity(0)==0 && title->lighting_.ToJson()==steadyLighting.ToJson(),
        "Destroyed UNALIVE relit after fragment lifetime or modified saved lighting");
    const auto destroyedEnergy=capture(L"generated/title-tests/unalive-destroyed.png");
    steadyLighting.Apply(title->sceneLight_);
    check(capture(L"generated/title-tests/unalive-destroyed-lamp-forced-on.png")>destroyedEnergy+100,
        "Destruction spotlight shutoff had no rendered effect");
    title->ApplyLighting();
    title->BeginUnaliveExplosion(true);
    title->UpdateWorld(app,2,false);
    check(!title->unaliveExplosion_.destroyed && !title->unaliveExplosion_.preview && title->environment_.GetModel()==title->environmentFull_ &&
        title->TraceUnalive(unaliveFirePosition,{0,0,1},5).has_value(),"UNALIVE preview failed to restore mesh/raycast");
    check(spotIntensity(0)==steadyLighting.spots[0].intensity,"UNALIVE preview restoration failed to relight its lamp");
    // Destroy each background word independently, including shots in front of the plinth.
    title->BeginUnaliveExplosion();
    title->UpdateWorld(app,2,false);

    auto& instruction=title->instructionExplosion_;
    check(instruction.letters.size()==12 && instruction.fragments.size()==40,
        "SHOOT TO START glyph/fragment renderers were not prepared");
    size_t instructionVertices=0;
    for (const auto& letter : instruction.letters) instructionVertices+=letter.object->GetModel()->GetSourceVertexCount();
    check(instructionVertices==instruction.source.geometry->triangles.size()*3,
        "SHOOT TO START extraction lost or duplicated original geometry");
    const BulletTrace textTrace=[&](const Vector3& origin,const Vector3& direction,float distance) {
        return title->TraceTitleText(origin,direction,distance);
    };
    Vector3 instructionFirePosition{};
    bool foundInstructionFace=false;
    for (const auto& face : instruction.source.geometry->faces) {
        const auto a=face[1]-face[0], b=face[2]-face[0];
        if (std::abs(a.x*b.y-a.y*b.x)<.0001f) continue;
        const auto origin=(face[0]+face[1]+face[2])*(1.0f/3)-Vector3{0,0,3};
        const auto hit=TraceBulletPath(origin,{0,0,1},5,title->level_.collision,0,{},textTrace);
        if (hit && hit->targetIndex==1) {
            instructionFirePosition=origin; foundInstructionFace=true; break;
        }
    }
    check(foundInstructionFace && !instruction.destroyed,"SHOOT TO START raycast was blocked by its plinth or aiming destroyed it");
    const auto& instructionFirst=instruction.letters[0].motion.bounds;
    const auto& instructionNext=instruction.letters[1].motion.bounds;
    const Vector3 instructionGap{(instructionFirst.max.x+instructionNext.min.x)*.5f,
        (instructionFirst.min.y+instructionFirst.max.y)*.5f,instructionFirst.min.z-3};
    check(!title->TraceInstruction(instructionGap,{0,0,1},5),"SHOOT TO START hitbox filled the gap between glyphs");
    StageWorld instructionBlocked;
    StageCollider instructionWall;
    instructionWall.local={instructionFirePosition+Vector3{-2,-2,1},instructionFirePosition+Vector3{2,2,1.01f}};
    instructionBlocked.colliders.push_back(instructionWall);
    title->bullets_.Spawn(weapon,Matrix4x4::Translation(instructionFirePosition),0,title->random_,instructionBlocked,title->enemies_,textTrace);
    title->bullets_.Update(.1f,instructionBlocked,title->enemies_,{},textTrace,
        [&](const Bullet&,const BulletHit&) { title->BeginInstructionExplosion(); });
    check(!instruction.destroyed,"SHOOT TO START exploded through an occluding wall");
    const auto instructionAllocations=Object3d::debugInitializationCount;
    title->bullets_.Spawn(weapon,Matrix4x4::Translation(instructionFirePosition),0,title->random_,title->level_.collision,title->enemies_,textTrace);
    check(!instruction.destroyed,"SHOOT TO START exploded before bullet flight");
    title->UpdateWorld(app,.1f,false);
    check(instruction.destroyed && instruction.active && instruction.elapsed==0 && instruction.fragmentCount==30 &&
        title->environment_.GetModel()==title->environmentTextVariants_[3] && !title->start_.Starting() && !title->transitionRequested_,
        "SHOOT TO START impact failed, restored UNALIVE, or started the game");
    check(!title->TraceInstruction(instructionFirePosition,{0,0,1},5),"Destroyed SHOOT TO START still blocks shots");
    check(spotIntensity(0)==0 && spotIntensity(2)==0 && spotIntensity(1)==steadyLighting.spots[1].intensity,
        "SHOOT TO START impact failed to turn off its shared GAME START lamp");
    for (const auto& letter : instruction.letters) {
        check(StageLength(letter.motion.velocity)>0 && StageLength(letter.motion.angularVelocity)>0 && letter.motion.age==0 &&
            letter.object->GetEnableLighting()==0,"SHOOT TO START launch/spin/impact flash failed");
    }
    check(Object3d::debugInitializationCount==instructionAllocations,"SHOOT TO START impact allocated renderers");
    capture(L"generated/title-tests/instruction-flash.png");
    title->UpdateWorld(app,.3f,false);
    capture(L"generated/title-tests/instruction-explosion.png");
    title->BeginInstructionExplosion();
    check(instruction.elapsed>=.3f,"Repeated SHOOT TO START hit restarted burst");
    title->UpdateWorld(app,2,false);
    check(instruction.destroyed && !instruction.active && !title->start_.Starting() && !app.Scenes().IsTransitioning() &&
        title->environment_.GetModel()==title->environmentTextVariants_[3] && title->startTarget_->CaptureDebug().parts[0].hp==1,
        "SHOOT TO START restored itself, began fading, or damaged GAME START");
    capture(L"generated/title-tests/instruction-destroyed.png");
    title->RestoreUnalive();
    check(spotIntensity(0)==steadyLighting.spots[0].intensity && spotIntensity(2)==0,
        "Restoring UNALIVE relit the destroyed instruction's lamp");
    check(instruction.destroyed && title->environment_.GetModel()==title->environmentTextVariants_[2],
        "Restoring UNALIVE also restored SHOOT TO START");
    title->BeginUnaliveExplosion(true);
    check(title->environment_.GetModel()==title->environmentTextVariants_[3],"UNALIVE preview restored SHOOT TO START");
    title->UpdateWorld(app,2,false);
    check(instruction.destroyed && title->environment_.GetModel()==title->environmentTextVariants_[2],
        "UNALIVE preview completion restored SHOOT TO START");
    title->BeginUnaliveExplosion();
    title->BeginInstructionExplosion(true);
    title->UpdateWorld(app,2,false);
    check(!instruction.destroyed && !instruction.preview && title->unaliveExplosion_.destroyed &&
        title->environment_.GetModel()==title->environmentTextVariants_[1] &&
        title->TraceInstruction(instructionFirePosition,{0,0,1},5).has_value(),
        "SHOOT TO START preview failed to restore its own mesh/raycast independently");
    title->BeginInstructionExplosion();
    title->RestoreInstruction();
    check(!instruction.destroyed && title->unaliveExplosion_.destroyed && title->environment_.GetModel()==title->environmentTextVariants_[1],
        "SHOOT TO START restore changed UNALIVE destruction");
    check(spotIntensity(0)==0 && spotIntensity(2)==steadyLighting.spots[2].intensity,
        "Instruction restoration failed to relight only its own spotlight");
    // Restore the shared-lamp target while testing GAME START preview and impact.
    // UNALIVE stays destroyed during the GAME START / fade regression checks.
    title->UpdateWorld(app,2,false);

    check(title->startExplosion_.letters.size()==9 && title->startExplosion_.fragments.size()==40,"Whole-letter/fragment renderers were not prepared");
    size_t letterVertices=0;
    for (const auto& letter : title->startExplosion_.letters) letterVertices+=letter.object->GetModel()->GetSourceVertexCount();
    check(letterVertices==title->startDefinition_.partAsset->defaults[0].geometry->triangles.size()*3,
        "Whole-letter extraction lost or duplicated GAME START geometry");
    const auto previewAllocations=Object3d::debugInitializationCount;
    title->BeginStartExplosion(true);
    title->UpdateWorld(app,0,false);
    check(title->explosionPreview_ && !title->start_.Starting() && !title->startTarget_->IsDead() &&
        !app.Scenes().IsTransitioning() && title->startExplosion_.fragmentCount==30,"Preview damaged target or started transition");
    check(spotIntensity(2)==0,"GAME START preview left its spotlight on");
    capture(L"generated/title-tests/explosion-preview-flash.png");
    title->UpdateWorld(app,2,false);
    check(!title->explosionPreview_ && !title->startTarget_->IsDead() && !app.Scenes().IsTransitioning() &&
        Object3d::debugInitializationCount==previewAllocations,"Preview failed to restore title or allocated renderers");
    check(spotIntensity(2)==steadyLighting.spots[2].intensity,"GAME START preview failed to restore its spotlight");
    const auto settings=title->explosionSettings_;
    title->explosionSettings_.fragmentCount=40;
    title->explosionSettings_.flashDuration=0;
    title->explosionSettings_.shakeDuration=0;
    title->BeginStartExplosion(true);
    title->UpdateWorld(app,0,false);
    check(title->startExplosion_.fragmentCount==40 && title->startExplosion_.letters[0].object->GetEnableLighting()==2,
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
    check(title->startTarget_->IsDead() && dead.faces.empty() && !dead.visible[0] && title->startExplosion_.fragmentCount==30,
        "GAME START failed to replace hidden generic shards with title burst");
    check(Object3d::debugInitializationCount==gpuCount,"GAME START impact allocated renderers");
    check(spotIntensity(2)==0 && spotIntensity(1)==steadyLighting.spots[1].intensity,
        "GAME START impact failed to extinguish its spotlight on the impact frame");
    check(title->lighting_.ToJson()==steadyLighting.ToJson(),"Destruction modified saved lighting settings");
    const auto blastCenter=(startState.parts[0].bounds.min+startState.parts[0].bounds.max)*.5f;
    for (const auto& letter : title->startExplosion_.letters) {
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
    for (size_t i=0;i<title->startExplosion_.fragmentCount;++i) {
        const auto& fragment=title->startExplosion_.fragments[i];
        check(StageLength(fragment.motion.velocity)>0 && StageLength(fragment.motion.angularVelocity)>0 &&
            fragment.motion.age==0,"Small fragments failed to launch/spin");
        if (title->startExplosion_.castsShadow[i]) ++shadowFragments;
        if (i<3) check(fragment.motion.velocity.z<0 && !title->startExplosion_.castsShadow[i],"Lens pass missing or casts a tiny shadow");
    }
    const auto burstShadowDraws=title->shadowMap_.DrawCount();
    const auto shadowFlags=title->startExplosion_.castsShadow;
    for (size_t i=0;i<title->startExplosion_.fragmentCount;++i) title->startExplosion_.castsShadow[i]=false;
    capture(L"generated/title-tests/explosion-letter-shadows.png");
    check(shadowFragments>0 && shadowFragments<title->startExplosion_.fragmentCount &&
        burstShadowDraws==title->shadowMap_.DrawCount()+shadowFragments,"Fragment shadow size filtering failed");
    title->startExplosion_.castsShadow=shadowFlags;
    check(!title->start_.Begin(),"Repeated start reset timer");
    title->UpdateWorld(app,.08f,false);
    check(title->startExplosion_.letters[0].object->GetEnableLighting()==2 &&
        title->startExplosion_.letters[0].object->GetMaterialColor().x==1,"Flash did not restore lit letter material");
    title->UpdateWorld(app,.10f,false);
    check(StageLength(title->camera_.GetTranslate()-baseCamera)<1e-5f,"Camera shake drifted after duration");
    title->UpdateWorld(app,.2f,false);
    for (size_t i=0;i<3;++i)
        check(StageLength(title->startExplosion_.fragments[i].motion.position-baseCamera)<2.5f,"Perspective fragments did not pass near camera");
    capture(L"generated/title-tests/explosion-near-pass.png");
    title->UpdateWorld(app,.02f,false);
    check(title->NextScene().empty() && title->startExplosion_.letters[0].motion.age>0,"Explosion delay/animation");
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
    auto* freshTitle=dynamic_cast<TitleScene*>(app.Scenes().Current());
    check(freshTitle && !freshTitle->unaliveExplosion_.destroyed && !freshTitle->instructionExplosion_.destroyed &&
        freshTitle->environment_.GetModel()==freshTitle->environmentFull_,
        "Background text destruction leaked into the next title scene");
    title=freshTitle;
    for (size_t i=0;i<title->lighting_.spots.size();++i)
        check(spotIntensity(i)==title->lighting_.spots[i].intensity,"Fresh title failed to restore spotlight brightness");
    // Keep the regression where both decorative words are destroyed before starting.
    title->BeginUnaliveExplosion(); title->BeginInstructionExplosion();
    title->UpdateWorld(app,2,false);
    title->bullets_.Spawn(weapon,Matrix4x4::Translation(firePosition),0,title->random_,empty,title->enemies_);
    title->UpdateWorld(app,.1f,false);
    check(title->start_.Starting() && spotIntensity(0)==0 && spotIntensity(2)==0,
        "GAME START failed after both decorative words were destroyed");
    title->UpdateWorld(app,.45f,false);
    check(app.Scenes().IsTransitioning(),"Both decorative words' destruction blocked the GAME START fade");
    std::ofstream("generated/title-tests/result.txt") << "PASS: destruction spotlight impact-frame shutoff / GPU render difference, flicker cannot relight destroyed text, shared instruction/start lamp, saved tuning preservation, enemy respawn and text preview/restore relight; independent lamp off/relight GPU renders, finite bursts / frame-rate consistency, base intensity preservation, disable restores light, separate gameplay randomness, flicker JSON validation / legacy steady lighting; SHOOT TO START twelve original glyphs / 30 fragments, actual stage/plinth bullet impact, gap and wall occlusion, launch/spin/flash, no game start or allocations, independent destruction and preview/restore of both decorative words, GAME START transitions after both are destroyed; UNALIVE seven glyphs / 30 fragments, actual bullet impact / gap and wall occlusion, no game start or fade, duplicate impact guard, destruction persists after lifetime, preview restores mesh/raycast, fresh title restores UNALIVE, GAME START still transitions after UNALIVE destruction; nine whole glyphs with original geometry and impact positions preserved, 30 title fragments / 40 preallocated slots, preview without damage/transition and automatic restore, maximum count / zero flash and shake durations, radial letter launch / XYZ spin / deferred impact-frame time, short flash and lit-material restoration, camera shake and no residual offset, small fragments near camera, shadow size filtering, no impact/preview renderer allocations, foreground floor ambient visibility under full shadows, room shadow exclusion with object shadows retained, legacy lighting defaults, directional shadow depth readback/range/geometry, shadow ON/OFF render comparison, shadow frustum validation, lighting JSON round trip, validation/save/backup/external-edit protection, all three spot GPU slots, scene background restoration, title assets and Enemy destruction, one Head model/all letters/wall occlusion, reusable FadeOut/FadeIn duration/completion/clamp, .45s explosion hold, .75s out/in, duplicate request protection, actual all-black frames after HUD/post effects, black-frame presentation before Stage01 load, frozen gameplay during FadeIn, resume and immediate-change cancellation.\n";
}

