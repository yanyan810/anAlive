#pragma once

#include "GameApp.h"
#include "ImGuiManagaer.h"
#include "scene/Debug/EnemyAITestScene.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <tuple>

inline std::filesystem::path EnemyAiChatTestDirectory() {
    char run[33]{};
    if (GetEnvironmentVariableA("YAN_LIVE_AI_TEST_RUN", run, sizeof(run)) != 32 ||
        std::string_view(run).find_first_not_of("0123456789abcdef") != std::string_view::npos)
        throw std::runtime_error("Invalid AI chat test run");
    return std::filesystem::path("generated/live-ai-tests") / run;
}

#include "EnemyAiResearchTests.h"

inline void RunEnemyAiChatTests(GameApp& app) {
    using Scene = EnemyAITestScene;
    using Mode = Scene::AiMode;
    using Action = LiveEnemyDiagnostics::Action;
    using Clock = std::chrono::steady_clock;
    const auto directory = EnemyAiChatTestDirectory();
    if (std::filesystem::exists(directory / "research.txt")) {
        RunEnemyAiResearchTests(app);
        return;
    }
    const bool realModel = std::filesystem::exists(directory / "real-model.txt");
    const auto check = [](bool ok, const char* text) { if (!ok) throw std::runtime_error(text); };
    const auto frame = [&] {
        app.ImGui()->Begin();
        app.Update(1.0f / 60, 1.0f / 60);
        app.Draw();
    };
    const auto transition = [&](const char* name) {
        check(app.Scenes().TransitionTo(name, .01f, .01f), "transition not requested");
        for (int i = 0; i < 60 && app.Scenes().IsTransitioning(); ++i) frame();
        check(!app.Scenes().IsTransitioning() && app.Scenes().CurrentName() == name, "scene transition failed");
    };
    transition("EnemyAITest");
    auto* scene = dynamic_cast<Scene*>(app.Scenes().Current());
    check(scene && scene->enemies_.size() >= 2, "enemy test scene unavailable");
    scene->liveDiagnostics_.Open(directory);
    std::string port;
    std::ifstream(directory / "port.txt") >> port;
    const auto fixtureEndpoint = "http://127.0.0.1:" + port;
    scene->aiSettings_.endpoint = fixtureEndpoint;
    scene->aiSettings_.timeoutSeconds = realModel ? 90 : 4;
    auto* enemy = scene->enemies_[scene->selected_];
    const auto initialEnemy = enemy->CaptureLiveCheckpoint();
    check(initialEnemy.has_value(), "AI enemy fixture checkpoint failed");
    auto lowHp = *initialEnemy;
    for (auto& part : lowHp.parts) part.hp *= .5f;
    for (auto& group : lowHp.groups) group.hp *= .5f;
    lowHp.ai.OrderPursuit(scene->player_.GetTransform().translate);
    enemy->RestoreLiveCheckpoint(Enemy::LiveCheckpoint(lowHp));
    const auto state = [&] {
        const auto position = enemy->GetPosition();
        const auto coordinates = [](const Vector3& value) { return std::array{value.x, value.y, value.z}; };
        const auto& player = scene->player_.GetTransform();
        return std::tuple(GetCurrentProcessId(), app.Scenes().Current(), enemy, enemy->GetSpawnId(), enemy->LiveHealth(),
            enemy->GetState(), coordinates(position), scene->nextId_, coordinates(player.translate), coordinates(player.rotate),
            scene->player_.GetHP(), coordinates(scene->camera_.GetTranslate()), coordinates(scene->camera_.GetRotate()));
    };
    const auto before = state();
    const auto wait = [&](auto predicate) {
        const auto deadline = Clock::now() + std::chrono::seconds(realModel ? 275 : 8);
        size_t frames = 0;
        while (!predicate() && Clock::now() < deadline) {
            frame();
            ++frames;
            std::this_thread::sleep_for(std::chrono::milliseconds(realModel ? 33 : 5));
        }
        if (!predicate()) throw std::runtime_error("AI test wait timed out: " + scene->aiStatus_);
        return frames;
    };
    const auto start = [&](const char* prompt) {
        scene->StartAiChat(app, prompt);
        if (!scene->aiReview_) throw std::runtime_error("AI did not start: " + scene->aiStatus_);
    };
    const auto idle = [&] { return !scene->aiReview_; };
    const auto approval = [&] { return idle() || scene->aiReview_->validated; };
    const auto speed = [&] {
        scene->EvaluateLuaPolicies();
        check(scene->LuaSpeed(*scene->enemies_[1]) == 1.0f, "AI changed a different enemy");
        return scene->LuaSpeed(*enemy);
    };
    const auto displacement = [&] {
        scene->pauseAll_ = false;
        scene->Update(app, .04f);
        scene->pauseAll_ = true;
        const auto moved = EnemyAI::distanceTo(lowHp.position, enemy->GetPosition());
        enemy->RestoreLiveCheckpoint(Enemy::LiveCheckpoint(lowHp));
        return moved;
    };
    if (realModel) {
        std::string report = "PASS: qwen2.5-coder:3b through the live game and isolated Lua VM.\n";
        const float baseDistance = displacement();
        constexpr const char* hpPrompt = "敵のHPが50%以下になったら移動速度を1.5倍にして";
        const struct { const char* prompt; float expected; const char* rejected; } cases[] = {
            {hpPrompt, 1.5f, nullptr},
            {"追跡中だけ2倍速にして", 2.0f, nullptr},
            {hpPrompt, 1.5f, "local enemy = ...\nif enemy.hp / enemy.maxHp <= 0.5 then\n"
                "    enemy.speedMultiplier = 1.5\nelse\n    enemy.speedMultiplier = 1.0\nend"},
            {hpPrompt, 1.5f, "if enemy.hp / enemy.maxHp <= 0.5 then\n    return 1.5\nelse\n    return 1.0\nend"},
        };
        for (const auto& [prompt, expected, rejected] : cases) {
            start((std::string("/確認 ") + prompt).c_str());
            if (rejected) {
                // Seed the first candidate with the historical response. The
                // rejection and subsequent HTTP repair use the production path.
                scene->aiClient_.Cancel();
                scene->aiReview_->source = rejected;
                scene->aiReview_->validationPending = true;
            }
            std::ofstream(directory / "inference-started.txt") << GetCurrentProcessId();
            wait(approval);
            if (!scene->aiReview_ || !scene->aiReview_->validated || !scene->aiReview_->approvalRequired)
                throw std::runtime_error(std::string(prompt) + " -> " + scene->aiStatus_ + " / " + scene->aiLastValidationError_);
            if (rejected) {
                check(scene->aiLastAttempt_ >= 2 && !scene->aiLastValidationError_.empty(), "real model repair did not validate its seed");
                report += "Seed rejected: " + scene->aiLastValidationError_ + "\n";
            }
            report += std::string(prompt) + " / attempts=" + std::to_string(scene->aiLastAttempt_) + "\n" + scene->aiLastSource_ + "\n";
            scene->aiReview_->approved = true;
            wait(idle);
            check(speed() == expected && state() == before, "real model policy failed positive case or changed the game state");
            check(baseDistance > 0 && std::abs(displacement() / baseDistance - expected) < .02f,
                "real model policy did not change actual movement");
            if (expected == 1.5f) {
                auto fullHp = *initialEnemy;
                fullHp.ai = lowHp.ai;
                enemy->RestoreLiveCheckpoint(std::move(fullHp));
            } else {
                enemy->ResetAI();
            }
            check(speed() == 1, "real model policy failed negative case");
            enemy->RestoreLiveCheckpoint(Enemy::LiveCheckpoint(lowHp));
        }
        std::ofstream(directory / "result.txt") << report;
        return;
    }
    start("敵のHPが50%以下になったら1.5倍速にして 遅延");
    std::ofstream(directory / "inference-started.txt") << GetCurrentProcessId();
    check(wait(idle) > 2, "inference did not overlap drawing and updating");
    if (scene->aiConnected_ != true || speed() != 1.5f || state() != before)
        throw std::runtime_error("standard HP policy failed: " + scene->aiStatus_);
    const auto movedHp = displacement();
    const auto first = scene->liveScript_.at("generation");
    check(scene->liveDiagnostics_.History().at("operations").back().at("changes").front().contains("operationId"),
        "AI change is missing its operation ID");
    scene->liveDiagnostics_.Request(Action::Undo);
    scene->Update(app, 0);
    check(scene->liveScript_.is_null() && speed() == 1 && state() == before, "AI Undo did not restore prior policy");
    const auto movedBase = displacement();
    scene->liveDiagnostics_.Request(Action::Redo);
    scene->Update(app, 0);
    check(scene->liveScript_.at("generation") == first && speed() == 1.5f && state() == before, "AI Redo failed");

    scene->SetAiMode(Mode::Automatic);
    scene->SetAiVerification(Scene::AiVerification::Joint);
    start("/確認 追跡中だけ2倍速にして");
    wait(approval);
    check(scene->aiReview_ && scene->aiReview_->approvalRequired && scene->aiReview_->mode == Mode::Review &&
        speed() == 1.5f, "one-shot confirmation did not gate changes");
    scene->StopAiChat("テストで変更を拒否しました。", true);
    check(speed() == 1.5f, "rejected change applied");
    scene->SetAiMode(Mode::Review);
    start("追跡中だけ2倍速にして");
    wait(approval);
    check(scene->aiReview_ && scene->aiReview_->approvalRequired, "review mode did not wait");
    scene->aiReview_->approved = true;
    wait(idle);
    check(speed() == 2 && state() == before, "approved Chase policy failed");
    const auto movedChase = displacement();
    check(movedBase > 0 && std::abs(movedHp / movedBase - 1.5f) < .02f &&
        std::abs(movedChase / movedBase - 2.0f) < .02f, "AI policy did not change actual movement");
    enemy->ResetAI();
    check(speed() == 1, "Chase policy ran outside Chase");
    enemy->RestoreLiveCheckpoint(Enemy::LiveCheckpoint(lowHp));
    start("/自動 敵のHPが50%以下になったら1.5倍速にして");
    wait(idle);
    check(speed() == 1.5f, "automatic mode did not apply allowed Lua");
    start("/自動 モデル承認");
    wait(approval);
    check(scene->aiReview_ && scene->aiReview_->approvalRequired, "model-requested review was ignored");
    scene->StopAiChat("テストで変更を拒否しました。", true);
    const auto generation = scene->liveScript_.at("generation");
    scene->SetAiMode(Mode::Standard);
    for (const auto* prompt : {"不正JSON", "応答打ち切り", "説明過長", "実行禁止", "構文失敗", "無限ループ",
        "未知の操作", "世代偽装", "非対応"}) {
        start(prompt);
        wait(idle);
        if (scene->liveScript_.at("generation") != generation || scene->aiStatus_.empty())
            throw std::runtime_error(std::string("Invalid AI response committed: ") + prompt);
        if (std::string_view(prompt) == "不正JSON" || std::string_view(prompt) == "応答打ち切り" ||
            std::string_view(prompt) == "説明過長")
            check(scene->aiLastAttempt_ == 1, "protocol error retried as a Lua failure");
    }
    std::string reproducedFailures;
    for (const auto* prompt : {"/確認 HPが50%以下で1.5倍速 戻り値なしの再現",
        "/確認 HPが50%以下で1.5倍速 enemy未定義の再現", "/自動 構文修正の再現", "/確認 入力束縛契約"}) {
        start(prompt);
        wait(approval);
        if (!scene->aiReview_ || !scene->aiReview_->approvalRequired || scene->aiLastAttempt_ != 2 ||
            scene->aiLastValidationError_.empty())
            reproducedFailures += std::string(prompt) + " -> " + scene->aiStatus_ + "\n";
        check(scene->liveScript_.at("generation") == generation && speed() == 1.5f && state() == before,
            "unvalidated first candidate modified the game");
        if (scene->aiReview_) {
            const auto& operations = scene->liveDiagnostics_.History().at("operations");
            const auto& failed = operations[operations.size() - 2];
            const auto& passed = operations.back();
            check(failed.at("kind") == "AI Lua validation" && failed.at("success") == false &&
                failed.at("changes").front().at("attempt") == 1 && failed.at("changes").front().contains("errorType") &&
                passed.at("kind") == "AI Lua validation" && passed.at("success") == true &&
                passed.at("changes").front().at("attempt") == 2 &&
                failed.at("changes").front().at("after") != passed.at("changes").front().at("after"),
                "initial and repaired candidates were not audited separately");
            scene->aiReview_->approved = true;
            wait(idle);
            check(scene->liveScript_.at("generation") != generation && speed() == 1.5f && state() == before,
                "approved repair did not commit safely");
            scene->liveDiagnostics_.Request(Action::Undo);
            scene->Update(app, 0);
            check(scene->liveScript_.at("generation") == generation, "repaired Lua was not undoable");
        }
    }
    if (!reproducedFailures.empty()) throw std::runtime_error("Lua repair regressions:\n" + reproducedFailures);
    start("試行上限");
    wait(idle);
    check(scene->aiLastAttempt_ == 3 && scene->aiStatus_.find("試行上限") != std::string::npos &&
        scene->liveScript_.at("generation") == generation, "failed attempts did not stop at three");
    start("同じエラー");
    wait(idle);
    check(scene->aiLastAttempt_ == 2 && scene->aiStatus_.find("繰り返") != std::string::npos &&
        scene->liveScript_.at("generation") == generation, "repeated Lua diagnostic requested another repair");
    const auto repairing = [&] { return idle() || (scene->aiReview_->attempt == 2 && scene->aiClient_.Busy()); };
    const auto savedPlayer = scene->player_.GetTransform();
    start("後発エラー");
    wait(idle);
    check(speed() == 2.5f, "late runtime error fixture failed to install");
    scene->player_.SetStage(&scene->world_, enemy->GetPosition() + Vector3{0,0,-5}, {});
    check(speed() == 1.5f && scene->luaOverrides_.at(enemy->GetSpawnId()).failed,
        "late runtime error did not restore previous speed policy");
    scene->player_.SetStage(&scene->world_, savedPlayer.translate, savedPlayer.rotate);
    scene->player_.RefreshVisuals();
    scene->liveDiagnostics_.Request(Action::Undo);
    scene->Update(app, 0);
    check(scene->liveScript_.at("generation") == generation, "Undo after runtime fallback failed");
    scene->aiSettings_.model = "missing";
    start("敵のHPが50%以下になったら1.5倍速にして");
    wait(idle);
    check(scene->aiConnected_ == true && scene->aiStatus_.find("モデル") != std::string::npos, "missing model not reported");
    scene->aiSettings_.model = "mock";
    scene->aiSettings_.timeoutSeconds = 1;
    start("タイムアウト");
    wait(idle);
    check(scene->aiStatus_.find("タイムアウト") != std::string::npos, "timeout not reported");
    scene->aiSettings_.timeoutSeconds = 4;
    scene->aiSettings_.endpoint = "http://127.0.0.1:1";
    start("接続失敗");
    wait(idle);
    check(scene->aiConnected_ == false && scene->aiStatus_.find("接続") != std::string::npos, "connection failure not reported");
    scene->aiSettings_.endpoint = fixtureEndpoint;
    start("自動修正遅延");
    wait(repairing);
    check(!idle(), "target change did not overlap repair");
    scene->selected_ = 1;
    wait(idle);
    scene->selected_ = 0;
    check(scene->liveScript_.at("generation") == generation, "stale enemy proposal applied");
    start("自動修正遅延");
    wait(repairing);
    check(!idle(), "generation change did not overlap repair");
    scene->liveDiagnostics_.Request(Action::Undo);
    scene->Update(app, 0);
    wait(idle);
    check(scene->liveScript_.at("generation") != generation && speed() == 2, "stale generation replaced an undo");
    start("自動修正遅延");
    wait(repairing);
    check(!idle(), "cancel did not overlap repair");
    scene->StopAiChat("キャンセル試験", true);
    check(idle() && speed() == 2, "cancel did not release request");
    start("自動修正遅延");
    wait(repairing);
    check(!idle(), "scene transition did not overlap repair");
    transition("Title");
    transition("EnemyAITest");
    scene = dynamic_cast<Scene*>(app.Scenes().Current());
    check(scene && !scene->aiReview_ && scene->liveScript_.is_null(), "AI response crossed scene lifetime");
    std::ofstream(directory / "result.txt") <<
        "PASS: local HTTP mock, Japanese HP/Chase proposals, mode and approval gates, scoped policies, Undo/Redo, drawing during inference, "
        "invalid JSON/Lua/operation, runtime fallback, model and connection failures, timeout, cancellation, scene/target/revision invalidation.\n"
        "PASS: missing return, missing input binding, syntax and binding contract repaired; sticky approval, three-attempt cap, "
        "duplicate-error stop, malformed/truncated response rejection, and cancellation/context changes during repair.\n"
        "Measured displacement speed multipliers: " << movedHp / movedBase << " (HP), " << movedChase / movedBase << " (Chase).\n";
}
