#pragma once
#include "GameApp.h"
#include "Object3dCommon.h"
#include "RenderManager.h"
#include "SceneManager.h"
#include "SrvManager.h"
#include "WinApp.h"
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
    auto* dx = app.Dx();
    auto* cmd = dx->GetCommandList();
    auto* offscreen = app.Render()->GetOffscreen();
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

    const auto render = [&](const auto& draw) {
        app.Srv()->PreDraw();
        app.Render()->BeginOffscreen();
        cmd->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        draw();
        cmd->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        cmd->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback.Get(), 0);
        app.Render()->EndOffscreen();
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
    const auto renderScene = [&] { return render([&] { game->DrawRender(app); }); };
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

    app.Scenes().Register("MuzzleLightShowroom", [] { return std::make_unique<GameScene>(true); });
    app.Scenes().Register("MuzzleLightDoors", [] {
        auto scene = std::make_unique<GameScene>(false, "stage04");
        scene->SetStartSequenceEnabled(false);
        return scene;
    });
    enum class Location { Hotel, Showroom, Doors };
    struct View {
        const char* id;
        Vector3 feet, rotation;
        float ads = 0.0f;
        Location location = Location::Hotel;
        bool variant = false;
    };
    const std::array views{
        View{"showroom-standard", {-22, 0, -8.8f}, {-0.02f, -2.88f, 0}, 0, Location::Showroom},
        View{"showroom-dark-room", {-22, 0, -8.8f}, {-0.02f, -2.88f, 0}, 0, Location::Showroom, true},
        View{"hotel-door-closed", {6.35f, 15.2f, 36.5f}, {0, 1.5707963f, 0}, 0, Location::Doors},
        View{"hotel-door-open", {6.35f, 15.2f, 36.5f}, {0, 1.5707963f, 0}, 0, Location::Doors, true},
        View{"room-hip", {28, 12, 6}, {0.14f, 3.14159265f, 0}, 0},
        View{"room-corner", {26.5f, 12, 4.5f}, {0.15f, 0.77f, 0}, 0},
        View{"near-wall-ads", {26.1f, 12, 6}, {0.0f, -1.5707963f, 0}, 1},
        View{"corridor-hip", {25, 12, 11.5f}, {0.07f, 1.5707963f, 0}, 0},
        View{"ballroom-hip", {13, 6, 18}, {0.16f, 1.5707963f, 0}, 0},
    };
    struct Setup { const char* id; Mode mode; bool indirect = false, bounce = false, occlusion = true; };
    const std::array setups{Setup{"off", Mode::None}, Setup{"point", Mode::Point},
        Setup{"bounce", Mode::None, false, true}, Setup{"point-bounce", Mode::Point, false, true},
        Setup{"bounce-unoccluded", Mode::None, false, true, false},
        Setup{"spot", Mode::Spot}, Setup{"area", Mode::Area}, Setup{"fill", Mode::None, true},
        Setup{"point-fill", Mode::Point, true}};
    std::ofstream report(dir / "comparison.csv");
    report << "view,mode,gpu_median_us,cpu_search_median_us,cpu_prepare_median_us,"
        "active_bounces,brighter_pixels,darker_pixels,newly_white_pixels\n";
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->EnumAdapterByLuid(dx->GetDevice()->GetAdapterLuid(), IID_PPV_ARGS(&adapter)))) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            char name[512]{};
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
            std::ofstream(dir / "adapter.txt") << name << '\n';
        }
    }
    const auto median = [](auto& timings) {
        std::sort(timings.begin(), timings.end());
        return timings[timings.size() / 2];
    };
    std::array<size_t, setups.size()> brighter{};
    auto location = Location::Hotel;
    std::optional<size_t> doorIndex;
    for (const auto& view : views) {
        if (view.location != location) {
            location = view.location;
            const auto scene = location == Location::Showroom ? "MuzzleLightShowroom" :
                (location == Location::Doors ? "MuzzleLightDoors" : "MuzzleLightTest");
            app.Scenes().Change(app, scene);
            game = dynamic_cast<GameScene*>(app.Scenes().Current());
            check(game && game->stageLoaded_, "Comparison stage failed to initialize");
            for (auto* enemy : game->enemies_) enemy->SetAIPaused(true);
            if (location == Location::Showroom) game->weaponEditor_.mode = WeaponEditorMode::Test;
        }
        game->ClearMuzzleLight();
        game->ClearMuzzleSmoke();
        game->muzzleLightSettings_ = {};
        auto& settings = game->muzzleLightSettings_;
        if (location == Location::Showroom) {
            game->stageLighting_ = StageLighting::FromPreset(
                view.variant ? StageLighting::Preset::DarkRoom : StageLighting::Preset::Standard);
        }
        if (location == Location::Doors) {
            auto& doors = game->level_.doors;
            if (view.variant) {
                check(doorIndex.has_value(), "Door index was not retained");
                doors.doors[*doorIndex].targetOpen = true;
                doors.Update(1.0f, game->level_.collision, [](const auto&) { return false; });
            } else {
                doors.Reset(game->level_.collision);
                doorIndex = doors.Nearest(game->level_.collision, view.feet);
                check(doorIndex.has_value() && !doors.doors[*doorIndex].targetOpen, "Closed door fixture unavailable");
            }
            const auto& door = doors.doors[*doorIndex];
            check(door.angle == (view.variant ? door.openAngle : 0.0f), "Door did not reach test pose");
            StageHit hit;
            const bool blocked = game->level_.collision.Raycast(view.feet + Vector3{0, 1.6f, 0},
                {1, 0, 0}, 3.1f, hit, 0.0f, true);
            check(view.variant ? !blocked : blocked && hit.colliderIndex == door.colliderIndex,
                "Optical ray did not follow the actual door");
        }
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
            if (i >= 5 && std::string_view(view.id) != "ballroom-hip") continue;
            const auto& setup = setups[i];
            settings.mode = setup.mode;
            settings.indirectEnabled = setup.indirect;
            settings.bounce.enabled = setup.bounce;
            settings.bounce.occlusionEnabled = setup.occlusion;
            game->heldMuzzleLightScale_ = 1.0f;
            game->heldMuzzleBounce_.reset();
            std::array<float, 11> search{};
            if (setup.bounce) {
                for (auto& time : search) {
                    const auto sample = game->FindMuzzleBouncePoints();
                    time = sample.calculationMs * 1000.0f;
                    check(sample.count > 0 && sample.count <= LocalLighting::kMaxBounceLights,
                        "Invalid bounce light count");
                }
            }
            renderScene(); // Discard the first draw for each mode.
            std::array<double, 9> timings;
            for (auto& time : timings) time = renderScene();
            std::array<double, 11> prepare;
            for (auto& time : prepare) {
                const auto start = std::chrono::steady_clock::now();
                game->PrepareMuzzleLight();
                time = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            }
            auto image = capture(std::string(view.id) + "-" + setup.id);
            const auto delta = i == 0 ? PixelDelta{} : compare(off, image);
            if (i == 0) off = std::move(image);
            brighter[i] += delta.brighter;
            report << view.id << ',' << setup.id << ',' << median(timings) << ',' << median(search) << ','
                << median(prepare) << ',' << game->activeMuzzleBounceLights_ << ',' << delta.brighter << ','
                << delta.darker << ',' << delta.newlyWhite << '\n';
            report.flush();
            check(delta.darker < 10, "Adding a local light darkened the scene");
            if (setup.bounce) check(delta.newlyWhite == 0, "Bounce light clipped new pixels to white");
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
    report << "ballroom-hip,point-fill-blocked,,,,," << blockedDelta.brighter << ','
        << blockedDelta.darker << ',' << blockedDelta.newlyWhite << '\n';
    check(blockedDelta.brighter < 100, "Light leaked through a thin wall proxy");
    game->level_.collision.colliders.pop_back();

    const auto* pistol = game->weapons_.Find("pistol");
    check(pistol && game->player_.EquipWeapon(*pistol), "Pistol fixture unavailable");
    game->muzzleLightSettings_.directEnabled = false;
    game->muzzleLightSettings_.indirectEnabled = false;
    game->muzzleLightSettings_.bounce.enabled = true;
    const int ammo = game->player_.CurrentWeapon().Magazine();
    game->StartMuzzleLight(1.0f);
    game->muzzleLightPulse_->started -= std::chrono::seconds(1);
    check(game->PrepareMuzzleLight() != 0 && game->activeMuzzleBounceLights_ > 0, "Low FPS skipped the first bounce frame");
    check(game->PrepareMuzzleLight() == 0 && game->activeMuzzleBounceLights_ == 0, "Expired bounce remained active");
    game->StartMuzzleLight(1.0f);
    game->muzzleLightPulse_->started -= std::chrono::seconds(1);
    game->StartMuzzleLight(1.0f);
    check(game->PrepareMuzzleLight() != 0 && game->muzzleLightPulse_, "Repeated shot did not restart the flash");
    check(game->player_.CurrentWeapon().Magazine() == ammo, "Light-only flash consumed ammunition");
    game->heldMuzzleLightScale_ = 1.0f;
    game->BeginGameOver(app);
    check(game->gameOver_.Active() && game->PrepareMuzzleLight() == 0 && !game->heldMuzzleBounce_,
        "Game over retained muzzle lighting");
    app.Scenes().Change(app, "MuzzleLightTest");
    game = dynamic_cast<GameScene*>(app.Scenes().Current());
    check(game && game->PrepareMuzzleLight() == 0, "Restart retained muzzle lighting");

    app.Scenes().Change(app, "MuzzleLightShowroom");
    game = dynamic_cast<GameScene*>(app.Scenes().Current());
    check(game && !game->enemies_.empty(), "Showroom enemy fixture unavailable");
    game->weaponEditor_.mode = WeaponEditorMode::Test;
    game->stageLighting_ = StageLighting::FromPreset(StageLighting::Preset::DarkRoom);
    auto& settings = game->muzzleLightSettings_;
    settings = {};
    settings.directEnabled = settings.indirectEnabled = false;
    auto& stage = game->level_.collision;
    const auto drawEnemy = [&] {
        render([&] {
            game->PrepareStageLighting();
            const Object3dCommon::ScopedLightingEnvironment environment(*app.ObjCom(), game->stageLight_);
            const Object3dCommon::ScopedLocalLight local(*app.ObjCom(), game->PrepareMuzzleLight());
            game->enemies_.front()->Draw(false);
        });
    };
    auto originalColliders = std::move(stage.colliders);
    const auto enemyPosition = game->enemies_.front()->GetPosition();
    StageCollider floor;
    floor.local = {{-8, -0.2f, -8}, {8, 0, 8}};
    stage.colliders = {floor};
    game->player_.SetStage(&stage, {0, 0, -2.15f}, {0.16f, 0, 0});
    game->player_.RefreshVisuals();
    game->enemies_.front()->SetPosition({0, 0, 0.35f});
    game->enemies_.front()->UpdateVisuals(0.0f);
    drawEnemy();
    auto enemyUnlit = capture("enemy-off");
    game->heldMuzzleLightScale_ = 1.0f;
    drawEnemy();
    check(game->heldMuzzleBounce_ && game->activeMuzzleBounceLights_ == 1, "Floor bounce fixture failed");
    const auto sample = *game->heldMuzzleBounce_;
    const auto enemyBounce = compare(enemyUnlit, capture("enemy-bounce"));
    check(enemyBounce.brighter > 100 && enemyBounce.darker == 0, "Bounce did not light the enemy model");

    StageCollider incidentBlocker;
    incidentBlocker.local = {{-8, 0.6f, -8}, {8, 0.7f, 8}};
    StageDoor movingBlocker;
    movingBlocker.colliderIndex = stage.colliders.size();
    stage.colliders.push_back(incidentBlocker);
    game->level_.doors.doors.push_back(movingBlocker);
    check(game->PrepareMuzzleLight() == 0 && game->activeMuzzleBounceLights_ == 0,
        "A movable blocker across the original muzzle ray left its held bounce active");
    game->level_.doors.doors.pop_back();
    stage.colliders.pop_back();

    StageCollider divider;
    divider.local = {{-8, -0.2f, -0.72f}, {8, 4, -0.66f}};
    stage.colliders.push_back(divider);
    drawEnemy();
    const auto blockedBounce = compare(enemyUnlit, capture("enemy-bounce-blocked"));
    check(blockedBounce.brighter == 0, "Bounce leaked through a divider between the reflection and the receiver");
    check(game->heldMuzzleBounce_->calculationMs == sample.calculationMs &&
        StageLength(game->heldMuzzleBounce_->points[0].position - sample.points[0].position) == 0,
        "Held bounce points were searched again");
    settings.bounce.occlusionEnabled = false;
    drawEnemy();
    check(compare(enemyUnlit, capture("enemy-bounce-unoccluded")).brighter == enemyBounce.brighter,
        "Occlusion toggle did not restore bounce contribution");
    report << "enemy,bounce,,,,1," << enemyBounce.brighter << ',' << enemyBounce.darker << ','
        << enemyBounce.newlyWhite << '\n';
    report << "enemy,bounce-blocked,,,,1," << blockedBounce.brighter << ',' << blockedBounce.darker
        << ',' << blockedBounce.newlyWhite << '\n';

    stage.colliders = std::move(originalColliders);
    game->enemies_.front()->SetPosition(enemyPosition);
    game->enemies_.front()->UpdateVisuals(0.0f);
    game->ClearMuzzleLight();
    game->player_.SetStage(&stage, {-22, 0, -8.8f}, {-0.02f, -2.88f, 0});
    game->player_.RefreshVisuals();
    settings.bounce.occlusionEnabled = true;
    auto& input = *app.GetInput();
    SetForegroundWindow(app.Win()->GetHwnd());
    input.SetCameraControlEnabled(true);
    std::ofstream shots(dir / "shots.txt");
    if (input.IsCameraControlEnabled() && input.HasFocus()) {
        game->suppressFireUntilRelease_ = false;
        const auto* single = game->weapons_.Find("pistol");
        check(single && game->player_.EquipWeapon(*single), "Single-shot test weapon missing");
        const auto before = game->shotCount_;
        input.prevLeftMouseDown_ = false;
        input.leftMouseDown_ = true;
        game->UpdateCombat(app, 0.0f, true);
        check(game->shotCount_ == before + 1 && game->PrepareMuzzleLight() != 0 &&
            game->activeMuzzleBounceLights_ > 0, "Normal single shot did not generate a bounce");
        const auto* automatic = game->weapons_.Find("smg");
        check(automatic && game->player_.EquipWeapon(*automatic), "Automatic test weapon missing");
        input.prevLeftMouseDown_ = true;
        uint32_t peak = 0;
        for (int i = 0; i < 10; ++i) {
            game->UpdateCombat(app, 0.1f, true);
            game->PrepareMuzzleLight();
            peak = std::max(peak, game->activeMuzzleBounceLights_);
        }
        const auto rounds = game->shotCount_ - before - 1;
        check(rounds >= 3 && peak > 0 && peak <= LocalLighting::kMaxBounceLights,
            "Automatic fire did not generate capped bounce lighting");
        shots << "Single shot: 1\nAutomatic fire: " << rounds << " shots\nPeak active bounces: " << peak << '\n';
        input.leftMouseDown_ = input.prevLeftMouseDown_ = false;
    } else {
        shots << "Gameplay input skipped: test window did not receive focus\n";
    }
    input.SetCameraControlEnabled(false);
    game->heldMuzzleLightScale_ = 1.0f;
    check(game->PrepareMuzzleLight() != 0 && game->activeMuzzleBounceLights_ > 0, "Held-light fixture did not activate");
    app.Scenes().Change(app, "Title");
    check(app.ObjCom()->GetLocalLightAddress() == disabledAddress, "Scene change retained muzzle lighting");
    std::ofstream(dir / "result.txt") << "PASS\n";
}
