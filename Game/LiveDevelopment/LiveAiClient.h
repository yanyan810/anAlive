#pragma once

#include "ProjectResearch.h"

#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

// One bounded request at a time. The worker owns the HTTP connection and only
// receives value snapshots. Cancelling or destroying the UI never waits on it.
class LiveAiClient {
public:
    static constexpr size_t kReasonMaxCharacters = 120;
    static constexpr size_t kLuaMaxCharacters = 2048;
    struct Settings {
        std::string endpoint = "http://127.0.0.1:11434";
        std::string model = "qwen2.5-coder:3b";
        int timeoutSeconds = 45;
    };
    struct Result {
        nlohmann::json proposal;
        std::string error;
        bool connected = false;
        std::optional<ProjectResearch::Turn> research;
    };
    struct Repair {
        std::string source, errorType, error, requirement;
    };

    ~LiveAiClient();
    void Start(const Settings& settings, const nlohmann::json& context, const std::string& prompt,
        const std::optional<Repair>& repair = std::nullopt);
    void StartResearch(Settings settings, std::filesystem::path root, ProjectResearch::Limits limits,
        std::string question, std::vector<ProjectResearch::Turn> history);
    ProjectResearch::Progress ResearchProgress() const;
    void Cancel() noexcept;
    bool Busy() const { return job_ != nullptr; }
    std::optional<Result> Poll();

private:
    struct Job;
    std::shared_ptr<Job> job_;
};
