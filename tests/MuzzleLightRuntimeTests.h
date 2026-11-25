#pragma once
#include "GameApp.h"
#include "Object3dCommon.h"
#include "RenderManager.h"
#include "SceneManager.h"
#include "SrvManager.h"
#include "scene/Main/GameScene.h"
#include "DirectXTex.h"
#include <fstream>

inline void RunMuzzleLightRuntimeTests(GameApp& app) {
    using Mode = LocalLighting::Mode;
    const auto check = [](bool ok, const char* message) {
        if (!ok) throw std::runtime_error(message);
    };
    const std::filesystem::path dir = "generated/muzzle-light-tests";
    app.Scenes().Register("MuzzleLightTest", [] {
        auto scene = std::make_unique<GameScene>(false, "stage03");
        scene->SetStartSequenceEnabled(false);
        return scene;
    });
    app.Scenes().Change(app, "MuzzleLightTest");
    auto* game = dynamic_cast<GameScene*>(app.Scenes().Current());
    check(game && game->stageLoaded_, "Stage03 failed to initialize");
    for (auto* enemy : game->enemies_) enemy->SetAIPaused(true);
    game->ClearMuzzleSmoke();
    auto& settings = game->muzzleLightSettings_;
    settings = {};
    auto* dx = app.Dx();
    auto* cmd = dx->GetCommandList();
    auto& render = *app.Render();
    auto* offscreen = render.GetOffscreen();
    const auto disabledAddress = app.ObjCom()->GetLocalLightAddress();

    UINT64 frequency = 0;
    check(SUCCEEDED(dx->GetCommandQueue()->GetTimestampFrequency(&frequency)) && frequency > 0,
        "GPU timestamp frequency unavailable");
    D3D12_QUERY_HEAP_DESC queryDesc{};
    queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryDesc.Count = 2;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queries;
    check(SUCCEEDED(dx->GetDevice()->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&queries))), "Query heap failed");
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_READBACK);
    const auto buffer = CD3DX12_RESOURCE_DESC::Buffer(16);
    check(SUCCEEDED(dx->GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))), "Timestamp readback failed");

    const auto renderScene = [&] {
        app.Srv()->PreDraw();
        render.BeginOffscreen();
        cmd->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        game->DrawRender(app);
        cmd->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        cmd->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback.Get(), 0);
        render.EndOffscreen();
        dx->PreDraw(false);
        dx->PostDraw();
        check(app.ObjCom()->GetLocalLightAddress() == disabledAddress, "Scene light scope leaked after rendering");
        UINT64* ticks = nullptr;
        const D3D12_RANGE readRange{0, 16}, noWrite{0, 0};
        check(SUCCEEDED(readback->Map(0, &readRange, reinterpret_cast<void**>(&ticks))), "Timestamp map failed");
        const double microseconds = 1.0e6 * static_cast<double>(ticks[1] - ticks[0]) / static_cast<double>(frequency);
        readback->Unmap(0, &noWrite);
        return microseconds;
    };
    const auto capture = [&](const std::string& name) {
        DirectX::ScratchImage pixels;
        const auto state = offscreen->GetState();
        check(SUCCEEDED(DirectX::CaptureTexture(dx->GetCommandQueue(), offscreen->GetResource(), false,
            pixels, state, state)), "GPU color readback failed");
        check(SUCCEEDED(DirectX::SaveToWICFile(*pixels.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), (dir / (name + ".png")).c_str())), "PNG save failed");
        return pixels;
    };
    struct PixelDelta { size_t brighter = 0, darker = 0, newlyWhite = 0; };
    const auto compare = [](const DirectX::ScratchImage& oldImage, const DirectX::ScratchImage& newImage) {
        PixelDelta delta;
        const auto& a = *oldImage.GetImage(0, 0, 0);
        const auto& b = *newImage.GetImage(0, 0, 0);
        for (size_t y = 0; y < a.height; ++y) {
            for (size_t x = 0; x < a.width; ++x) {
                const auto* p = a.pixels + y * a.rowPitch + x * 4;
                const auto* q = b.pixels + y * b.rowPitch + x * 4;
                const int change = q[0] + q[1] + q[2] - p[0] - p[1] - p[2];
                if (change >= 3) ++delta.brighter;
                if (change <= -3) ++delta.darker;
                if (q[0] == 255 && q[1] == 255 && q[2] == 255 &&
                    (p[0] != 255 || p[1] != 255 || p[2] != 255)) ++delta.newlyWhite;
            }
        }
        return delta;
    };

    struct View { const char* id; Vector3 feet, rotation; float ads; };
    const std::array views{
        View{"room-hip", {28, 12, 6}, {0.14f, 3.14159265f, 0}, 0},
        View{"near-wall-ads", {26.1f, 12, 6}, {0.0f, -1.5707963f, 0}, 1},
        View{"corridor-hip", {25, 12, 11.5f}, {0.07f, 1.5707963f, 0}, 0},
        View{"ballroom-hip", {13, 6, 18}, {0.16f, 1.5707963f, 0}, 0},
    };
    struct Setup { const char* id; Mode mode; bool indirect; };
    const std::array setups{Setup{"off", Mode::None, false}, Setup{"point", Mode::Point, false},
        Setup{"spot", Mode::Spot, false}, Setup{"area", Mode::Area, false}, Setup{"fill", Mode::None, true},
        Setup{"point-fill", Mode::Point, true}};
    std::ofstream report(dir / "comparison.csv");
    report << "view,mode,gpu_median_us,brighter_pixels,darker_pixels,newly_white_pixels\n";
    std::array<size_t, setups.size()> brighter{};
    for (const auto& view : views) {
        game->player_.SetStage(&game->level_.collision, view.feet, view.rotation);
        game->player_.RefreshVisuals();
        game->adsBlend_ = view.ads;
        game->camera_.SetFovY((view.ads > 0 ? 47.0f : 60.0f) * 0.0174532925f);
        game->camera_.Update();
        game->ground_.Update(0.0f);
        game->SyncShutterVisuals();
        game->SyncDoorVisuals();
        game->SyncWeaponVisuals(app);
        for (auto* enemy : game->enemies_) enemy->UpdateVisuals(0.0f);
        DirectX::ScratchImage off;
        for (size_t i = 0; i < setups.size(); ++i) {
            const auto& setup = setups[i];
            settings.mode = setup.mode;
            settings.indirectEnabled = setup.indirect;
            game->heldMuzzleLightScale_ = 1.0f;
            renderScene(); // Discard the first draw for each mode.
            std::array<double, 7> timings;
            for (auto& time : timings) time = renderScene();
            std::sort(timings.begin(), timings.end());
            auto image = capture(std::string(view.id) + "-" + setup.id);
            const auto delta = i == 0 ? PixelDelta{} : compare(off, image);
            if (i == 0) off = std::move(image);
            brighter[i] += delta.brighter;
            report << view.id << ',' << setup.id << ',' << timings[timings.size() / 2] << ','
                << delta.brighter << ',' << delta.darker << ',' << delta.newlyWhite << '\n';
            check(delta.darker < 10, "Adding a local light darkened the scene");
        }
    }
    for (size_t i = 1; i < setups.size(); ++i) check(brighter[i] > 100, "A lighting mode produced no visible change");

    // A thin stage proxy between the light and the same visible room geometry must cut its contribution.
    StageCollider blocker;
    blocker.position = StagePoint({0, 0, 1.15f}, game->camera_.GetWorldMatrix());
    blocker.rotation = game->camera_.GetRotate();
    blocker.local = {{-20, -20, -0.035f}, {20, 20, 0.035f}};
    blocker.UpdateMatrices();
    game->level_.collision.colliders.push_back(blocker);
    renderScene();
    auto blocked = capture("ballroom-hip-point-fill-blocked");
    game->heldMuzzleLightScale_.reset();
    renderScene();
    auto unlit = capture("ballroom-hip-after-clear");
    const auto blockedDelta = compare(unlit, blocked);
    report << "ballroom-hip,point-fill-blocked,," << blockedDelta.brighter << ','
        << blockedDelta.darker << ',' << blockedDelta.newlyWhite << '\n';
    check(blockedDelta.brighter < 100, "Light leaked through a thin wall proxy");
    game->level_.collision.colliders.pop_back();

    const auto* pistol = game->weapons_.Find("pistol");
    check(pistol && game->player_.EquipWeapon(*pistol), "Pistol fixture unavailable");
    const int ammo = game->player_.CurrentWeapon().Magazine();
    game->StartMuzzleLight(1.0f);
    game->muzzleLightPulse_->started -= std::chrono::seconds(1);
    check(game->PrepareMuzzleLight() != 0, "Low FPS skipped the first flash frame");
    check(game->PrepareMuzzleLight() == 0, "Expired flash remained active");
    game->StartMuzzleLight(1.0f);
    game->muzzleLightPulse_->started -= std::chrono::seconds(1);
    game->StartMuzzleLight(1.0f);
    check(game->PrepareMuzzleLight() != 0 && game->muzzleLightPulse_, "Repeated shot did not restart the flash");
    check(game->player_.CurrentWeapon().Magazine() == ammo, "Light-only flash consumed ammunition");
    game->heldMuzzleLightScale_ = 1.0f;
    game->BeginGameOver(app);
    check(game->gameOver_.Active() && game->PrepareMuzzleLight() == 0, "Game over retained muzzle lighting");
    app.Scenes().Change(app, "MuzzleLightTest");
    game = dynamic_cast<GameScene*>(app.Scenes().Current());
    check(game && game->PrepareMuzzleLight() == 0, "Restart retained muzzle lighting");
    game->heldMuzzleLightScale_ = 1.0f;
    check(game->PrepareMuzzleLight() != 0, "Held-light fixture did not activate");
    app.Scenes().Change(app, "Title");
    check(app.ObjCom()->GetLocalLightAddress() == disabledAddress, "Scene change retained muzzle lighting");
    std::ofstream(dir / "result.txt") << "PASS\n";
}
