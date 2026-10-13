#pragma once
#include "GameApp.h"
#include "IScene.h"
#include "DirectXCommon.h"
#include "ImGuiManagaer.h"
#include "DirectXTex.h"
#include "SrvManager.h"
#include "scene/Main/GameScene.h"
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

inline void Draw(GameApp& app, [[maybe_unused]] bool expectDebugInput = true) {
#ifdef USE_IMGUI
    app.ImGui()->Begin();
#endif
    app.Draw();
#ifdef USE_IMGUI
    const auto* window = ImGui::FindWindowByName("Screen Transition");
    Check(window && window->Active, "Transition debug panel disappeared during fade");
    if (expectDebugInput) Check((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse) == 0, "Fade disabled ImGui mouse input");
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
        Check(p[3] == 255, "Fade made an opaque game/UI texture transparent");
    }
    Check(SUCCEEDED(DirectX::SaveToWICFile(*pixels, DirectX::WIC_FLAGS_NONE,
        DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), path)), "Fade GPU PNG failed");
}

struct FadeFrame {
    uint8_t sceneMinAlpha = 255;
    uint64_t displayedEnergy = 0;
};

// Read back both the faded RGBA image and the final ImGui image composite.
// RGB-only checks miss a second application of alpha by ImGui::Image.
inline FadeFrame CaptureFadeFrame(GameApp& app, FadeManager& fade) {
    auto* dx = app.Dx();
    constexpr auto format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    auto scene = dx->CreateRenderTextureResource(64, 64, format, {0,0,0,1});
    auto heap = dx->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
    D3D12_RENDER_TARGET_VIEW_DESC rtv{};
    rtv.Format = format; rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    const auto sceneHandle = heap->GetCPUDescriptorHandleForHeapStart();
    dx->GetDevice()->CreateRenderTargetView(scene.Get(), &rtv, sceneHandle);
#ifdef USE_IMGUI
    auto displayed = dx->CreateRenderTextureResource(1280, 720, format, {0,0,0,1});
    auto displayedHandle = sceneHandle;
    displayedHandle.ptr += dx->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    dx->GetDevice()->CreateRenderTargetView(displayed.Get(), &rtv, displayedHandle);
    const auto srv = app.Srv()->Allocate();
    app.Srv()->CreateSRVTexture2D(srv, scene.Get(), format, 1);
    const bool previousWorkspace = app.ImGui()->IsWeaponWorkspace();
    app.ImGui()->SetWeaponWorkspace(true); // Only the image under test is submitted.
    app.ImGui()->Begin();
#endif
    Sprite hud;
    hud.Initialize(app.SpriteCom(), dx, "resources/white1x1.png");
    const auto& texture = TextureManager::GetInstance()->GetMetaData("resources/white1x1.png");
    hud.SetScale({1.0f/static_cast<float>(texture.width),1.0f/static_cast<float>(texture.height),1});
    hud.SetColor({0,1,0,1});
    hud.Update(Matrix4x4::MakeIdentity4x4(), Matrix4x4::MakeOrthographicMatrix(0,0,1,1,0,100));
    dx->PreDraw(false);
    auto* command = dx->GetCommandList();
    command->OMSetRenderTargets(1, &sceneHandle, FALSE, nullptr);
    const float clear[] = {0,0,0,1};
    command->ClearRenderTargetView(sceneHandle, clear, 0, nullptr);
    const D3D12_VIEWPORT sceneViewport{0,0,64,64,0,1};
    const D3D12_RECT sceneScissor{0,0,64,64};
    command->RSSetViewports(1, &sceneViewport); command->RSSetScissorRects(1, &sceneScissor);
    hud.Draw(); fade.Draw();
    auto sceneState = D3D12_RESOURCE_STATE_RENDER_TARGET;
#ifdef USE_IMGUI
    dx->TransitionResource(scene.Get(), sceneState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    sceneState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    command->OMSetRenderTargets(1, &displayedHandle, FALSE, nullptr);
    command->ClearRenderTargetView(displayedHandle, clear, 0, nullptr);
    ImGui::GetForegroundDrawList()->AddImage(
        static_cast<ImTextureID>(app.Srv()->GetGPUDescriptionHandle(srv).ptr), {0,0}, {1280,720});
    app.ImGui()->End(command);
    app.ImGui()->SetWeaponWorkspace(previousWorkspace);
#endif
    dx->SetBackBufferRenderTarget(); dx->PostDraw(); dx->WaitForGPU();
    DirectX::ScratchImage sceneImage;
    Check(SUCCEEDED(DirectX::CaptureTexture(dx->GetCommandQueue(), scene.Get(), false,
        sceneImage, sceneState, sceneState)), "Fade RGBA capture failed");
    FadeFrame frame;
    const auto* pixels = sceneImage.GetImage(0,0,0);
    for (size_t y=0; y<pixels->height; ++y) for (size_t x=0; x<pixels->width; ++x)
        frame.sceneMinAlpha = std::min(frame.sceneMinAlpha, pixels->pixels[y*pixels->rowPitch+x*4+3]);
#ifdef USE_IMGUI
    DirectX::ScratchImage displayedImage;
    const auto displayedState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    Check(SUCCEEDED(DirectX::CaptureTexture(dx->GetCommandQueue(), displayed.Get(), false,
        displayedImage, displayedState, displayedState)), "ImGui fade composite capture failed");
    pixels = displayedImage.GetImage(0,0,0);
#endif
    for (size_t y=0; y<pixels->height; ++y) for (size_t x=0; x<pixels->width; ++x) {
        const auto* p = pixels->pixels + y*pixels->rowPitch + x*4;
        frame.displayedEnergy += static_cast<uint64_t>(p[0])+p[1]+p[2];
    }
    return frame;
}

inline void CheckCompletedTransition(GameApp& app, const std::string& scene) {
    auto& scenes = app.Scenes();
    const auto outStarts = scenes.Fade().StateStartCount(TransitionState::FadeOut);
    const auto inStarts = scenes.Fade().StateStartCount(TransitionState::FadeIn);
    for (int frame=0; frame<12; ++frame) {
        app.Update(0, 1.0f/60);
        Draw(app, false); // Normal gameplay can capture the mouse after FadeIn.
        Check(scenes.CurrentName()==scene && !scenes.IsTransitioning() && scenes.Fade().Alpha()==0 &&
            scenes.Fade().StateStartCount(TransitionState::FadeOut)==outStarts &&
            scenes.Fade().StateStartCount(TransitionState::FadeIn)==inStarts,
            "Completed scene transition restarted fading on a later host frame");
    }
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
    fade.SetColor({0,0,0}); fade.SetEasing(FadeEasing::Linear); fade.FadeIn(1);
    const auto fadeInStarts = fade.StateStartCount(TransitionState::FadeIn);
    uint64_t previousEnergy = 0;
    bool opaqueScene = true, monotonicDisplay = true;
    std::ofstream frames("generated/transition-tests/fade-in-frames.csv");
    frames << "frame,fade_alpha,scene_min_alpha,displayed_energy\n";
    for (int step=0; step<15; ++step) {
        const auto frame = CaptureFadeFrame(app, fade);
        frames << step << ',' << fade.Alpha() << ',' << static_cast<int>(frame.sceneMinAlpha) << ',' << frame.displayedEnergy << '\n';
        opaqueScene = opaqueScene && frame.sceneMinAlpha==255;
        monotonicDisplay = monotonicDisplay && frame.displayedEnergy>=previousEnergy;
        previousEnergy = frame.displayedEnergy;
        fade.Update(.1f);
    }
    frames.close();
    Check(opaqueScene, "Fade changed Scene texture alpha: ImGui applies opacity a second time");
    Check(monotonicDisplay, "Final display darkened again during FadeIn or after completion");
    Check(fade.State()==TransitionState::Idle && fade.StateStartCount(TransitionState::FadeIn)==fadeInStarts,
        "Completed FadeIn restarted");

    auto& scenes = app.Scenes();
    const auto gameOutStarts = scenes.Fade().StateStartCount(TransitionState::FadeOut);
    const auto gameInStarts = scenes.Fade().StateStartCount(TransitionState::FadeIn);
    Check(scenes.TransitionTo("Game",.05f,.05f), "Stage entry request rejected");
    for (int frame=0; frame<60 && scenes.IsTransitioning(); ++frame) {
        app.Update(0,1.0f/60); Draw(app);
    }
    Check(!scenes.IsTransitioning() && scenes.Fade().StateStartCount(TransitionState::FadeOut)==gameOutStarts+1 &&
        scenes.Fade().StateStartCount(TransitionState::FadeIn)==gameInStarts+1, "Stage entry faded more than once");
    CheckCompletedTransition(app, "Game");
    auto* game = dynamic_cast<GameScene*>(scenes.Current());
    Check(game && game->IsReadyForTransition() && !game->stage_.Goals().empty(), "Clear menu stage initialization");
    Check(!game->ReturnToTitle(app), "Clear menu returned to title during gameplay");
    Check(game->stage_.Update(.5f, game->stage_.Goals().front().position), "Clear menu goal did not activate");
    game->OnStageClear(app);
    Check(game->clearOverlay_.IsTitleHovered({640,505}) &&
        game->clearOverlay_.IsTitleHovered({470,483}) &&
        !game->clearOverlay_.IsTitleHovered({811,505}) &&
        !game->clearOverlay_.IsTitleHovered({640,528}), "Clear title button bounds");
    Draw(app);
    // Recompose the real Sprite clear menu over the game's rendered scene for
    // a layout capture in both Debug and Release (without editor chrome).
    app.Dx()->PreDraw(false);
    app.Render()->GetOffscreen()->BeginForPostEffect();
    game->DrawOverlay2D(app);
    app.Render()->GetOffscreen()->End();
    app.Dx()->SetBackBufferRenderTarget(); app.Dx()->PostDraw();
    DirectX::ScratchImage clearImage;
    const auto clearState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    Check(SUCCEEDED(DirectX::CaptureTexture(app.Dx()->GetCommandQueue(), app.Render()->GetOffscreen()->GetResource(),
        false, clearImage, clearState, clearState)), "Clear menu GPU capture failed");
    Check(SUCCEEDED(DirectX::SaveToWICFile(*clearImage.GetImage(0,0,0), DirectX::WIC_FLAGS_NONE,
        DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), L"generated/transition-tests/clear-title-menu.png")), "Clear menu PNG failed");
    const auto clearTime = game->stage_.Time();
    Check(game->ReturnToTitle(app) && !game->ReturnToTitle(app), "Clear title request / duplicate guard");
    scenes.Update(app,.25f);
    Check(scenes.CurrentName()=="Game" && game->stage_.Time()==clearTime && scenes.Fade().Alpha()==.5f,
        "Clear results changed during return fade");
    scenes.Update(app,.25f); Draw(app); scenes.Update(app,0);
    Check(scenes.CurrentName()=="Title" && scenes.Current()->IsReadyForTransition() && scenes.Fade().Alpha()==1,
        "Clear did not return to initialized title under black fade");
    Draw(app); scenes.Update(app,0); scenes.Update(app,.5f);
    Check(!scenes.IsTransitioning() && scenes.Fade().Alpha()==0, "Clear title fade did not complete");
    CheckCompletedTransition(app, "Title");

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
    app.Update(0,.025f); const auto alpha = scenes.Fade().Alpha();
    for (int tick=0; tick<8; ++tick) scenes.UpdateScene(app, 100);
    Check(alpha==.025f && scenes.Fade().Alpha()==alpha, "Simulation ticks changed unscaled fade clock");
    scenes.Fade().Reset();
    Check(scenes.TransitionTo("TransitionProbeA",0,.75f), "Slow first frame transition rejected");
    scenes.Update(app,0); Draw(app); scenes.Update(app,0); Draw(app); scenes.Update(app,0);
    Check(scenes.Fade().Alpha()==1 && scenes.Fade().Elapsed()==0, "FadeIn did not start fully opaque");
    Draw(app);
    app.Update(0,5); // First GPU frame / debugger pause must not consume the entire fade.
    Check(scenes.IsTransitioning() && scenes.Fade().Alpha()>.93f && scenes.Fade().Elapsed()==.05f,
        "Long stage-start frame skipped the beginning of FadeIn");
    scenes.Update(app,.7f);
    Check(!scenes.IsTransitioning(), "Bounded fade did not finish");
    Check(scenes.TransitionTo("TransitionProbeA",1,1), "Valid request after failure rejected");
    scenes.Change(app,"TransitionProbeB");
    Check(!scenes.IsTransitioning() && scenes.Fade().State()==TransitionState::Idle, "Immediate change left pending fade");
    std::ofstream("generated/transition-tests/result.txt") <<
        "PASS: four easing curves and endpoints; explicit Hold; zero duration and state notifications; live duration/easing/color; "
        "invalid input; real D3D12 all-pixel UI coverage at 853x481, 1920x1080 and 127x255; "
        "zero-duration scene switch with outgoing/incoming opaque frames; duplicate/reentrant requests; delayed readiness; "
        "factory/null/OnEnter/deferred-load failure recovery; input and ImGui panel availability; unscaled host clock; cancellation; "
        "actual stage clear/result freeze/title return and button bounds; long first GPU frame does not skip FadeIn; "
        "opaque RGBA and monotonic final ImGui brightness across FadeIn and completion; stage entry/title return stay Idle on later host frames.\n";
}
