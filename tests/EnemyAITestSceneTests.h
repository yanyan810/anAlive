#pragma once
#include "GameApp.h"
#include "scene/Debug/EnemyAITestScene.h"
#include "scene/Flow/TitleScene.h"
#include "ImGuiManagaer.h"
#include "SrvManager.h"
#include "DirectXTex.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <tuple>

inline void RunEnemyAITestSceneTests(GameApp& app, bool checkLiveDiagnostics, bool checkLivePhase2, bool checkLiveRestart) {
    const auto check=[](bool ok,const char* text) { if (!ok) throw std::runtime_error(text); };
    const auto frame=[&]() {
#ifdef USE_IMGUI
        app.ImGui()->Begin();
#endif
        app.Update(1.0f/60,1.0f/60); app.Draw();
    };
    const auto transition=[&](const char* name) {
        check(app.Scenes().TransitionTo(name,.05f,.05f),"transition rejected");
        for (int i=0;i<60 && app.Scenes().IsTransitioning();++i) frame();
        check(!app.Scenes().IsTransitioning() && app.Scenes().CurrentName()==name,"fade transition did not finish");
    };
    check(dynamic_cast<TitleScene*>(app.Scenes().Current())!=nullptr,"initial title missing");
    transition("EnemyAITest");
    auto* scene=dynamic_cast<EnemyAITestScene*>(app.Scenes().Current());
    check(scene && scene->enemies_.size()==8 && scene->pool_.Active()==8,"AI scene initial enemies/pool");
    check(scene->world_.colliders.size()==10,"AI scene collision layout missing");
    // Capture only environment geometry: enemies/debug overlays already used the
    // live camera and would hide a stale background WVP in a whole-scene comparison.
    const auto captureEnvironment=[&](const wchar_t* path) {
        auto* dx=app.Dx(); auto* target=app.Render()->GetOffscreen();
        app.Srv()->PreDraw(); dx->PreDraw(); target->Begin();
        for (const auto& object:scene->geometry_) object->Draw();
        target->End(); dx->SetBackBufferRenderTarget(); dx->PostDraw(); dx->WaitForGPU();
        DirectX::ScratchImage image;
        const auto state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        check(SUCCEEDED(DirectX::CaptureTexture(dx->GetCommandQueue(),target->GetResource(),false,image,state,state)),"environment capture failed");
        const auto* pixels=image.GetImage(0,0,0);
        check(SUCCEEDED(DirectX::SaveToWICFile(*pixels,DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path)),"environment PNG failed");
        std::vector<uint8_t> result;
        for (size_t y=0;y<pixels->height;++y)
            result.insert(result.end(),pixels->pixels+y*pixels->rowPitch,pixels->pixels+y*pixels->rowPitch+pixels->width*4);
        return result;
    };
    app.GetInput()->SetCameraControlEnabled(false);
    const auto initialPlayer=scene->player_.GetTransform();
    scene->Update(app,0);
    if (checkLiveDiagnostics || checkLivePhase2 || checkLiveRestart) {
#if defined(_DEBUG) && defined(_M_X64) && defined(USE_IMGUI)
        const auto coordinates=[](const Vector3& value) { return std::array{value.x,value.y,value.z}; };
        struct EnemySnapshot {
            const Enemy* instance;
            uint64_t spawnId;
            std::string id;
            std::array<float,3> position;
            std::vector<float> partHp;
            EnemyState state;
            bool pursuit, paused;
            bool operator==(const EnemySnapshot&) const = default;
        };
        const auto snapshot=[&]() {
            std::vector<EnemySnapshot> enemies;
            for (const auto* enemy : scene->enemies_) {
                EnemySnapshot value{enemy,enemy->GetSpawnId(),enemy->GetId(),coordinates(enemy->GetPosition()),{},
                    enemy->GetState(),enemy->AI().pursuitOrdered,enemy->AI().paused};
                for (const auto& part : enemy->CaptureDebug().parts) value.partHp.push_back(part.hp);
                enemies.push_back(std::move(value));
            }
            const auto& player=scene->player_.GetTransform();
            const auto& camera=scene->camera_;
            return std::tuple(GetCurrentProcessId(),app.Scenes().Current(),app.Scenes().CurrentName(),
                coordinates(player.translate),coordinates(player.rotate),scene->player_.GetHP(),
                coordinates(camera.GetTranslate()),coordinates(camera.GetRotate()),camera.GetFovY(),
                scene->pauseAll_,std::move(enemies));
        };
        const auto before=snapshot();
        if (checkLivePhase2 || checkLiveRestart) {
            using Json = nlohmann::json;
            using Action = LiveEnemyDiagnostics::Action;
            const std::filesystem::path fixtures("generated/live-phase2-tests");
            std::string run;
            std::ifstream(fixtures / "run.txt") >> run;
            check(run.size() == 32 && run.find_first_not_of("0123456789abcdef") == std::string::npos, "invalid live fixture directory");
            const auto directory = fixtures / run;
            std::map<std::string, Json> manifests;
            for (const auto& path : std::filesystem::directory_iterator(directory)) {
                std::ifstream manifest(path.path() / "generation.json");
                if (manifest) {
                    auto document = Json::parse(manifest);
                    const auto variant = document.at("variant").get<std::string>();
                    manifests.emplace(variant, std::move(document));
                }
            }
            auto& host = scene->liveDiagnostics_;
            host.Open(directory);
            const auto id = [&](const char* variant) { return manifests.at(variant).at("generation").get<std::string>(); };
            const auto artifact = [&](const char* variant, const char* extension) {
                return directory / id(variant) / ("EnemyAIDiagnostics-" + id(variant) + extension);
            };
            const auto publish = [&](const Json& manifest) {
                std::ofstream out(directory / "latest.json", std::ios::binary | std::ios::trunc);
                out << manifest;
                out.close();
                check(static_cast<bool>(out), "fixture manifest write failed");
            };
            const auto request = [&](Action action, const std::string& expected, std::string target = {}) {
                host.Request(action, std::move(target));
                scene->Update(app, 0);
                if (host.GenerationId() != expected || !host.Error().empty())
                    throw std::runtime_error("DLL switch failed: " + host.Error());
                check(before == snapshot(), "DLL operation changed live game state");
            };
            if (checkLiveRestart) {
                DWORD previousPid = 0;
                std::ifstream(directory / "first-pid.txt") >> previousPid;
                check(previousPid != 0 && previousPid != GetCurrentProcessId(), "restart test used the original process");
                check(host.GenerationId().empty() && host.Generations().size() == 4 &&
                    host.History().at("operations").back().at("kind") == "JSON apply", "history missing after process restart");
                request(Action::Restore, id("A"), id("A"));
                std::ofstream result("generated/enemy-ai-tests/scene-result.txt");
                result<<"PASS: persisted DLL catalog and JSON journal loaded after game process restart; restored A in PID "
                    <<GetCurrentProcessId()<<" (previous PID "<<previousPid<<").\n";
                return;
            }
            const auto apply = [&](const char* variant) {
                publish(manifests.at(variant));
                request(Action::ApplyLatest, id(variant));
                check(host.Evaluate(scene->MakeDiagnosticsInput()).has_value(), "loaded DLL did not produce diagnostics");
            };
            apply("A");
            const auto a = host.Evaluate(scene->MakeDiagnosticsInput());
            apply("B");
            const auto b = host.Evaluate(scene->MakeDiagnosticsInput());
            check(a && b && a->detail[0] == '\0' && b->detail[0] != '\0', "running DLL output did not change");
            apply("C");
            request(Action::Undo, id("B"));
            request(Action::Undo, id("A"));
            request(Action::Redo, id("B"));
            apply("D");
            check(!host.CanRedo() && host.Generations().size() == 4, "new DLL did not branch or preserve catalog");
            request(Action::Undo, id("B"));
            const auto reject = [&](Action action, const char* message) {
                const auto undo = host.CanUndo();
                const auto redo = host.CanRedo();
                host.Request(action);
                scene->Update(app, 0);
                check(host.GenerationId() == id("B") && host.CanUndo() == undo && host.CanRedo() == redo &&
                    host.Error().find(message) != std::string::npos && before == snapshot(), "failed DLL changed current generation/state or cursor");
            };
            for (const auto* extension : {".dll", ".pdb"}) {
                const auto original = artifact("D", extension);
                const auto hidden = std::filesystem::path(original).concat(".missing");
                std::filesystem::rename(original, hidden);
                const auto d = std::ranges::find(host.Generations(), id("D"), &LiveEnemyDiagnostics::Generation::id);
                try {
                    check(d != host.Generations().end() && !host.UnavailableReason(*d).empty(), "missing DLL/PDB not reported");
                    reject(Action::Redo, extension);
                }
                catch (...) { std::filesystem::rename(hidden, original); throw; }
                std::filesystem::rename(hidden, original);
            }
            auto badHash = manifests.at("D");
            badHash["generation"] = id("TEST_OUTPUT");
            publish(badHash);
            reject(Action::ApplyLatest, "SHA-256");
            publish(manifests.at("TEST_ABI"));
            reject(Action::ApplyLatest, "ABI");
            publish(manifests.at("TEST_OUTPUT"));
            reject(Action::ApplyLatest, "診断結果");
            request(Action::Redo, id("D"));
            host.Close();
            host.Open(directory);
            check(host.GenerationId().empty() && host.Generations().size() == 4 && !host.History().at("operations").empty(),
                "persisted DLL catalog could not be reopened");
            request(Action::Restore, id("C"), id("C"));
            request(Action::Undo, "");
            request(Action::Redo, id("C"));

            scene->checkpointAction_ = EnemyAITestScene::CheckpointAction::Capture;
            scene->Update(app, 0);
            check(scene->checkpoint_ && scene->manualCheckpoint_, "checkpoint capture failed");
            scene->player_.SetPositionForDebug(initialPlayer.translate + Vector3{1,0,0});
            scene->player_.ApplyDamage(10);
            scene->camera_.SetFovY(.9f);
            auto* enemy = scene->enemies_.front();
            enemy->SetPosition(enemy->GetPosition() + Vector3{1,0,0});
            enemy->ApplyDamage(size_t{0}, 1, {1,0,0});
            enemy->SetAIPaused(true);
            scene->pauseAll_ = false;
            scene->Update(app, 0);
            const auto mutated = snapshot();
            check(mutated != before, "checkpoint fixture did not change live state");
            scene->checkpoint_->schema = 999;
            scene->checkpointAction_ = EnemyAITestScene::CheckpointAction::Restore;
            scene->Update(app, 0);
            check(snapshot() == mutated && !host.History().at("operations").back().at("success").get<bool>(),
                "invalid checkpoint changed state");
            scene->checkpoint_->schema = EnemyAITestScene::Checkpoint::kSchema;
            scene->checkpointAction_ = EnemyAITestScene::CheckpointAction::Restore;
            scene->Update(app, 0);
            check(snapshot() == before && host.History().at("operations").back().at("success").get<bool>(),
                "checkpoint did not restore player/camera/enemy state");

            using Target = EnemyAITestScene::JsonTarget;
            const auto applyJson = [&](Target target, const std::string& draft, bool applyToRuntime = true) {
                scene->jsonRequest_ = EnemyAITestScene::JsonRequest{target, enemy->GetSpawnId(), draft, applyToRuntime};
                scene->Update(app, 0);
                check(before == snapshot(), "JSON operation changed live game state");
            };
            const auto operationCount = host.History().at("operations").size();
            scene->OpenJsonDraft(Target::EnemySenses);
            auto senses = Json::parse(scene->jsonDraft_);
            const float viewDistance = enemy->Senses().viewDistance;
            applyJson(Target::EnemySenses, "{");
            auto duplicate = senses.dump();
            duplicate.pop_back();
            duplicate += ",\"viewDistance\":43}";
            applyJson(Target::EnemySenses, duplicate);
            senses["viewDistance"] = -5;
            applyJson(Target::EnemySenses, senses.dump());
            check(enemy->Senses().viewDistance == viewDistance && host.History().at("operations").size() == operationCount + 3 &&
                !host.History().at("operations").back().at("success").get<bool>(), "invalid JSON was committed or not journaled");
            senses["viewDistance"] = 42;
            applyJson(Target::EnemySenses, senses.dump(), false);
            check(enemy->Senses().viewDistance == viewDistance, "validation applied a change");
            applyJson(Target::EnemySenses, senses.dump());
            check(enemy->Senses().viewDistance == 42 && host.History().at("operations").size() == operationCount + 4,
                "valid senses were not applied");

            const auto weaponPath = "resources/Data/weapons.json";
            const auto readWeaponFile = [&] {
                std::ifstream source(weaponPath, std::ios::binary);
                return std::string(std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>());
            };
            const auto diskWeapon = readWeaponFile();
            auto weapons = scene->liveWeaponDocument_;
            auto& equipped = *std::ranges::find_if(weapons.at("weapons"), [&](const Json& weapon) {
                return weapon.at("id") == scene->player_.CurrentWeapon().Definition().id;
            });
            const auto& runtime = scene->player_.CurrentWeapon();
            scene->weaponTestShot_ = true;
            scene->Update(app, 0);
            scene->weaponTestReload_ = true;
            scene->Update(app, 0);
            check(runtime.Reloading(), "test weapon did not start a reload");
            const auto ammo = std::tuple(runtime.Magazine(), runtime.Reserve(), runtime.ReloadState(), runtime.ReloadRemaining());
            const auto originalDamage = runtime.Definition().damage;
            equipped["damage"] = originalDamage + 1;
            equipped["magazineSize"] = runtime.Definition().magazineSize + 1;
            applyJson(Target::Weapons, weapons.dump());
            check(runtime.Definition().damage == originalDamage, "incompatible weapon migration applied");
            equipped["magazineSize"] = runtime.Definition().magazineSize;
            applyJson(Target::Weapons, weapons.dump());
            check(runtime.Definition().damage == originalDamage + 1 &&
                ammo == std::tuple(runtime.Magazine(), runtime.Reserve(), runtime.ReloadState(), runtime.ReloadRemaining()) &&
                diskWeapon == readWeaponFile(), "weapon change did not preserve ammo/reload or edited the source file");

            const auto setLua = [&](std::string label, std::string source = {}, bool file = false) {
                const auto state = snapshot();
                const auto nextSpawn = scene->nextId_;
                scene->luaRequest_ = EnemyAITestScene::LuaRequest{std::move(label), std::move(source), file};
                scene->Update(app, 0);
                check(state == snapshot() && scene->nextId_ == nextSpawn, "Lua update changed game state");
            };
            const auto luaId = [&] { return scene->liveScript_.at("generation").get<std::string>(); };
            const auto originalEnemy = enemy->CaptureLiveCheckpoint();
            check(originalEnemy.has_value(), "cannot snapshot Lua test enemy");
            auto lowHp = *originalEnemy;
            for (auto& part : lowHp.parts) part.hp *= .5f;
            for (auto& group : lowHp.groups) group.hp *= .5f;
            lowHp.ai.OrderPursuit(initialPlayer.translate);
            const auto resetLowHp = [&] { enemy->RestoreLiveCheckpoint(Enemy::LiveCheckpoint(lowHp)); };
            resetLowHp();
            for (auto* other : scene->enemies_) if (other != enemy) other->SetAIPaused(true);
            const auto measure = [&] {
                resetLowHp();
                const auto start = enemy->GetPosition();
                scene->pauseAll_ = false;
                scene->Update(app, .05f);
                scene->pauseAll_ = true;
                return EnemyAI::distanceTo(start, enemy->GetPosition()) / .05f;
            };
            setLua("A", {}, true);
            const float speedA = measure();
            setLua("B", {}, true);
            const float speedB = measure();
            setLua("C", {}, true);
            const float speedC = measure();
            enemy->ResetAI();
            scene->EvaluateLuaPolicies();
            check(scene->LuaSpeed(*enemy) == 1.0f, "Lua C used Chase multiplier while Idle");
            enemy->RestoreLiveCheckpoint(Enemy::LiveCheckpoint(*originalEnemy));
            scene->EvaluateLuaPolicies();
            check(scene->LuaSpeed(*enemy) == 1.0f, "Lua C used low HP multiplier at full HP");
            setLua("A", {}, true);
            check(speedA > 0 && std::abs(speedB / speedA - 1.5f) < .02f &&
                std::abs(speedC / speedA - 2.0f) < .02f && std::abs(measure() - speedA) < .02f,
                "Lua A/B/C did not change actual displacement as expected");
            const auto activeLua = luaId();
            for (const auto& [source, diagnostic] : {
                std::pair{"local =", "policy"},
                std::pair{"while true do end", "limit"},
                std::pair{"local a = {}; for i = 1, 100000 do a[i] = i end; return 1", "memory"},
                std::pair{"return os.execute('whoami')", "_ENV"},
                std::pair{"return 10", "between 0 and 3"}}) {
                setLua("Invalid", source);
                check(luaId() == activeLua && host.Error().find(diagnostic) != std::string::npos &&
                    !host.History().at("operations").back().at("success").get<bool>(), "unsafe Lua candidate accepted");
            }
            setLua("Runtime", "local e = ...; if e.id == 'Enemy_000' and e.distance < 8 then return missing() end; return 2");
            check(host.Error().empty(), "conditional runtime error fixture failed validation");
            scene->player_.SetStage(&scene->world_, enemy->GetPosition() + Vector3{0,0,-5}, {});
            scene->pauseAll_ = false;
            scene->Update(app, .001f);
            scene->pauseAll_ = true;
            check(scene->luaFailed_ && scene->LuaSpeed(*enemy) == 1.0f &&
                host.History().at("operations").back().at("kind") == "Lua runtime", "runtime fault did not fall back");
            scene->player_.SetStage(&scene->world_, initialPlayer.translate, initialPlayer.rotate);
            scene->player_.RefreshVisuals();
            enemy->RestoreLiveCheckpoint(Enemy::LiveCheckpoint(*originalEnemy));
            for (auto* other : scene->enemies_) other->SetAIPaused(false);
            setLua("A", {}, true);
            const auto batchBase = snapshot();
            const auto baselineLua = luaId();
            const auto baselineDll = host.GenerationId();
            const auto batch = [&](const char* variant, float range) {
                publish(manifests.at(variant));
                host.Request(Action::ApplyLatest);
                scene->luaRequest_ = EnemyAITestScene::LuaRequest{"Batch", "return 1.75"};
                senses["viewDistance"] = range;
                scene->jsonRequest_ = EnemyAITestScene::JsonRequest{Target::EnemySenses, enemy->GetSpawnId(), senses.dump(), true};
                scene->Update(app, 0);
                check(snapshot() == batchBase, "compound operation changed game state");
            };
            batch("A", -1);
            check(host.GenerationId() == baselineDll && luaId() == baselineLua && enemy->Senses().viewDistance == 42,
                "invalid JSON allowed partial commit");
            batch("TEST_ABI", 55);
            check(host.GenerationId() == baselineDll && luaId() == baselineLua && enemy->Senses().viewDistance == 42,
                "invalid DLL allowed partial commit");
            batch("A", 55);
            const auto batchLua = luaId();
            check(host.GenerationId() == id("A") && batchLua != baselineLua && enemy->Senses().viewDistance == 55 &&
                host.History().at("operations").back().at("changes").size() == 3, "compound commit failed");
            host.Request(Action::Undo);
            scene->Update(app, 0);
            check(host.GenerationId() == baselineDll && luaId() == baselineLua && enemy->Senses().viewDistance == 42 && host.CanRedo(),
                "compound Undo did not restore all resources");
            host.Request(Action::Redo);
            scene->Update(app, 0);
            check(host.GenerationId() == id("A") && luaId() == batchLua && enemy->Senses().viewDistance == 55,
                "compound Redo did not restore all resources");
            host.Request(Action::Undo);
            scene->Update(app, 0);
            setLua("Branch", "return 0.5");
            check(!host.CanRedo() && snapshot() == batchBase, "Lua branch did not drop compound redo");
            // Leave a JSON entry at the end for the existing restart assertion.
            senses["viewDistance"] = 43;
            applyJson(Target::EnemySenses, senses.dump());
            std::ofstream(directory / "lua-result.txt") << "PASS: A/B/C/A measured speeds " <<
                speedA << " / " << speedB << " / " << speedC << " units/s; bounded Lua failures and atomic DLL/Lua/JSON Undo/Redo.\n";
            std::ofstream(directory / "first-pid.txt") << GetCurrentProcessId();
        } else {
            scene->liveDiagnostics_.Request(LiveEnemyDiagnostics::Action::ApplyLatest);
            check(scene->liveDiagnostics_.GenerationId().empty(),"live DLL was applied before the scene update");
            scene->Update(app,0);
            if (scene->liveDiagnostics_.GenerationId().empty())
                throw std::runtime_error("live DLL was not applied: " + scene->liveDiagnostics_.Error());
            check(scene->liveDiagnostics_.Evaluate(scene->MakeDiagnosticsInput()).has_value(),"live DLL did not produce diagnostics");
            check(before==snapshot(),"applying the live DLL changed gameplay state");
        }
#else
        throw std::runtime_error("Live enemy diagnostics require Debug x64.");
#endif
    }
    const auto initialPixels=captureEnvironment(L"generated/enemy-ai-tests/camera-initial.png");
    scene->player_.SetStage(&scene->world_,initialPlayer.translate,initialPlayer.rotate+Vector3{.12f,.45f,0});
    scene->Update(app,0);
    const auto rotatedPixels=captureEnvironment(L"generated/enemy-ai-tests/camera-rotated.png");
    check(initialPixels!=rotatedPixels,"camera rotation left environment frozen (stale object WVP)");
    scene->player_.SetStage(&scene->world_,initialPlayer.translate+Vector3{3,0,0},initialPlayer.rotate);
    scene->Update(app,0);
    const auto movedPixels=captureEnvironment(L"generated/enemy-ai-tests/camera-moved.png");
    check(initialPixels!=movedPixels,"camera translation left environment frozen (stale object WVP)");
    scene->player_.SetStage(&scene->world_,initialPlayer.translate,initialPlayer.rotate);
    scene->Update(app,0);
    check(initialPixels==captureEnvironment(L"generated/enemy-ai-tests/camera-restored.png"),"restoring camera did not restore environment pixels");
    // Use the actual pooled enemy and the same corner colliders as the playable
    // chase/lost-sight area. The north-east hiding point is occluded by the L wall.
    auto* corner=scene->enemies_[3];
    { auto senses=corner->Senses(); senses.detectionTime=0; senses.fieldOfView=360; corner->ConfigureSenses(senses); }
    const Vector3 observed{-24,0,20}, hidden{-14,0,40};
    for (int i=0;i<5;++i) corner->Update(.1f,observed+Vector3{static_cast<float>(i-4),0,0},&scene->world_);
    check(corner->AI().visible && corner->GetState()==EnemyState::Chase,"corner area cannot initially detect player");
    check(corner->AI().memory.count>=2 && corner->AI().memory.hasDirection && corner->AI().memory.escapeDirection.x>.9f,"lateral visible history did not infer east escape");
    double lastVisibleTime=corner->AI().memory.samples[corner->AI().memory.count-1].time;
    const auto chaseStart=corner->GetPosition();
    for (int i=0;i<600;++i) {
        const auto damage=corner->Update(.1f,hidden,&scene->world_);
        const auto& ai=corner->AI();
        check(ai.pursuitOrdered && StageLength(ai.lastSeen-hidden)<.001f &&
            (corner->GetState()==EnemyState::Chase || corner->GetState()==EnemyState::Attack),"corner discovery returned to Search/Wander or lost live target");
        if (i==0) check(!ai.visible && ai.occluded,"corner hiding fixture did not break sight");
        if (ai.visible) lastVisibleTime=ai.memory.clock;
        for (size_t j=0;j<ai.memory.count;++j)
            check(ai.memory.samples[j].time<=lastVisibleTime,"corner history recorded a hidden position");
        check(!ai.occluded || (damage==0 && corner->PendingAttackCount()==0),"persistent corner chase attacked through wall");
        check(EnemyNavigation::Clear(&scene->world_,corner->GetPosition(),corner->GetPosition(),corner->Definition().collisionRadius,corner->Definition().collisionHeight),"persistent corner chase intersected wall");
    }
    check(StageLength(corner->GetPosition()-chaseStart)>5 && EnemyAI::distanceTo(corner->GetPosition(),hidden)<2,"persistent corner chase did not reach player around L wall");
    corner->ResetAI(); corner->ConfigureSenses(corner->Definition().senses);
    check(!corner->AI().pursuitOrdered && corner->AI().memory.count==0 && !corner->AI().memory.hasDirection && corner->AI().memory.clock==0,"AI reset retained discovery/history");
    auto* enemy=scene->enemies_[0]; scene->selected_=0;
    const auto at=enemy->GetPosition();
    { auto senses=enemy->Senses(); enemy->ResetAI(); senses.viewDistance=0; enemy->ConfigureSenses(senses); }
    enemy->Update(1,at+Vector3{0,0,5},&scene->world_);
    check(enemy->GetState()==EnemyState::Idle && !enemy->AI().hasLastSeen,"live distance adjustment ignored");
    { auto senses=enemy->Senses(); senses.viewDistance=100; senses.fieldOfView=360; senses.detectionTime=0; enemy->ConfigureSenses(senses); }
    enemy->Update(.1f,at+Vector3{0,0,5},&scene->world_);
    check(enemy->GetState()==EnemyState::Chase && enemy->AI().hasLastSeen,"live detection adjustment ignored");
    enemy->SetAIPaused(true); const auto stopped=enemy->GetPosition();
    enemy->Update(1,at+Vector3{0,0,6},&scene->world_);
    check(StageLength(enemy->GetPosition()-stopped)==0 && enemy->PendingAttackCount()==0,"selected AI pause failed");
    const auto primePursuit=[&]() {
        { auto senses=enemy->Senses(); enemy->ResetAI(); senses.viewDistance=100;
          senses.detectionTime=0; senses.fieldOfView=360; enemy->ConfigureSenses(senses); }
        enemy->SetAIPaused(false);
        enemy->Update(.1f,enemy->GetPosition()+Vector3{0,0,5},&scene->world_);
        check(enemy->AI().pursuitOrdered,"pooled enemy did not latch its own discovery");
        { auto senses=enemy->Senses(); senses.viewDistance=0; senses.loseSightTime=0;
          senses.searchDuration=0; senses.memoryRetentionTime=.2f; enemy->ConfigureSenses(senses); }
        for (int i=0;i<70;++i) enemy->Update(.1f,{1000,0,1000},&scene->world_);
        check(enemy->GetState()==EnemyState::Chase && enemy->AI().pursuitOrdered && !enemy->AI().visible &&
            enemy->AI().lastSeen.x==1000 && enemy->AI().lastSeen.z==1000,"live sense tuning or distance loss cleared discovery");
    };
    primePursuit();
    const auto beforeReset=enemy->GetPosition();
    enemy->ResetAI();
    check(!enemy->AI().pursuitOrdered && !enemy->AI().hasLastSeen && !enemy->AI().paused && enemy->AI().navigation.path.empty() &&
        enemy->AI().searchElapsed==0 && enemy->AI().searchPointsVisited==0 && !enemy->AI().searchFinishing &&
        !enemy->AI().lastSeenReached && !enemy->AI().investigateUnreachable && enemy->AI().investigateBlockedTime==0 &&
        enemy->AI().memory.count==0 && !enemy->AI().memory.hasDirection && enemy->AI().memory.clock==0 &&
        StageLength(enemy->GetPosition()-beforeReset)==0,"AI reset teleported or retained search memory");
    enemy->ConfigureSenses(enemy->Definition().senses);
    primePursuit(); // Recycle a slot while discovery/path/live tuning are dirty.
    { auto senses=enemy->Senses(); senses.memoryRetentionTime=.3f; senses.memoryRecordInterval=0; senses.searchDirectionPriority=.2f; enemy->ConfigureSenses(senses); }
    const auto definition=enemy->Definition().id;
    const auto spawnId=enemy->GetSpawnId();
    scene->Remove(app,false); check(scene->pool_.Active()==7 && scene->enemies_.size()==7,"selected deletion failed");
    scene->definition_=definition;
    scene->Spawn(app,{-24,0,2});
    auto* reused=scene->enemies_.back();
    check(reused==enemy && reused->GetSpawnId()!=spawnId && !reused->AI().pursuitOrdered && !reused->AI().hasLastSeen &&
        reused->Senses().detectionTime==reused->Definition().senses.detectionTime && reused->AI().searchElapsed==0 &&
        reused->AI().searchPointsVisited==0 && reused->AI().SearchRemaining()==0 && !reused->AI().searchFinishing &&
        !reused->AI().investigateUnreachable && reused->AI().navigation.path.empty() &&
        reused->AI().memory.count==0 && !reused->AI().memory.hasDirection && reused->AI().memory.clock==0 &&
        reused->Senses().searchSpeedMultiplier==reused->Definition().senses.searchSpeedMultiplier &&
        reused->Senses().memoryRetentionTime==reused->Definition().senses.memoryRetentionTime &&
        reused->Senses().memoryRecordInterval==reused->Definition().senses.memoryRecordInterval &&
        reused->Senses().searchDirectionPriority==reused->Definition().senses.searchDirectionPriority,"pool reuse leaked AI search tuning/memory");
    scene->pauseAll_=true; frame();
    const auto frozen=reused->GetPosition(); frame();
    check(StageLength(frozen-reused->GetPosition())==0,"global pause moved enemy");
    scene->Remove(app,true); check(scene->pool_.Active()==0 && scene->enemies_.empty(),"delete all left active slots");
    for (const auto& [id,d]:scene->definitions_.All()) {
        (void)d; scene->definition_=id; scene->Spawn(app,{0,0,2});
        check(scene->enemies_.size()==1 && scene->enemies_[0]->Definition().id==id,"definition selector/spawn failed");
        scene->Remove(app,true);
    }
    scene->definition_="normal"; scene->Spawn(app,{24,0,-2});
    check(scene->enemies_.empty() && !scene->status_.empty(),"spawn inside wall accepted");
    transition("Title");
    check(dynamic_cast<TitleScene*>(app.Scenes().Current())!=nullptr && !app.GetInput()->IsCameraControlEnabled(),"return to title/input cleanup failed");
    std::ofstream result("generated/enemy-ai-tests/scene-result.txt");
    result<<"PASS: real D3D title fades/camera restoration, actual pooled enemy independently latches sight, follows player around L wall for 60s without Search/Wander, no hidden visual samples or wall attacks; live sense tuning/distance loss retain pursuit; pause/reset and same-slot reuse clear discovery/history/path; all definitions; wall spawn rejection.\n";
    if (checkLiveDiagnostics) result<<"PASS: live DLL applied at scene update; PID, scene, player, camera, AI pause, enemy instances, IDs, positions and part HP preserved.\n";
    if (checkLivePhase2) result<<"PASS: DLL A/B/C Undo/Redo, branch D, missing DLL/PDB, SHA-256, exported ABI, invalid output, reopened history and arbitrary restore; same PID/scene/entities/HP; checkpoint capture, invalid schema and restore; JSON syntax/range rejection, weapon reload migration and disk isolation.\n";
}
