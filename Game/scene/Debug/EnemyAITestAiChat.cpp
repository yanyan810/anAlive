#include "EnemyAITestScene.h"
#include "GameApp.h"
#include "DebugAI/DebugAIManager.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>
#include <cfloat>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
constexpr auto kScene = "EnemyAITest";
constexpr auto kResource = "enemy.lua";

const Json* ScopedPolicy(const Json& script, uint64_t spawnId) {
    if (!script.contains("overrides")) return nullptr;
    const auto& policies = script.at("overrides");
    const auto found = std::ranges::find_if(policies, [&](const Json& policy) {
        return policy.at("spawnId") == spawnId;
    });
    return found == policies.end() ? nullptr : &*found;
}

bool DuringReplay(GameApp& app) {
    const auto* debugAi = app.DebugAI();
    return debugAi && (debugAi->IsReplayPlaying() || debugAi->IsReplaySessionRecording());
}

bool TakePrefix(std::string& text, std::string_view prefix) {
    if (!text.starts_with(prefix)) return false;
    const auto start = text.find_first_not_of(" \t\r\n", prefix.size());
    text = start == std::string::npos ? "" : text.substr(start);
    return true;
}

size_t Utf8Characters(std::string_view text) {
    return static_cast<size_t>(std::ranges::count_if(text, [](unsigned char byte) { return (byte & 0xc0) != 0x80; }));
}

bool HasEnemyBinding(std::string_view source) {
    const auto trim = [](std::string_view& text, std::string_view space) {
        text.remove_prefix(std::min(text.size(), text.find_first_not_of(space)));
    };
    trim(source, " \t\r\n");
    if (!source.starts_with("local") || source.size() <= 5 || (source[5] != ' ' && source[5] != '\t')) return false;
    for (const std::string_view token : {"local", "enemy", "=", "..."}) {
        if (!source.starts_with(token)) return false;
        source.remove_prefix(token.size());
        trim(source, " \t\r");
    }
    return source.empty() || source.starts_with('\n') || source.starts_with(';') || source.starts_with("--");
}

LiveAiClient::Repair RepairFeedback(const std::string& source, const LiveLua::Error& error) {
    using Kind = LiveLua::ErrorKind;
    std::string type, requirement;
    switch (error.kind) {
    case Kind::Syntax:
        type = "syntax";
        requirement = "Fix the Lua syntax in the candidate. Return raw Lua with matching if/end and a numeric return on every path.";
        break;
    case Kind::Runtime:
        type = "runtime";
        requirement = "Inspect the failing line and the actual cause of any nil access. Bind local enemy = ... before reading input. Remove all function calls and global names, including set_enemy_speed_lua. Return the absolute numeric multiplier directly and guard division by zero.";
        break;
    case Kind::Result:
        type = "return_value";
        requirement = "Return a finite number in [0.0, 3.0] on every path. An assignment to enemy.speedMultiplier does not return a value. End with return 1.0 for the normal case.";
        break;
    case Kind::Limit:
        type = "resource_limit";
        requirement = "Use a few arithmetic comparisons and if/return clauses. Remove unbounded loops, recursion and large allocations.";
        break;
    case Kind::Contract:
        type = "api_contract";
        requirement = "Begin with a first statement local enemy = ..., including for a constant multiplier. Use this local as read-only.";
        break;
    }
    return {source, std::move(type), error.what(), std::move(requirement)};
}

std::string_view DiagnosticSignature(std::string_view text) {
    constexpr std::string_view chunk = "enemy policy:";
    if (text.starts_with(chunk)) {
        const auto offset = text.find(':', chunk.size());
        if (offset != std::string_view::npos &&
            text.substr(chunk.size(), offset - chunk.size()).find_first_not_of("0123456789") == std::string_view::npos)
            return text.substr(offset + 1);
    }
    return text;
}
}

const char* EnemyAITestScene::AiModeName(AiMode mode) {
    switch (mode) {
    case AiMode::Standard: return "標準";
    case AiMode::Review: return "確認";
    case AiMode::Automatic: return "自動実行";
    }
    return "";
}

const char* EnemyAITestScene::AiVerificationName(AiVerification mode) {
    switch (mode) {
    case AiVerification::Human: return "人間が確認";
    case AiVerification::Joint: return "共同確認";
    case AiVerification::Ai: return "AI が確認";
    }
    return "";
}

void EnemyAITestScene::RecordAi(const char* kind, bool success, const std::string& message, std::string_view errorType) {
    Json info = {{"resource", "ai.chat"}, {"before", nullptr}, {"after", nullptr},
        {"mode", AiModeName(aiReview_ ? aiReview_->mode : aiMode_)},
        {"verification", AiVerificationName(aiReview_ ? aiReview_->verification : aiVerification_)}};
    if (!liveScript_.is_null()) info["luaGeneration"] = liveScript_.at("generation");
    if (!errorType.empty()) info["errorType"] = errorType;
    if (aiReview_) {
        info["operationId"] = aiReview_->operationId;
        info["enemyId"] = aiReview_->enemyId;
        info["baseGeneration"] = aiReview_->generation;
        info["model"] = aiReview_->model;
        info["attempt"] = aiReview_->attempt;
        if (std::string_view(kind) == "AI request") info["prompt"] = aiReview_->prompt;
        if ((!success || std::string_view(kind) == "AI Lua validation") && !aiReview_->source.empty()) {
            info["before"] = aiReview_->before;
            info["after"] = aiReview_->source;
        }
    }
    try {
        liveDiagnostics_.RecordResult(kind, success, message, Json::array({std::move(info)}));
    } catch (const std::exception&) {
        // The history panel exposes journal failures. Generation cannot commit without the journal.
    }
}

void EnemyAITestScene::SetAiMode(AiMode mode) {
    if (mode == aiMode_) return;
    if (aiReview_) StopAiChat("操作モードが変わったため、問い合わせを取り消しました。");
    aiMode_ = mode;
    RecordAi("AI mode", true, AiModeName(mode));
}

void EnemyAITestScene::SetAiVerification(AiVerification mode) {
    if (mode == aiVerification_) return;
    if (aiReview_) StopAiChat("確認方針が変わったため、問い合わせを取り消しました。");
    aiVerification_ = mode;
    RecordAi("AI verification", true, AiVerificationName(mode));
}

bool EnemyAITestScene::AiContextMatches(GameApp& app, const AiReview& review) const {
    return app.Scenes().Current() == this && app.Scenes().CurrentName() == kScene &&
        !app.Scenes().IsTransitioning() && !DuringReplay(app) && !enemies_.empty() &&
        enemies_[selected_]->GetSpawnId() == review.spawnId && enemies_[selected_]->GetId() == review.enemyId &&
        liveDiagnostics_.Revision() == review.revision;
}

void EnemyAITestScene::StartAiChat(GameApp& app, std::string prompt) {
    if (aiResearchMode_ || aiReview_) return;
    aiLastReason_.clear();
    aiLastSource_.clear();
    aiLastBefore_.clear();
    aiLastValidationError_.clear();
    aiLastAttempt_ = 0;
    try {
        AiReview review;
        review.mode = aiMode_;
        review.verification = aiVerification_;
        if (TakePrefix(prompt, "/確認 ") || TakePrefix(prompt, "確認モード：") || TakePrefix(prompt, "確認モード:"))
            review.mode = AiMode::Review;
        else if (TakePrefix(prompt, "/標準 ") || TakePrefix(prompt, "標準モード：") || TakePrefix(prompt, "標準モード:"))
            review.mode = AiMode::Standard;
        else if (TakePrefix(prompt, "/自動 ") || TakePrefix(prompt, "自動実行モード：") || TakePrefix(prompt, "自動実行モード:"))
            review.mode = AiMode::Automatic;
        if (enemies_.empty() || prompt.find_first_not_of(" \t\r\n") == std::string::npos || prompt.size() > 2048)
            throw std::runtime_error("対象の敵と 2048 バイト以内の指示を指定してください。");
        const auto& enemy = *enemies_[selected_];
        if (aiSceneId_.empty()) aiSceneId_ = NewLiveId();
        review.operationId = NewLiveId();
        review.enemyId = enemy.GetId();
        review.spawnId = enemy.GetSpawnId();
        review.revision = liveDiagnostics_.Revision();
        const auto luaGeneration = liveScript_.is_null() ? std::string("builtin") : liveScript_.at("generation").get<std::string>();
        review.generation = luaGeneration + "/" + std::to_string(review.revision);
        review.before = liveScript_.is_null() ? "return 1.0" : liveScript_.at("source").get<std::string>();
        if (const auto* scoped = ScopedPolicy(liveScript_, review.spawnId)) {
            const auto runtime = luaOverrides_.find(review.spawnId);
            if (runtime != luaOverrides_.end() && runtime->second.failed)
                review.before = scoped->value("fallback", review.before);
            else review.before = scoped->at("source").get<std::string>();
        }
        review.prompt = std::move(prompt);
        review.model = aiSettings_.model;
        if (!AiContextMatches(app, review)) throw std::runtime_error("敵AIテストの通常更新中に送信してください。");
        const auto [hp, maxHp] = enemy.LiveHealth();
        Json context = {{"scene", kScene}, {"enemyId", review.enemyId}, {"type", EnemyTypeName(enemy.Definition().type)},
            {"hp", hp}, {"maxHp", maxHp}, {"state", EnemyStateName(enemy.GetState())}, {"speedMultiplier", LuaSpeed(enemy)},
            {"distance", EnemyAI::distanceTo(player_.GetTransform().translate, enemy.GetPosition())},
            {"baseGeneration", review.generation}, {"currentLua", review.before}, {"luaApiVersion", 1},
            {"operations", {"set_enemy_speed_lua", "unsupported"}}, {"verification", AiVerificationName(review.verification)}};
        if (review.verification != AiVerification::Human && !liveDiagnostics_.History().is_null()) {
            const auto& operations = liveDiagnostics_.History().at("operations");
            auto recent = Json::array();
            for (size_t i = operations.size(); i > 0 && recent.size() < 4; --i) {
                const auto& entry = operations[i - 1];
                const auto& summary = entry.at("summary").get_ref<const std::string&>();
                if (summary.size() <= 512)
                    recent.push_back({{"kind", entry.at("kind")}, {"success", entry.at("success")}, {"summary", summary}});
            }
            context["recentResults"] = std::move(recent);
        }
        aiConnected_.reset();
        review.context = std::move(context);
        aiClient_.Start(aiSettings_, review.context, review.prompt);
        aiLastAttempt_ = review.attempt;
        aiReview_ = std::move(review);
        aiStatus_ = "Ollama に接続・生成中…";
        RecordAi("AI request", true, aiStatus_);
    } catch (const std::exception& error) {
        aiStatus_ = error.what();
        RecordAi("AI error", false, aiStatus_);
    }
}

void EnemyAITestScene::StopAiChat(std::string reason, bool keepResponse) {
    aiClient_.Cancel();
    aiStatus_ = std::move(reason);
    if (aiReview_) RecordAi("AI stopped", false, aiStatus_);
    aiReview_.reset();
    if (!keepResponse) {
        aiLastReason_.clear();
        aiLastSource_.clear();
        aiLastBefore_.clear();
    }
}

Json EnemyAITestScene::AiChanges(const AiReview& review) const {
    auto next = liveScript_.is_null() ? Json{{"source", "return 1.0"}, {"apiVersion", 1}} : liveScript_;
    next["label"] = "Local AI";
    next["generation"] = "";
    next["sceneInstance"] = aiSceneId_;
    if (!next.contains("overrides")) next["overrides"] = Json::array();
    auto& policies = next["overrides"];
    for (auto entry = policies.begin(); entry != policies.end();) {
        if (entry->at("spawnId") == review.spawnId ||
            std::ranges::find(enemies_, entry->at("spawnId").get<uint64_t>(), &Enemy::GetSpawnId) == enemies_.end())
            entry = policies.erase(entry);
        else ++entry;
    }
    Json override = {{"spawnId", review.spawnId}, {"source", review.source}};
    if (next.at("source") != review.before) override["fallback"] = review.before;
    policies.push_back(std::move(override));
    return Json::array({{{"resource", kResource}, {"operationId", review.operationId}, {"enemyId", review.enemyId},
        {"baseGeneration", review.generation}, {"reason", review.reason}, {"mode", AiModeName(review.mode)}, {"attempt", review.attempt},
        {"verification", AiVerificationName(review.verification)}, {"before", liveScript_}, {"after", std::move(next)}}});
}

void EnemyAITestScene::ValidateAiCandidate(AiReview& review) {
    review.validationPending = false;
    try {
        auto changes = AiChanges(review);
        PreparedChanges prepared;
        // Check the executable policy first so Lua reports the actual failing path.
        PrepareChanges(changes, false, prepared);
        if (!HasEnemyBinding(review.source))
            throw LiveLua::Error(LiveLua::ErrorKind::Contract, "Lua chunk must start with local enemy = ...");
    } catch (const LiveLua::Error& error) {
        auto feedback = RepairFeedback(review.source, error);
        const bool repeated = std::ranges::any_of(review.failures, [&](const auto& previous) {
            return previous.source == feedback.source ||
                (previous.errorType == feedback.errorType &&
                    DiagnosticSignature(previous.error) == DiagnosticSignature(feedback.error));
        });
        aiLastValidationError_ = feedback.error;
        RecordAi("AI Lua validation", false, feedback.error, feedback.errorType);
        if (repeated || review.attempt >= kAiMaxAttempts) {
            StopAiChat(repeated ? "同じLuaコードまたは検証エラーが繰り返されたため、自動修正を終了しました。"
                : "Lua の生成・自動修正が試行上限に達しました。", true);
            return;
        }
        try {
            aiClient_.Start(aiSettings_, review.context, review.prompt, feedback);
            ++review.attempt;
            aiLastAttempt_ = review.attempt;
            review.failures.push_back(std::move(feedback));
            aiStatus_ = "Lua を自動修正中…";
        } catch (const std::exception& startError) {
            StopAiChat(startError.what(), true);
        }
        return;
    } catch (const std::exception& error) {
        aiLastValidationError_ = error.what();
        RecordAi("AI Lua validation", false, aiLastValidationError_);
        StopAiChat(aiLastValidationError_, true);
        return;
    }
    review.validated = true;
    review.applyAfter = std::chrono::steady_clock::now() + std::chrono::milliseconds(350);
    aiStatus_ = review.approvalRequired ? "検証済み。承認を待っています。" : "検証済み。自動反映を待っています。";
    RecordAi("AI Lua validation", true, "Lua の検証に成功しました。");
}

void EnemyAITestScene::UpdateAiChat(GameApp& app) {
    if (!aiReview_) return;
    auto& review = *aiReview_;
    if (!AiContextMatches(app, review)) {
        StopAiChat("シーン・対象・変更世代が更新されました。対象を確認して再送信してください。", true);
        return;
    }
    if (const auto result = aiClient_.Poll()) {
        aiConnected_ = result->connected;
        try {
            if (!result->error.empty()) throw std::runtime_error(result->error);
            const auto& plan = result->proposal;
            if (plan.size() != 7 || plan.at("scene") != kScene || plan.at("enemyId") != review.enemyId ||
                plan.at("baseGeneration") != review.generation)
                throw std::runtime_error("AI 応答の形式・対象・世代が一致しません。");
            const auto& operation = plan.at("operation").get_ref<const std::string&>();
            const auto& source = plan.at("lua").get_ref<const std::string&>();
            const auto& reason = plan.at("reason").get_ref<const std::string&>();
            const bool requestsApproval = plan.at("requiresApproval").get<bool>();
            if (reason.empty() || Utf8Characters(reason) > LiveAiClient::kReasonMaxCharacters ||
                source.size() > 4096 || Utf8Characters(source) > LiveAiClient::kLuaMaxCharacters ||
                source.find('\0') != std::string::npos || reason.find('\0') != std::string::npos)
                throw std::runtime_error("AI 応答の理由または Lua が無効です。");
            aiLastReason_ = reason;
            if (operation == "unsupported" && source.empty()) {
                StopAiChat("現在の Lua API の対象外です。", true);
                return;
            }
            if (operation != "set_enemy_speed_lua" || source.empty())
                throw std::runtime_error("許可されていない操作種別です。");
            review.source = source;
            review.reason = reason;
            aiLastBefore_ = review.before;
            aiLastSource_ = source;
            review.approvalRequired = review.approvalRequired || requestsApproval || review.mode == AiMode::Review;
            review.validationPending = true;
            aiStatus_ = "Lua を検証中…";
        } catch (const std::exception& error) {
            StopAiChat(error.what(), true);
        }
        return;
    }
    if (review.validationPending) {
        ValidateAiCandidate(review);
        return;
    }
    if (!review.validated || (review.approvalRequired && !review.approved) ||
        (!review.approved && std::chrono::steady_clock::now() < review.applyAfter) ||
        liveDiagnostics_.HasPending() || luaRequest_ || jsonRequest_ || checkpointAction_ || halfHealthEnemy_) return;

    PreparedChanges prepared;
    const auto prepare = [&](Json& changes, bool replay) { PrepareChanges(changes, replay, prepared); };
    const auto applied = liveDiagnostics_.ApplyPending(MakeDiagnosticsInput(), nullptr, AiChanges(review), prepare, "AI Lua apply");
    if (applied) {
        CommitChanges(std::move(prepared));
        aiStatus_ = "選択中の敵に Lua を反映しました。Undo / Redo が使えます。";
    } else {
        aiStatus_ = liveDiagnostics_.Error().empty() ? "Lua に差分はありません。" : liveDiagnostics_.Error();
    }
    aiReview_.reset();
}

void EnemyAITestScene::SetResearchMode(bool enabled) {
    if (enabled == aiResearchMode_) return;
    if (aiReview_) StopAiChat("チャットの用途を切り替えたため、問い合わせを取り消しました。");
    if (aiClient_.Busy()) {
        aiClient_.Cancel();
        researchStatus_ = "調査を取り消しました。";
    }
    liveDiagnostics_.CancelPending();
    luaRequest_.reset();
    jsonRequest_.reset();
    checkpointAction_.reset();
    halfHealthEnemy_.reset();
    weaponTestShot_ = weaponTestReload_ = false;
    aiResearchMode_ = enabled;
}

void EnemyAITestScene::StartResearch(GameApp& app, std::string question) {
    if (!aiResearchMode_ || aiClient_.Busy() || app.Scenes().Current() != this ||
        app.Scenes().IsTransitioning() || DuringReplay(app)) return;
    try {
        if (question.find_first_not_of(" \t\n\r") == std::string::npos || question.size() > 1000)
            throw std::runtime_error("質問は UTF-8 で 1〜1000 バイトです。");
        researchProgress_ = {};
        aiClient_.StartResearch(aiSettings_, std::filesystem::current_path(), researchLimits_, std::move(question), researchHistory_);
        researchStatus_ = "Ollama に接続中…";
        aiConnected_.reset();
    } catch (const std::exception& error) {
        researchStatus_ = error.what();
    }
}

void EnemyAITestScene::UpdateResearch(GameApp& app) {
    if (!aiClient_.Busy()) return;
    if (app.Scenes().Current() != this || DuringReplay(app)) {
        aiClient_.Cancel();
        researchStatus_ = "シーンまたはリプレイの状態が変わったため、調査を取り消しました。";
        return;
    }
    researchProgress_ = aiClient_.ResearchProgress();
    auto result = aiClient_.Poll();
    if (!result) return;
    aiConnected_ = result->connected;
    if (!result->research) {
        researchStatus_ = std::move(result->error);
        return;
    }
    researchStatus_ = "調査が完了しました。";
    researchHistory_.push_back(std::move(*result->research));
    size_t bytes = 0;
    size_t keep = 0;
    for (auto i = researchHistory_.rbegin(); i != researchHistory_.rend() && keep < 6; ++i) {
        bytes += i->question.size() + i->answer.size() + i->modelAnswer.size() + i->modelError.size();
        for (const auto& claim : i->claims) bytes += claim.text.size() + claim.references.size() * sizeof(size_t);
        for (const auto& ref : i->references) bytes += ref.path.size() + ref.version.size() + ref.excerpt.size();
        for (const auto& operation : i->progress.operations) bytes += operation.size();
        if (bytes > 40000) break;
        ++keep;
    }
    researchHistory_.erase(researchHistory_.begin(), researchHistory_.end() - keep);
}

void EnemyAITestScene::DrawResearch(GameApp& app) {
    const bool busy = aiClient_.Busy();
    ImGui::TextWrapped("使用モデル %s", aiSettings_.model.c_str());
    if (ImGui::TreeNode("調査上限")) {
        ImGui::BeginDisabled(busy);
        ImGui::SliderInt("最大調査ステップ", &researchLimits_.steps, 1, 10);
        ImGui::SliderInt("検索結果数", &researchLimits_.results, 1, 12);
        ImGui::SliderInt("取得行数", &researchLimits_.lines, 1, 120);
        ImGui::SliderInt("送信テキスト (バイト)", &researchLimits_.textBytes, 3000, 64000);
        ImGui::SliderInt("調査全体のタイムアウト (秒)", &researchLimits_.seconds, 1, 120);
        ImGui::EndDisabled();
        ImGui::TreePop();
    }
    ImGui::TextUnformatted("質問");
    ImGui::InputTextMultiline("##project-question", &researchQuestion_, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3));
    ImGui::BeginDisabled(busy || researchQuestion_.empty() || app.Scenes().Current() != this || DuringReplay(app));
    if (ImGui::Button("調査する")) StartResearch(app, researchQuestion_);
    ImGui::EndDisabled();
    if (busy) {
        ImGui::SameLine();
        if (ImGui::Button("調査をキャンセル")) {
            aiClient_.Cancel();
            researchStatus_ = "調査を取り消しました。";
        }
    } else if (!researchHistory_.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("会話を消去")) {
            researchHistory_.clear();
            researchProgress_ = {};
            researchStatus_.clear();
        }
    }
    if (!researchStatus_.empty()) ImGui::TextWrapped("%s", researchStatus_.c_str());
    if (researchProgress_.limit != 0) {
        ImGui::Text("調査ステップ %d / %d", researchProgress_.step, researchProgress_.limit);
        if (busy) ImGui::TextWrapped("%s %s", researchProgress_.activity.c_str(), researchProgress_.path.c_str());
        if (ImGui::TreeNode("最後の検索操作")) {
            for (const auto& operation : researchProgress_.operations) ImGui::TextWrapped("%s", operation.c_str());
            ImGui::TreePop();
        }
    }
    for (auto turn = researchHistory_.rbegin(); turn != researchHistory_.rend(); ++turn) {
        ImGui::PushID(static_cast<int>(turn - researchHistory_.rbegin()));
        if (turn == researchHistory_.rbegin()) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        if (ImGui::TreeNode("##turn", "%s", turn->question.c_str())) {
            if (turn->limitReached) ImGui::TextUnformatted("調査上限に達しました。");
            if (!turn->modelError.empty()) ImGui::TextWrapped("モデル回答を採用できませんでした　%s", turn->modelError.c_str());
            if (turn->claims.empty()) ImGui::TextWrapped("%s", turn->answer.c_str());
            for (const auto& claim : turn->claims) {
                std::string ids;
                for (const auto index : claim.references) ids += " E" + std::to_string(index + 1);
                ImGui::TextWrapped("%s%s　%s", claim.verified ? "確認済み" : "未確認", ids.c_str(), claim.text.c_str());
            }
            for (size_t index = 0; index < turn->references.size(); ++index) {
                const auto& ref = turn->references[index];
                ImGui::PushID(&ref);
                if (ImGui::TreeNode("##source", "E%zu %s:%d–%d", index + 1, ref.path.c_str(), ref.first, ref.last)) {
                    ImGui::TextWrapped("%s", ref.excerpt.c_str());
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void EnemyAITestScene::DrawAiChat(GameApp& app) {
    if (!ImGui::TreeNode("ローカル AI チャット")) return;
    int mode = aiResearchMode_ ? 1 : 0;
    if (ImGui::Combo("チャットの用途", &mode, "Lua変更\0プロジェクト調査\0")) SetResearchMode(mode == 1);
    if (ImGui::TreeNode("Ollama 接続設定")) {
        ImGui::BeginDisabled(aiReview_.has_value() || aiClient_.Busy());
        ImGui::InputText("接続先", &aiSettings_.endpoint);
        ImGui::InputText("モデル", &aiSettings_.model);
        ImGui::InputInt("タイムアウト (秒)", &aiSettings_.timeoutSeconds);
        ImGui::EndDisabled();
        ImGui::TextUnformatted("接続先は 127.0.0.1 の HTTP ポートです。");
        ImGui::TreePop();
    }
    if (aiConnected_) ImGui::TextUnformatted(*aiConnected_ ? "Ollama: HTTP 応答あり" : "Ollama: 接続失敗");
    if (aiResearchMode_) {
        DrawResearch(app);
        ImGui::TreePop();
        return;
    }
    if (ImGui::BeginCombo("操作モード", AiModeName(aiMode_))) {
        for (const auto mode : {AiMode::Standard, AiMode::Review, AiMode::Automatic})
            if (ImGui::Selectable(AiModeName(mode), mode == aiMode_)) SetAiMode(mode);
        ImGui::EndCombo();
    }
    if (ImGui::BeginCombo("動作確認", AiVerificationName(aiVerification_))) {
        for (const auto mode : {AiVerification::Human, AiVerification::Joint, AiVerification::Ai})
            if (ImGui::Selectable(AiVerificationName(mode), mode == aiVerification_)) SetAiVerification(mode);
        ImGui::EndCombo();
    }
    if (aiVerification_ != AiVerification::Human)
        ImGui::TextWrapped("状態と最近の操作結果を AI に送ります。画面操作は手動です。");
    if (!enemies_.empty()) ImGui::Text("対象: %s", enemies_[selected_]->GetId().c_str());
    ImGui::InputTextMultiline("##ai-japanese", &aiPrompt_, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4));
    ImGui::TextWrapped("例  HPが50%%以下で追跡中なら2倍速にして");
    ImGui::BeginDisabled(aiReview_.has_value() || enemies_.empty() || aiPrompt_.empty() || DuringReplay(app));
    if (ImGui::Button("送信")) StartAiChat(app, aiPrompt_);
    ImGui::EndDisabled();
    if (aiReview_) {
        ImGui::SameLine();
        if (ImGui::Button(aiReview_->validated ? "変更を拒否" : "問い合わせをキャンセル"))
            StopAiChat("ユーザーが取り消しました。", true);
    }
    if (aiReview_ && aiReview_->validated && aiReview_->approvalRequired) {
        ImGui::SameLine();
        if (ImGui::Button("承認して反映")) aiReview_->approved = true;
    }
    if (!aiStatus_.empty()) ImGui::TextWrapped("%s", aiStatus_.c_str());
    if (aiLastAttempt_ > 0) ImGui::Text("試行 %u / %u", aiLastAttempt_, kAiMaxAttempts);
    if (!aiLastValidationError_.empty())
        ImGui::TextWrapped("最後の検証エラー  %s", aiLastValidationError_.c_str());
    if (!aiLastReason_.empty()) ImGui::TextWrapped("%s", aiLastReason_.c_str());
    if (!aiLastSource_.empty()) {
        if (ImGui::TreeNode("変更前の Lua")) {
            ImGui::TextUnformatted(aiLastBefore_.c_str());
            ImGui::TreePop();
        }
        ImGui::TextUnformatted("最新の Lua 候補");
        ImGui::TextWrapped("%s", aiLastSource_.c_str());
    }
    if (ImGui::TreeNode("AI 実行履歴")) {
        const auto& history = liveDiagnostics_.History();
        if (!history.is_null()) {
            const auto& operations = history.at("operations");
            for (auto entry = operations.rbegin(); entry != operations.rend(); ++entry) {
                const auto& kind = entry->at("kind").get_ref<const std::string&>();
                if (kind == "AI Lua validation") {
                    const auto& info = entry->at("changes").front();
                    const auto attempt = info.at("attempt").get<unsigned>();
                    const bool success = entry->at("success").get<bool>();
                    ImGui::PushID(&*entry);
                    const auto label = std::to_string(attempt) + " 回目 / " +
                        (attempt == 1 ? "初回生成" : "自動修正") + (success ? " / 検証成功" : " / 検証失敗");
                    if (ImGui::TreeNode(label.c_str())) {
                        ImGui::TextWrapped("%s", entry->at("summary").get_ref<const std::string&>().c_str());
                        if (info.contains("errorType")) ImGui::TextUnformatted(info.at("errorType").get_ref<const std::string&>().c_str());
                        if (info.at("after").is_string()) ImGui::TextWrapped("%s", info.at("after").get_ref<const std::string&>().c_str());
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                } else if (kind.starts_with("AI ")) {
                    ImGui::TextWrapped("%s • %s", kind.c_str(), entry->at("summary").get_ref<const std::string&>().c_str());
                }
            }
        }
        ImGui::TreePop();
    }
    ImGui::TreePop();
}
