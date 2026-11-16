#pragma once

#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>

// One operation may contain several resource changes. The journal does not interpret
// their payloads, and scene checkpoint data stays with its owning scene.
class LiveChangeHistory {
public:
    LiveChangeHistory();
    ~LiveChangeHistory();
    void Open(const std::filesystem::path& directory);
    void Close();
    const nlohmann::json& Document() const { return document_; }
    void Commit(nlohmann::json document);
    static void Append(nlohmann::json& document, nlohmann::json operation);

private:
    struct Lock;
    std::unique_ptr<Lock> lock_;
    std::filesystem::path directory_;
    nlohmann::json document_;
};
