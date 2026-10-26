#pragma once
#include "StartPlayerVisual.h"
#include "EnemyAI.h"
#include "CameraSequence.h"
#include "DeathPostEffect.h"
#include <memory>

// Scene-local presentation actor: never registered with EnemyPool/StageProgress.
class GameOverSequence {
public:
    enum class State { Inactive, Death, Collapse, FloorHold, Closing, Corpse, Opening, Awake, Rising, Possess, Wander };
    enum class DeathPose { FromImpact, Supine, Prone, LeftSide, RightSide };
    struct PostSettings {
        bool enabled=true, vignetteEnabled=true, desaturationEnabled=true, blurEnabled=true, chromaticEnabled=true;
        Vector3 red{.10f,.004f,.010f}; // Linear RGB, deliberately dark.
        float staggerVignette=.28f, collapseVignette=.36f, closingVignette=.50f;
        float collapseDesaturation=.50f, closingDesaturation=.72f;
        float collapseBlurPixels=.65f, closingBlurPixels=.90f;
        float openingVignette=.16f, openingDesaturation=.24f, openingBlurPixels=.35f;
        float staggerChromaticPixels=.65f, landingChromaticPixels=.75f, landingBlurPixels=.40f;
        float staggerPulseDelay=0, staggerPulseSeconds=.14f, landingPulseDelay=0, landingPulseSeconds=.16f;
        float staggerVignetteFadeSeconds=.075f;
        float desaturationStart=0, blurStart=0; // Collapse fractions.
        float recoveryStart=.10f, riseRecovery=.80f, curve=1.1f; // Rising fraction / amount restored.
        float edgeInner=.35f, edgeOuter=1.0f;
        bool mosaicEnabled=true;
        float mosaicStrength=.26f, mosaicCellSize=.006f, mosaicColorStrength=.8f;
        float mosaicChangeRate=.35f; // Smooth spatial-pattern changes per second; 0 freezes it.
        // Start/peak: seconds from Opening; decay: seconds from Rising.
        float mosaicStartSeconds=.35f, mosaicPeakSeconds=1.95f, mosaicDecaySeconds=3.5f;
    };
    struct Settings {
        float staggerSeconds=.3f, collapseSeconds=1.2f;
        float staggerDip=.06f, staggerPitch=.025f, staggerShake=.012f, staggerShakeHz=10;
        float collapseCurve=3;
        float landingSeconds=.18f, landingShake=.03f, landingLift=.012f, landingShakeHz=12;
        float floorHoldSeconds=.3f, closeSeconds=.7f, corpseSeconds=1.2f;
        float openSeconds=1.8f, awakeSeconds=.6f;
        float closeCurve=3, openCurve=1.1f, eyelidCurve=.75f;
        float riseSeconds=3.2f, possessSeconds=1.1f, settleSeconds=3;
        // Pause timing/duration in seconds from Rising start; head time and heights are fractions.
        float riseHeadEnd=.16f, risePauseStartSeconds=.55f, risePauseSeconds=.45f;
        float riseHeadLift=.04f, risePauseHeight=.04f, riseCurve=1.4f;
        float riseHeadPitch=.055f, riseHeadRoll=.045f;
        float slowMotion=.15f, floorEyeHeight=.24f, fallDistance=.16f;
        float cameraRadius=.12f, fallenRoll=1.25f;
        float shake=.065f, wanderSpeed=1.05f, wanderRadius=6, collisionRadius=.32f;
        DeathPose pose=DeathPose::FromImpact;
        PostSettings post;
    };
    struct DeathContext {
        Transform player;
        CameraPose camera;
        Vector3 impactDirection{}; // Incoming travel direction; zero means unknown.
        float eyeHeight=1.65f;
    };
    void Begin(Object3dCommon* common,DirectXCommon* dx,Camera* camera,
        const EnemyDefinition* normal,const PlayerVisualSettings& visual,const DeathContext& death);
    void Reset();
    void Update(float dt,const StageWorld& world,Camera& camera);
    void Draw();
    bool Active() const { return state_!=State::Inactive; }
    bool MenuReady() const { return state_==State::Wander; }
    State Phase() const { return state_; }
    float Elapsed() const { return elapsed_; }
    float MenuElapsed() const { return wanderTime_; }
    float WorldTimeScale() const { return state_==State::Inactive ? 1 : state_==State::Death ? settings.slowMotion : 0; }
    float WorldDelta(float dt) const {
        return state_==State::Death && std::isfinite(dt) ? std::min(std::max(0.0f,dt),static_cast<float>(std::max(0.0,Duration()-phaseTime_)))*settings.slowMotion : 0;
    }
    const DeathContext& Death() const { return death_; }
    const Vector3& Position() const { return position_; }
    const EnemyAI& AI() const { return ai_; }
    bool HasVisual() const { return body_!=nullptr; }
    struct VisionState { float shade=0, opening=1; };
    VisionState Vision() const;
    DeathPostEffectParameters PostEffect() const;
    float MenuDelay() const;
    Settings settings;
private:
    friend void RunGameOverSequenceRuntimeTests(class GameApp& app);
    float Duration() const;
    float Duration(State state) const;
    float RiseProgress() const;
    float CollapseProgress() const;
    float LandingTime() const;
    float RisePauseStart() const;
    float RisePauseEnd() const;
    float RiseHeadEnd() const;
    static float Ease(float p,float curve) { return Smooth(std::pow(std::clamp(p,0.0f,1.0f),std::clamp(curve,.5f,4.0f))); }
    void Advance();
    void Step(float dt,const StageWorld& world);
    void UpdateCamera(const StageWorld& world);
    void Present(const StageWorld& world,Camera& camera);
    static float Smooth(float p) { p=std::clamp(p,0.0f,1.0f); return p*p*(3-2*p); }
    std::unique_ptr<StartPlayerVisual> body_;
    DeathContext death_;
    PlayerVisualSettings visual_;
    State state_=State::Inactive;
    float elapsed_=0, heading_=0, wanderTime_=0;
    double phaseTime_=0;
    Vector3 position_{}, fallRotation_{}, cameraPosition_{}, cameraRotation_{};
    float rollSign_=1;
    EnemyAI ai_;
    StageWorld::PlayerMotion floorMotion_;
    bool bodyVisible_=false;
};
