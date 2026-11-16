#include "LiveChangeHistory.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <fstream>
#include <stdexcept>

namespace {
constexpr int kHistorySchema = 1;
constexpr size_t kJournalLimit = 64;
constexpr auto kMaxHistoryBytes = 32ull * 1024 * 1024;
}

struct LiveChangeHistory::Lock {
    HANDLE file = INVALID_HANDLE_VALUE;
    ~Lock() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
};

LiveChangeHistory::LiveChangeHistory() = default;
LiveChangeHistory::~LiveChangeHistory() = default;

void LiveChangeHistory::Open(const std::filesystem::path& directory) {
    Close();
    directory_ = std::filesystem::absolute(directory);
    std::filesystem::create_directories(directory_);
    const auto file = directory_ / "history.json";
    // Hold the writer lock for the scene lifetime. The file may remain after exit.
    auto lock = std::make_unique<Lock>();
    lock->file = CreateFileW((directory_ / "history.lock").c_str(), GENERIC_WRITE, 0,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock->file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("履歴をロックできません (Win32 " + std::to_string(GetLastError()) + ")。");
    nlohmann::json document = {{"schemaVersion", kHistorySchema},
        {"generations", nlohmann::json::array()}, {"operations", nlohmann::json::array()}};
    if (std::filesystem::exists(file)) {
        if (std::filesystem::file_size(file) > kMaxHistoryBytes)
            throw std::runtime_error("履歴 JSON が上限を超えています。");
        std::ifstream stream(file, std::ios::binary);
        if (!stream) throw std::runtime_error("履歴 JSON を開けません。");
        document = nlohmann::json::parse(stream);
        if (document.at("schemaVersion") != kHistorySchema ||
            !document.at("generations").is_array() || !document.at("operations").is_array())
            throw std::runtime_error("履歴 JSON のスキーマが一致しません。");
        for (const auto& entry : document.at("operations")) {
            if (!entry.at("kind").is_string() || !entry.at("success").is_boolean() ||
                !entry.at("summary").is_string() || !entry.at("changes").is_array())
                throw std::runtime_error("履歴 JSON の操作記録が無効です。");
            for (const auto& change : entry.at("changes")) {
                if (!change.at("resource").is_string() || !change.contains("before") || !change.contains("after"))
                    throw std::runtime_error("履歴 JSON の変更記録が無効です。");
            }
        }
    }
    document_ = std::move(document);
    lock_ = std::move(lock);
}

void LiveChangeHistory::Close() {
    lock_.reset();
    document_ = nullptr;
}

void LiveChangeHistory::Commit(nlohmann::json document) {
    if (!lock_) throw std::runtime_error("履歴の書き込みを開始できません。");
    const auto file = directory_ / "history.json";
    const auto draft = directory_ / "history.json.tmp";
    const auto bytes = document.dump(2) + '\n';
    if (bytes.size() > kMaxHistoryBytes) throw std::runtime_error("履歴 JSON が上限を超えています。");
    {
        std::ofstream stream(draft, std::ios::binary | std::ios::trunc);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        if (!stream) throw std::runtime_error("一時履歴 JSON を書き込めません。");
    }
    if (!MoveFileExW(draft.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("履歴 JSON を置き換えられません (Win32 " + std::to_string(GetLastError()) + ")。");
    document_ = std::move(document);
}

void LiveChangeHistory::Append(nlohmann::json& document, nlohmann::json operation) {
    auto& operations = document.at("operations");
    operations.push_back(std::move(operation));
    while (operations.size() > kJournalLimit) operations.erase(operations.begin());
}
