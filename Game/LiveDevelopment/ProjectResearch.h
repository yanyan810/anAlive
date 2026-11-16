#pragma once

#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ProjectResearch {
struct Limits {
    int steps = 8;
    int results = 6;
    int lines = 48;
    int textBytes = 24000;
    int seconds = 90;
};

struct Evidence {
    std::string path, version;
    int first = 0, last = 0;
    std::string excerpt;
};

struct Progress {
    int step = 0, limit = 0;
    std::string activity, path;
    std::vector<std::string> operations;
};

struct Claim {
    std::string text;
    std::vector<size_t> references;
    bool verified = false;
};

struct Turn {
    std::string question, answer;
    std::vector<Evidence> references;
    Progress progress;
    std::vector<Claim> claims;
    std::string topic, model, quantization, modelAnswer, modelError;
    bool limitReached = false;
    double loadMs = 0, inferenceMs = 0;
};

// A caller supplies an immutable project root and an overall cancellation/deadline check.
class SourceReader {
public:
    SourceReader(const std::filesystem::path& root, std::function<void()> check);
    nlohmann::json Execute(const nlohmann::json& operation, const Limits& limits, std::string_view focus = {}) const;
    Evidence Read(std::string_view path, int first, int lines) const;
    std::optional<Evidence> FindRange(std::string_view path, std::string_view marker, int lines, int before = 0) const;
    bool StillCurrent(const Evidence& evidence) const;

private:
    struct File;
    File Load(const std::filesystem::path& relative) const;
    std::filesystem::path root_;
    std::wstring physicalRoot_;
    std::function<void()> check_;
};

// Transport receives an Ollama request body and returns the validated JSON object.
using Ask = std::function<nlohmann::json(nlohmann::json)>;
using Report = std::function<void(const Progress&)>;
std::string EvidenceTopic(std::string_view question);
bool InvestigateEvidence(const SourceReader& source, const Limits& limits, std::string_view topic, Turn& turn, const Report& report);
void Validate(const Limits& limits);
Turn Investigate(const SourceReader& source, const Limits& limits, std::string question,
    const std::vector<Turn>& history, const Ask& ask, const Report& report);
}
