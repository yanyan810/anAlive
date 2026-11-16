#pragma once

#include "EnemyAIDiagnosticsAbi.h"
#include "LiveChangeHistory.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class LiveEnemyDiagnostics {
public:
    enum class Action { ApplyLatest, Undo, Redo, Restore };
    struct Generation {
        std::string id, dllHash, pdbHash, builtAtUtc, variant;
        uint32_t abiVersion = 0;
    };

    explicit LiveEnemyDiagnostics(std::filesystem::path directory = "generated/live-enemy-diagnostics");
    ~LiveEnemyDiagnostics();

    void Open(std::filesystem::path directory = {});
    void Close();
    void Request(Action action, std::string generation = {});
    bool HasPending() const { return pending_.has_value(); }
    void CancelPending() { pending_.reset(); }
    // The preparation callback validates scene changes and fills their before/after
    // values without mutating the scene. The caller commits its prepared state only
    // when this returns true, before simulation resumes.
    bool ApplyPending(const EnemyAIDiagnosticsInput& input, nlohmann::json checkpoint = nullptr,
        nlohmann::json changes = nlohmann::json::array(),
        const std::function<void(nlohmann::json&, bool replay)>& prepare = {},
        const char* changeKind = "Live apply");
    std::optional<EnemyAIDiagnosticsOutput> Evaluate(const EnemyAIDiagnosticsInput& input);
    const std::string& GenerationId() const;
    bool CanUndo() const { return cursor_ > 0; }
    bool CanRedo() const { return cursor_ < timeline_.size(); }
    uint64_t Revision() const { return revision_; }
    const std::string& Error() const { return error_; }
    const std::vector<Generation>& Generations() const { return generations_; }
    std::string UnavailableReason(const Generation& generation) const;
    const nlohmann::json& History() const { return history_.Document(); }
    void RecordResult(const char* kind, bool success, std::string summary,
        nlohmann::json changes = nlohmann::json::array(), nlohmann::json checkpoint = nullptr);

private:
    struct RequestData {
        Action action;
        std::string generation;
    };
    struct Module;
    Generation ReadLatest() const;
    std::unique_ptr<Module> Load(const Generation& generation, const EnemyAIDiagnosticsInput& input) const;

    std::filesystem::path directory_;
    LiveChangeHistory history_;
    std::unique_ptr<Module> active_;
    std::vector<Generation> generations_;
    std::vector<nlohmann::json> timeline_;
    size_t cursor_ = 0;
    uint64_t revision_ = 0;
    std::optional<RequestData> pending_;
    std::string error_;
};
