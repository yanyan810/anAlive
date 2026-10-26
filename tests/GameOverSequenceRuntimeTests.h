#pragma once
#include "GameApp.h"
#include "scene/Main/GameScene.h"
#include "FirstPersonCamera.h"
#include "DirectXTex.h"
#include "GameOverPostEffectRuntimeTests.h"
#include <fstream>

inline void RunGameOverSequenceRuntimeTests(GameApp& app) {
    const auto check=[](bool ok,const char* message) { if (!ok) throw std::runtime_error(message); };
    const auto viewHeading=[](const Vector3& r) {
        const auto m=Matrix4x4::RotateXYZ(r.x,r.y,r.z); return std::atan2(m.m[2][0],m.m[2][2]);
    };
    const auto rotationDistance=[](const Vector3& a,const Vector3& b) {
        const auto x=Matrix4x4::RotateXYZ(a.x,a.y,a.z),y=Matrix4x4::RotateXYZ(b.x,b.y,b.z);
        float distance=0;
        for (int i=0;i<3;++i) for (int j=0;j<3;++j) distance=std::max(distance,std::abs(x.m[i][j]-y.m[i][j]));
        return distance;
    };
    const auto postDistance=[](const DeathPostEffectParameters& a,const DeathPostEffectParameters& b) {
        return std::max({std::abs(a.vignette-b.vignette),std::abs(a.desaturation-b.desaturation),
            std::abs(a.blurPixels-b.blurPixels),std::abs(a.chromaticPixels-b.chromaticPixels),
            std::abs(a.mosaicStrength-b.mosaicStrength),
            std::abs(a.mosaicCellSize-b.mosaicCellSize),std::abs(a.mosaicColorStrength-b.mosaicColorStrength),
            a.mosaicStrength>0 && b.mosaicStrength>0 ? std::abs(a.mosaicTime-b.mosaicTime) : 0.0f,
            std::abs(a.vignetteColor.x-b.vignetteColor.x),std::abs(a.vignetteColor.y-b.vignetteColor.y),std::abs(a.vignetteColor.z-b.vignetteColor.z)});
    };
    const std::wstring args=GetCommandLineW();
    const std::string stage=args.find(L"--gameover-stage01")!=std::wstring::npos ? "stage01" :
        args.find(L"--gameover-stage02")!=std::wstring::npos ? "stage02" : "stage03";
    const auto dir=std::filesystem::path("generated/gameover-tests")/stage;
    std::filesystem::create_directories(dir);
    std::ofstream trace(dir/"trace.txt");
    std::ofstream mosaicTrace(dir/"mosaic-force-trace.csv");
    mosaicTrace<<"frame,requested,sequence,renderer,color,cell\n";
    auto& scenes=app.Scenes(); auto& input=*app.GetInput();
    using State=GameOverSequence::State;
    const auto timeAt=[](const GameOverSequence& seq,State state,float progress) {
        float time=0;
        for (int i=static_cast<int>(State::Death);i<static_cast<int>(state);++i) time+=seq.Duration(static_cast<State>(i));
        return time+seq.Duration(state)*progress;
    };
    const auto draw=[&] {
#ifdef USE_IMGUI
        app.ImGui()->Begin();
#endif
        app.Draw();
    };
    const auto capture=[&](const char* name) {
        draw();
        app.Dx()->PreDraw(false); app.Render()->RenderPostEffectsForSceneTexture();
        check(app.Render()->BeginSceneTextureOverlay(),"Post-processed screenshot overlay target missing");
        scenes.DrawOverlay2D(app); app.Render()->EndSceneTextureOverlay();
        app.Dx()->SetBackBufferRenderTarget(); app.Dx()->PostDraw(); app.Dx()->WaitForGPU();
        DirectX::ScratchImage image; const auto state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        check(SUCCEEDED(DirectX::CaptureTexture(app.Dx()->GetCommandQueue(),app.Render()->GetSceneTextureResource(),false,image,state,state)),"GameOver GPU readback failed");
        check(SUCCEEDED(DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),(dir/(std::string(name)+".png")).c_str())),"GameOver capture failed");
        const auto* game=dynamic_cast<const GameScene*>(scenes.Current());
        if (game && game->gameOver_.Phase()==State::Corpse) {
            const auto* pixels=image.GetImage(0,0,0);
            for (size_t y=0;y<pixels->height;++y) for (size_t x=0;x<pixels->width;++x) {
                const auto* p=pixels->pixels+y*pixels->rowPitch+x*4;
                check(p[0]==0 && p[1]==0 && p[2]==0 && p[3]==255,"Unconscious view leaked scene pixels");
            }
        }
        if (game && game->gameOver_.Phase()==State::Closing) {
            // Compare the real 3D scene against a black-vignette reference with
            // the identical eyelids. Red must survive only in the exposed slit.
            const auto original=app.Render()->DeathPostEffect();
            auto black=original; black.vignetteColor={0,0,0,1}; app.Render()->SetDeathPostEffect(black);
            app.Dx()->PreDraw(false); app.Render()->RenderPostEffectsForSceneTexture();
            check(app.Render()->BeginSceneTextureOverlay(),"Closing reference overlay target missing");
            scenes.DrawOverlay2D(app); app.Render()->EndSceneTextureOverlay();
            app.Dx()->SetBackBufferRenderTarget(); app.Dx()->PostDraw(); app.Dx()->WaitForGPU();
            DirectX::ScratchImage reference;
            check(SUCCEEDED(DirectX::CaptureTexture(app.Dx()->GetCommandQueue(),app.Render()->GetSceneTextureResource(),false,reference,state,state)),"Closing reference readback failed");
            const auto* actual=image.GetImage(0,0,0); const auto* baseline=reference.GetImage(0,0,0);
            const auto vision=game->gameOver_.Vision();
            size_t redPixels=0;
            for (size_t y=0;y<actual->height;++y) for (size_t x=0;x<actual->width;++x) {
                const auto* p=actual->pixels+y*actual->rowPitch+x*4;
                const auto* b=baseline->pixels+y*baseline->rowPitch+x*4;
                if (p[0]>b[0] && p[0]-b[0]>p[1]-b[1]) ++redPixels;
                const float u=(static_cast<float>(x)+.5f)/static_cast<float>(actual->width);
                const float v=(static_cast<float>(y)+.5f)/static_cast<float>(actual->height);
                const float gap=GameOverVisionOverlay::HalfGap(u,vision.opening,game->gameOver_.settings.eyelidCurve);
                if (std::abs(v-.5f)>gap+.005f)
                    check(p[0]==0 && p[1]==0 && p[2]==0,"Death red was drawn over opaque eyelids");
            }
            check(redPixels>0,"Death red disappeared from visible scene before complete closure");
            trace<<name<<" visibleRedPixels="<<redPixels<<std::endl;
            app.Render()->SetDeathPostEffect(original);
        }
    };
    const auto finish=[&] {
        for (int i=0;i<100 && scenes.IsTransitioning();++i) { scenes.Update(app,.05f); draw(); }
        check(!scenes.IsTransitioning() && scenes.LastTransitionError().empty(),"GameOver fade/reload failed");
    };
    const auto release=[&] {
        std::fill(std::begin(input.keys_),std::end(input.keys_),0);
        std::fill(std::begin(input.prevKeys_),std::end(input.prevKeys_),0);
        input.leftMouseDown_=input.prevLeftMouseDown_=false; input.mouseDelta_={};
    };
    scenes.Register("GameOverTest",[stage] { return std::make_unique<GameScene>(false,stage); });
    scenes.Change(app,"GameOverTest");
    auto* game=dynamic_cast<GameScene*>(scenes.Current());
    check(game && game->stageLoaded_ && !game->sequenceOwnsCamera_,"GameOver stage loading failed");
    game->initialCapturePending_=false;
    const auto start=game->level_.playerPosition;
    // Deliberately die away from the spawn point. Retry must use saved stage data.
    const auto deathPoint=game->level_.collision.Move(start,start+Vector3{.25f,0,.25f},.4f,1.8f,true);
    game->player_.SetStage(&game->level_.collision,deathPoint,game->level_.playerRotation);
    game->player_.RefreshVisuals();
    game->player_.ApplyDamage(100,{1,0,0}); game->Update(app,0);
    check(game->gameOver_.Phase()==GameOverSequence::State::Death && game->gameOver_.HasVisual(),"HP zero did not start death");
    check(!input.IsCameraControlEnabled() && !game->player_.IsMovementEnabled(),"Death did not release controls");
    check(StageLength(game->gameOver_.Death().player.translate-deathPoint)<1e-5f && game->gameOver_.Death().impactDirection.x==1,"Death context lost");
    const auto* body=game->gameOver_.body_.get();
    const auto count=game->enemyPool_.Active(); const auto defeated=game->stage_.DefeatedCount();
    const auto clock=game->stage_.Time(); const auto spawns=game->spawnSystem_.ElapsedTime();
    const auto ammo=game->player_.CurrentWeapon().Magazine();
    input.keys_[DIK_W]=input.keys_[DIK_R]=input.keys_[DIK_E]=input.keys_[DIK_RETURN]=0x80;
    input.leftMouseDown_=true; input.mouseDelta_={500,500};
    float previous=0;
    const auto& seq=game->gameOver_;
    struct Checkpoint { float first; const char* second; State phase; };
    Vector3 floorPosition{},floorRotation{};
    for (const auto& checkpoint:std::vector<Checkpoint>{
        {timeAt(seq,State::Death,.2f),"00-death-impact-post",State::Death},
        {timeAt(seq,State::Death,.5f),"01-stagger",State::Death},
        {timeAt(seq,State::Collapse,.25f),"02-fall-start",State::Collapse},
        {timeAt(seq,State::Collapse,.75f),"03-falling",State::Collapse},
        {timeAt(seq,State::FloorHold,.2f),"04-landing",State::FloorHold},
        {timeAt(seq,State::FloorHold,.9f),"05-floor-visible",State::FloorHold},
        {timeAt(seq,State::Closing,.001f),"06a-closing-start",State::Closing},
        {timeAt(seq,State::Closing,.5f),"06b-closing-middle",State::Closing},
        {timeAt(seq,State::Closing,.8f),"06c-closing-almost-closed",State::Closing},
        {timeAt(seq,State::Corpse,.5f),"07-darkness",State::Corpse},
        {timeAt(seq,State::Opening,.25f),"08-opening-early",State::Opening},
        {timeAt(seq,State::Opening,.75f),"09-opening-late",State::Opening},
        {timeAt(seq,State::Awake,.5f),"10-eyes-open-still",State::Awake},
        {timeAt(seq,State::Rising,seq.RiseHeadEnd()*.5f),"11-head-movement",State::Rising},
        {timeAt(seq,State::Rising,(seq.RisePauseStart()+seq.RisePauseEnd())*.5f),"12-head-pause",State::Rising},
        {timeAt(seq,State::Rising,.6f),"13-rising",State::Rising},
        {timeAt(seq,State::Rising,.9f),"14-finish-rise",State::Rising},
        {timeAt(seq,State::Possess,.5f),"15-possess",State::Possess},
        {seq.MenuDelay()+.35f,"16-game-over",State::Wander}}) {
        game->Update(app,checkpoint.first-previous); previous=checkpoint.first;
        check(game->gameOver_.Phase()==checkpoint.phase,"Consciousness state transition is mistimed");
        check(scenes.Fade().State()==TransitionState::Idle,"Consciousness overlay altered scene transition fade");
        check(game->gameOver_.MenuReady()==(checkpoint.phase==State::Wander),"Menu appeared before Wander");
        check(game->gameOver_.body_.get()==body,"Corpse was replaced during reanimation");
        check(!game->gameOver_.bodyVisible_,"First-person body appeared during reanimation");
        if (game->gameOver_.Phase()!=GameOverSequence::State::Wander)
            check(std::abs(std::remainder(viewHeading(game->camera_.GetRotate())-viewHeading(game->gameOver_.Death().camera.rotation),6.2831853f))<.04f,"Death camera changed viewing direction");
        check(StageLength(game->player_.GetTransform().translate-deathPoint)<1e-5f,"Dead player moved");
        check(game->stage_.Time()==clock && game->spawnSystem_.ElapsedTime()==spawns && game->stage_.IsPlaying(),"Death advanced stage/spawns/clear");
        check(game->enemyPool_.Active()==count && game->stage_.DefeatedCount()==defeated,"Presentation actor affected enemy accounting");
        check(game->shotCount_==0 && game->player_.CurrentWeapon().Magazine()==ammo && !input.IsCameraControlEnabled(),"Held controls escaped death guard");
        check(game->player_.GetHP()==0 && !game->player_.IsMovementEnabled(),"Death brace accepted damage/movement controls");
        if (checkpoint.phase==State::Death) {
            check(rotationDistance(body->Rotation(),{0,game->level_.playerVisual.WorldYaw(seq.heading_),0})<.001f,"Body collapsed during upright brace");
            check(std::abs(game->camera_.GetTranslate().y-seq.Death().camera.position.y)<.1f,"Death brace lowered eye into collapse too early");
        }
        if (checkpoint.phase==State::FloorHold) {
            floorPosition=game->camera_.GetTranslate(); floorRotation=game->camera_.GetRotate();
        }
        if (game->gameOver_.Phase()==GameOverSequence::State::Corpse) {
            const auto r=body->Rotation(); const auto matrix=Matrix4x4::RotateXYZ(r.x,r.y,r.z);
            check(std::abs(matrix.m[1][1])<.001f && std::abs(matrix.m[2][1])<.001f,"Supine/prone corpse is not flat at this player heading");
        }
        if (checkpoint.phase==State::Closing || checkpoint.phase==State::Corpse || checkpoint.phase==State::Opening || checkpoint.phase==State::Awake)
            check(StageLength(game->camera_.GetTranslate()-floorPosition)<.001f && rotationDistance(game->camera_.GetRotate(),floorRotation)<.001f,"Camera rose before eyes fully opened and awake hold ended");
        const auto vision=game->gameOver_.Vision();
        const auto post=game->gameOver_.PostEffect();
        if (checkpoint.phase==State::Corpse || checkpoint.phase==State::Wander) check(!post.Active(),"Blackness/Wander retained post processing");
        if (checkpoint.phase<=State::Closing)
            check(post.vignette>0 && post.vignetteColor.x==seq.settings.post.red.x && post.vignetteColor.y==seq.settings.post.red.y && post.vignetteColor.z==seq.settings.post.red.z,"Death tint faded before eyelids fully closed");
        else check(post.vignetteColor.x==0 && post.vignetteColor.y==0 && post.vignetteColor.z==0,"Death red returned after complete closure");
        if (checkpoint.phase==State::Death || checkpoint.phase==State::Collapse || checkpoint.phase==State::FloorHold)
            check(vision.shade==0 && vision.opening==1,"Eyes closed/dimmed before the post-collapse hold ended");
        capture(checkpoint.second);
        check(app.Render()->DeathPostEffect().Active()==post.Active(),"Renderer did not follow death post state");
        check(postDistance(post,app.Render()->DeathPostEffect())<.00001f,"Mosaic parameters changed between sequence and renderer");
        trace<<checkpoint.second<<" time="<<checkpoint.first<<" phase="<<static_cast<int>(game->gameOver_.Phase())<<" shade="<<vision.shade<<" opening="<<vision.opening
            <<" vignette="<<post.vignette<<" red="<<post.vignetteColor.x<<","<<post.vignetteColor.y<<","<<post.vignetteColor.z
            <<" desaturation="<<post.desaturation<<" blurPx="<<post.blurPixels<<" chromaticPx="<<post.chromaticPixels<<" mosaic="<<post.mosaicStrength<<" mosaicClock="<<post.mosaicTime<<std::endl;
        if (std::string(checkpoint.second)=="09-opening-late" || checkpoint.phase==State::Awake || std::string(checkpoint.second)=="13-rising") {
            const auto saved=game->gameOver_.settings.post;
            game->gameOver_.settings.post.mosaicStrength=1;
            game->gameOver_.settings.post.mosaicColorStrength=1;
            game->gameOver_.settings.post.mosaicCellSize=.015f;
            capture((std::string(checkpoint.second)+"-mosaic-force").c_str());
            const auto forced=game->gameOver_.PostEffect(), rendered=app.Render()->DeathPostEffect();
            check(std::abs(rendered.mosaicStrength-forced.mosaicStrength)<.00001f && rendered.mosaicColorStrength==1 && rendered.mosaicCellSize==.015f,"Forced mosaic was reduced in renderer");
            if (checkpoint.phase==State::Awake) check(forced.mosaicStrength==1 && rendered.mosaicStrength==1,"Forced mosaic was clamped below 1.0");
            mosaicTrace<<checkpoint.second<<",1,"<<forced.mosaicStrength<<','<<rendered.mosaicStrength<<','<<rendered.mosaicColorStrength<<','<<rendered.mosaicCellSize<<std::endl;
            game->gameOver_.settings.post=saved;
            game->gameOver_.settings.post.mosaicEnabled=false;
            capture((std::string(checkpoint.second)+"-mosaic-off").c_str());
            game->gameOver_.settings.post=saved;
            app.Render()->SetDeathPostEffect(game->gameOver_.PostEffect());
        }
    }
    check(game->gameOver_.MenuReady() && !game->gameOverOverlay_.Armed(),"Menu timing/held Enter guard failed");
    check(game->gameOver_.AI().state==EnemyState::Wander,"Possessed actor did not use Wander");
    game->OnStageClear(app);
    check(game->stage_.IsPlaying(),"Clear handler accepted an active death sequence");
    release(); game->Update(app,0); game->Update(app,0);
    check(game->gameOverOverlay_.Armed(),"Menu did not arm on release");
    const auto beforeWander=game->gameOver_.Position();
    for (int i=0;i<1200;++i) {
        const auto before=game->gameOver_.Position(); game->Update(app,1.0f/60);
        const auto after=game->gameOver_.Position();
        check(EnemyNavigation::Clear(&game->level_.collision,before,after,game->gameOver_.settings.collisionRadius,game->player_.Settings().cameraHeight+.18f),"Possessed actor crossed a wall/unsupported floor");
    }
    check(StageLength(beforeWander-game->gameOver_.Position())>.1f,"Wander actor is stuck at stage start");
    check(StageLength(game->camera_.GetTranslate()-(game->gameOver_.Position()+Vector3{0,game->player_.Settings().cameraHeight,0}))<.001f,"Camera not at possessed eye");
    check(rotationDistance(game->camera_.GetRotate(),{0,game->gameOver_.heading_,0})<.001f,"Post-rise camera shake did not settle");
    capture("17-wander");
    check(game->LeaveGameOver(app,true) && !game->LeaveGameOver(app,true),"Retry duplicate guard failed");
    finish(); game=dynamic_cast<GameScene*>(scenes.Current());
    check(game && game->stageId_==stage && !game->gameOver_.Active() && !game->gameOver_.HasVisual(),"Retry did not reset sequence/current stage");
    check(StageLength(game->player_.GetTransform().translate-start)<1e-5f && game->player_.GetHP()==100 && game->stage_.Time()==0,"Retry did not reset spawn/HP/time");
    check(game->player_.IsMovementEnabled() && game->gameOver_.WorldTimeScale()==1,"Retry left movement/timing state behind");
    check(game->gameOver_.Vision().shade==0 && game->gameOver_.Vision().opening==1,"Retry retained eyelids/blackout");
    check(!game->gameOver_.PostEffect().Active() && !app.Render()->DeathPostEffect().Active(),"Retry retained death post effect");
    capture("18-retry");
    game->player_.ApplyDamage(100); game->Update(app,0); game->Update(app,game->gameOver_.MenuDelay()+.35f);
    check(game->LeaveGameOver(app,false),"Title request rejected"); finish();
    check(!app.Render()->DeathPostEffect().Active(),"Title retained death post effect");
    check(scenes.CurrentName()=="Title" && !input.IsCameraControlEnabled(),"Title return failed"); capture("19-title");
    check(scenes.TransitionTo("GameOverTest",.05f,.05f),"Stage reload after Title failed"); finish();
    game=dynamic_cast<GameScene*>(scenes.Current());
    check(game && game->player_.GetHP()==100 && !game->gameOver_.Active(),"Stage reload retained death");
    game->player_.ApplyDamage(100); game->Update(app,0); game->Update(app,timeAt(game->gameOver_,State::Opening,.4f));
    const auto* interrupted=game;
    scenes.Change(app,"GameOverTest");
    // SceneManager retires outgoing resources until the next GPU-safe update.
    check(!interrupted->gameOver_.Active() && !interrupted->gameOver_.HasVisual() && interrupted->gameOver_.WorldTimeScale()==1 &&
        interrupted->gameOver_.Vision().shade==0 && interrupted->gameOver_.Vision().opening==1,"Reload during opening did not clean up OnExit");
    check(!interrupted->gameOver_.PostEffect().Active() && !app.Render()->DeathPostEffect().Active(),"Interrupted scene leaked death post effect");
    game=dynamic_cast<GameScene*>(scenes.Current());
    check(game && !game->gameOver_.Active() && game->player_.GetHP()==100,"Interrupted stage reload failed");
    // Compare sequence phase/camera at different update rates on a stationary world.
    const GameOverSequence::DeathContext death{game->player_.GetTransform(),{game->player_.EyePosition(),{},1.04719755f},{},game->player_.Settings().cameraHeight};
    Camera aCamera,bCamera,cCamera; GameOverSequence a,b,c; StageWorld flat;
    a.Begin(app.ObjCom(),app.Dx(),&aCamera,game->enemyDefinitions_.Find("normal"),{},death);
    b.Begin(app.ObjCom(),app.Dx(),&bCamera,game->enemyDefinitions_.Find("normal"),{},death);
    for (int i=0;i<120;++i) a.Update(1.0f/30,flat,aCamera);
    for (int i=0;i<576;++i) b.Update(1.0f/144,flat,bCamera);
    check(a.Phase()==b.Phase() && StageLength(aCamera.GetTranslate()-bCamera.GetTranslate())<.002f && StageLength(aCamera.GetRotate()-bCamera.GetRotate())<.002f,"Sequence depends on FPS");
    // All camera phases in a translated/rotated room: floor, corner, corridor,
    // low ceiling, spawn gate, initial overlap and an airborne death.
    const Vector3 origin{17,2,-11};
    const auto box=[&](StageWorld& world,const Vector3& low,const Vector3& high,float yaw,bool gate=false) {
        StageCollider c; c.local={low,high}; c.position=origin; c.rotation={0,yaw,0};
        c.playerOnly=gate; c.UpdateMatrices(); world.colliders.push_back(c);
    };
    const auto clear=[&](const StageWorld& world,const Vector3& point,float radius) {
        for (const auto& c:world.colliders) {
            const auto local=StagePoint(point,c.inverse), pad=FirstPersonCamera::Padding(c,radius-.003f);
            const auto low=c.local.min-pad, high=c.local.max+pad;
            check(local.x<=low.x || local.x>=high.x || local.y<=low.y || local.y>=high.y || local.z<=low.z || local.z>=high.z,"Death camera penetrated stage geometry");
        }
    };
    for (int scenario=0;scenario<6;++scenario) for (float yaw:{0.0f,.7f,3.13f,6.7f}) {
        StageWorld room; room.playerGravity=true;
        box(room,{-8,-.5f,-8},{8,0,8},yaw);
        box(room,{.34f,0,-8},{.7f,2.3f,8},yaw,scenario==3);
        if (scenario==1) box(room,{-8,0,.34f},{8,2.3f,.7f},yaw);
        if (scenario==2) box(room,{-.7f,0,-8},{-.34f,2.3f,8},yaw);
        if (scenario==3) box(room,{-8,1.95f,-8},{8,2.4f,8},yaw);
        if (scenario==2) { room.colliders[1].scale={1.1f,1,.8f}; room.colliders[1].UpdateMatrices(); }
        const auto swept=FirstPersonCamera::Move(room,origin+Vector3{0,1,0},
            origin+Vector3{20*std::cos(yaw),-4,-20*std::sin(yaw)},.12f);
        clear(room,swept,.12f);
        const auto& wall=room.colliders[1];
        check(StagePoint(swept,wall.inverse).x<=wall.local.min.x-FirstPersonCamera::Padding(wall,.12f).x+.003f,
            "Long 3D camera sweep tunnelled through wall/gate");
        auto context=death; context.player.translate=origin; context.player.rotate.y=yaw+1.1f;
        const float pitch=scenario==0 ? -1.5533f : scenario==1 ? 1.5533f : -.6f;
        context.camera={origin+Vector3{0,context.eyeHeight,0},{pitch,yaw,scenario==4 ? .08f : 0},.75f};
        if (scenario==4) { // The camera begins slightly inside a wall after an external teleport.
            context.camera.position+=Vector3{std::cos(yaw)*.3f,0,-std::sin(yaw)*.3f};
        }
        if (scenario==5) { context.player.translate.y+=.8f; context.camera.position.y+=.8f; }
        a.Reset(); b.Reset(); c.Reset(); a.settings={}; b.settings={}; c.settings={};
        a.Begin(app.ObjCom(),app.Dx(),&aCamera,game->enemyDefinitions_.Find("normal"),{},context);
        b.Begin(app.ObjCom(),app.Dx(),&bCamera,game->enemyDefinitions_.Find("normal"),{},context);
        c.Begin(app.ObjCom(),app.Dx(),&cCamera,game->enemyDefinitions_.Find("normal"),{},context);
        a.Update(0,room,aCamera); b.Update(0,room,bCamera); c.Update(0,room,cCamera);
        if (scenario!=4) check(StageLength(aCamera.GetTranslate()-context.camera.position)<.001f &&
            StageLength(aCamera.GetRotate()-context.camera.rotation)<.001f,"Death did not preserve FPS pose");
        clear(room,aCamera.GetTranslate(),a.settings.cameraRadius);
        float sampleTime=0;
        std::vector<float> sampleTimes;
        for (auto phase:{State::Death,State::Collapse,State::FloorHold,State::Closing,State::Corpse,State::Opening,State::Awake,State::Rising,State::Possess}) {
            for (float p:{0.0f,.5f,1.0f}) sampleTimes.push_back(timeAt(a,phase,p));
        }
        sampleTimes.push_back(timeAt(a,State::Opening,.25f));
        sampleTimes.push_back(timeAt(a,State::Opening,.75f));
        sampleTimes.push_back(timeAt(a,State::Rising,a.RisePauseStart()));
        sampleTimes.push_back(timeAt(a,State::Rising,a.RisePauseEnd()));
        sampleTimes.push_back(timeAt(a,State::FloorHold,0)+a.settings.landingSeconds*.35f);
        sampleTimes.push_back(timeAt(a,State::FloorHold,0)+a.settings.landingSeconds);
        sampleTimes.push_back(a.settings.post.staggerPulseDelay+a.settings.post.staggerPulseSeconds*.5f);
        sampleTimes.push_back(timeAt(a,State::FloorHold,0)+a.settings.post.landingPulseDelay+a.settings.post.landingPulseSeconds*.5f);
        std::sort(sampleTimes.begin(),sampleTimes.end());
        for (float time:sampleTimes) {
            const auto advance=[&](GameOverSequence& seq,Camera& cam,float tick) {
                float left=time-sampleTime;
                while (left>0) {
                    const float step=std::min(left,tick); const auto prev=cam.GetTranslate(); const auto rot=cam.GetRotate();
                    const auto oldVision=seq.Vision();
                    const auto oldPhase=seq.Phase();
                    seq.Update(step,room,cam); left-=step;
                    const auto vision=seq.Vision();
                    if (oldPhase==State::Closing && seq.Phase()==State::Closing)
                        check(vision.opening<=oldVision.opening+1e-6f,"Closing reversed direction");
                    if (oldPhase==State::Opening && seq.Phase()==State::Opening)
                        check(vision.opening>=oldVision.opening-1e-6f,"Awakening blinked/closed again");
                    if (seq.Phase()==State::Death || seq.Phase()==State::Collapse || seq.Phase()==State::FloorHold || seq.Phase()>=State::Awake)
                        check(vision.opening==1 && vision.shade==0,"Unexpected eyelids outside Closing/Corpse/Opening");
                    const float maxVisionChange=std::max(4/seq.Duration(State::Closing),8/seq.Duration(State::Opening))*step+.0001f;
                    check(std::abs(oldVision.shade-vision.shade)<=maxVisionChange && std::abs(oldVision.opening-vision.opening)<=maxVisionChange,"Darkness/eyelids jumped at a phase boundary");
                    clear(room,cam.GetTranslate(),seq.settings.cameraRadius);
                    check(!seq.bodyVisible_,"FPS corpse mesh became visible");
                    check(std::abs(std::remainder(viewHeading(cam.GetRotate())-viewHeading(context.camera.rotation),6.2831853f))<.1f,"Camera snapped away from death yaw");
                    check(StageLength(cam.GetTranslate()-prev)<.25f && rotationDistance(cam.GetRotate(),rot)<.16f,"Camera jumped at a phase boundary");
                    check(std::hypot(cam.GetTranslate().x-origin.x,cam.GetTranslate().z-origin.z)<.4f,"Death camera left first person for an orbit");
                }
            };
            advance(a,aCamera,1.0f/30); advance(b,bCamera,1.0f/144); advance(c,cCamera,1.0f/60);
            trace<<"camera scenario="<<scenario<<" yaw="<<yaw<<" time="<<time<<" phase="<<static_cast<int>(a.Phase())<<"/"<<static_cast<int>(b.Phase())
                <<" positionDelta="<<StageLength(aCamera.GetTranslate()-bCamera.GetTranslate())
                <<" rotationDelta="<<rotationDistance(aCamera.GetRotate(),bCamera.GetRotate())<<std::endl;
            check(a.Phase()==b.Phase() && StageLength(aCamera.GetTranslate()-bCamera.GetTranslate())<.012f &&
                rotationDistance(aCamera.GetRotate(),bCamera.GetRotate())<.003f,"Collision camera depends on FPS");
            check(a.Phase()==c.Phase() && StageLength(aCamera.GetTranslate()-cCamera.GetTranslate())<.012f &&
                rotationDistance(aCamera.GetRotate(),cCamera.GetRotate())<.003f,"60 FPS camera differs");
            for (const auto* other:{&b,&c}) check(std::abs(a.Vision().shade-other->Vision().shade)<.001f &&
                std::abs(a.Vision().opening-other->Vision().opening)<.001f,"Darkness/eyelids depend on FPS");
            for (const auto* other:{&b,&c}) check(postDistance(a.PostEffect(),other->PostEffect())<.003f,"Death post effect depends on FPS");
            if (a.Phase()==State::FloorHold || a.Phase()==State::Closing || a.Phase()==State::Corpse || a.Phase()==State::Opening || a.Phase()==State::Awake) {
                check(std::abs(aCamera.GetTranslate().y-(origin.y+a.settings.floorEyeHeight))<.01f,"Corpse did not hold floor-level FPS");
                check(rotationDistance(aCamera.GetRotate(),context.camera.rotation)>.6f,"Collapse did not roll FPS camera");
            }
            if (a.Phase()==State::Possess) check(std::abs(aCamera.GetTranslate().y-(origin.y+context.eyeHeight))<.01f,"Rising did not restore eye height");
            sampleTime=time;
        }
        // A single delayed update must sweep through the same phase boundaries.
        b.Reset(); b.Begin(app.ObjCom(),app.Dx(),&bCamera,game->enemyDefinitions_.Find("normal"),{},context);
        b.Update(sampleTime,room,bCamera); clear(room,bCamera.GetTranslate(),b.settings.cameraRadius);
        check(StageLength(aCamera.GetTranslate()-bCamera.GetTranslate())<.012f &&
            rotationDistance(aCamera.GetRotate(),bCamera.GetRotate())<.003f,"Long frame changed camera path");
        check(postDistance(a.PostEffect(),b.PostEffect())<.003f,"Long frame changed post effect timeline");
    }
    check(FirstPersonCamera::NearBounds({0,.5f,0},{{-.2f,0,-.2f},{.2f,1,.2f}},.16f) &&
        !FirstPersonCamera::NearBounds({0,.5f,1},{{-.2f,0,-.2f},{.2f,1,.2f}},.16f),"FPS enemy mesh proximity guard failed");
    // Upright bracing trembles, ends at a matching pose, then falls with weight.
    a.Reset(); b.Reset(); a.settings={}; b.settings={}; b.settings.staggerShake=0;
    b.settings.landingShake=b.settings.landingLift=0;
    a.Begin(app.ObjCom(),app.Dx(),&aCamera,game->enemyDefinitions_.Find("normal"),{},death);
    b.Begin(app.ObjCom(),app.Dx(),&bCamera,game->enemyDefinitions_.Find("normal"),{},death);
    a.Update(a.settings.staggerSeconds*.5f,flat,aCamera); b.Update(b.settings.staggerSeconds*.5f,flat,bCamera);
    check(a.Phase()==State::Death && aCamera.GetTranslate().y>death.camera.position.y-.1f &&
        aCamera.GetTranslate().y<death.camera.position.y,"Brace did not stay upright with a slight dip");
    check(rotationDistance(aCamera.GetRotate(),bCamera.GetRotate())>.001f,"Brace did not tremble");
    a.Update(a.settings.staggerSeconds*.5f,flat,aCamera); b.Update(b.settings.staggerSeconds*.5f,flat,bCamera);
    check(a.Phase()==State::Collapse && StageLength(aCamera.GetTranslate()-bCamera.GetTranslate())<.001f &&
        rotationDistance(aCamera.GetRotate(),bCamera.GetRotate())<.001f,"Brace shake did not finish continuously");
    std::vector<float> fallHeights;
    for (float p:{.1f,.2f,.5f,.6f,.95f,1.0f}) {
        a.Update(timeAt(a,State::Collapse,p)-a.Elapsed(),flat,aCamera); fallHeights.push_back(aCamera.GetTranslate().y);
    }
    const float earlySpeed=(fallHeights[0]-fallHeights[1])/.1f;
    const float middleSpeed=(fallHeights[2]-fallHeights[3])/.1f;
    const float lateSpeed=(fallHeights[4]-fallHeights[5])/.05f;
    check(earlySpeed>0 && middleSpeed>earlySpeed*2 && lateSpeed<middleSpeed*.5f,"Fall did not accelerate then brake before contact");
    a.Update(a.settings.landingSeconds*.35f,flat,aCamera);
    b.Update(b.settings.collapseSeconds+b.settings.landingSeconds*.35f,flat,bCamera);
    check(aCamera.GetTranslate().y>bCamera.GetTranslate().y+.001f &&
        rotationDistance(aCamera.GetRotate(),bCamera.GetRotate())>.001f,"Floor contact did not produce a small camera pulse");
    const float settle=a.settings.landingSeconds*.65f+.001f;
    a.Update(settle,flat,aCamera); b.Update(settle,flat,bCamera);
    check(StageLength(aCamera.GetTranslate()-bCamera.GetTranslate())<.001f &&
        rotationDistance(aCamera.GetRotate(),bCamera.GetRotate())<.001f,"Contact shake did not settle to the floor pose");
    // A shorter floor hold must not cut off contact shake at the Closing boundary.
    a.Reset(); a.settings={}; a.settings.floorHoldSeconds=.03f;
    a.Begin(app.ObjCom(),app.Dx(),&aCamera,game->enemyDefinitions_.Find("normal"),{},death);
    a.Update(timeAt(a,State::Closing,0)-.00001f,flat,aCamera);
    const auto contactPosition=aCamera.GetTranslate(),contactRotation=aCamera.GetRotate();
    a.Update(.00002f,flat,aCamera);
    check(a.Phase()==State::Closing && StageLength(aCamera.GetTranslate()-contactPosition)<.001f &&
        rotationDistance(aCamera.GetRotate(),contactRotation)<.001f &&
        aCamera.GetTranslate().y>death.player.translate.y+a.settings.floorEyeHeight+.001f,"Short floor hold interrupted the contact pulse");
    // Closing slows toward shut; opening is monotonic and never blinks again.
    a.Reset(); a.settings={}; a.Begin(app.ObjCom(),app.Dx(),&aCamera,game->enemyDefinitions_.Find("normal"),{},death);
    a.Update(timeAt(a,State::Closing,0),flat,aCamera);
    float previousOpening=1,previousTravel=2;
    for (int i=0;i<4;++i) {
        a.Update(a.settings.closeSeconds*.25f,flat,aCamera);
        const float opening=a.Vision().opening,travel=previousOpening-opening;
        check(travel>0 && travel<previousTravel,"Closing did not gradually slow down");
        previousOpening=opening; previousTravel=travel;
    }
    a.Update(timeAt(a,State::Rising,a.RisePauseStart()+.01f)-a.Elapsed(),flat,aCamera);
    const auto pausedPosition=aCamera.GetTranslate(),pausedRotation=aCamera.GetRotate();
    check(pausedPosition.y<death.player.translate.y+a.settings.floorEyeHeight+.15f,"Body rose before the head pause");
    a.Update(a.settings.riseSeconds*(a.RisePauseEnd()-a.RisePauseStart()-.02f),flat,aCamera);
    check(StageLength(aCamera.GetTranslate()-pausedPosition)<.001f && rotationDistance(aCamera.GetRotate(),pausedRotation)<.001f,"Rising pause did not hold position/rotation");
    for (auto phase:{State::Death,State::Collapse,State::FloorHold,State::Closing,State::Corpse,State::Opening,State::Awake,State::Rising}) {
        a.Reset(); a.settings={}; a.settings.corpseSeconds=.25f; a.settings.openSeconds=.6f; a.settings.awakeSeconds=.12f;
        a.settings.staggerSeconds=.45f; a.settings.collapseSeconds=.75f;
        a.settings.riseSeconds=1.4f; a.settings.openCurve=2; a.settings.riseCurve=.7f;
        a.settings.floorHoldSeconds=.17f; a.settings.closeSeconds=.35f;
        a.settings.risePauseStartSeconds=.3f; a.settings.risePauseSeconds=.2f;
        a.Begin(app.ObjCom(),app.Dx(),&aCamera,game->enemyDefinitions_.Find("normal"),{},death);
        a.Update(timeAt(a,phase,.5f),flat,aCamera);
        check(a.Phase()==phase,"Custom consciousness timing ignored Settings");
        check(std::abs(a.RisePauseStart()*a.settings.riseSeconds-.3f)<1e-5f &&
            std::abs((a.RisePauseEnd()-a.RisePauseStart())*a.settings.riseSeconds-.2f)<1e-5f,"Rising pause start/duration ignored seconds settings");
        a.Reset(); check(a.Vision().shade==0 && a.Vision().opening==1 && !a.HasVisual() && !a.PostEffect().Active(),"Reset retained a consciousness phase/post effect");
    }
    // Retiming a pause before the requested head end must not jump to a
    // different pause height, including when its hold duration is zero.
    for (float pauseSeconds:{0.0f,.2f}) {
        a.Reset(); a.settings={}; a.settings.riseHeadEnd=.4f;
        a.settings.risePauseStartSeconds=.12f; a.settings.risePauseSeconds=pauseSeconds; a.settings.risePauseHeight=.4f;
        a.Begin(app.ObjCom(),app.Dx(),&aCamera,game->enemyDefinitions_.Find("normal"),{},death);
        a.Update(timeAt(a,State::Rising,a.RisePauseStart())-.00001f,flat,aCamera);
        const auto before=aCamera.GetTranslate(),rotation=aCamera.GetRotate();
        a.Update(.00002f,flat,aCamera);
        check(StageLength(aCamera.GetTranslate()-before)<.001f && rotationDistance(aCamera.GetRotate(),rotation)<.001f,"Retimed/disabled rise pause caused a camera discontinuity");
    }
    // Actual GPU coverage on wide, odd-sized and portrait viewports. Green UI
    // under the mask also verifies ordinary Sprite rectangles after SetQuad.
    for (const auto& size:std::vector<std::pair<uint32_t,uint32_t>>{{1280,720},{853,479},{1920,1080},{600,900}}) {
        const auto [width,height]=size;
        for (int probe=0;probe<4;++probe) {
            auto* dx=app.Dx(); constexpr auto format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            auto target=dx->CreateRenderTextureResource(width,height,format,{1,0,0,1});
            auto heap=dx->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,false);
            D3D12_RENDER_TARGET_VIEW_DESC rtv{}; rtv.Format=format; rtv.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
            const auto handle=heap->GetCPUDescriptorHandleForHeapStart(); dx->GetDevice()->CreateRenderTargetView(target.Get(),&rtv,handle);
            Sprite background; background.Initialize(app.SpriteCom(),dx,"resources/white1x1.png");
            const auto& white=TextureManager::GetInstance()->GetMetaData("resources/white1x1.png");
            background.SetScale({1.0f/static_cast<float>(white.width),1.0f/static_cast<float>(white.height),1});
            background.SetColor({0,1,0,1});
            background.Update(Matrix4x4::MakeIdentity4x4(),Matrix4x4::MakeOrthographicMatrix(0,0,1,1,0,100));
            dx->PreDraw(false); auto* command=dx->GetCommandList(); command->OMSetRenderTargets(1,&handle,FALSE,nullptr);
            const float red[]={1,0,0,1}; command->ClearRenderTargetView(handle,red,0,nullptr);
            const D3D12_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
            const D3D12_RECT scissor{0,0,static_cast<LONG>(width),static_cast<LONG>(height)};
            command->RSSetViewports(1,&viewport); command->RSSetScissorRects(1,&scissor);
            background.Draw();
            const GameOverSequence::VisionState vision=probe==0 ? GameOverSequence::VisionState{1,0} :
                probe==1 ? GameOverSequence::VisionState{0,.22f} : probe==2 ? GameOverSequence::VisionState{} : GameOverSequence::VisionState{.5f,1};
            game->gameOverVision_.Draw(vision,.75f);
            dx->SetBackBufferRenderTarget(); dx->PostDraw(); dx->WaitForGPU();
            DirectX::ScratchImage image; constexpr auto state=D3D12_RESOURCE_STATE_RENDER_TARGET;
            check(SUCCEEDED(DirectX::CaptureTexture(dx->GetCommandQueue(),target.Get(),false,image,state,state)),"Eyelid viewport GPU readback failed");
            const auto* pixels=image.GetImage(0,0,0);
            for (size_t y=0;y<height;++y) for (size_t x=0;x<width;++x) {
                const auto* p=pixels->pixels+y*pixels->rowPitch+x*4;
                check(p[0]==0 && p[2]==0 && p[3]==255,"Vision mask leaked background or broke opacity");
                if (probe==0 || (probe==1 && (y<height/4 || y>height*3/4 || x==0 || x==width-1)))
                    check(p[1]==0,"Closed/curved eyelids missed an edge pixel");
                if (probe==2) check(p[1]==255,"Fully open eyes left residual mask or default Sprite quad changed");
                if (probe==3) check(p[1]>0 && p[1]<255,"Dimming did not cover the entire viewport");
            }
            if (probe==1) check(pixels->pixels[(height/2)*pixels->rowPitch+(width/2)*4+1]==255,"Eyelids did not reveal the center slit");
            const auto path=dir/("vision-"+std::to_string(width)+"x"+std::to_string(height)+"-"+std::to_string(probe)+".png");
            check(SUCCEEDED(DirectX::SaveToWICFile(*pixels,DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path.c_str())),"Eyelid viewport capture failed");
        }
    }
    // Model facing must stay flat for all four extensible death poses/headings.
    for (auto pose:{GameOverSequence::DeathPose::Supine,GameOverSequence::DeathPose::Prone,GameOverSequence::DeathPose::LeftSide,GameOverSequence::DeathPose::RightSide}) {
        for (float yaw:{0.0f,.7f,1.5707963f,3.14159265f}) {
            a.Reset(); a.settings={}; a.settings.pose=pose;
            auto context=death; context.player.rotate.y=yaw;
            a.Begin(app.ObjCom(),app.Dx(),&aCamera,game->enemyDefinitions_.Find("normal"),{},context);
            float worldTime=0;
            const float corpseTime=timeAt(a,State::Corpse,.5f);
            for (float left=corpseTime;left>0;) {
                const float tick=std::min(left,1.0f/30); worldTime+=a.WorldDelta(tick); a.Update(tick,flat,aCamera); left-=tick;
            }
            const auto r=a.body_->Rotation(); const auto m=Matrix4x4::RotateXYZ(r.x,r.y,r.z);
            const bool side=pose==GameOverSequence::DeathPose::LeftSide || pose==GameOverSequence::DeathPose::RightSide;
            check(a.Phase()==GameOverSequence::State::Corpse && std::abs(m.m[1][1])<.001f && std::abs(m.m[side ? 0 : 2][1])<.001f,"Death pose changed with player heading");
            check(!a.bodyVisible_ && std::abs(std::remainder(viewHeading(aCamera.GetRotate())-viewHeading(context.camera.rotation),6.2831853f))<.04f,
                "Death pose changed first-person viewing direction/visibility");
            check(std::abs(worldTime-a.settings.staggerSeconds*a.settings.slowMotion)<.0001f,"Slow motion duration depends on update rate");
        }
    }
    // Phase endpoints are continuous where the scene is visible. Closed eyes
    // permit the Corpse bypass without processing invisible scene pixels.
    a.Reset(); a.settings={}; a.Begin(app.ObjCom(),app.Dx(),&aCamera,game->enemyDefinitions_.Find("normal"),{},death);
    for (auto phase:{State::Collapse,State::FloorHold,State::Closing,State::Awake,State::Rising,State::Possess,State::Wander}) {
        const float time=phase==State::Wander ? a.MenuDelay() : timeAt(a,phase,0);
        a.Update(time-.00001f-a.Elapsed(),flat,aCamera); const auto before=a.PostEffect();
        a.Update(.00002f,flat,aCamera);
        check(postDistance(before,a.PostEffect())<.003f,"Visible post effect jumped at a phase boundary");
    }
    a.Reset(); a.Begin(app.ObjCom(),app.Dx(),&aCamera,game->enemyDefinitions_.Find("normal"),{},death);
    a.Update(timeAt(a,State::FloorHold,.1f),flat,aCamera);
    a.settings.post.enabled=false; check(!a.PostEffect().Active(),"Master death post disable failed");
    a.settings.post.enabled=true;
    a.settings.post.vignetteEnabled=a.settings.post.desaturationEnabled=a.settings.post.blurEnabled=a.settings.post.chromaticEnabled=false;
    a.settings.post.mosaicEnabled=false;
    check(!a.PostEffect().Active(),"Disabling all death post types failed");
    // The awakening anomaly must never leak into death, closed eyes or Wander.
    // Its peak is after full opening; decay leaves the existing UI unaffected.
    a.Reset(); a.settings={}; a.Begin(app.ObjCom(),app.Dx(),&aCamera,game->enemyDefinitions_.Find("normal"),{},death);
    for (auto phase:{State::Death,State::Collapse,State::FloorHold,State::Closing,State::Corpse}) {
        a.Update(timeAt(a,phase,.5f)-a.Elapsed(),flat,aCamera);
        check(a.PostEffect().mosaicStrength==0,"Rainbow mosaic appeared before awakening");
    }
    a.Update(timeAt(a,State::Opening,0)+.2f-a.Elapsed(),flat,aCamera);
    check(a.PostEffect().mosaicStrength==0,"Mosaic ignored its opening delay");
    a.Update(timeAt(a,State::Awake,.5f)-a.Elapsed(),flat,aCamera);
    check(std::abs(a.PostEffect().mosaicStrength-a.settings.post.mosaicStrength)<.0001f,"Mosaic failed to peak just after opening");
    a.settings.post.mosaicEnabled=false; check(a.PostEffect().mosaicStrength==0,"Mosaic toggle failed at its peak");
    a.settings.post.mosaicEnabled=true; a.settings.post.enabled=false;
    check(!a.PostEffect().Active(),"Master post toggle retained awakening mosaic"); a.settings.post.enabled=true;
    a.settings.post.mosaicStrength=.025f; a.settings.post.mosaicCellSize=.012f; a.settings.post.mosaicColorStrength=.2f;
    const auto tuned=a.PostEffect();
    check(std::abs(tuned.mosaicStrength-.025f)<.0001f && tuned.mosaicCellSize==.012f && tuned.mosaicColorStrength==.2f,"Mosaic tuning did not reach renderer parameters");
    a.settings.post.mosaicPeakSeconds=2.35f;
    check(a.PostEffect().mosaicStrength<tuned.mosaicStrength,"Mosaic peak timing setting had no effect");
    a.settings.post={};
    a.Update(timeAt(a,State::Rising,.5f)-a.Elapsed(),flat,aCamera);
    const float midRise=a.PostEffect().mosaicStrength;
    check(midRise>0 && midRise<a.settings.post.mosaicStrength*.7f,"Mosaic did not decay during rise");
    a.Update(timeAt(a,State::Possess,.8f)-a.Elapsed(),flat,aCamera);
    check(a.PostEffect().mosaicStrength==0,"Mosaic remained in late Possess");
    a.settings.post.mosaicDecaySeconds=100;
    check(a.PostEffect().mosaicStrength>0,"Mosaic decay duration setting had no effect");
    a.Update(a.MenuDelay()+.01f-a.Elapsed(),flat,aCamera);
    check(!a.PostEffect().Active(),"Long mosaic decay leaked into Wander");
    a.Reset(); check(!a.PostEffect().Active(),"Mosaic reset failed");
    RunGameOverPostEffectRuntimeTests(app,dir);
    a.Update(std::numeric_limits<float>::quiet_NaN(),flat,aCamera); a.Update(-1,flat,aCamera);
    check(std::isfinite(a.Elapsed()),"Invalid dt corrupted sequence");
    a.Reset(); a.Reset(); check(!a.Active() && !a.HasVisual(),"Reset not idempotent");
    app.Dx()->WaitForGPU();
    std::ofstream(dir/"result.txt")<<"PASS: "<<stage<<" death post: forced 1/1/0.015 reaches GPU constants, visible colored blocks on grey, slow continuous pattern changes, retained scene detail, real Opening/Awake/Rising forced/off captures; persistent red through Closing start/mid/99.2%-closed GPU views, opaque eyelids remain black, red absent on reopening; phase-synced recovery, mosaic delay/peak/decay/settings/reset, 30/60/144 FPS and boundary continuity, 4 GPU viewport sizes and timestamp benchmarks, UI isolation, exact disabled-copy pixels/no extra draws, Corpse/Wander bypass, Retry/Title/interruption reset; braced death/camera collision (24 cases), single opening/rise/Wander (20s), frozen HP/weapons/enemies/stage, held-input guards, 4 poses x 4 headings, invalid dt. Menu destinations exercised through the UI's command handler.\n";
}
