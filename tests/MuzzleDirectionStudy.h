#pragma once
#include "GameApp.h"
#include "Object3dCommon.h"
#include "RenderManager.h"
#include "SceneManager.h"
#include "SrvManager.h"
#include "scene/Main/GameScene.h"
#include "DirectXTex.h"
#include <fstream>
#include <numbers>

inline void RunMuzzleDirectionStudy(GameApp& app) {
    using Mode = LocalLighting::Mode;
    using Json = nlohmann::json;
    const auto check = [](bool ok, const char* message) {
        if (!ok) throw std::runtime_error(message);
    };
    const auto vector = [](const Vector3& v) { return Json::array({v.x, v.y, v.z}); };
    const std::filesystem::path dir = "generated/muzzle-direction-study";
    struct View { const char* name; bool showroom; Vector3 feet, rotation; };
    constexpr float quarterTurn = std::numbers::pi_v<float> / 4.0f;
    const std::array views{
        View{"showroom-corner", true, {-20.9f, 0, -12.9f}, {0, -3 * quarterTurn + 0.006f, 0}},
        View{"hotel-corner", false, {28.7f, 12, 6.7f}, {0, quarterTurn + 0.008f, 0}},
    };
    auto* dx = app.Dx();
    auto& render = *app.Render();
    auto* offscreen = render.GetOffscreen();
    auto& common = *app.ObjCom();
    Json results = Json::object();
    for (const auto& view : views) {
        app.Scenes().Register(view.name, [showroom = view.showroom] {
            auto scene = std::make_unique<GameScene>(showroom, showroom ? "stage01" : "stage03");
            scene->SetStartSequenceEnabled(false);
            return scene;
        });
        app.Scenes().Change(app, view.name);
        auto* game = dynamic_cast<GameScene*>(app.Scenes().Current());
        check(game && game->stageLoaded_, "Corner study stage unavailable");
        auto& stage = game->level_.collision;
        game->player_.SetStage(&stage, view.feet, view.rotation);
        game->player_.RefreshVisuals();
        game->camera_.SetAspect(1280.0f / 720.0f);
        game->camera_.SetFovY(std::numbers::pi_v<float> / 3);
        game->camera_.Update();
        game->ground_.Update(0.0f);
        if (view.showroom) game->stageLighting_ = StageLighting::FromPreset(StageLighting::Preset::DarkRoom);

        auto& settings = game->muzzleLightSettings_;
        const auto saved = settings;
        auto& result = results[view.name];
        result["saved"] = {{"direct_enabled", saved.directEnabled}, {"indirect_enabled", saved.indirectEnabled},
            {"bounce_enabled", saved.bounce.enabled}, {"direct_range", saved.range}, {"direct_decay", saved.decay},
            {"bounce_range", saved.bounce.range}, {"bounce_decay", saved.bounce.decay},
            {"bounce_intensity_ratio", saved.bounce.intensity}, {"search_range", saved.bounce.rayDistance},
            {"occlusion_enabled", saved.bounce.occlusionEnabled}};
        game->heldMuzzleLightScale_ = 1.0f;
        settings.bounce.enabled = true;
        game->heldMuzzleBounce_ = game->FindMuzzleBouncePoints();
        const auto& bounceSample = *game->heldMuzzleBounce_;
        check(bounceSample.count > 0, "Corner study had no reflection hits");

        const auto anchor = game->ResolveMuzzleAnchor(settings.offset);
        const auto& world = game->camera_.GetWorldMatrix();
        const auto& projection = game->camera_.GetProjectionMatrix();
        const Vector3 eye = game->camera_.GetTranslate();
        const Vector3 right{world.m[0][0], world.m[0][1], world.m[0][2]};
        const Vector3 forward{world.m[2][0], world.m[2][1], world.m[2][2]};
        check(StageLength(anchor.position - bounceSample.origin) < 0.001f &&
            std::abs(StageLength(forward) - 1.0f) < 0.001f, "Light and camera transforms disagree");
        WeaponDefinition weapon = *game->weapons_.Find("pistol");
        weapon.pelletCount = 0;
        std::mt19937 unusedRandom;
        BulletShotPose shot;
        BulletSimulation preview;
        preview.SpawnShot(weapon, world, game->adsBlend_, unusedRandom,
            [&](const auto& origin, const auto& direction, float range) {
                return TraceBulletPath(origin, direction, range, stage, 0, {});
            }, &shot);
        result["pose"] = {{"eye", vector(eye)}, {"camera_forward", vector(forward)},
            {"local_offset", vector(settings.offset)}, {"right", vector(right)},
            {"muzzle", vector(anchor.position)}, {"clipped", anchor.obstructed},
            {"bullet_muzzle", vector(shot.muzzle)}, {"bullet_axis_before_spread", vector(shot.direction)}};

        float rayWeight = 0.0f;
        result["rays"] = Json::array();
        for (size_t i = 0; i < bounceSample.rayCount; ++i) {
            const auto& ray = bounceSample.rays[i];
            rayWeight += ray.weight;
            result["rays"].push_back({{"end", vector(ray.end)}, {"normal", vector(ray.normal)},
                {"distance", StageLength(ray.end - anchor.position)}, {"weight", ray.weight},
                {"collider", ray.hit ? stage.colliders[ray.colliderIndex].id : ""}});
        }
        result["selected"] = Json::array();
        for (size_t i = 0; i < bounceSample.count; ++i) {
            const auto& point = bounceSample.points[i];
            result["selected"].push_back({{"position", vector(point.position)}, {"normal", vector(point.normal)},
                {"weight", point.weight}, {"collider", stage.colliders[point.colliderIndex].id}});
        }
        // Comparison with a single smooth field using the mean contribution of all traced rays.
        const float prototypeFill = saved.intensity * saved.bounce.intensity *
            rayWeight / static_cast<float>(bounceSample.rayCount) * bounceSample.emissionScale;
        result["prototype_fill_intensity"] = prototypeFill;
        struct Setup { const char* name; Mode mode; bool bounce = false; };
        const std::array setups{
            Setup{"off", Mode::None}, Setup{"point", Mode::Point}, Setup{"spot", Mode::Spot},
            Setup{"area", Mode::Area}, Setup{"bounce", Mode::None, true},
            Setup{"point-bounce", Mode::Point, true}, Setup{"bounce-no-limit", Mode::None, true},
            Setup{"bounce-no-occlusion", Mode::None, true}, Setup{"bounce-longer", Mode::None, true},
            Setup{"point-longer", Mode::Point}, Setup{"point-no-limit", Mode::Point},
            Setup{"point-no-occlusion", Mode::Point}, Setup{"ray-mean-fill", Mode::None},
            Setup{"forward", Mode::Forward}, Setup{"forward-no-bias", Mode::Forward},
            Setup{"forward-bounce", Mode::Forward, true},
        };
        for (const auto& setup : setups) {
            const std::string_view name = setup.name;
            settings = saved;
            settings.mode = setup.mode;
            settings.directEnabled = setup.mode != Mode::None;
            settings.indirectEnabled = name == "ray-mean-fill";
            settings.indirectIntensity = prototypeFill;
            settings.indirectRange = saved.bounce.range;
            settings.bounce.enabled = setup.bounce;
            if (name == "forward-no-bias") settings.forwardBias = 0.0f;
            if (name == "bounce-longer") settings.bounce.range = 12.0f;
            if (name.starts_with("point-") && name != "point-bounce") settings.range = 12.0f;
            auto light = game->BuildMuzzleLightParameters(*game->heldMuzzleLightScale_, &bounceSample);
            if (name == "bounce-no-limit" || name == "point-no-limit") {
                for (auto& visibility : light.visibility) visibility.nearestOmittedRange = 0.0f;
            }
            if (name == "bounce-no-occlusion" || name == "point-no-occlusion") light.visibility = {};

            app.Srv()->PreDraw();
            render.BeginOffscreen();
            game->PrepareStageLighting();
            game->muzzleLightBuffer_.Update(light);
            {
                const Object3dCommon::ScopedLightingEnvironment environment(common, game->stageLight_);
                const Object3dCommon::ScopedLocalLight local(common, game->muzzleLightBuffer_.Address());
                game->ground_.Draw();
            }
            render.EndOffscreen();
            dx->PreDraw(false);
            dx->PostDraw();
            const auto state = offscreen->GetState();
            DirectX::ScratchImage pixels;
            check(SUCCEEDED(DirectX::CaptureTexture(dx->GetCommandQueue(), offscreen->GetResource(), false,
                pixels, state, state)), "Corner study color readback failed");
            const auto& img = *pixels.GetImage(0, 0, 0);
            const auto file = dir / (std::string(view.name) + "-" + setup.name + ".png");
            check(SUCCEEDED(DirectX::SaveToWICFile(img, DirectX::WIC_FLAGS_NONE,
                DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), file.c_str())), "Corner study PNG write failed");

            auto& capture = result["modes"][setup.name];
            capture["direct_axis"] = vector(light.direction);
            capture["visibility"] = Json::array();
            for (size_t i = 0; i <= light.bounceCount; ++i)
                capture["visibility"].push_back({{"boxes", light.visibility[i].occluderCount},
                    {"cutoff", light.visibility[i].nearestOmittedRange}, {"octants", light.visibility[i].omittedRanges}});
            capture["probes"] = Json::array();
            for (float screenX : {-0.55f, 0.0f, 0.55f}) {
                const auto viewRay = forward + right * (screenX / projection.m[0][0]);
                StageHit hit;
                if (!stage.Raycast(eye, viewRay, 25.0f, hit, 0.0f, true))
                    throw std::runtime_error(std::string(view.name) + " missed at screen x=" + std::to_string(screenX));
                const auto receiver = eye + viewRay * (hit.distance / StageLength(viewRay));
                const auto opticalDelta = receiver - anchor.position;
                const float opticalDistance = StageLength(opticalDelta);
                const auto toReceiver = opticalDelta * (1.0f / opticalDistance);
                const auto octant = [&](const Vector3& source) {
                    const auto biased = receiver + hit.normal * 0.035f - source;
                    return (biased.x >= 0 ? 1 : 0) | (biased.y >= 0 ? 2 : 0) | (biased.z >= 0 ? 4 : 0);
                };
                const auto cutoff = [&](size_t slot, const Vector3& source) {
                    const auto& visibility = light.visibility[slot];
                    return visibility.nearestOmittedRange > 0.0f ? visibility.omittedRanges[octant(source)] : 0.0f;
                };
                Json probes = Json::array();
                for (size_t i = 0; i < light.bounceCount; ++i) {
                    const auto& bounce = light.bounces[i];
                    const auto delta = receiver - bounce.position;
                    const float distance = StageLength(delta);
                    const float spread = std::sqrt(distance * distance + 0.36f);
                    probes.push_back({{"distance", distance},
                        {"exitance", (StageDot(delta, bounce.normal) + bounce.surfaceOffset) / spread},
                        {"incidence", -StageDot(delta, hit.normal) / spread},
                        {"occluder_cutoff", cutoff(i + 1, bounce.position)}});
                }
                constexpr int radius = 12;
                const int xCenter = static_cast<int>((screenX + 1.0f) * img.width * 0.5f);
                const int yCenter = static_cast<int>(img.height / 2);
                float pixelSum = 0.0f;
                for (int y = yCenter - radius; y <= yCenter + radius; ++y) {
                    for (int x = xCenter - radius; x <= xCenter + radius; ++x) {
                        const auto* rgb = img.pixels + y * img.rowPitch + x * 4;
                        pixelSum += (rgb[0] + rgb[1] + rgb[2]) / 3.0f;
                    }
                }
                capture["probes"].push_back({{"screen_x", screenX}, {"pixel_mean", pixelSum / (25 * 25)},
                    {"world", vector(receiver)}, {"normal", vector(hit.normal)},
                    {"wall", stage.colliders[hit.colliderIndex].id},
                    {"eye_distance", hit.distance}, {"muzzle_distance", opticalDistance},
                    {"forward_cosine", StageDot(toReceiver, forward)},
                    {"receiver_cosine", -StageDot(toReceiver, hit.normal)},
                    {"direct_occluder_cutoff", cutoff(0, anchor.position)}, {"bounce_terms", std::move(probes)}});
            }
            if (name == "forward-no-bias") {
                for (size_t i = 0; i < capture["probes"].size(); ++i) {
                    const auto point = result["modes"]["point"]["probes"][i]["pixel_mean"].get<float>();
                    const auto unbiased = capture["probes"][i]["pixel_mean"].get<float>();
                    check(std::abs(point - unbiased) < 0.02f, "Zero forward bias diverged from Point");
                }
            }
        }
        std::ofstream(dir / "measurements.json") << results.dump(2) << '\n';
        if (!view.showroom) {
            const auto& modes = result["modes"];
            const float leftAtRange = modes["point-longer"]["probes"][0]["pixel_mean"].get<float>();
            const float leftWithoutCutoff = modes["point-no-limit"]["probes"][0]["pixel_mean"].get<float>();
            const float leftUnlit = modes["off"]["probes"][0]["pixel_mean"].get<float>();
            check(leftAtRange > leftUnlit + 2.0f && std::abs(leftAtRange - leftWithoutCutoff) < 0.1f,
                "The extended beam still cuts off before the unobstructed hotel wall");

            // Overflow behind the emitter must not affect clear forward segments.
            auto colliders = std::move(stage.colliders);
            game->camera_.SetTranslate({0, 1.6f, 0});
            game->camera_.SetRotate({});
            game->camera_.Update();
            settings.mode = Mode::Point;
            settings.directEnabled = true;
            settings.bounce.enabled = false;
            settings.range = 12.0f;
            const auto pushWall = [&](float z) {
                StageCollider wall;
                wall.local = {{-3, 0, -0.01f}, {3, 3, 0.01f}};
                wall.position.z = z;
                wall.UpdateMatrices();
                stage.colliders.push_back(wall);
            };
            for (size_t i = 0; i <= LocalLighting::kMaxOccluders; ++i)
                pushWall(-1.0f - static_cast<float>(i) * 0.1f);
            const auto rearOnly = game->BuildMuzzleLightParameters(1.0f, nullptr).visibility[0];
            pushWall(8.0f);
            const auto both = game->BuildMuzzleLightParameters(1.0f, nullptr).visibility[0];
            stage.colliders = std::move(colliders);
            check(rearOnly.occluderCount == LocalLighting::kMaxOccluders && rearOnly.omittedRanges[3] > 0.0f &&
                rearOnly.omittedRanges[7] == 0.0f, "Rear overflow shortened the forward range");
            check(both.omittedRanges[7] > rearOnly.omittedRanges[3] && both.omittedRanges[7] < 8.0f,
                "Overflow on the forward path did not retain its visibility limit");
        }
    }
    std::ofstream(dir / "result.txt") << "PASS\n";
}
