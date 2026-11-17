#pragma once
#include "UpperHotelRouteChecks.h"
#include "GameApp.h"
#include "scene/Main/GameScene.h"
#include "DirectXCommon.h"
#include "ImGuiManagaer.h"
#include "RenderManager.h"
#include "DirectXTex.h"

inline void RunUpperHotelRuntimeTests(GameApp& app) {
    using UpperHotelChecks::Require;
    app.Scenes().Change(app,"Stage04");
    auto* game=dynamic_cast<GameScene*>(app.Scenes().Current());
    Require(game && game->stageLoaded_ && game->doorVisuals_.size()==30,"Stage04 GameScene loading");
    Require(game->removableMeshBindings_.size()==33 && std::all_of(game->removableMeshBindings_.begin(),game->removableMeshBindings_.end(),
        [](const auto& meshes) { return !meshes.empty(); }),
        "breakable window mesh bindings");
    Require(game->enemies_.size()==8,"All eight non-defense enemies must spawn at StageStart");
    for (const auto& point : game->spawnSystem_.Points()) {
        const bool defense=point.id.starts_with("F1_Defense_");
        Require(point.activation==(defense ? SpawnActivation::Manual : SpawnActivation::StageStart),
            "unexpected spawn activation: "+point.id);
    }
    const auto captureFrame=[&](const char* name) {
#ifdef USE_IMGUI
        app.ImGui()->Begin();
#endif
        app.Draw();
        app.Dx()->PreDraw(false);
        app.Render()->GetOffscreen()->BeginForPostEffect();
        game->DrawOverlay2D(app);
        app.Render()->GetOffscreen()->End();
        app.Dx()->SetBackBufferRenderTarget();
        app.Dx()->PostDraw();
        app.Dx()->WaitForGPU();
        DirectX::ScratchImage image;
        const auto state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        Require(SUCCEEDED(DirectX::CaptureTexture(app.Dx()->GetCommandQueue(),
            app.Render()->GetOffscreen()->GetResource(),false,image,state,state)),"Capture upper hotel");
        const auto path=std::filesystem::path("generated/upper-hotel-tests")/(std::string(name)+".png");
        Require(SUCCEEDED(DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path.c_str())),"Save upper hotel capture");
    };
    const auto verifyLaunch=[&](const char* launchName,const char* slideName,const char* straightName) {
        Require(game->escapeCarWheels_.size()==4,"Each tire and hub has its own axle transform");
        const auto& escape=*game->level_.hotelEscape;
        const auto& motion=escape.bodyMotion;
        const auto seek=[&](double seconds) { game->Update(app,static_cast<float>(seconds-game->escapeTime_)); };
        const double slideEnd=escape.departure+escape.drift.slideSeconds;
        const double recoveryEnd=slideEnd+escape.drift.recoverySeconds;
        const auto heading=escape.route.front().Heading();
        const auto forward=HotelEscape::CarPose{escape.start,heading}.Forward();
        const auto carInFrame=[&] {
            const auto point=escape.CarAt(game->escapeTime_).position+Vector3{0,.8f,0};
            const auto& vp=game->camera_.GetViewProjectionMatrix();
            const auto clip=StagePoint(point,vp);
            const float w=point.x*vp.m[0][3]+point.y*vp.m[1][3]+point.z*vp.m[2][3]+vp.m[3][3];
            return w>0 && std::abs(clip.x)<.9f*w && std::abs(clip.y)<.9f*w;
        };
        const auto widest=escape.CarAt(slideEnd);
        const auto travelAt=[&](double t) {
            return escape.CarAt(t+.001).position-escape.CarAt(t-.001).position;
        };
        const auto slipAngle=[&](double t) {
            const auto car=escape.CarAt(t);
            const auto travel=travelAt(t);
            return std::abs(std::atan2(StageDot(travel,car.Right()),StageDot(travel,car.Forward())));
        };
        float peakSlip=0;
        for (double t=escape.departure+.04;t<recoveryEnd+.12;t+=1.0/240)
            peakSlip=std::max(peakSlip,slipAngle(t));
        constexpr float deg=CameraSequence::DegreesToRadians;
        Require(std::abs(StageDot(widest.position-escape.start,HotelEscape::CarPose{escape.start,heading}.Right())-
                escape.drift.lateralMeters)<.001f &&
            std::abs(peakSlip-escape.drift.yawDegrees*deg)<.5f*deg && peakSlip>=15*deg && peakSlip<=25*deg &&
            slipAngle(escape.departure+.06)<.5f*deg,
            "The car starts along its heading and briefly reaches the configured sideslip");
        const auto& cast=game->escapeEnemies_;
        Require(cast.size()==3 && cast[0].impactTime>escape.departure && cast[0].impactTime<slideEnd &&
            cast[1].impactTime>recoveryEnd && cast[2].impactTime>cast[1].impactTime,
            "One drift impact and two straight impacts");
        Require(game->escapeLaunchCamera_.has_value() && escape.cut<cast[0].impactTime && cast[2].impactTime<escape.fade,
            "The front launch cut precedes the chase through all three impacts");
        const auto aligned=escape.DistanceAt(recoveryEnd);
        const auto gaining=escape.DistanceAt(recoveryEnd+.10);
        Require(escape.DistanceAt(recoveryEnd+.20)-gaining>gaining-aligned+.01f,"The car keeps accelerating after straightening");
        const Vector3 front{0,.45f,2}, rear{0,.45f,-2};
        const auto parked=Matrix4x4::MakeAffineMatrix({1,1,1},escape.rotation,escape.start);
        constexpr double rollPhase=.4375;
        seek(escape.departure-motion.ignitionSeconds*(1-rollPhase));
        const auto& revving=game->escapeCar_->GetWorldMatrix();
        const float bodyRoll=std::asin(std::abs(revving.m[0][1]));
        const float expectedRoll=motion.rollDegrees*CameraSequence::DegreesToRadians*std::sin(std::numbers::pi_v<float>*static_cast<float>(rollPhase));
        const auto frontQuarter=game->camera_.GetTranslate()-escape.start;
        Require(StageDot(frontQuarter,forward)>3 && frontQuarter.y<1.5f &&
            StageLength(game->camera_.GetTranslate()-game->escapeCameras_[0].Evaluate(0).position)<.01f && carInFrame(),
            "The stationary rev uses the low front-quarter camera");
        Require(std::hypot(revving.m[3][0]-escape.start.x,revving.m[3][2]-escape.start.z)<.0001f &&
            std::abs(bodyRoll-expectedRoll)<.001f,
            "The body rocks over a stationary chassis");
        for (const auto& wheel:game->escapeCarWheels_) {
            Require(StageLength(StagePoint(wheel.center,wheel.visual->GetWorldMatrix())-StagePoint(wheel.center,parked))<.0001f,
                "All four axles stay fixed during the rev");
            const float spin=std::abs(wheel.visual->GetRotate().x-escape.rotation.x);
            Require(wheel.center.z<0 ? spin>.05f : spin<.0001f,"Only the rear wheels spin in place");
        }
        seek(escape.departure-.01);
        Require(StagePoint(rear,game->escapeCar_->GetWorldMatrix()).y<StagePoint(rear,parked).y-.12f &&
            StagePoint(front,game->escapeCar_->GetWorldMatrix()).y>StagePoint(front,parked).y+.02f,
            "The rear suspension loads before the car moves");
        const float wheelRadius=game->escapeCarWheels_.front().radius;
        const auto spinAt=[&](double time) { return escape.WheelAngleAt(time,wheelRadius,true); };
        const float leadingSpin=spinAt(escape.departure)-spinAt(escape.departure-.005);
        const float trailingSpin=spinAt(escape.departure+.005)-spinAt(escape.departure);
        Require(leadingSpin>.1f && std::abs(trailingSpin-leadingSpin)<.01f,"Rear wheel spin continues through departure");

        seek(escape.departure+.105);
        const auto& body=game->escapeCar_->GetWorldMatrix();
        const auto car=escape.CarAt(game->escapeTime_);
        const auto chassis=Matrix4x4::MakeAffineMatrix({1,1,1},car.rotation,car.position);
        Require(StageLength(game->camera_.GetTranslate()-game->escapeLaunchCamera_->Evaluate(game->escapeTime_-escape.departure).position)<.01f &&
            StageDot(game->camera_.GetTranslate()-car.position,forward)>2 && carInFrame(),
            "The rebound is framed by the short head-on launch cut");
        Require(StagePoint(front,body).y>StagePoint(front,chassis).y+.22f &&
            std::abs(body.m[0][1])>.05f,"The body rebounds upward and rolls into the slide");
        auto unboosted=escape;
        unboosted.bodyMotion.boostAcceleration=0;
        Require(escape.DistanceAt(game->escapeTime_)>2*unboosted.DistanceAt(game->escapeTime_),
            "The rebound coincides with the initial acceleration burst");
        const auto travel=car.position-escape.start;
        Require(std::abs(travel.y)<.0001f &&
            std::abs(StageDot(travel,escape.route.front().direction)-escape.DistanceAt(game->escapeTime_))<.0001f,
            "Grounded wheels follow the existing launch distance");
        for (const auto& wheel:game->escapeCarWheels_)
            Require(StageLength(StagePoint(wheel.center,wheel.visual->GetWorldMatrix())-StagePoint(wheel.center,chassis))<.0001f,
                "Axles follow the driven chassis through launch");
        captureFrame(launchName);

        const double hit=cast[0].impactTime;
        const auto hitPose=escape.CarAt(hit);
        const auto toEnemy=cast[0].enemy->GetPosition()-hitPose.position;
        Require(std::abs(StageDot(toEnemy,hitPose.Forward()))<1.7f &&
            std::abs(StageDot(toEnemy,hitPose.Right()))>.98f && slipAngle(hit)>4*deg && slipAngle(hit)<12*deg &&
            (hitPose.rotation.y-heading.y)*escape.drift.lateralMeters>0,
            "The first enemy hits the sliding flank after the body has turned toward the trajectory");
        seek(hit+.035);
        Require(cast[0].enemy->IsDead() && !cast[1].enemy->IsDead() && !cast[2].enemy->IsDead() &&
            StageDot(game->camera_.GetTranslate()-escape.CarAt(game->escapeTime_).position,forward)<-2 && carInFrame(),
            "The rear chase frames the first drift impact");
        captureFrame(slideName);

        seek(escape.departure+motion.launchSeconds+.05);
        const auto recovering=escape.CarAt(game->escapeTime_);
        Require(StageLength(game->escapeCar_->GetTranslate()-recovering.position)<.0001f &&
            std::abs(game->escapeCar_->GetWorldMatrix().m[2][1])<.0001f,"The rebound settles before the body finishes countersteering");
        const auto roadHeading=escape.route.front().Heading();
        seek(slideEnd+escape.drift.recoverySeconds*.55);
        const auto& countersteer=game->escapeCar_->GetWorldMatrix();
        Require(std::atan2(countersteer.m[2][0],countersteer.m[2][2])>roadHeading.y+.05f &&
            slipAngle(game->escapeTime_)<.5f*deg,
            "Countersteer follows travel back to the centerline");
        seek(recoveryEnd);
        auto previousPosition=game->escapeCar_->GetTranslate();
        float previousSpeed=0;
        std::array<bool,2> straightImpactLift{};
        constexpr float frameTime=1.f/60;
        while (game->escapeTime_+frameTime<cast[2].impactTime+.06) {
            game->Update(app,frameTime);
            const auto position=game->escapeCar_->GetTranslate();
            const auto travel=position-previousPosition;
            const float speed=StageDot(travel,forward)/frameTime;
            Require(speed>=previousSpeed-.01f && std::abs(StageDot(travel,HotelEscape::CarPose{position,roadHeading}.Right()))<.0001f,
                "The car advances without braking or drifting after recovery");
            previousPosition=position;
            previousSpeed=speed;

            const auto baseline=game->escapeCameras_[1].Evaluate(game->escapeTime_-escape.cut).position;
            const auto reaction=game->camera_.GetTranslate()-baseline;
            Require(std::hypot(reaction.x,reaction.z)<.0001f,"Impacts do not jolt the chase camera along the road");
            for (size_t i=0;i<straightImpactLift.size();++i)
                if (game->escapeTime_>cast[i+1].impactTime && game->escapeTime_<cast[i+1].impactTime+.15 && reaction.y>.015f)
                    straightImpactLift[i]=true;
        }
        Require(straightImpactLift[0] && straightImpactLift[1],"Both straight impacts still give the camera a visible lift");
        seek(cast[2].impactTime+.06);
        Require(std::all_of(cast.begin(),cast.end(),[](const auto& entry) { return entry.enemy->IsDead(); }) &&
            StageLength(game->escapeCar_->GetRotate()-roadHeading)<.0001f && slipAngle(game->escapeTime_)<.5f*deg &&
            StageLength(game->camera_.GetTranslate()-game->escapeCameras_[1].Evaluate(game->escapeTime_-escape.cut).position)<.15f && carInFrame(),
            "The rear chase frames both straight impacts");
        captureFrame(straightName);
    };
    const auto capture=[&](const char* name,const Vector3& feet,const Vector3& rotation) {
        game->player_.SetStage(&game->level_.collision,feet,rotation);
        game->player_.RefreshVisuals();
        game->ground_.Update(0);
        game->SyncDoorVisuals();
        game->SyncShutterVisuals();
        game->SyncWeaponVisuals(app);
        for (auto* enemy : game->enemies_) enemy->UpdateVisuals(0);
        captureFrame(name);
    };
    Require(game->escapeCar_ && game->escapeEnemies_.empty(),"Parked car and hidden escape cast");
    const auto parked=game->escapeCar_->GetTranslate();
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
    auto& input=*app.GetInput();
    const auto before=game->CaptureDebug();
    const auto view=game->camera_.GetTranslate();
    const auto poolCount=game->enemyPool_.Active();
    input.keys_[DIK_K]=0x80; input.prevKeys_[DIK_K]=0;
    Require(game->HandleHotelEscapeKey(app),"K preview entry point");
    for (int i=0;i<3;++i) {
        input.keys_[DIK_K]=0x80; input.prevKeys_[DIK_K]=0;
        game->Update(app,.05f);
        Require(game->escapeMode_==GameScene::EscapeMode::Preview && game->escapeEnemies_.size()==3,"K preview and repeated trigger");
    }
    input.keys_[DIK_K]=0;
    Require(game->escapeTime_>=.09 && StageLength(game->escapeCar_->GetTranslate()-parked)<.001f,"Preview was restarted or departed early");
    std::vector<double> previewTimes;
    for (const auto& entry:game->escapeEnemies_) previewTimes.push_back(entry.impactTime);
    while (game->escapeTime_<previewTimes.back()+.05) game->Update(app,1.f/37);
    Require(game->escapeTime_>game->level_.hotelEscape->cut && std::all_of(game->escapeEnemies_.begin(),game->escapeEnemies_.end(),[](const auto& entry) {
        const auto state=entry.enemy->CaptureDebug();
        return entry.enemy->IsDead() && (!state.faces.empty() || !state.detached.empty());
    }),"All three impacts use existing fragmentation during the rear chase");
    const auto impactView=game->camera_.GetTranslate();
    const auto& escape=*game->level_.hotelEscape;
    const float impactRange=StageLength(escape.CarAt(game->escapeTime_).position-impactView);
    game->Update(app,2);
    Require(game->camera_.GetTranslate().y>impactView.y+4 &&
        StageLength(escape.CarAt(game->escapeTime_).position-game->camera_.GetTranslate())>impactRange+4,
        "The chase camera rises and falls back after the impacts");
    game->Update(app,10);
    const auto restored=game->CaptureDebug();
    Require(!game->EscapePlaying() && game->stage_.IsPlaying() && game->escapeEnemies_.empty() &&
        game->enemyPool_.Active()==poolCount && StageLength(game->escapeCar_->GetTranslate()-parked)<.001f,
        "Preview retires cast and parks car");
    Require(StageLength(game->camera_.GetTranslate()-view)<.001f && restored.fov==before.fov &&
        StageLength(restored.player.transform.translate-before.player.transform.translate)<.001f &&
        StageLength(restored.player.transform.rotate-before.player.transform.rotate)<.001f &&
        restored.player.hp==before.player.hp && restored.player.weapon.Magazine()==before.player.weapon.Magazine() &&
        restored.stage.Time()==before.stage.Time() && restored.spawns.ElapsedTime()==before.spawns.ElapsedTime(),"Preview preserves player, stage and spawn state");
    for (size_t i=0;i<restored.enemies.size();++i)
        Require(StageLength(restored.enemies[i].position-before.enemies[i].position)<.001f &&
            restored.enemies[i].random==before.enemies[i].random,"Preview preserves normal enemies");
    input.keys_[DIK_K]=0x80;
    Require(game->HandleHotelEscapeKey(app) && game->escapeTime_==0 && game->escapeEnemies_.size()==previewTimes.size(),
        "K starts another preview with a fresh cast");
    input.keys_[DIK_K]=0;
    for (size_t i=0;i<previewTimes.size();++i)
        Require(game->escapeEnemies_[i].impactTime==previewTimes[i] && !game->escapeEnemies_[i].enemy->IsDead(),
            "Impact schedule is identical on replay");
    for (const auto& wheel:game->escapeCarWheels_)
        Require(StageLength(wheel.visual->GetRotate()-game->level_.hotelEscape->rotation)<.0001f,"Wheel spin resets on replay");
    if (std::wstring_view(GetCommandLineW()).find(L"--escape-launch-capture")!=std::wstring_view::npos) {
        std::filesystem::create_directories("generated/upper-hotel-tests/escape-launch-frames");
        game->Update(app,.25f);
        for (int frame=0;frame<216;++frame) {
            const auto name="escape-launch-frames/frame-"+std::to_string(1000+frame).substr(1);
            captureFrame(name.c_str());
            game->Update(app,1.f/60);
        }
    }
    game->Update(app,10);
    Require(!game->EscapePlaying() && game->stage_.IsPlaying() && game->enemyPool_.Active()==poolCount,
        "A slow preview frame still completes and releases the cast");
#endif
    const auto shootWindow=[&](std::string_view name,const Vector3& feet,float yaw) {
        const auto& windows=game->level_.removableMeshes;
        const auto window=std::find_if(windows.begin(),windows.end(),
            [&](const auto& entry) { return entry.meshNode==name; });
        Require(window!=windows.end(),"missing breakable window: "+std::string(name));
        auto& collider=game->level_.collision.colliders[window->colliderIndex];
        Require(collider.enabled,"pane was already broken");
        game->player_.SetStage(&game->level_.collision,feet,{0,yaw,0});
        game->player_.RefreshVisuals();
        auto weapon=game->player_.CurrentWeapon().Definition();
        weapon.hipSpreadDegrees=weapon.adsSpreadDegrees=0;
        game->bullets_.Spawn(weapon,game->camera_.GetWorldMatrix(),0,game->pelletRandom_,game->level_.collision,game->enemies_);
        Require(collider.enabled,"aiming or bullet spawn removed glass before impact");
        game->UpdatePlayerBullets(.1f);
        Require(!collider.enabled,"player bullet did not break "+std::string(name));
        const auto& hidden=game->ground_.GetHiddenMeshes();
        const auto binding=static_cast<size_t>(window-windows.begin());
        Require(std::find(hidden.begin(),hidden.end(),game->removableMeshBindings_[binding].front())!=hidden.end(),
            "broken pane still rendered");
    };
    const auto countEnemies=[&](std::string_view source) {
        return std::count_if(game->enemies_.begin(),game->enemies_.end(),[&](const Enemy* enemy) {
            return enemy->GetSpawnTriggerId().starts_with(source);
        });
    };
    const auto breachHidden=[&](std::string_view name) {
        const auto& meshes=game->level_.removableMeshes;
        const auto mesh=std::find_if(meshes.begin(),meshes.end(),[&](const auto& item) { return item.meshNode==name; });
        Require(mesh!=meshes.end() && !mesh->breakOnBullet,"missing event-only breach mesh");
        const auto binding=static_cast<size_t>(mesh-meshes.begin());
        const auto& hidden=game->ground_.GetHiddenMeshes();
        return std::find(hidden.begin(),hidden.end(),game->removableMeshBindings_[binding].front())!=hidden.end();
    };
    const Vector3 startDoor{6.35f,UpperHotelChecks::Upper,36.5f};
    capture("01-start-door",startDoor,{0,1.5708f,0});
    UpperHotelChecks::WalkRoute(game->level_,game->stage_,[&](const Vector3& feet,const std::string& name) {
        game->player_.SetStage(&game->level_.collision,feet,{});
        const auto index=game->level_.doors.Nearest(game->level_.collision,feet);
        Require(index && game->level_.doors.doors[*index].closed.id==name,"E target "+name);
        auto& door=game->level_.doors.doors[*index];
        game->HandleInteraction(false,true);
        Require(!door.targetOpen,"door moved before E");
        game->HandleInteraction(true,true);
        for (int i=0;i<30;++i) game->UpdateDoors(1.f/60);
        Require(door.targetOpen && door.angle==door.openAngle,"E opened "+name);
    },[&](std::string_view part,const Vector3& feet) {
        game->player_.SetStage(&game->level_.collision,feet,{});
        game->UpdateEnemySpawns(1.f/60);
        if (part=="item") {
            Require(game->HandleInteraction(true,true),"E failed to pick up the starter Sub");
            shootWindow("F5_Room516_WindowGlass",feet,1.5707963f);
            capture("07-fifth-floor-lounge",{14.5f,UpperHotelChecks::Upper,38},{0,-2.9f,0});
        } else if (part=="corner") {
            Require(game->enemies_.size()==8 && countEnemies("F5_")==3,"5F enemies are present before the corner");
            capture("02-fifth-floor-corner",feet,{0,1.5708f,0});
        } else if (part=="arrival") {
            Require(game->enemies_.size()==8 && countEnemies("F4_")==1,"4F room enemy is present before arrival");
            capture("03-fourth-floor-fire",{42,UpperHotelChecks::Lower,9.5f},{0,-1.5708f,0});
        } else if (part=="detour") {
            capture("08-fifth-floor-room-detour",feet,{0,1.5708f,0});
        } else if (part=="ledge") {
            capture("04-exterior-ledge",feet,{.18f,-1.75f,0});
        } else if (part=="ledge-extension") {
            capture("09-fourth-floor-ledge-extension",feet,{.12f,1.75f,0});
        } else if (part=="elevator") {
            capture("10-fourth-floor-elevator",feet,{});
        } else if (part=="transfer") {
            Require(countEnemies("F1_ArrivalEnemy_")==4 && countEnemies("F1_Defense_")==0 &&
                !game->transferUsed_,"lobby enemies exist before transfer; defense has not started");
            const auto weapon=game->player_.CurrentWeapon();
            game->player_.ApplyDamage(8);
            const auto hp=game->player_.GetHP();
            game->Update(app,1.f/60);
            const auto& equipped=game->player_.CurrentWeapon();
            Require(game->transferUsed_ && game->stage_.IsPlaying() &&
                StageLength(game->player_.GetTransform().translate-game->level_.transfer->arrivalPosition)<.01f,
                "entering 4F cabin transfers to 1F");
            Require(equipped.Definition().id==weapon.Definition().id && equipped.Magazine()==weapon.Magazine() &&
                equipped.Reserve()==weapon.Reserve() && game->player_.GetHP()==hp,"transfer must retain inventory and HP");
            Require(countEnemies("F1_ArrivalEnemy_")==4 && countEnemies("F1_Defense_")==0,
                "1F arrival must not spawn a duplicate group");
            for (const auto* enemy : game->enemies_)
                Require(enemy->AI().paused==!enemy->GetSpawnTriggerId().starts_with("F1_"),
                    "only enemies left upstairs are paused after transfer");
            capture("17-first-floor-elevator-arrival",game->level_.transfer->arrivalPosition,
                game->level_.transfer->arrivalRotation);
        } else if (part=="lobby") {
            capture("11-first-floor-lobby",feet,{0,1.32f,0});
        } else if (part=="stair-collapse") {
            capture("15-fourth-floor-stair-collapse",{50.5f,UpperHotelChecks::Lower,10.6f},{.12f,3.14159f,0});
        } else if (part=="stairwell") {
            capture("16-first-floor-stairwell",feet,{-.16f,0,0});
        } else if (part=="regular") {
            Require(countEnemies("F1_Defense_")==3 && countEnemies("F1_Defense_Breach")==0,
                "first wave comes only from three regular sources");
            const auto stair=std::find_if(game->enemies_.begin(),game->enemies_.end(),[](const Enemy* enemy) {
                return enemy->GetSpawnTriggerId()=="F1_Defense_Stairs";
            });
            Require(stair!=game->enemies_.end() && std::abs((*stair)->GetPosition().x-50.5f)<.02f,
                "stair reinforcements spawn on the connected bottom flight");
            capture("12-first-floor-shutter",{42,.08f,16.5f},{0,1.5708f,0});
        } else if (part=="breachA") {
            Require(countEnemies("F1_Defense_BreachA")==1 && countEnemies("F1_Defense_BreachB")==0 &&
                breachHidden("F1_BreachA_Panel") && !breachHidden("F1_BreachB_Panel"),"A opening and spawn at 12s");
            capture("13-first-floor-breach-a",{23,.08f,9},{0,3.14159f,0});
        } else if (part=="breachB") {
            Require(countEnemies("F1_Defense_BreachB")==1 && countEnemies("F1_Defense_BreachC")==0 &&
                breachHidden("F1_BreachB_Panel") && !breachHidden("F1_BreachC_Panel"),"B opening and spawn at 20s");
        } else if (part=="breachC") {
            Require(countEnemies("F1_Defense_BreachC")==1 && breachHidden("F1_BreachC_Panel"),"C opening and spawn at 26s");
        } else if (part=="lastWave") {
            for (const auto* name : {"Stairs","Staff","Side"})
                Require(countEnemies(std::string("F1_Defense_")+name)==4,"regular source count");
            Require(countEnemies("F1_Defense_BreachA")==3 && countEnemies("F1_Defense_BreachB")==2 &&
                countEnemies("F1_Defense_BreachC")==2 && countEnemies("F1_Defense_")==19,"all six sources in final wave");
        } else if (part=="open") {
            capture("14-first-floor-exit-open",{42,.08f,16.5f},{0,1.5708f,0});
        } else if (part=="goal") {
            const auto defeated=game->stage_.DefeatedCount();
            game->Update(app,1.f/60);
            Require(game->stage_.State()==StageState::Cleared && game->EscapePlaying() && countEnemies("F1_Defense_")==19,
                "walk-through goal without another encounter or a kill requirement");
            if (std::wstring_view(GetCommandLineW()).find(L"--escape-camera-capture")!=std::wstring_view::npos) {
                std::filesystem::create_directories("generated/upper-hotel-tests/escape-camera-frames");
                for (int frame=0;frame<450;++frame) {
                    const auto name="escape-camera-frames/frame-"+std::to_string(1000+frame).substr(1);
                    captureFrame(name.c_str());
                    if (game->EscapePlaying()) game->Update(app,1.f/60);
                }
            } else {
                game->Update(app,.55f); captureFrame("escape-01-ready");
                verifyLaunch("escape-stage04-launch","escape-stage04-slide-impact","escape-stage04-straight-impact");
                game->Update(app,.65f); captureFrame("escape-03-road");
                game->Update(app,4); captureFrame("escape-04-result");
            }
            Require(game->escapeMode_==GameScene::EscapeMode::Results && game->escapeEnemies_.empty() &&
                game->stage_.DefeatedCount()==defeated,"Clear screen keeps gameplay kill count");
        } else if (part.starts_with("window")) {
            const bool finalPane=part=="window403";
            const auto name="F4_Room"+std::string(part.substr(6))+"_WindowGlass";
            if (finalPane) capture("05-window-intact",feet,{});
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
            if (part=="window405") {
                const auto intact=game->CaptureDebug();
                const auto previouslyHidden=game->ground_.GetHiddenMeshes();
                shootWindow(name,feet,0);
                game->RestoreDebug(app,intact);
                Require(game->ground_.GetHiddenMeshes()==previouslyHidden,"rewind restores the hit pane");
            }
#endif
            shootWindow(name,feet,0);
            if (finalPane) capture("06-window-broken",feet,{});
        }
    },[&](const Vector3& feet) {
        game->player_.SetStage(&game->level_.collision,feet,{});
        game->HandleInteraction(true,true);
        game->ProcessShutterEvents();
    },[&]() { game->ProcessShutterEvents(); });
    Require(game->stage_.State()==StageState::Cleared,"1F final goal");
    game->player_.SetStage(&game->level_.collision,startDoor,{});
    const auto index=game->level_.doors.Nearest(game->level_.collision,startDoor);
    Require(index.has_value(),"open door must still be reachable");
    game->HandleInteraction(true,true);
    for (int i=0;i<30;++i) game->UpdateDoors(1.f/60);
    Require(!game->level_.doors.doors[*index].targetOpen && game->level_.doors.doors[*index].angle==0,"E closes door");

    const auto fallToDeath=[&](const Vector3& from) {
        game->stage_.Reset();
        game->player_.SetStage(&game->level_.collision,from,{});
        for (int i=0;i<90 && !game->gameOver_.Active();++i) game->Update(app,1.f/60);
        Require(game->player_.IsDead() && game->gameOver_.Active(),"fall must allow a retry");
    };
    fallToDeath({11,UpperHotelChecks::Upper,-.83f});
    Require(game->player_.GetTransform().translate.y>UpperHotelChecks::Lower,"5F fall only triggered below 4F");

    app.Scenes().Change(app,game->SceneName());
    game=dynamic_cast<GameScene*>(app.Scenes().Current());
    Require(game && game->stageLoaded_ && game->stage_.IsPlaying() && game->enemies_.size()==8 &&
        StageLength(game->player_.GetTransform().translate-game->level_.playerPosition)<.001f &&
        game->weapons_.Pickups().size()==1 && game->weapons_.Pickups()[0].visible,"Restart resets upper hotel");
    for (const auto& door : game->level_.doors.doors)
        Require(door.targetOpen==door.initiallyOpen && door.angle==(door.initiallyOpen ? door.openAngle : 0),
            "Restart resets authored state for "+door.closed.id);
    for (const auto& mesh : game->level_.removableMeshes)
        Require(game->level_.collision.colliders[mesh.colliderIndex].enabled,"Restart restores glass and breach collision");
    Require(game->ground_.GetHiddenMeshes().empty(),"Restart restores glass meshes");
    Require(!game->EscapePlaying() && game->escapeEnemies_.empty() &&
        StageLength(game->escapeCar_->GetTranslate()-parked)<.001f,"Restart resets escape car and cast");
    Require(!game->transferUsed_ && game->level_.shutters.shutters.front().state==ShutterState::Closed &&
        countEnemies("F1_ArrivalEnemy_")==4 && countEnemies("F1_Defense_")==0,
        "Restart restores the initial lobby enemies and resets defense");
    const Vector3 outsideLedge{11,UpperHotelChecks::Lower,-2.2f};
    game->ConfigurePlacementTest(EnemyPlacements{});
    game->player_.SetStage(&game->level_.collision,outsideLedge,{});
    for (int i=0;i<90 && !game->PlacementTestFinished();++i) game->Update(app,1.f/60);
    Require(game->player_.IsDead() && game->PlacementTestFinished(),"F7 test play must end after a fatal fall");
    const auto stopped=game->player_.GetTransform().translate;
    game->Update(app,1.f/60);
    Require(StageLength(game->player_.GetTransform().translate-stopped)==0,"F7 test continued after fall death");
    game->placementTest_.reset();
    game->player_.ResetHPForDebug();
    fallToDeath(outsideLedge);

    app.Scenes().Change(app,"Stage03");
    game=dynamic_cast<GameScene*>(app.Scenes().Current());
    Require(game && game->stageLoaded_ && game->escapeCar_ && !game->escapeHiddenMeshes_.empty(),"Stage03 escape configuration");
    const auto lowerParked=game->escapeCar_->GetTranslate();
    const auto hidden=game->ground_.GetHiddenMeshes();
    game->player_.SetStage(&game->level_.collision,{-16,0,18},{});
    game->stage_.ObserveEnemy(10000,true);
    game->Update(app,0);
    Require(game->EscapePlaying() && game->escapeEnemies_.size()==3 &&
        game->ground_.GetHiddenMeshes().size()>hidden.size(),"Stage03 Goal clears the cinematic road");
    game->Update(app,.55f); captureFrame("escape-stage03-01-ready");
    verifyLaunch("escape-stage03-launch","escape-stage03-slide-impact","escape-stage03-straight-impact");
    game->Update(app,.65f); captureFrame("escape-stage03-03-road");
    game->Update(app,4);
    Require(game->escapeMode_==GameScene::EscapeMode::Results && game->stage_.DefeatedCount()==1 &&
        game->ground_.GetHiddenMeshes()==hidden && game->escapeEnemies_.empty(),"Stage03 preserves kills and retires cast");
    app.Scenes().Change(app,"Stage03");
    game=dynamic_cast<GameScene*>(app.Scenes().Current());
    Require(game && game->escapeCar_ && StageLength(game->escapeCar_->GetTranslate()-lowerParked)<.001f &&
        game->escapeEnemies_.empty() && !game->EscapePlaying(),"Stage03 restart parks car");
    app.Scenes().Change(app,"Game");
    game=dynamic_cast<GameScene*>(app.Scenes().Current());
    Require(game && game->stageLoaded_ && !game->escapeCar_,"Stage01 has no escape sequence");
    std::ofstream("generated/upper-hotel-tests/result.txt")<<
        "PASS: Stage04 route, transfer and six ingress routes. Stage03 and Stage04 Goal, front-quarter rev, head-on launch, steady straight drive and rear chase through all three impacts, results and restart. Stage01 isolated. "
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
        "K trigger with injected input, repeat guard, deterministic impacts, debris and state restoration verified."
#endif
        "\n";
}
