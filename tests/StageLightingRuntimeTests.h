#pragma once
#include "GameApp.h"
#include "Object3dCommon.h"
#include "SrvManager.h"
#include "scene/Main/GameScene.h"
#include "DirectXTex.h"
#include <fstream>

inline void RunStageLightingRuntimeTests(GameApp& app) {
    using Preset = StageLighting::Preset;
    const auto check = [](bool ok, const char* message) {
        if (!ok) throw std::runtime_error(message);
    };
    const std::filesystem::path dir = "generated/stage-lighting-tests";
    std::filesystem::create_directories(dir);
    const auto fixturePath = dir / ("roundtrip-" + std::to_string(GetCurrentProcessId()) + ".lighting.json");
    check(StageLighting::Load(fixturePath).ToJson() == StageLighting{}.ToJson(), "Missing file did not use Standard");
    auto* dx = app.Dx();
    auto* cmd = dx->GetCommandList();
    auto* common = app.ObjCom();
    auto* offscreen = app.Render()->GetOffscreen();
    UINT64 frequency = 0;
    check(SUCCEEDED(dx->GetCommandQueue()->GetTimestampFrequency(&frequency)) && frequency > 0, "No GPU timestamps");
    D3D12_QUERY_HEAP_DESC queryDesc{};
    queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryDesc.Count = 2;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestamps;
    check(SUCCEEDED(dx->GetDevice()->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&timestamps))), "Timestamp heap failed");
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    const CD3DX12_HEAP_PROPERTIES readbackHeap(D3D12_HEAP_TYPE_READBACK);
    const auto readbackDesc = CD3DX12_RESOURCE_DESC::Buffer(16);
    check(SUCCEEDED(dx->GetDevice()->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE,
        &readbackDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))), "Timestamp readback failed");

    const auto frame = [&](GameScene& game, const std::function<void()>& fixture = {}) {
        app.Srv()->PreDraw();
        cmd->EndQuery(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        if (!fixture) game.DrawShadow(app);
        app.Render()->BeginOffscreen();
        if (fixture) {
            game.PrepareStageLighting();
            const Object3dCommon::ScopedLightingEnvironment environment(*common, game.stageLight_);
            fixture();
        } else {
            game.DrawRender(app);
        }
        app.Render()->EndOffscreen();
        cmd->EndQuery(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        cmd->ResolveQueryData(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback.Get(), 0);
        dx->PreDraw(false);
        dx->PostDraw();
        check(common->GetLightingEnvironment() == nullptr, "Environment leaked after scene draw");
        UINT64* ticks = nullptr;
        const D3D12_RANGE readRange{0, 16}, noWrite{0, 0};
        check(SUCCEEDED(readback->Map(0, &readRange, reinterpret_cast<void**>(&ticks))), "Timestamp map failed");
        const double milliseconds = 1000.0 * static_cast<double>(ticks[1] - ticks[0]) / static_cast<double>(frequency);
        readback->Unmap(0, &noWrite);
        return milliseconds;
    };
    const auto capture = [&](const std::string& name) {
        DirectX::ScratchImage image;
        const auto state = offscreen->GetState();
        check(SUCCEEDED(DirectX::CaptureTexture(dx->GetCommandQueue(), offscreen->GetResource(), false,
            image, state, state)), "Lighting readback failed");
        check(SUCCEEDED(DirectX::SaveToWICFile(*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), (dir / (name + ".png")).c_str())), "Lighting PNG write failed");
        return image;
    };
    const auto differences = [](const DirectX::ScratchImage& a, const DirectX::ScratchImage& b) {
        const auto& first = *a.GetImage(0, 0, 0);
        const auto& second = *b.GetImage(0, 0, 0);
        size_t changed = 0;
        for (size_t y = 0; y < first.height; ++y) {
            for (size_t x = 0; x < first.width; ++x) {
                const auto* p = first.pixels + y * first.rowPitch + x * 4;
                const auto* q = second.pixels + y * second.rowPitch + x * 4;
                changed += p[0] != q[0] || p[1] != q[1] || p[2] != q[2];
            }
        }
        return changed;
    };
    const auto pixel = [](const DirectX::ScratchImage& data, size_t x, size_t y) {
        const auto& img = *data.GetImage(0, 0, 0);
        const auto* p = img.pixels + y * img.rowPitch + x * 4;
        return std::array{p[0], p[1], p[2]};
    };
    const auto time = [&](GameScene& game) {
        frame(game);
        std::array<double, 9> timings;
        for (auto& value : timings) value = frame(game);
        std::sort(timings.begin(), timings.end());
        return timings[timings.size() / 2];
    };
    Camera probeCamera;
    probeCamera.SetTranslate({0, 0, -4});
    probeCamera.SetFovY(0.9f);
    probeCamera.Update();
    Object3d probe;
    probe.Initialize(common, dx);
    probe.SetCamera(&probeCamera);
    probe.SetModel("cube/cube.obj");
    probe.SetTexture("resources/white1x1.png");
    probe.SetEnableLighting(1);
    probe.SetMaterialColor({0.6f, 0.75f, 0.8f, 1});
    probe.Update(0);

    struct View { const char* stage; Preset preset; Vector3 feet, rotation; };
    const std::array views{
        View{"showroom", Preset::Standard, {-22.0f, 0, -8.8f}, {-0.02f, -2.88f, 0}},
        View{"stage01", Preset::Standard, {-12.0f, 0, -9.0f}, {0, -2.75f, 0}},
        View{"stage02", Preset::Outdoor, {3, 0, -6}, {0.12f, 0, 0}},
        View{"stage03", Preset::Hotel, {28, 12, 6}, {0.14f, 3.14159265f, 0}},
        View{"stage04", Preset::Hotel, {3.1f, 15.2f, 36.5f}, {0, 0, 0}},
    };
    std::ofstream timings(dir / "gpu.csv");
    timings << "scene,preset,gpu_draw_ms\n";
    DirectX::ScratchImage reference;
    for (const auto& view : views) {
        const std::string stage = view.stage;
        app.Scenes().Register("StageLightingTest", [stage] {
            const bool showroom = stage == "showroom";
            auto game = std::make_unique<GameScene>(showroom, showroom ? "stage01" : stage);
            game->SetStartSequenceEnabled(false);
            return game;
        });
        app.Scenes().Change(app, "StageLightingTest");
        auto* game = dynamic_cast<GameScene*>(app.Scenes().Current());
        check(game && game->stageLoaded_ && game->stageLightingError_.empty(), "Stage lighting failed to load");
        check(game->stageLighting_.preset == view.preset, "A previous preset leaked or the stage preset failed to load");
        const auto refresh = [&] {
            game->player_.SetStage(&game->level_.collision, view.feet, view.rotation);
            game->player_.RefreshVisuals();
            game->ground_.Update(0);
            game->SyncShutterVisuals();
            game->SyncDoorVisuals();
            game->SyncWeaponVisuals(app);
            for (auto* enemy : game->enemies_) enemy->UpdateVisuals(0);
        };
        refresh();
        timings << stage << ",saved," << time(*game) << '\n';
        auto saved = capture(stage + "-saved");

        game->stageLighting_ = StageLighting{};
        frame(*game, [&] { probe.Draw(); });
        auto uniform = capture(stage + "-standard-fixture");
        if (stage == "showroom") reference = std::move(uniform);
        else check(differences(reference, uniform) == 0, "Identical setup rendered differently across scenes");

        if (stage == "showroom") {
            const auto wall = pixel(saved, 400, 360);
            check(wall[0] > 60 && wall[0] < 120, "Showroom shadow face lost ambient fill");
            game->stageLighting_.ambientEnabled = false;
            frame(*game);
            auto lambert = capture("showroom-lambert-no-ambient");
            check(pixel(lambert, 400, 360) == std::array<uint8_t, 3>{0, 0, 0}, "Lambert shadow-face control was not black");
            game->stageLighting_.environment.diffuseMode = Object3dLight::DiffuseMode::HalfLambert;
            frame(*game);
            auto half = capture("showroom-half-lambert-no-ambient");
            check(pixel(half, 400, 360)[0] > 0, "Half-Lambert did not illuminate the same wall");

            game->stageLighting_ = {};
            probe.SetEnableLighting(0);
            frame(*game, [&] { probe.Draw(); });
            auto unlit = capture("unlit-control");
            game->stageLighting_ = StageLighting::FromPreset(Preset::DarkRoom);
            frame(*game, [&] { probe.Draw(); });
            check(differences(unlit, capture("unlit-under-dark-room")) == 0, "Unlit object was affected by the scene");
            probe.SetEnableLighting(1);

            Object3d skinned;
            skinned.Initialize(common, dx);
            skinned.SetCamera(&probeCamera);
            skinned.SetModel("enemy/boss/boss.gltf");
            AABB bounds{};
            check(skinned.HasSkinningModel() && skinned.GetModel()->GetLocalAABB(bounds), "Skinned fixture missing");
            const auto extent = bounds.max - bounds.min;
            const float scale = 1.8f / std::max({extent.x, extent.y, extent.z});
            skinned.SetScale({scale, scale, scale});
            skinned.SetTranslate((bounds.min + bounds.max) * (-0.5f * scale));
            skinned.SetEnableLighting(1);
            skinned.Update(0);
            game->stageLighting_ = {};
            game->stageLighting_.directionalEnabled = false;
            frame(*game, [&] { skinned.Draw(); });
            auto skinnedLit = capture("skinned-ambient");
            game->stageLighting_.ambientEnabled = false;
            frame(*game, [&] { skinned.Draw(); });
            check(differences(skinnedLit, capture("skinned-ambient-off")) > 100, "Ambient did not affect skinned geometry");

            Enemy debris;
            const auto* definition = game->enemyDefinitions_.Find("normal");
            check(definition != nullptr, "Debris fixture enemy definition missing");
            debris.PrepareForPool(common, dx, &probeCamera, *definition);
            debris.ResetForSpawn(1, "lighting-fixture", {0, -1.f, 0}, {});
            debris.Die({0, 1, -1});
            debris.UpdateVisuals(0.08f);
            check(!debris.CaptureDebug().faces.empty(), "Face fixture did not produce debris");
            game->stageLighting_.ambientEnabled = true;
            frame(*game, [&] { debris.Draw(false); });
            auto facesLit = capture("fracture-ambient");
            game->stageLighting_.ambientEnabled = false;
            frame(*game, [&] { debris.Draw(false); });
            check(differences(facesLit, capture("fracture-ambient-off")) > 20, "Ambient did not affect fracture triangles");

            game->stageLightingPath_ = fixturePath;
            game->LoadStageLighting();
            const auto authored = StageLighting::FromPreset(Preset::DarkRoom);
            game->stageLighting_ = authored;
            check(game->SaveStageLighting() && StageLighting::Load(fixturePath).ToJson() == authored.ToJson(),
                "Save did not create a new stage sidecar");
            game->stageLighting_ = StageLighting{};
            game->LoadStageLighting();
            check(game->stageLighting_.ToJson() == authored.ToJson(), "Reload did not restore all authored fields");
            auto invalid = authored.ToJson();
            invalid["directional"]["direction"] = {0, 0, 0};
            std::ofstream(fixturePath) << invalid.dump();
            check(!game->SaveStageLighting(), "Save overwrote an external edit");
            game->LoadStageLighting();
            check(!game->stageLightingError_.empty() && game->stageLighting_.ToJson() == authored.ToJson(),
                "Invalid reload discarded the last good settings");
        }
        if (stage == "stage03") {
            game->stageLighting_ = StageLighting::FromPreset(Preset::Hotel);
            game->stageLighting_.shadow.enabled = true;
            game->stageLighting_.shadow.strength = 1.0f;
            timings << stage << ",shadow-on," << time(*game) << '\n';
            auto shadowed = capture("stage03-shadow-on");
            check(game->stageShadowMap_ && game->stageShadowMap_->DrawCount() > 0 && game->stageLight_.GetDirectionalShadowSrv().ptr,
                "Enabled shadow did not submit casters");
            check(differences(saved, shadowed) > 20, "Shadow pass did not affect the rendered scene");
            game->stageLighting_.shadow.enabled = false;
            frame(*game);
            check(!game->stageLight_.GetDirectionalShadowSrv().ptr &&
                differences(saved, capture("stage03-shadow-reset")) == 0, "A disabled shadow remained visible");
            game->BeginGameOver(app);
            frame(*game);
            check(game->gameOver_.Active() && common->GetLightingEnvironment() == nullptr, "Game over leaked the environment");
        }
    }
    app.Scenes().Change(app, "StageLightingTest");
    auto* restarted = dynamic_cast<GameScene*>(app.Scenes().Current());
    check(restarted && restarted->stageLighting_.preset == Preset::Hotel && !restarted->stageShadowMap_,
        "Restart did not use the saved Stage04 preset");
    app.Scenes().Change(app, "Title");
    check(common->GetLightingEnvironment() == nullptr, "Gameplay lighting leaked into Title");
    std::ofstream(dir / "result.txt") << "PASS: gameplay presets, identical fixture pixels, showroom lighting modes, save/reload, invalid JSON, shadow enable/disable, game over, restart and title isolation\n";
}
