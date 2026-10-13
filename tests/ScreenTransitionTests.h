#pragma once
#include "GameApp.h"
#include "IScene.h"
#include "DirectXCommon.h"
#include "ImGuiManagaer.h"
#include "DirectXTex.h"
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#ifdef USE_IMGUI
#include "imgui_internal.h"
#endif

namespace ScreenTransitionTests {
inline void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Probe {
    int enters = 0, exits = 0, updates = 0, loads = 0, draws = 0;
    int readyAfter = 0;
    bool failEnter = false, failLoading = false, recursive = false, recursiveRejected = false;
};
class ProbeScene final : public IScene {
    std::shared_ptr<Probe> probe_;
public:
    explicit ProbeScene(std::shared_ptr<Probe> probe) : probe_(std::move(probe)) {}
    void OnEnter(GameApp& app) override {
        ++probe_->enters;
        probe_->loads = 0;
        if (probe_->recursive) {
            const auto name = app.Scenes().CurrentName();
            app.Scenes().Change(app, "TransitionProbeA");
            probe_->recursiveRejected = !app.Scenes().TransitionTo("TransitionProbeA") && app.Scenes().CurrentName() == name;
        }
        if (probe_->failEnter) throw std::runtime_error("Injected OnEnter failure");
    }
    void OnExit(GameApp&) override { ++probe_->exits; }
    void Update(GameApp&, float) override { ++probe_->updates; }
    void UpdateLoading(GameApp&, float) override { ++probe_->loads; }
    bool IsReadyForTransition() const override { return probe_->loads >= probe_->readyAfter; }
    std::string InitializationError() const override {
        return probe_->failLoading && probe_->loads >= 2 ? "Injected deferred load failure" : "";
    }
    void Draw(GameApp&) override { ++probe_->draws; }
};

inline void Draw(GameApp& app) {
#ifdef USE_IMGUI
    app.ImGui()->Begin();
#endif
    app.Draw();
#ifdef USE_IMGUI
    const auto* window = ImGui::FindWindowByName("Screen Transition");
    Check(window && window->Active, "Transition debug panel disappeared during fade");
    Check((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse) == 0, "Fade disabled ImGui mouse input");
#endif
}

// Capture every pixel, including the boundary, in different actual D3D12
// target/viewport resolutions. Green UI is drawn underneath the overlay.
inline void CheckPixels(GameApp& app, FadeManager& fade, uint32_t width, uint32_t height,
    uint8_t red, uint8_t green, uint8_t blue, const wchar_t* path) {
    auto* dx = app.Dx();
    const auto format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    auto target = dx->CreateRenderTextureResource(width, height, format, {1,0,0,1});
    auto heap = dx->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    D3D12_RENDER_TARGET_VIEW_DESC rtv{};
    rtv.Format = format; rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    const auto handle = heap->GetCPUDescriptorHandleForHeapStart();
    dx->GetDevice()->CreateRenderTargetView(target.Get(), &rtv, handle);
    Sprite hud;
    hud.Initialize(app.SpriteCom(), dx, "resources/white1x1.png");
    const auto& texture = TextureManager::GetInstance()->GetMetaData("resources/white1x1.png");
    hud.SetScale({1.0f / static_cast<float>(texture.width), 1.0f / static_cast<float>(texture.height), 1});
    hud.SetColor({0,1,0,1});
    hud.Update(Matrix4x4::MakeIdentity4x4(), Matrix4x4::MakeOrthographicMatrix(0,0,1,1,0,100));
    dx->PreDraw(false);
    auto* command = dx->GetCommandList();
    command->OMSetRenderTargets(1, &handle, FALSE, nullptr);
    const float clear[] = {1,0,0,1};
    command->ClearRenderTargetView(handle, clear, 0, nullptr);
    const D3D12_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
    const D3D12_RECT scissor{0,0,static_cast<LONG>(width),static_cast<LONG>(height)};
    command->RSSetViewports(1, &viewport); command->RSSetScissorRects(1, &scissor);
    hud.Draw(); fade.Draw();
    dx->SetBackBufferRenderTarget();
    dx->PostDraw(); dx->WaitForGPU();
    DirectX::ScratchImage image;
    const auto state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    Check(SUCCEEDED(DirectX::CaptureTexture(dx->GetCommandQueue(), target.Get(), false, image, state, state)), "Fade GPU capture failed");
    const auto* pixels = image.GetImage(0,0,0);
    Check(pixels->width == width && pixels->height == height, "Fade GPU capture dimensions");
    for (size_t y=0; y<height; ++y) for (size_t x=0; x<width; ++x) {
        const auto* p = pixels->pixels + y * pixels->rowPitch + x * 4;
        Check(p[0] == red && p[1] == green && p[2] == blue, "Fade leaked game UI or missed an edge pixel");
    }
    Check(SUCCEEDED(DirectX::SaveToWICFile(*pixels, DirectX::WIC_FLAGS_NONE,
        DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), path)), "Fade GPU PNG failed");
}
}

inline void RunScreenTransitionTests(GameApp& app) {
    using namespace ScreenTransitionTests;
    FadeManager fade;
    Check(fade.State() == TransitionState::Idle && fade.Alpha() == 0, "Initial fade state");
    const std::array<float,4> expected{.25f,.0625f,.4375f,.125f};
    for (size_t i=0; i<expected.size(); ++i) {
        fade.Reset(); fade.SetEasing(static_cast<FadeEasing>(i)); fade.FadeOut(2); fade.Update(.5f);
        Check(std::abs(fade.Alpha()-expected[i]) < 1e-6f && fade.Progress()==.25f, "FadeOut easing");
        fade.Update(100);
        Check(fade.State()==TransitionState::Hold && fade.Alpha()==1, "FadeOut did not enter Hold");
        fade.Update(1000); Check(fade.Alpha()==1 && fade.IsFinished(), "Hold expired automatically");
        fade.FadeIn(2); fade.Update(.5f);
        Check(std::abs(fade.Alpha()-(1-expected[i])) < 1e-6f, "FadeIn easing");
        fade.Update(2); Check(fade.State()==TransitionState::Idle && fade.Alpha()==0, "FadeIn endpoint");
    }
    fade.SetEasing(FadeEasing::Linear);
    const auto outStarts = fade.StateStartCount(TransitionState::FadeOut);
    const auto outEnds = fade.StateEndCount(TransitionState::FadeOut);
    fade.FadeOut(0);
    Check(fade.Alpha()==1 && fade.State()==TransitionState::Hold &&
        fade.StateStartCount(TransitionState::FadeOut)==outStarts+1 &&
        fade.StateEndCount(TransitionState::FadeOut)==outEnds+1, "Instant fade lost state notifications");
    fade.FadeIn(0); Check(fade.Alpha()==0 && fade.State()==TransitionState::Idle, "Instant FadeIn");
    fade.FadeOut(1); fade.Update(.25f); fade.SetDuration(2);
    Check(fade.Duration()==2 && fade.Elapsed()==.25f && fade.Alpha()==.125f, "Live duration editing");
    fade.SetEasing(FadeEasing::EaseIn); Check(fade.Alpha()==.015625f, "Live easing editing");
    fade.SetDuration(0); Check(fade.State()==TransitionState::Hold && fade.Alpha()==1, "Live instant duration");
    fade.FadeIn(1); fade.Update(-1); fade.Update(std::numeric_limits<float>::quiet_NaN());
    Check(fade.Alpha()==1, "Invalid delta advanced fade");
    bool rejected = false;
    try { fade.FadeOut(-1); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected && fade.State()==TransitionState::FadeIn, "Invalid duration changed fade state");
    fade.Initialize(app.SpriteCom(), app.Dx());
    fade.Hold();
    CheckPixels(app, fade, 853, 481, 0,0,0, L"generated/transition-tests/black-853x481.png");
    fade.SetColor({1,1,1});
    CheckPixels(app, fade, 1920, 1080, 255,255,255, L"generated/transition-tests/white-1920x1080.png");
    fade.SetColor({1,0,1});
    CheckPixels(app, fade, 127, 255, 255,0,255, L"generated/transition-tests/color-127x255.png");
    fade.Reset();
    CheckPixels(app, fade, 853, 481, 0,255,0, L"generated/transition-tests/reset-ui.png");

    auto& scenes = app.Scenes();
    const auto a = std::make_shared<Probe>(), b = std::make_shared<Probe>();
    b->readyAfter = 3; b->recursive = true;
    scenes.Register("TransitionProbeA", [a] { return std::make_unique<ProbeScene>(a); });
    scenes.Register("TransitionProbeB", [b] { return std::make_unique<ProbeScene>(b); });
    scenes.Change(app, "TransitionProbeA");
    Check(scenes.TransitionTo("TransitionProbeB", 0,0), "Zero-duration scene request rejected");
    Check(!scenes.TransitionTo("TransitionProbeB"), "Duplicate scene request accepted");
    scenes.Update(app, 0); scenes.Update(app, 100);
    Check(b->enters==0 && scenes.Fade().Alpha()==1, "Scene entered before opaque frame");
    Draw(app);
    scenes.Update(app, 100);
    Check(b->enters==1 && b->updates==0 && b->recursiveRejected && scenes.IsWaitingForScene(), "Loading/reentrancy guard");
    Draw(app); Check(b->draws==0, "Partially initialized scene was drawn");
    scenes.Update(app, 100); Check(scenes.Fade().Alpha()==1 && b->updates==0, "Loading did not hold fade");
    scenes.Update(app, 100); Check(b->loads==3 && scenes.Fade().Alpha()==1, "Readiness released overlay early");
    scenes.Update(app, 100); Check(scenes.IsTransitioning(), "Incoming opaque frame skipped");
    Draw(app);
    scenes.Update(app, 100);
    Check(!scenes.IsTransitioning() && scenes.Fade().Alpha()==0 && b->enters==1 && b->updates==0, "Instant FadeIn / loading delta");
    scenes.Update(app, .016f); Check(b->updates==1, "Gameplay did not resume");
    scenes.Change(app,"TransitionProbeA");

    scenes.Register("TransitionNull", [] { return std::unique_ptr<IScene>{}; });
    scenes.Register("TransitionThrow", []() -> std::unique_ptr<IScene> { throw std::runtime_error("Injected factory failure"); });
    const auto fail = std::make_shared<Probe>(); fail->failEnter = true;
    scenes.Register("TransitionFail", [fail] { return std::make_unique<ProbeScene>(fail); });
    const auto asyncFail = std::make_shared<Probe>(); asyncFail->readyAfter = 100; asyncFail->failLoading = true;
    scenes.Register("TransitionAsyncFail", [asyncFail] { return std::make_unique<ProbeScene>(asyncFail); });
    for (const std::string name : {"TransitionNull", "TransitionThrow", "TransitionFail", "TransitionAsyncFail"}) {
        const int priorEnters = a->enters;
        Check(scenes.TransitionTo(name,0,0), "Failure recovery request rejected");
        scenes.Update(app,0); Draw(app); scenes.Update(app,0);
        if (name=="TransitionAsyncFail") scenes.Update(app,.1f);
        Check(scenes.CurrentName()=="TransitionProbeA" && !scenes.LastTransitionError().empty() && scenes.Fade().Alpha()==1,
            "Failed scene did not recover under opaque fade");
        Check(a->enters==priorEnters+((name=="TransitionFail" || name=="TransitionAsyncFail") ? 1 : 0), "Factory failure unnecessarily restarted old scene");
        Draw(app); scenes.Update(app,100);
        Check(!scenes.IsTransitioning() && scenes.Fade().Alpha()==0, "Failure recovery stuck in transition");
    }
    Check(!scenes.TransitionTo("Missing") && !scenes.TransitionTo("TransitionProbeA",-1,1), "Invalid scene request accepted");
    scenes.Fade().FadeOut(1);
    app.Update(0,.25f); const auto alpha = scenes.Fade().Alpha();
    for (int tick=0; tick<8; ++tick) scenes.UpdateScene(app, 100);
    Check(alpha==.25f && scenes.Fade().Alpha()==alpha, "Simulation ticks changed unscaled fade clock");
    scenes.Fade().Reset();
    Check(scenes.TransitionTo("TransitionProbeA",1,1), "Valid request after failure rejected");
    scenes.Change(app,"TransitionProbeB");
    Check(!scenes.IsTransitioning() && scenes.Fade().State()==TransitionState::Idle, "Immediate change left pending fade");
    std::ofstream("generated/transition-tests/result.txt") <<
        "PASS: four easing curves and endpoints; explicit Hold; zero duration and state notifications; live duration/easing/color; "
        "invalid input; real D3D12 all-pixel UI coverage at 853x481, 1920x1080 and 127x255; "
        "zero-duration scene switch with outgoing/incoming opaque frames; duplicate/reentrant requests; delayed readiness; "
        "factory/null/OnEnter/deferred-load failure recovery; input and ImGui panel availability; unscaled host clock; cancellation.\n";
}
