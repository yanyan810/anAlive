#include "EnemyAITestScene.h"
#include "GameApp.h"
#include "DebugJsonEditor.h"
#include "WeaponEditor.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.cpp"
#include <array>
#include <objbase.h>
#include <ranges>
#include <set>
#include <stdexcept>
#include <utility>

namespace {
using Json = nlohmann::json;
using Action = LiveEnemyDiagnostics::Action;
constexpr char kWeaponsPath[] = "resources/Data/weapons.json";
constexpr size_t kMaxDraftBytes = 64 * 1024;

#pragma comment(lib, "ole32.lib")

}

std::string EnemyAITestScene::NewLiveId() {
    GUID id{};
    if (FAILED(CoCreateGuid(&id))) throw std::runtime_error("Cannot create Lua generation ID");
    char value[33]{};
    std::snprintf(value, sizeof(value), "%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
        id.Data1, static_cast<unsigned>(id.Data2), static_cast<unsigned>(id.Data3),
        static_cast<unsigned>(id.Data4[0]), static_cast<unsigned>(id.Data4[1]),
        static_cast<unsigned>(id.Data4[2]), static_cast<unsigned>(id.Data4[3]),
        static_cast<unsigned>(id.Data4[4]), static_cast<unsigned>(id.Data4[5]),
        static_cast<unsigned>(id.Data4[6]), static_cast<unsigned>(id.Data4[7]));
    return value;
}

namespace {
std::string ReadLuaFile(const std::string& variant) {
    if (variant != "A" && variant != "B" && variant != "C")
        throw std::runtime_error("Unknown Lua sample");
    std::ifstream file("resources/LiveDevelopment/enemy-" + variant + ".lua", std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open Lua sample " + variant);
    std::string source(LiveLua::kSourceLimit + 1, '\0');
    file.read(source.data(), static_cast<std::streamsize>(source.size()));
    if (file.bad() || !file.eof()) throw std::runtime_error("Cannot read Lua sample or source exceeds 16 KiB");
    source.resize(static_cast<size_t>(file.gcount()));
    return source;
}

float RunLuaPolicy(LiveLua& lua, const std::array<LiveLua::Field, 7>& fields, LiveLua::Budget& budget) {
    const auto result = lua.Evaluate(fields, budget);
    if (result < 0 || result > 3)
        throw LiveLua::Error(LiveLua::ErrorKind::Result, "Lua speed multiplier must be between 0 and 3");
    return static_cast<float>(result);
}

std::array<LiveLua::Field, 7> PolicyFields(std::string_view id, std::string_view type, double hp, double maxHp,
    std::string_view state, double distance, double multiplier) {
    return {{{"id", id}, {"type", type}, {"hp", hp}, {"maxHp", maxHp}, {"state", state},
        {"distance", distance}, {"speedMultiplier", multiplier}}};
}

bool Finite(const Vector3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

struct SenseField {
    const char* name;
    float EnemySenses::*member;
    float max;
};
constexpr SenseField kSenseFields[] = {
    {"viewDistance", &EnemySenses::viewDistance, 100},
    {"fieldOfView", &EnemySenses::fieldOfView, 360},
    {"detectionTime", &EnemySenses::detectionTime, 5},
    {"loseSightTime", &EnemySenses::loseSightTime, 5},
};

Json EncodeSenses(const EnemySenses& senses) {
    auto document = Json::object();
    for (const auto& field : kSenseFields) document[field.name] = senses.*field.member;
    return document;
}

EnemySenses ParseSenses(const Json& draft, EnemySenses senses) {
    if (!draft.is_object() || draft.size() != std::size(kSenseFields))
        throw std::runtime_error("索敵 JSON には表示された 4 項目が必要です。");
    for (const auto& field : kSenseFields) {
        const auto& value = draft.at(field.name);
        const auto number = value.is_number() ? value.get<double>() : std::numeric_limits<double>::quiet_NaN();
        if (!std::isfinite(number) || number < 0 || number > field.max)
            throw std::runtime_error(std::string("値が範囲外です: ") + field.name);
        senses.*field.member = static_cast<float>(number);
    }
    if (!senses.IsValid()) throw std::runtime_error("索敵設定が無効です。");
    return senses;
}

Json ParseDraft(const std::string& source) {
    if (source.size() > kMaxDraftBytes) throw std::runtime_error("変更案が 64 KiB を超えています。");
    std::vector<std::set<std::string>> keys;
    return Json::parse(source, [&](int depth, Json::parse_event_t event, Json& value) {
        const auto index = static_cast<size_t>(depth);
        if (depth > 32) throw std::runtime_error("JSON のネストが深すぎます。");
        if (event == Json::parse_event_t::object_start) {
            keys.resize(index + 1);
            keys.back().clear();
        } else if (event == Json::parse_event_t::key && !keys.at(index - 1).insert(value.get<std::string>()).second) {
            throw std::runtime_error("JSON に重複したキーがあります。");
        }
        return true;
    });
}

void ValidateWeaponFields(const Json& draft) {
    const auto fields = WeaponEditor::Encode(WeaponDefinition{});
    for (const auto& weapon : draft.at("weapons")) {
        for (const auto& [key, value] : weapon.items()) {
            if (key == "spreadDegrees") continue; // Legacy alias accepted by WeaponSystem.
            const auto field = fields.find(key);
            if (field == fields.end() || (field->is_number() ? !value.is_number() : value.type() != field->type()))
                throw std::runtime_error("武器の未対応項目または無効な型です: " + key);
        }
    }
}
}

EnemyAITestScene::Checkpoint EnemyAITestScene::CaptureCheckpoint() const {
    Checkpoint state;
    state.id = checkpointId_ + 1;
    state.nextSpawnId = nextId_;
    state.player = player_.CaptureDebug();
    state.cameraPosition = camera_.GetTranslate();
    state.cameraRotation = camera_.GetRotate();
    state.fov = camera_.GetFovY();
    state.projectiles = projectiles_;
    state.area = area_;
    state.selected = selected_;
    state.definition = definition_;
    state.spawnPosition = spawnPosition_;
    state.paused = pauseAll_;
    state.invulnerable = invulnerable_;
    for (const auto* enemy : enemies_) {
        auto saved = enemy->CaptureLiveCheckpoint();
        if (!saved) throw std::runtime_error("分離したモデル片がある敵はチェックポイントの対象外です。");
        state.enemies.push_back(std::move(*saved));
    }
    ValidateCheckpoint(state);
    return state;
}

void EnemyAITestScene::ValidateCheckpoint(const Checkpoint& state) const {
    if (state.schema != Checkpoint::kSchema || state.scene != "EnemyAITest")
        throw std::runtime_error("チェックポイントのシーンまたはスキーマが一致しません。");
    if (state.enemies.size() != enemies_.size() || state.nextSpawnId != nextId_)
        throw std::runtime_error("チェックポイントと現在の敵の構成が一致しません。");
    for (size_t i = 0; i < enemies_.size(); ++i) {
        if (!enemies_[i]->CanRestoreLiveCheckpoint(state.enemies[i]))
            throw std::runtime_error("チェックポイントの敵 ID、部位構成、または状態が無効です。");
    }
    const auto& p = state.player;
    if (!Finite(p.transform.translate) || !Finite(p.transform.rotate) || !Finite(p.transform.scale) ||
        !std::isfinite(p.hp) || p.hp < 0 || p.hp > 100 || !std::isfinite(p.motion.verticalSpeed) ||
        !Finite(state.cameraPosition) || !Finite(state.cameraRotation) ||
        !std::isfinite(state.fov) || state.fov <= 0 || state.fov >= std::numbers::pi_v<float> ||
        !Finite(state.spawnPosition) || state.area < 0 || state.area >= 6 || !definitions_.Find(state.definition) ||
        state.selected < 0 || (enemies_.empty() ? state.selected != 0 : static_cast<size_t>(state.selected) >= enemies_.size()) ||
        state.projectiles.Projectiles().size() > projectileVisuals_.size())
        throw std::runtime_error("チェックポイントのプレイヤー、カメラ、または出現状態が無効です。");
}

void EnemyAITestScene::RestoreCheckpoint(Checkpoint&& state) noexcept {
    player_.RestoreLiveCheckpoint(std::move(state.player));
    camera_.SetFovY(state.fov);
    projectiles_ = std::move(state.projectiles);
    nextId_ = state.nextSpawnId;
    selected_ = state.selected;
    area_ = state.area;
    definition_ = std::move(state.definition);
    spawnPosition_ = state.spawnPosition;
    pauseAll_ = state.paused;
    invulnerable_ = state.invulnerable;
    for (size_t i = 0; i < enemies_.size(); ++i) enemies_[i]->RestoreLiveCheckpoint(std::move(state.enemies[i]));
}

Json EnemyAITestScene::DescribeCheckpoint(const Checkpoint& state, const char* result) const {
    return {{"id", state.id}, {"schemaVersion", state.schema}, {"scene", state.scene},
        {"enemyCount", state.enemies.size()}, {"nextSpawnId", state.nextSpawnId}, {"result", result}};
}

bool EnemyAITestScene::UpdateLiveDevelopment(GameApp& app) {
    if (aiResearchMode_) {
        UpdateResearch(app);
        return false;
    }
    UpdateAiChat(app);
    if (const auto action = std::exchange(checkpointAction_, std::nullopt)) {
        std::optional<Checkpoint> restore;
        try {
            if (*action == CheckpointAction::Capture) {
                auto saved = CaptureCheckpoint();
                liveDiagnostics_.RecordResult("Checkpoint capture", true, "チェックポイントを作成・検証しました。",
                    Json::array(), DescribeCheckpoint(saved, "captured and validated"));
                checkpointId_ = saved.id;
                checkpoint_ = std::move(saved);
                manualCheckpoint_ = true;
                checkpointStatus_ = "作成・検証済み";
            } else {
                if (!checkpoint_) throw std::runtime_error("復元するチェックポイントがありません。");
                auto saved = *checkpoint_;
                ValidateCheckpoint(saved);
                const auto& current = player_.CurrentWeapon().Definition();
                if (!current.id.empty() && !saved.player.weapon.ApplyLiveDefinition(current))
                    throw std::runtime_error("保存時の装弾・リロード状態を現在の武器定義に移行できません。");
                // Settings have their own history. Restore the saved AI progression against the current tuning.
                for (size_t i = 0; i < enemies_.size(); ++i)
                    saved.enemies[i].ai.settings.senses = enemies_[i]->Senses();
                liveDiagnostics_.RecordResult("Checkpoint restore", true, "チェックポイントを検証し、状態を復元しました。",
                    Json::array(), DescribeCheckpoint(saved, "validated and restored"));
                restore = std::move(saved);
            }
        } catch (const std::exception& error) {
            checkpointStatus_ = error.what();
            try {
                liveDiagnostics_.RecordResult(*action == CheckpointAction::Restore ? "Checkpoint restore" : "Checkpoint capture",
                    false, checkpointStatus_, Json::array(),
                    *action == CheckpointAction::Restore && checkpoint_ ? DescribeCheckpoint(*checkpoint_, "rejected") : Json(nullptr));
            } catch (const std::exception&) {
                // The journal reports its own error in the panel.
            }
        }
        if (restore) {
            RestoreCheckpoint(std::move(*restore));
            player_.RefreshVisuals();
            camera_.SetTranslate(restore->cameraPosition);
            camera_.SetRotate(restore->cameraRotation);
            camera_.Update();
            for (auto& object : geometry_) object->Update(0);
            for (auto* enemy : enemies_) enemy->UpdateVisuals(0);
            SyncProjectiles(app);
            checkpointStatus_ = "復元済み";
            return true;
        }
    } else if (liveDiagnostics_.HasPending() || luaRequest_ || jsonRequest_) {
        auto lua = std::exchange(luaRequest_, std::nullopt);
        auto json = std::exchange(jsonRequest_, std::nullopt);
        Json changes = Json::array();
        if (lua) changes.push_back({{"resource", "enemy.lua"}, {"before", liveScript_}, {"after",
            {{"label", lua->label}, {"source", lua->source}, {"apiVersion", 1}, {"generation", lua->generation}}}});
        if (json) changes.push_back(MakeJsonChange(*json));
        PreparedChanges prepared;
        const auto prepare = [&](Json& operation, bool replay) {
            if (lua && lua->readFile) operation.front().at("after")["source"] = ReadLuaFile(lua->label);
            PrepareChanges(operation, replay, prepared);
        };
        if (json && !json->apply) {
            try {
                PrepareChanges(changes, false, prepared);
                jsonStatus_ = "構文・値・移行ルールを検証しました。";
            } catch (const std::exception& error) {
                jsonStatus_ = error.what();
            }
            return false;
        }

        Json details;
        try {
            auto saved = CaptureCheckpoint();
            details = DescribeCheckpoint(saved, "captured and validated; in-place switch");
            checkpointId_ = saved.id;
            if (!manualCheckpoint_) checkpoint_ = std::move(saved);
            checkpointStatus_ = "適用前の状態を検証しました。";
        } catch (const std::exception& error) {
            checkpointStatus_ = error.what();
            details = {{"schemaVersion", Checkpoint::kSchema}, {"scene", "EnemyAITest"},
                {"result", "capture rejected; in-place switch"}, {"error", checkpointStatus_}};
        }
        const char* kind = lua ? "Lua apply" : "JSON apply";
        const bool applied = liveDiagnostics_.ApplyPending(MakeDiagnosticsInput(), std::move(details),
            std::move(changes), prepare, kind);
        if (applied) CommitChanges(std::move(prepared));
        const auto& error = liveDiagnostics_.Error();
        if (json) jsonStatus_ = applied ? "JSON を反映しました。" : error.empty() ? "差分はありません。" : error;
        if (lua) luaStatus_ = applied ? "Lua を反映しました。" : error.empty() ? "差分はありません。" : error;
    } else if (const auto id = std::exchange(halfHealthEnemy_, std::nullopt)) {
        const auto enemy = std::ranges::find(enemies_, *id, &Enemy::GetSpawnId);
        if (enemy != enemies_.end() && !(*enemy)->IsDead()) {
            if (auto saved = (*enemy)->CaptureLiveCheckpoint()) {
                const auto [hp, maxHp] = (*enemy)->LiveHealth();
                if (hp > maxHp * .5f) {
                    const float factor = maxHp * .5f / hp;
                    for (auto& part : saved->parts) part.hp *= factor;
                    for (auto& group : saved->groups) group.hp *= factor;
                    (*enemy)->RestoreLiveCheckpoint(std::move(*saved));
                }
            }
        }
    }
    return false;
}

void EnemyAITestScene::LoadLiveWeapons() {
    try {
        DebugJsonEditor source;
        if (!source.Open(kWeaponsPath)) throw std::runtime_error(source.error);
        WeaponSystem weapons;
        if (!weapons.LoadLiveDefinitions(source.document)) throw std::runtime_error(weapons.Error());
        const auto* initial = weapons.InitialWeapon();
        // The test room uses a pistol when the FPS stage has no initial weapon.
        if (!initial) initial = weapons.Find("pistol");
        if (initial && !player_.EquipWeapon(*initial))
            throw std::runtime_error("武器を装備できません。");
        liveWeaponDocument_ = std::move(source.document);
    } catch (const std::exception& error) {
        jsonStatus_ = error.what();
    }
}

void EnemyAITestScene::OpenJsonDraft(JsonTarget target) {
    try {
        if (target == JsonTarget::EnemySenses) {
            if (enemies_.empty()) throw std::runtime_error("敵を選択してください。");
            const auto* enemy = enemies_[selected_];
            jsonDraft_ = EncodeSenses(enemy->Senses()).dump(2);
            jsonEnemyId_ = enemy->GetSpawnId();
        } else {
            if (liveWeaponDocument_.is_null()) throw std::runtime_error("実行中の武器定義がありません。");
            jsonDraft_ = liveWeaponDocument_.dump(2);
        }
        jsonTarget_ = target;
        jsonDraftOpen_ = true;
        jsonStatus_ = "実行中の設定から変更案を作成しました。";
    } catch (const std::exception& error) {
        jsonStatus_ = error.what();
    }
}

Json EnemyAITestScene::MakeJsonChange(const JsonRequest& request) const {
    if (request.target == JsonTarget::EnemySenses) {
        const auto enemy = std::ranges::find(enemies_, request.enemyId, &Enemy::GetSpawnId);
        return {{"resource", "enemy.senses"}, {"spawnId", request.enemyId},
            {"enemyId", enemy == enemies_.end() ? "" : (*enemy)->GetId()},
            {"before", enemy == enemies_.end() ? Json(nullptr) : EncodeSenses((*enemy)->Senses())},
            {"after", request.draft}};
    }
    return {{"resource", kWeaponsPath}, {"before", liveWeaponDocument_}, {"after", request.draft}};
}

void EnemyAITestScene::PrepareChanges(Json& changes, bool replay, PreparedChanges& prepared) {
    for (auto& change : changes) {
        const auto& resource = change.at("resource");
        auto& before = change.at("before");
        auto& after = change.at("after");
        if (resource == "EnemyAIDiagnostics") continue;
        if (resource == "enemy.lua") {
            if (!replay && !liveScript_.is_null() && liveScript_.at("source") == after.at("source") &&
                liveScript_.at("label") == after.at("label") && !luaFailed_ &&
                liveScript_.value("overrides", Json(nullptr)) == after.value("overrides", Json(nullptr)) &&
                (after.at("generation") == "" || after.at("generation") == liveScript_.at("generation"))) {
                after = liveScript_;
                continue;
            }
            if (!after.is_null()) {
                if (after.at("apiVersion") != 1) throw std::runtime_error("Lua API version mismatch");
                LiveLua::Budget budget;
                // Preparation compiles and probes multiple VMs once per edit.
                // The simulation path keeps its independent 2 ms budget.
                budget.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(15);
                const auto compile = [&](const std::string& source, std::string_view id, std::string_view type) {
                    if (source.size() > LiveLua::kSourceLimit) throw std::runtime_error("Lua source exceeds 16 KiB");
                    if (!source.empty() && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(),
                        static_cast<int>(source.size()), nullptr, 0) == 0) throw std::runtime_error("Lua source must be UTF-8");
                    auto policy = std::make_unique<LiveLua>(source);
                    RunLuaPolicy(*policy, PolicyFields(id, type, 100, 100, "Idle", 10, 1), budget);
                    RunLuaPolicy(*policy, PolicyFields(id, type, 50, 100, "Chase", 10, 1), budget);
                    RunLuaPolicy(*policy, PolicyFields(id, type, 50, 100, "Idle", 10, 1), budget);
                    RunLuaPolicy(*policy, PolicyFields(id, type, 30, 100, "Idle", 10, 1), budget);
                    return policy;
                };
                if (!after.contains("source")) throw std::runtime_error("Lua source is missing");
                prepared.lua = compile(after.at("source").get_ref<const std::string&>(), "probe", "Normal");
                if (after.contains("overrides")) {
                    const auto& overrides = after.at("overrides");
                    if (!overrides.is_array() || overrides.size() > 8 || after.at("sceneInstance") != aiSceneId_)
                        throw std::runtime_error("選択した敵向けのLuaは同じシーン内で復元してください。上限は8体です。");
                    for (const auto& entry : overrides) {
                        const auto id = entry.at("spawnId").get<uint64_t>();
                        const auto enemy = std::ranges::find(enemies_, id, &Enemy::GetSpawnId);
                        // A removed enemy's policy is inert and its spawn ID will not be reused.
                        if (enemy == enemies_.end()) continue;
                        const auto type = EnemyTypeName((*enemy)->Definition().type);
                        ScopedLua scoped;
                        scoped.policy = compile(entry.at("source").get_ref<const std::string&>(), (*enemy)->GetId(), type);
                        if (entry.contains("fallback"))
                            scoped.fallback = compile(entry.at("fallback").get_ref<const std::string&>(), (*enemy)->GetId(), type);
                        if (!prepared.overrides.emplace(id, std::move(scoped)).second)
                            throw std::runtime_error("Duplicate Lua target");
                    }
                }
                const auto player = player_.GetTransform().translate;
                for (const auto* enemy : enemies_) {
                    const auto [hp, maxHp] = enemy->LiveHealth();
                    const auto scoped = prepared.overrides.find(enemy->GetSpawnId());
                    auto& policy = scoped == prepared.overrides.end() ? prepared.lua : scoped->second.policy;
                    RunLuaPolicy(*policy, PolicyFields(enemy->GetId(), EnemyTypeName(enemy->Definition().type),
                        hp, maxHp, EnemyStateName(enemy->GetState()), EnemyAI::distanceTo(player, enemy->GetPosition()),
                        LuaSpeed(*enemy)), budget);
                }
                if (after.at("generation") == "") after["generation"] = NewLiveId();
            }
            prepared.script = after;
            prepared.replaceLua = true;
        } else if (resource == "enemy.senses") {
            const auto enemy = std::ranges::find(enemies_, change.at("spawnId").get<uint64_t>(), &Enemy::GetSpawnId);
            if (enemy == enemies_.end()) throw std::runtime_error("変更案の対象の敵が見つかりません。");
            const auto current = EncodeSenses((*enemy)->Senses());
            if (replay && before != current) throw std::runtime_error("索敵設定が履歴の状態から変更されています。");
            before = current;
            if (!replay) after = ParseDraft(after.get_ref<const std::string&>());
            prepared.senses = ParseSenses(after, (*enemy)->Senses());
            after = EncodeSenses(prepared.senses);
            prepared.enemy = *enemy;
        } else if (resource == kWeaponsPath) {
            if (replay && before != liveWeaponDocument_) throw std::runtime_error("武器定義が履歴の状態から変更されています。");
            before = liveWeaponDocument_;
            if (!replay) after = ParseDraft(after.get_ref<const std::string&>());
            if (!after.is_object()) throw std::runtime_error("武器定義は JSON オブジェクトで指定してください。");
            auto otherFields = after;
            auto previousFields = liveWeaponDocument_;
            otherFields.erase("weapons");
            previousFields.erase("weapons");
            if (otherFields != previousFields) throw std::runtime_error("実行中に変更できる武器 JSON 項目は weapons のみです。");
            WeaponSystem weapons;
            if (!weapons.LoadLiveDefinitions(after)) throw std::runtime_error(weapons.Error());
            ValidateWeaponFields(after);
            auto runtime = player_.CurrentWeapon();
            if (!runtime.Definition().id.empty()) {
                const auto* definition = weapons.Find(runtime.Definition().id);
                if (!definition || !runtime.ApplyLiveDefinition(*definition))
                    throw std::runtime_error("装備中の武器 ID、リロード中またはバースト中の変更に互換性がありません。");
            }
            prepared.weapon = std::move(runtime);
            prepared.weapons = after;
        } else {
            throw std::runtime_error("Unsupported live resource");
        }
    }
}

void EnemyAITestScene::CommitChanges(PreparedChanges&& prepared) noexcept {
    if (prepared.enemy) prepared.enemy->ConfigureSenses(prepared.senses);
    if (prepared.weapon) {
        player_.ApplyLiveWeapon(std::move(*prepared.weapon));
        liveWeaponDocument_.swap(prepared.weapons);
    }
    if (prepared.replaceLua) {
        lua_ = std::move(prepared.lua);
        luaOverrides_.swap(prepared.overrides);
        liveScript_.swap(prepared.script);
        luaFailed_ = false;
        luaStatus_.clear();
        luaSpeeds_.clear();
        luaSpeedIds_.clear();
    }
}

float EnemyAITestScene::LuaSpeed(const Enemy& enemy) const {
    const auto found = std::ranges::find(luaSpeedIds_, enemy.GetSpawnId());
    return found == luaSpeedIds_.end() ? 1.0f : luaSpeeds_[found - luaSpeedIds_.begin()];
}

void EnemyAITestScene::EvaluateLuaPolicies() {
    if (!lua_ || luaFailed_) return;
    std::vector<float> speeds(enemies_.size(), 1.0f);
    std::vector<uint64_t> ids;
    ids.reserve(enemies_.size());
    std::vector<std::string> scopedErrors;
    LiveLua::Budget budget;
    const auto player = player_.GetTransform().translate;
    try {
        for (size_t i = 0; i < enemies_.size(); ++i) {
            const auto* enemy = enemies_[i];
            ids.push_back(enemy->GetSpawnId());
            if (!enemy->IsDead() && !enemy->AI().paused) {
                const auto [hp, maxHp] = enemy->LiveHealth();
                const auto fields = PolicyFields(enemy->GetId(), EnemyTypeName(enemy->Definition().type),
                    hp, maxHp, EnemyStateName(enemy->GetState()), EnemyAI::distanceTo(player, enemy->GetPosition()),
                    LuaSpeed(*enemy));
                const auto found = luaOverrides_.find(enemy->GetSpawnId());
                if (found == luaOverrides_.end()) {
                    speeds[i] = RunLuaPolicy(*lua_, fields, budget);
                    continue;
                }
                auto& scoped = found->second;
                if (!scoped.failed) {
                    try {
                        speeds[i] = RunLuaPolicy(*scoped.policy, fields, budget);
                        continue;
                    } catch (const std::exception& error) {
                        scoped.failed = true;
                        scopedErrors.push_back(enemy->GetId() + ": Lua 実行エラー。以前の速度ルールを使用します。 " + error.what());
                    }
                }
                try {
                    speeds[i] = RunLuaPolicy(scoped.fallback ? *scoped.fallback : *lua_, fields, budget);
                } catch (const std::exception&) {
                    // An earlier policy can also fail on inputs first seen after adoption.
                    speeds[i] = 1.0f;
                }
            }
        }
        luaSpeeds_.swap(speeds);
        luaSpeedIds_.swap(ids);
    } catch (const std::exception& error) {
        luaFailed_ = true;
        luaSpeeds_.clear();
        luaSpeedIds_.clear();
        luaStatus_ = error.what();
        try {
            liveDiagnostics_.RecordResult("Lua runtime", false, luaStatus_, Json::array({
                {{"resource", "enemy.lua"}, {"before", liveScript_.at("generation")}, {"after", nullptr}}}));
        } catch (const std::exception&) {
            // The runtime already selected the built-in multiplier.
        }
    }
    // Journal I/O runs after evaluation so it cannot consume another policy's VM budget.
    for (const auto& error : scopedErrors) {
        aiStatus_ = error;
        RecordAi("AI runtime", false, aiStatus_);
    }
}
void EnemyAITestScene::DrawLiveLuaPanel() {
    if (!ImGui::TreeNode("Lua 5.4")) return;
    if (liveScript_.is_null()) ImGui::TextUnformatted("Lua: 組み込み倍率");
    else ImGui::TextWrapped("Lua %s / %s%s", liveScript_.at("label").get_ref<const std::string&>().c_str(),
        liveScript_.at("generation").get_ref<const std::string&>().c_str(), luaFailed_ ? " / 1.0 にフォールバック" : "");
    if (!enemies_.empty()) {
        const auto& enemy = *enemies_[selected_];
        const auto [hp, maxHp] = enemy.LiveHealth();
        ImGui::Text("選択中: %.2f 倍 / HP %.1f / %.1f", LuaSpeed(enemy), hp, maxHp);
        ImGui::BeginDisabled(hp <= maxHp * .5f);
        if (ImGui::Button("選択中の敵の HP を半分にする")) halfHealthEnemy_ = enemy.GetSpawnId();
        ImGui::EndDisabled();
    }
    for (const char* variant : {"A", "B", "C"}) {
        if (*variant != 'A') ImGui::SameLine();
        const auto label = std::string("Lua ") + variant + " を反映";
        if (ImGui::Button(label.c_str())) luaRequest_ = LuaRequest{variant, {}, true};
    }
    if (ImGui::Button("現在のソースを編集")) {
        luaDraft_ = liveScript_.is_null() ? "return 1.0\n" : liveScript_.at("source").get<std::string>();
        luaDraftOpen_ = true;
    }
    if (luaDraftOpen_) {
        ImGui::InputTextMultiline("##live-lua-draft", &luaDraft_, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 7));
        if (ImGui::Button("Lua 変更案を反映")) luaRequest_ = LuaRequest{"Draft", luaDraft_};
    }
    if (!luaStatus_.empty()) ImGui::TextWrapped("%s", luaStatus_.c_str());
    ImGui::TreePop();
}

void EnemyAITestScene::DrawLiveJsonPanel() {
    if (!ImGui::TreeNode("JSON 即時反映")) return;
    ImGui::BeginDisabled(enemies_.empty());
    if (ImGui::Button("選択中の敵の索敵設定を開く")) OpenJsonDraft(JsonTarget::EnemySenses);
    ImGui::EndDisabled();
    if (ImGui::Button("武器定義を開く")) OpenJsonDraft(JsonTarget::Weapons);
    if (jsonDraftOpen_) {
        const bool enemy = jsonTarget_ == JsonTarget::EnemySenses;
        if (enemy) ImGui::Text("対象の Spawn ID: %llu", static_cast<unsigned long long>(jsonEnemyId_));
        else ImGui::TextUnformatted(kWeaponsPath);
        if (!enemy) ImGui::TextUnformatted("武器定義の weapons を編集できます。");
        ImGui::TextUnformatted("変更案は実行中のシーンに反映します。ディスクへの保存は既存エディターから行えます。");
        ImGui::InputTextMultiline("##live-json-draft", &jsonDraft_, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 9));
        if (ImGui::Button("検証")) jsonRequest_ = JsonRequest{jsonTarget_, jsonEnemyId_, jsonDraft_, false};
        ImGui::SameLine();
        if (ImGui::Button("実行中のゲームへ反映")) jsonRequest_ = JsonRequest{jsonTarget_, jsonEnemyId_, jsonDraft_, true};
    }
    const auto& weapon = player_.CurrentWeapon();
    if (!weapon.Definition().id.empty()) {
        ImGui::TextWrapped("%s / 装弾 %d / 予備 %d / %s", weapon.Definition().id.c_str(),
            weapon.Magazine(), weapon.Reserve(), WeaponReloadStateName(weapon.ReloadState()));
        if (weapon.Reloading()) ImGui::Text("リロード残り %.2f 秒", weapon.ReloadRemaining());
        if (ImGui::Button("装弾を 1 回消費")) weaponTestShot_ = true;
        ImGui::SameLine();
        if (ImGui::Button("リロード開始")) weaponTestReload_ = true;
    }
    if (!jsonStatus_.empty()) ImGui::TextWrapped("%s", jsonStatus_.c_str());
    ImGui::TreePop();
}

void EnemyAITestScene::DrawLiveDiagnosticsPanel(GameApp& app) {
    if (!ImGui::CollapsingHeader("AI Live Development", ImGuiTreeNodeFlags_DefaultOpen)) return;
    const auto& generation = liveDiagnostics_.GenerationId();
    ImGui::Text("PID: %lu", GetCurrentProcessId());
    ImGui::TextWrapped("世代: %s", generation.empty() ? "組み込み表示" : generation.c_str());
    DrawAiChat(app);
    if (aiResearchMode_) return;
    if (ImGui::Button("最新 DLL を反映")) liveDiagnostics_.Request(Action::ApplyLatest);
    const bool shortcuts = app.GetInput()->HasFocus() && !app.GetInput()->IsCameraControlEnabled() &&
        !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    ImGui::SameLine();
    ImGui::BeginDisabled(!liveDiagnostics_.CanUndo());
    if (ImGui::Button("Undo") || (shortcuts && liveDiagnostics_.CanUndo() && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z)))
        liveDiagnostics_.Request(Action::Undo);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!liveDiagnostics_.CanRedo());
    if (ImGui::Button("Redo") || (shortcuts && liveDiagnostics_.CanRedo() && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y)))
        liveDiagnostics_.Request(Action::Redo);
    ImGui::EndDisabled();
    if (!liveDiagnostics_.Error().empty()) ImGui::TextWrapped("%s", liveDiagnostics_.Error().c_str());
    if (ImGui::TreeNode("DLL 世代一覧")) {
        for (const auto& entry : liveDiagnostics_.Generations() | std::views::reverse) {
            ImGui::PushID(entry.id.c_str());
            ImGui::TextWrapped("%s / ABI %u / %s\n%s", entry.variant.c_str(), entry.abiVersion,
                entry.builtAtUtc.c_str(), entry.id.c_str());
            const auto unavailable = liveDiagnostics_.UnavailableReason(entry);
            if (!unavailable.empty()) ImGui::TextWrapped("復元不可: %s", unavailable.c_str());
            ImGui::BeginDisabled(entry.id == generation || !unavailable.empty());
            if (ImGui::Button("この世代を復元")) liveDiagnostics_.Request(Action::Restore, entry.id);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("チェックポイント")) {
        if (ImGui::Button("状態を保存")) checkpointAction_ = CheckpointAction::Capture;
        ImGui::SameLine();
        ImGui::BeginDisabled(!checkpoint_);
        if (ImGui::Button("保存した状態を復元")) checkpointAction_ = CheckpointAction::Restore;
        ImGui::EndDisabled();
        if (checkpoint_) ImGui::TextWrapped("#%llu / %s / Schema %d / 敵 %zu",
            static_cast<unsigned long long>(checkpoint_->id), checkpoint_->scene.c_str(), checkpoint_->schema, checkpoint_->enemies.size());
        if (!checkpointStatus_.empty()) ImGui::TextWrapped("%s", checkpointStatus_.c_str());
        ImGui::TreePop();
    }
    DrawLiveJsonPanel();
    DrawLiveLuaPanel();
    if (ImGui::TreeNode("変更をまとめて反映")) {
        ImGui::Checkbox("最新 DLL", &batchLatestDll_);
        ImGui::Checkbox("Lua 変更案", &batchLua_);
        ImGui::Checkbox("JSON 変更案", &batchJson_);
        ImGui::BeginDisabled((!batchLatestDll_ && !batchLua_ && !batchJson_) ||
            (batchLua_ && !luaDraftOpen_) || (batchJson_ && !jsonDraftOpen_));
        if (ImGui::Button("選択した変更を一括反映")) {
            if (batchLatestDll_) liveDiagnostics_.Request(Action::ApplyLatest);
            if (batchLua_) luaRequest_ = LuaRequest{"Draft", luaDraft_};
            if (batchJson_) jsonRequest_ = JsonRequest{jsonTarget_, jsonEnemyId_, jsonDraft_, true};
        }
        ImGui::EndDisabled();
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("変更履歴")) {
        if (const auto& history = liveDiagnostics_.History(); !history.is_null()) {
            const auto& operations = history.at("operations");
            for (size_t index = operations.size(); index > 0; --index) {
                const auto& entry = operations[index - 1];
                ImGui::PushID(static_cast<int>(index));
                ImGui::TextWrapped("%s [%s] %s", entry.at("kind").get_ref<const std::string&>().c_str(),
                    entry.at("success").get<bool>() ? "成功" : "失敗", entry.at("summary").get_ref<const std::string&>().c_str());
                if (!entry.at("changes").empty() && ImGui::TreeNode("差分")) {
                    for (const auto& change : entry.at("changes")) {
                        const auto& resource = change.at("resource");
                        ImGui::PushID(&change);
                        ImGui::TextUnformatted(resource.get_ref<const std::string&>().c_str());
                        const auto diff = Json::diff(change.at("before"), change.at("after")).dump(2);
                        ImGui::TextWrapped("%s", diff.c_str());
                        const auto& after = change.at("after");
                        if (entry.at("success") == true && resource == "enemy.lua" && after.is_object() && !after.contains("overrides") &&
                            after.contains("generation") && after.contains("label") && after.contains("source") &&
                            after.at("generation").is_string() && after.at("label").is_string() && after.at("source").is_string() &&
                            ImGui::Button("この Lua 世代を復元")) {
                            luaRequest_ = LuaRequest{after.at("label").get<std::string>(), after.at("source").get<std::string>(),
                                false, after.at("generation").get<std::string>()};
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }
        ImGui::TreePop();
    }
}
