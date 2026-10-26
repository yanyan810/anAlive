#include "GameOverSequence.h"
#include "FirstPersonCamera.h"
#include <numbers>

namespace {
constexpr float pi=std::numbers::pi_v<float>;
Vector3 Mix(const Vector3& a,const Vector3& b,float t) { return a+(b-a)*t; }
Vector3 RollView(const Vector3& view,float roll,const Vector3& previous) {
    // Engine Euler order is X*Y*Z: adding Z would roll around world Z and
    // steer the view differently at each heading. Roll around local forward.
    const auto m=Matrix4x4::Multiply(Matrix4x4::RotateZ(roll),Matrix4x4::RotateXYZ(view.x,view.y,view.z));
    const float y=std::asin(std::clamp(-m.m[0][2],-1.0f,1.0f));
    Vector3 a{0,y,std::atan2(-m.m[1][0],m.m[1][1])};
    if (std::abs(std::cos(y))>1e-5f) {
        a.x=std::atan2(m.m[1][2],m.m[2][2]); a.z=std::atan2(m.m[0][1],m.m[0][0]);
    }
    Vector3 b{a.x+pi,pi-a.y,a.z+pi};
    const auto unwrap=[&](const Vector3& r) {
        return previous+Vector3{std::remainder(r.x-previous.x,2*pi),
            std::remainder(r.y-previous.y,2*pi),std::remainder(r.z-previous.z,2*pi)};
    };
    a=unwrap(a); b=unwrap(b);
    return StageLength(a-previous)<=StageLength(b-previous) ? a : b;
}
}

void GameOverSequence::Reset() {
    body_.reset(); state_=State::Inactive; death_={}; ai_={}; floorMotion_={};
    elapsed_=wanderTime_=0; phaseTime_=0; position_={}; bodyVisible_=false;
}

void GameOverSequence::Begin(Object3dCommon* common,DirectXCommon* dx,Camera* camera,
    const EnemyDefinition* normal,const PlayerVisualSettings& visual,const DeathContext& death) {
    if (Active()) return;
    Reset(); death_=death; visual_=visual; position_=death.player.translate;
    // Keep the last rendered viewing direction, including ADS/pitch and wrapped yaw.
    heading_=death.camera.rotation.y;
    cameraPosition_=death.camera.position; cameraRotation_=death.camera.rotation;
    body_=std::make_unique<StartPlayerVisual>();
    body_->Initialize(common,dx,camera,normal,death.eyeHeight);
    // Native Normal forward is -X. A local Z tilt falls along its forward axis.
    auto pose=settings.pose;
    if (pose==DeathPose::FromImpact) {
        const Vector3 forward{std::sin(heading_),0,std::cos(heading_)};
        pose=StageDot(death.impactDirection,forward)>0 ? DeathPose::Prone : DeathPose::Supine;
    }
    switch (pose) {
    case DeathPose::Prone: fallRotation_={0,0,pi*.5f}; break;
    case DeathPose::LeftSide: fallRotation_={pi*.5f,0,0}; break;
    case DeathPose::RightSide: fallRotation_={-pi*.5f,0,0}; break;
    default: fallRotation_={0,0,-pi*.5f}; break;
    }
    rollSign_=pose==DeathPose::LeftSide || pose==DeathPose::Prone ? -1.0f : 1.0f;
    ai_={};
    if (normal) ai_.settings.senses=normal->senses;
    ai_.settings.moveSpeed=std::max(0.0f,settings.wanderSpeed);
    ai_.settings.senses.wanderSpeedMultiplier=1;
    ai_.settings.senses.wanderRadius=std::max(.5f,settings.wanderRadius);
    // This actor has no combat target. Retain EnemyAI's normal Wander/path logic.
    ai_.settings.senses.viewDistance=0;
    ai_.hasLastSeen=true; ai_.lastSeen=position_; ai_.BeginWander(position_);
    state_=State::Death;
    camera->SetNearZ(.05f);
}

float GameOverSequence::Duration() const { return Duration(state_); }

float GameOverSequence::Duration(State state) const {
    float d=0;
    switch (state) {
    case State::Death: d=settings.staggerSeconds; break;
    case State::Collapse: d=settings.collapseSeconds; break;
    case State::FloorHold: d=settings.floorHoldSeconds; break;
    case State::Closing: d=settings.closeSeconds; break;
    case State::Corpse: d=settings.corpseSeconds; break;
    case State::Opening: d=settings.openSeconds; break;
    case State::Awake: d=settings.awakeSeconds; break;
    case State::Rising: d=settings.riseSeconds; break;
    case State::Possess: d=settings.possessSeconds; break;
    default: return std::numeric_limits<float>::infinity();
    }
    return std::isfinite(d) ? std::max(.01f,d) : .01f;
}

float GameOverSequence::MenuDelay() const {
    float time=0;
    for (auto state:{State::Death,State::Collapse,State::FloorHold,State::Closing,State::Corpse,State::Opening,State::Awake,State::Rising,State::Possess}) time+=Duration(state);
    return time;
}

GameOverSequence::VisionState GameOverSequence::Vision() const {
    // Keep the entire fall and the following floor hold fully visible.
    if (state_==State::Closing) {
        const float t=std::clamp(static_cast<float>(phaseTime_/Duration()),0.0f,1.0f);
        const float closed=1-std::pow(1-t,std::clamp(settings.closeCurve,1.0f,4.0f));
        return {closed,1-closed}; // Ease-out: eyelids slow as consciousness fades.
    }
    if (state_==State::Corpse) return {1,0};
    if (state_!=State::Opening) return {};
    const float opening=Ease(static_cast<float>(phaseTime_/Duration()),settings.openCurve);
    return {1-opening,opening}; // A single irreversible awakening, without a human blink.
}

float GameOverSequence::RisePauseStart() const {
    return std::clamp(settings.risePauseStartSeconds/Duration(State::Rising),.02f,.95f);
}

DeathPostEffectParameters GameOverSequence::PostEffect() const {
    const auto& s=settings.post;
    DeathPostEffectParameters result;
    // Black eyelids already cover Corpse; skip scene processing entirely there.
    if (!s.enabled || !Active() || state_==State::Corpse || state_==State::Wander) return result;
    const float t=std::clamp(static_cast<float>(phaseTime_/Duration()),0.0f,1.0f);
    const auto blend=[](float a,float b,float p) { return a+(b-a)*p; };
    const auto ramp=[&](float p,float start) { start=std::clamp(start,0.0f,.95f); return Ease((p-start)/(1-start),s.curve); };
    const auto pulse=[](float time,float delay,float seconds) {
        if (!std::isfinite(seconds) || seconds<=0) return 0.0f;
        const float p=(time-std::max(0.0f,delay))/seconds;
        if (p<=0 || p>=1) return 0.0f;
        const float v=std::sin(pi*p); return v*v;
    };
    // Keep the death tint underneath the opaque eyelids until full closure.
    // Opening starts with the existing black vignette; never revive the red.
    const float red=state_<=State::Closing ? 1.0f : 0.0f;
    if (state_==State::Death) {
        const float fadeSeconds=std::clamp(s.staggerVignetteFadeSeconds,.01f,Duration(State::Death));
        result.vignette=s.staggerVignette*Ease(static_cast<float>(phaseTime_)/fadeSeconds,s.curve);
        const float available=std::max(0.0f,Duration(State::Death)-std::max(0.0f,s.staggerPulseDelay));
        result.chromaticPixels=s.staggerChromaticPixels*pulse(static_cast<float>(phaseTime_),s.staggerPulseDelay,std::min(s.staggerPulseSeconds,available));
    } else if (state_==State::Collapse) {
        const float fall=CollapseProgress(), p=Ease(fall,s.curve);
        result.vignette=blend(s.staggerVignette,s.collapseVignette,p);
        result.desaturation=s.collapseDesaturation*ramp(fall,s.desaturationStart);
        result.blurPixels=s.collapseBlurPixels*ramp(fall,s.blurStart);
    } else if (state_==State::FloorHold || state_==State::Closing) {
        const float p=state_==State::Closing ? Ease(t,s.curve) : 0;
        result.vignette=blend(s.collapseVignette,s.closingVignette,p);
        result.desaturation=blend(s.collapseDesaturation,s.closingDesaturation,p);
        result.blurPixels=blend(s.collapseBlurPixels,s.closingBlurPixels,p);
    } else {
        const float p=state_==State::Opening ? Ease(t,s.curve) : 1;
        result.vignette=blend(s.closingVignette,s.openingVignette,p);
        result.desaturation=blend(s.closingDesaturation,s.openingDesaturation,p);
        result.blurPixels=blend(s.closingBlurPixels,s.openingBlurPixels,p);
        float remaining=1;
        if (state_==State::Rising) remaining=1-ramp(t,s.recoveryStart)*std::clamp(s.riseRecovery,0.0f,1.0f);
        if (state_==State::Possess) remaining=(1-std::clamp(s.riseRecovery,0.0f,1.0f))*(1-Ease(t,s.curve));
        result.vignette*=remaining; result.desaturation*=remaining; result.blurPixels*=remaining;
    }
    const float impact=pulse(LandingTime(),s.landingPulseDelay,s.landingPulseSeconds);
    result.chromaticPixels+=s.landingChromaticPixels*impact;
    result.blurPixels+=s.landingBlurPixels*impact;
    result.vignetteColor={s.red.x*red,s.red.y*red,s.red.z*red,1};
    result.vignette=s.vignetteEnabled ? std::clamp(result.vignette,0.0f,.85f) : 0;
    result.desaturation=s.desaturationEnabled ? std::clamp(result.desaturation,0.0f,1.0f) : 0;
    result.blurPixels=s.blurEnabled ? std::clamp(result.blurPixels,0.0f,3.0f) : 0;
    result.chromaticPixels=s.chromaticEnabled ? std::clamp(result.chromaticPixels,0.0f,2.0f) : 0;
    result.edgeInner=std::clamp(s.edgeInner,0.0f,.95f);
    result.edgeOuter=std::clamp(s.edgeOuter,result.edgeInner+.05f,1.5f);
    if (s.mosaicEnabled && state_>=State::Opening) {
        // The envelope and slow pattern motion share the existing phase clock.
        double awakeTime=phaseTime_;
        for (int i=static_cast<int>(State::Opening);i<static_cast<int>(state_);++i) awakeTime+=Duration(static_cast<State>(i));
        const auto finite=[](float value,float fallback) { return std::isfinite(value) ? value : fallback; };
        const float risingAt=Duration(State::Opening)+Duration(State::Awake);
        const float start=std::clamp(finite(s.mosaicStartSeconds,.35f),0.0f,Duration(State::Opening)-.01f);
        const float peak=std::clamp(finite(s.mosaicPeakSeconds,1.95f),start+.01f,risingAt);
        // Even an excessively long configured fade finishes before Wander.
        const float decay=std::clamp(finite(s.mosaicDecaySeconds,3.5f),.01f,Duration(State::Rising)+Duration(State::Possess));
        const float time=static_cast<float>(awakeTime);
        result.mosaicStrength=std::clamp(finite(s.mosaicStrength,0),0.0f,1.0f)
            *Smooth((time-start)/(peak-start))*(1-Smooth((time-risingAt)/decay));
        result.mosaicCellSize=std::clamp(finite(s.mosaicCellSize,.006f),.001f,.05f);
        result.mosaicColorStrength=std::clamp(finite(s.mosaicColorStrength,0),0.0f,1.0f);
        result.mosaicTime=time;
        result.mosaicChangeRate=std::clamp(finite(s.mosaicChangeRate,0),0.0f,2.0f);
    }
    return result;
}
float GameOverSequence::RisePauseEnd() const {
    return std::clamp(RisePauseStart()+std::max(0.0f,settings.risePauseSeconds)/Duration(State::Rising),RisePauseStart(),.98f);
}
float GameOverSequence::RiseHeadEnd() const {
    // Leave a nonzero interpolation interval even when pause timing is moved
    // before the configured head motion ends and the two heights differ.
    return std::clamp(settings.riseHeadEnd,.01f,RisePauseStart()-.001f);
}

float GameOverSequence::CollapseProgress() const {
    const float t=std::clamp(static_cast<float>(phaseTime_/Duration(State::Collapse)),0.0f,1.0f);
    const float curve=std::clamp(settings.collapseCurve,2.0f,5.0f);
    // Zero velocity at both ends; accelerate until late in the fall, then brake.
    return std::pow(t,curve)*(curve+1-curve*t);
}

float GameOverSequence::LandingTime() const {
    if (state_<State::FloorHold) return -1;
    double time=phaseTime_;
    for (int i=static_cast<int>(State::FloorHold);i<static_cast<int>(state_);++i) time+=Duration(static_cast<State>(i));
    return static_cast<float>(time);
}

float GameOverSequence::RiseProgress() const {
    const float t=std::clamp(static_cast<float>(phaseTime_/Duration()),0.0f,1.0f);
    const float head=RiseHeadEnd(),pause=RisePauseStart(),resume=RisePauseEnd();
    const float lift=std::clamp(settings.riseHeadLift,0.0f,.2f);
    const float height=std::clamp(settings.risePauseHeight,lift,.9f);
    if (t<head) return lift*Ease(t/head,settings.riseCurve);
    if (t<pause) return lift+(height-lift)*Ease((t-head)/(pause-head),settings.riseCurve);
    if (t<resume) return height;
    return height+(1-height)*Ease((t-resume)/(1-resume),settings.riseCurve);
}

void GameOverSequence::Advance() {
    phaseTime_=0;
    state_=static_cast<State>(static_cast<int>(state_)+1);
    if (state_==State::Wander) { ai_.BeginWander(position_); ai_.wanderWait=0; }
}

void GameOverSequence::Step(float dt,const StageWorld& world) {
    if (state_!=State::Wander) {
        // Reuse the player's swept floor/gravity solver for airborne deaths.
        position_=world.MovePlayer(position_,position_,dt,floorMotion_,settings.collisionRadius,death_.eyeHeight+.18f);
    }
    if (state_==State::Wander) {
        wanderTime_+=dt;
        auto rotation=Vector3{0,heading_+pi*.5f,0};
        const auto before=position_;
        ai_.Update(position_,rotation,position_+Vector3{0,-10000,0},dt,false,&world,
            settings.collisionRadius,death_.eyeHeight+.18f,death_.eyeHeight);
        position_=world.Move(before,position_,settings.collisionRadius,death_.eyeHeight+.18f);
        const float target=rotation.y-pi*.5f;
        heading_+=std::remainder(target-heading_,2*pi)*(1-std::exp(-dt*4));
    }
}

void GameOverSequence::Update(float dt,const StageWorld& world,Camera& camera) {
    if (!Active()) return;
    dt=std::isfinite(dt) ? std::max(0.0f,dt) : 0;
    // Fixed maximum integration step; phase boundaries consume leftover time.
    // Bound pathological loading deltas without discarding normal frame time.
    float remaining=std::min(dt,30.0f);
    while (remaining>0) {
        const float step=std::min({remaining,1.0f/60,static_cast<float>(Duration()-phaseTime_)});
        Step(step,world); elapsed_+=step; phaseTime_+=step; remaining=std::max(0.0f,remaining-step);
        if (phaseTime_>=Duration()-1e-6f && state_!=State::Wander) Advance();
        // Sweep along the entire fall/rise path, also when one frame spans phases.
        UpdateCamera(world);
    }
    if (dt==0) UpdateCamera(world);
    Present(world,camera);
}

void GameOverSequence::UpdateCamera(const StageWorld& world) {
    const float p=Smooth(static_cast<float>(phaseTime_/Duration()));
    const Vector3 forward{std::sin(death_.camera.rotation.y),0,std::cos(death_.camera.rotation.y)};
    const Vector3 right{forward.z,0,-forward.x};
    const auto startEye=position_+(death_.camera.position-death_.player.translate);
    const auto loweredEye=startEye+Vector3{0,-std::max(0.0f,settings.staggerDip),0};
    const float radius=std::max(.1f,settings.cameraRadius);
    const auto floorEye=position_+right*(settings.fallDistance*rollSign_)+
        Vector3{0,std::max(radius+.04f,settings.floorEyeHeight),0};
    const auto eye=position_+Vector3{0,death_.eyeHeight,0};
    auto deathRotation=death_.camera.rotation, floorRotation=death_.camera.rotation;
    // Do not cross the vertical pole and reverse the horizontal view when the
    // player dies while looking almost straight up/down.
    deathRotation.x=std::clamp(deathRotation.x+settings.staggerPitch,-1.553343f,1.553343f);
    floorRotation.x=std::clamp(floorRotation.x+.26f,-1.553343f,1.553343f);
    const Vector3 risenRotation{death_.camera.rotation.x*.35f,death_.camera.rotation.y,0};
    Vector3 wanted{},view{}; float roll=0;
    if (state_==State::Death) {
        // A brief upright brace. The pulse vanishes at both boundaries, so the
        // fall starts from the same slightly lowered eye and viewing direction.
        wanted=Mix(startEye,loweredEye,p);
        view=Mix(death_.camera.rotation,deathRotation,p);
        const float t=static_cast<float>(phaseTime_/Duration());
        const float pulse=std::pow(std::sin(pi*t),2.0f);
        const float wave=2*pi*std::max(0.0f,settings.staggerShakeHz)*static_cast<float>(phaseTime_);
        view.x+=std::sin(wave)*settings.staggerShake*.5f*pulse;
        roll=std::sin(wave+pi*.25f)*settings.staggerShake*pulse;
    } else if (state_==State::Collapse) {
        const float fall=CollapseProgress();
        wanted=Mix(loweredEye,floorEye,fall);
        view=Mix(deathRotation,floorRotation,fall);
        roll=settings.fallenRoll*fall*rollSign_;
    } else if (state_==State::FloorHold || state_==State::Closing || state_==State::Corpse || state_==State::Opening || state_==State::Awake || state_==State::Rising) {
        const float rise=state_==State::Rising ? RiseProgress() : 0;
        wanted=Mix(floorEye,eye,rise);
        view=Mix(floorRotation,risenRotation,rise); roll=settings.fallenRoll*rollSign_*(1-rise);
        if (state_==State::Rising) {
            const float head=RiseHeadEnd();
            const float t=static_cast<float>(phaseTime_/Duration());
            const float motion=t<head ? std::sin(pi*Smooth(t/head)) : 0;
            view.x=std::clamp(view.x+settings.riseHeadPitch*motion,-1.553343f,1.553343f);
            roll+=settings.riseHeadRoll*motion*rollSign_;
        }
    } else {
        wanted=eye;
        view=state_==State::Possess ? Mix(risenRotation,{0,heading_,0},p) : Vector3{0,heading_,0};
        const float settling=state_==State::Possess ? p : std::exp(-wanderTime_/std::max(.01f,settings.settleSeconds));
        view+=Vector3{std::sin(elapsed_*7)*settings.shake*settling,
            std::sin(elapsed_*3.7f)*settings.shake*.5f*settling,0};
        roll=std::sin(elapsed_*5)*settings.shake*settling;
    }
    // Contact shake uses one clock across floor hold/closing/etc. Shortening
    // floorHoldSeconds never truncates the pulse or snaps the camera at Closing.
    const float contact=LandingTime();
    if (std::isfinite(settings.landingSeconds) && settings.landingSeconds>0 && contact>=0 && contact<settings.landingSeconds) {
        const float t=contact/settings.landingSeconds;
        const float envelope=std::pow(std::sin(pi*t),2.0f)*(1-t);
        const float wave=2*pi*std::max(0.0f,settings.landingShakeHz)*contact;
        wanted.y+=std::max(0.0f,settings.landingLift)*envelope;
        view.x+=std::sin(wave)*settings.landingShake*envelope;
        roll+=std::cos(wave)*settings.landingShake*.65f*envelope*rollSign_;
    }
    view.x=std::clamp(view.x,-1.553343f,1.553343f);
    cameraRotation_=RollView(view,roll,cameraRotation_);
    cameraPosition_=FirstPersonCamera::Move(world,cameraPosition_,wanted,radius);
}

void GameOverSequence::Present(const StageWorld&,Camera& camera) {
    camera.SetTranslate(cameraPosition_); camera.SetRotate(cameraRotation_);
    camera.SetFovY(death_.camera.fov+(pi/3-death_.camera.fov)*Smooth(elapsed_/Duration(State::Death)));
    camera.Update();
    const float rise=state_==State::Rising ? RiseProgress() : 0;
    const float tilt=state_==State::Collapse ? CollapseProgress() : state_==State::FloorHold || state_==State::Closing || state_==State::Corpse || state_==State::Opening || state_==State::Awake ? 1 : state_==State::Rising ? 1-rise : 0;
    const float whiten=state_==State::Rising ? rise : state_==State::Possess || state_==State::Wander ? 1 : 0;
    body_->UpdatePose(position_,{fallRotation_.x*tilt,visual_.WorldYaw(heading_),fallRotation_.z*tilt},
        {.22f+.78f*whiten,.28f+.72f*whiten,.36f+.64f*whiten,1});
    // The same reanimation actor/AI exists throughout, but its full body is never
    // rendered into its own eyes. No distance-dependent model pop or head clipping.
    bodyVisible_=false;
}

void GameOverSequence::Draw() { if (body_ && bodyVisible_) body_->Draw(); }
