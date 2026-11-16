#include "LiveEnemyDiagnostics.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

#pragma comment(lib, "bcrypt.lib")

namespace {
using Json = nlohmann::json;
using Generation = LiveEnemyDiagnostics::Generation;
constexpr size_t kGenerationLimit = 32;
constexpr size_t kTimelineLimit = 16;

bool IsLowerHex(std::string_view value, size_t length) {
    return value.size() == length && value.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

Generation ParseGeneration(const Json& manifest, bool allowPhaseOne = false) {
    Generation result{manifest.at("generation").get<std::string>(),
        manifest.at("dllSha256").get<std::string>(), manifest.at("pdbSha256").get<std::string>(),
        "", "", ENEMY_AI_DIAGNOSTICS_ABI_VERSION};
    if (!IsLowerHex(result.id, 32) || !IsLowerHex(result.dllHash, 64) || !IsLowerHex(result.pdbHash, 64))
        throw std::runtime_error("世代 ID または SHA-256 が無効です。");
    if (allowPhaseOne && !manifest.contains("schemaVersion") && !manifest.contains("abiVersion") &&
        !manifest.contains("variant") && !manifest.contains("builtAtUtc")) {
        result.variant = "Unknown (Phase 1)";
        return result;
    }
    const auto& abi = manifest.at("abiVersion");
    if (manifest.at("schemaVersion") != 1 || !abi.is_number_integer() || abi < 1 || abi > UINT32_MAX)
        throw std::runtime_error("DLL マニフェストのスキーマまたは ABI バージョンが無効です。");
    result.abiVersion = abi.get<uint32_t>();
    result.builtAtUtc = manifest.at("builtAtUtc").get<std::string>();
    result.variant = manifest.at("variant").get<std::string>();
    if (result.builtAtUtc.empty() || result.variant.empty())
        throw std::runtime_error("DLL マニフェストにビルド日時または Variant がありません。");
    return result;
}

Json EncodeGeneration(const Generation& generation) {
    return {{"schemaVersion", 1}, {"generation", generation.id}, {"dllSha256", generation.dllHash},
        {"pdbSha256", generation.pdbHash}, {"builtAtUtc", generation.builtAtUtc},
        {"variant", generation.variant}, {"abiVersion", generation.abiVersion}};
}

std::string UtcTime(std::time_t time) {
    std::tm utc{};
    char text[32]{};
    if (gmtime_s(&utc, &time) || !std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc))
        throw std::runtime_error("UTC の日時を取得できません。");
    return text;
}

std::filesystem::path Artifact(const std::filesystem::path& directory, const Generation& generation, const char* extension) {
    return directory / generation.id / ("EnemyAIDiagnostics-" + generation.id + extension);
}

class LockFileForRead {
public:
    explicit LockFileForRead(const std::filesystem::path& path)
        : file_(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {
        if (file_ == INVALID_HANDLE_VALUE) throw std::runtime_error("DLL/PDB の読み取りロックを取得できません。");
    }
    ~LockFileForRead() { CloseHandle(file_); }
    LockFileForRead(const LockFileForRead&) = delete;
    LockFileForRead& operator=(const LockFileForRead&) = delete;
private:
    HANDLE file_;
};

Json Operation(const char* kind, bool success, std::string summary, Json changes, Json checkpoint) {
    if (!success) {
        // A rejected source or a Lua error can contain arbitrary bytes. Make the
        // diagnostic readable without normalizing the payload of a valid generation.
        summary = Json::parse(Json(summary).dump(-1, ' ', false, Json::error_handler_t::replace)).get<std::string>();
        changes = Json::parse(changes.dump(-1, ' ', false, Json::error_handler_t::replace));
    }
    return {{"kind", kind}, {"success", success}, {"atUtc", UtcTime(std::time(nullptr))},
        {"summary", std::move(summary)}, {"changes", std::move(changes)}, {"checkpoint", std::move(checkpoint)}};
}

std::string Sha256(const std::filesystem::path& path, const char* kind) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error(std::string(kind) + " が見つからないか、開けません。");

    BCRYPT_HASH_HANDLE rawHash = nullptr;
    if (BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &rawHash, nullptr, 0, nullptr, 0, 0) < 0)
        throw std::runtime_error("SHA-256 の初期化に失敗しました。");
    const std::unique_ptr<void, decltype(&BCryptDestroyHash)> hash(rawHash, BCryptDestroyHash);
    std::array<unsigned char, 8192> buffer{};
    while (file.read(reinterpret_cast<char*>(buffer.data()), buffer.size()) || file.gcount() > 0) {
        if (BCryptHashData(hash.get(), buffer.data(), static_cast<ULONG>(file.gcount()), 0) < 0)
            throw std::runtime_error("SHA-256 の計算に失敗しました。");
    }
    if (file.bad() || !file.eof()) throw std::runtime_error(std::string(kind) + " の読み取りに失敗しました。");

    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(hash.get(), digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
        throw std::runtime_error("SHA-256 の取得に失敗しました。");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 15]);
    }
    return result;
}

template<size_t N>
bool IsUtf8(const char (&text)[N]) {
    const auto length = strnlen_s(text, N);
    return length < N && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text,
        static_cast<int>(length + 1), nullptr, 0) > 0;
}

using EvaluateFn = decltype(&EnemyAIDiagnosticsEvaluate);

std::optional<EnemyAIDiagnosticsOutput> TryEvaluate(EvaluateFn evaluate, const EnemyAIDiagnosticsInput& input) {
    EnemyAIDiagnosticsOutput output{};
    output.size = sizeof(output);
    if (evaluate(&input, &output) != 1 || output.size != sizeof(output) ||
        !std::isfinite(output.threat) || output.threat < 0 || output.threat > 1 ||
        !output.summary[0] || !IsUtf8(output.summary) || !IsUtf8(output.detail)) return std::nullopt;
    return output;
}
}

struct LiveEnemyDiagnostics::Module {
    struct Deleter {
        void operator()(HMODULE handle) const { FreeLibrary(handle); }
    };
    std::unique_ptr<LockFileForRead> dllGuard, pdbGuard;
    std::unique_ptr<std::remove_pointer_t<HMODULE>, Deleter> handle;
    EvaluateFn evaluate = nullptr;
    Generation generation;
};

LiveEnemyDiagnostics::LiveEnemyDiagnostics(std::filesystem::path directory) : directory_(std::move(directory)) {}
LiveEnemyDiagnostics::~LiveEnemyDiagnostics() = default;

void LiveEnemyDiagnostics::Open(std::filesystem::path directory) {
    Close();
    try {
        if (!directory.empty()) directory_ = std::move(directory);
        directory_ = std::filesystem::absolute(directory_);
        history_.Open(directory_);
        std::vector<Generation> generations;
        for (const auto& entry : history_.Document().at("generations")) {
            auto generation = ParseGeneration(entry);
            if (std::ranges::find(generations, generation.id, &Generation::id) != generations.end())
                throw std::runtime_error("履歴に重複した DLL 世代があります。");
            generations.push_back(std::move(generation));
        }
        generations_ = std::move(generations);
    } catch (const std::exception& error) {
        history_.Close();
        error_ = error.what();
    }
}

void LiveEnemyDiagnostics::Close() {
    active_.reset();
    pending_.reset();
    generations_.clear();
    timeline_.clear();
    cursor_ = 0;
    ++revision_;
    error_.clear();
    history_.Close();
}

void LiveEnemyDiagnostics::Request(Action action, std::string generation) {
    pending_ = RequestData{action, std::move(generation)};
}

const std::string& LiveEnemyDiagnostics::GenerationId() const {
    static const std::string empty;
    return active_ ? active_->generation.id : empty;
}

LiveEnemyDiagnostics::Generation LiveEnemyDiagnostics::ReadLatest() const {
    const auto path = directory_ / "latest.json";
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("latest.json がありません。DLL をビルドしてから読み込んでください。");
    if (std::filesystem::file_size(path) > 4096) throw std::runtime_error("latest.json が上限を超えています。");
    auto generation = ParseGeneration(Json::parse(file), true);
    if (generation.builtAtUtc.empty()) {
        const auto modified = std::filesystem::last_write_time(Artifact(directory_, generation, ".dll"));
        const auto systemTime = std::chrono::clock_cast<std::chrono::system_clock>(modified);
        generation.builtAtUtc = UtcTime(std::chrono::system_clock::to_time_t(systemTime));
    }
    return generation;
}

std::string LiveEnemyDiagnostics::UnavailableReason(const Generation& generation) const {
    if (generation.abiVersion != ENEMY_AI_DIAGNOSTICS_ABI_VERSION) return "ABI バージョン不一致";
    for (const auto* extension : {".dll", ".pdb"}) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(Artifact(directory_, generation, extension), error))
            return std::string(extension) + " が見つからないか、開けません";
    }
    return {};
}

std::unique_ptr<LiveEnemyDiagnostics::Module> LiveEnemyDiagnostics::Load(
    const Generation& generation, const EnemyAIDiagnosticsInput& input) const {
    if (const auto reason = UnavailableReason(generation); !reason.empty()) throw std::runtime_error(reason);
    const auto dll = Artifact(directory_, generation, ".dll");
    const auto pdb = Artifact(directory_, generation, ".pdb");
    // Deny writes and replacement between hashing and loading.
    auto module = std::make_unique<Module>();
    module->dllGuard = std::make_unique<LockFileForRead>(dll);
    module->pdbGuard = std::make_unique<LockFileForRead>(pdb);
    if (Sha256(dll, "DLL") != generation.dllHash || Sha256(pdb, "PDB") != generation.pdbHash)
        throw std::runtime_error("DLL/PDB の SHA-256 が公開済みの世代と一致しません。");

    module->handle.reset(LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
    if (!module->handle) throw std::runtime_error("DLL の読み込みに失敗しました (Win32 " +
        std::to_string(GetLastError()) + ")。");
    const auto getAbi = std::bit_cast<decltype(&EnemyAIDiagnosticsGetAbi)>(
        GetProcAddress(module->handle.get(), "EnemyAIDiagnosticsGetAbi"));
    module->evaluate = std::bit_cast<EvaluateFn>(GetProcAddress(module->handle.get(), "EnemyAIDiagnosticsEvaluate"));
    if (!getAbi || !module->evaluate) throw std::runtime_error("必須の DLL エクスポートがありません。");

    EnemyAIDiagnosticsAbi abi{};
    if (getAbi(&abi, sizeof(abi)) != 1 || abi.version != ENEMY_AI_DIAGNOSTICS_ABI_VERSION ||
        abi.inputSize != sizeof(EnemyAIDiagnosticsInput) || abi.outputSize != sizeof(EnemyAIDiagnosticsOutput))
        throw std::runtime_error("DLL の ABI バージョンまたは構造体サイズが一致しません。");
    if (!TryEvaluate(module->evaluate, input)) throw std::runtime_error("DLL の診断結果が無効です。");
    module->generation = generation;
    return module;
}

bool LiveEnemyDiagnostics::ApplyPending(const EnemyAIDiagnosticsInput& input, Json checkpoint, Json changes,
    const std::function<void(Json&, bool replay)>& prepare, const char* changeKind) {
    if (!pending_ && changes.empty()) return false;
    const auto request = std::exchange(pending_, std::nullopt);
    const char* operation = changes.size() + (request ? 1 : 0) > 1 ? "Live apply" : request ? "DLL apply" : changeKind;
    const auto from = GenerationId();
    std::string target = from;
    try {
        if (history_.Document().is_null()) throw std::runtime_error("DLL 履歴を使用できません。");
        auto timeline = timeline_;
        auto cursor = cursor_;
        std::optional<Generation> published;
        const bool undo = request && request->action == Action::Undo;
        const bool redo = request && request->action == Action::Redo;
        const bool replay = undo || redo;
        if (replay) {
            operation = undo ? "Live Undo" : "Live Redo";
            if (!changes.empty()) throw std::runtime_error("Undo / Redo cannot include new changes.");
            if (undo ? !CanUndo() : !CanRedo()) throw std::runtime_error("No operation to undo or redo.");
            changes = timeline[undo ? --cursor : cursor++];
            if (undo) {
                for (auto& change : changes) change.at("before").swap(change.at("after"));
            }
        } else if (request) {
            if (request->action == Action::ApplyLatest) {
                // Record the intent even if the manifest cannot be parsed.
                changes.push_back({{"resource", "EnemyAIDiagnostics"}, {"before", from}, {"after", "latest.json"}});
                published = ReadLatest();
                changes.back()["after"] = published->id;
            } else {
                operation = "DLL restore";
                changes.push_back({{"resource", "EnemyAIDiagnostics"}, {"before", from}, {"after", request->generation}});
                if (!IsLowerHex(request->generation, 32)) throw std::runtime_error("復元する DLL 世代 ID が無効です。");
            }
        }

        const auto dll = std::ranges::find_if(changes, [](const Json& change) {
            return change.at("resource") == "EnemyAIDiagnostics";
        });
        if (dll != changes.end()) target = dll->at("after").get<std::string>();
        auto generations = generations_;
        const auto known = std::ranges::find(generations, target, &Generation::id);
        if (known != generations.end() && published &&
            (known->dllHash != published->dllHash || known->pdbHash != published->pdbHash ||
                known->abiVersion != published->abiVersion))
            throw std::runtime_error("公開済みの世代 ID が別の成果物に再利用されています。");
        // Validate Lua/JSON before loading native code. A candidate module owns
        // its handle until the operation has been persisted successfully.
        if (prepare) prepare(changes, replay);
        else if (std::ranges::any_of(changes, [](const Json& change) {
            return change.at("resource") != "EnemyAIDiagnostics";
        })) throw std::runtime_error("Scene resources require a preparation callback.");
        std::unique_ptr<Module> candidate;
        if (dll != changes.end() && !target.empty()) {
            if (known == generations.end() && !published) throw std::runtime_error("履歴に DLL 世代がありません。");
            candidate = Load(known != generations.end() ? *known : *published, input);
        }
        if (!replay) {
            for (auto change = changes.begin(); change != changes.end();) {
                if (change->at("before") == change->at("after")) change = changes.erase(change);
                else ++change;
            }
            if (changes.empty()) {
                error_.clear();
                return false;
            }
            timeline.resize(cursor);
            timeline.push_back(changes);
            if (timeline.size() > kTimelineLimit) timeline.erase(timeline.begin());
            cursor = timeline.size();
        }

        if (candidate) {
            if (known != generations.end()) generations.erase(known);
            generations.push_back(candidate->generation);
        }
        while (generations.size() > kGenerationLimit) {
            const auto unused = std::ranges::find_if(generations, [&](const auto& generation) {
                if (generation.id == target) return false;
                return std::ranges::none_of(timeline, [&](const Json& entry) {
                    return std::ranges::any_of(entry, [&](const Json& change) {
                        return change.at("resource") == "EnemyAIDiagnostics" &&
                            (change.at("before") == generation.id || change.at("after") == generation.id);
                    });
                });
            });
            if (unused == generations.end()) break;
            generations.erase(unused);
        }

        auto document = history_.Document();
        auto& catalog = document.at("generations");
        catalog = Json::array();
        for (const auto& generation : generations) catalog.push_back(EncodeGeneration(generation));
        auto summary = std::to_string(changes.size()) + " resource(s)";
        if (target != from)
            summary += ": " + (from.empty() ? "組み込み表示" : from) + " → " +
                (target.empty() ? "組み込み表示" : target);
        LiveChangeHistory::Append(document, Operation(operation, true, std::move(summary), changes, checkpoint));
        history_.Commit(std::move(document));

        if (target != from) active_ = std::move(candidate);
        generations_.swap(generations);
        timeline_.swap(timeline);
        cursor_ = cursor;
        ++revision_;
        error_.clear();
        return true;
    } catch (const std::exception& error) {
        error_ = error.what();
        try {
            RecordResult(operation, false, error_, std::move(changes), std::move(checkpoint));
        } catch (const std::exception&) {
            // RecordResult exposes the journal error along with the original failure.
        }
        return false;
    }
}
std::optional<EnemyAIDiagnosticsOutput> LiveEnemyDiagnostics::Evaluate(const EnemyAIDiagnosticsInput& input) {
    if (!active_) return std::nullopt;
    auto output = TryEvaluate(active_->evaluate, input);
    if (!output) error_ = "稼働中の DLL が無効な診断結果を返しました。組み込み表示を使用します。";
    return output;
}

void LiveEnemyDiagnostics::RecordResult(const char* kind, bool success, std::string summary, Json changes, Json checkpoint) {
    try {
        auto document = history_.Document();
        LiveChangeHistory::Append(document, Operation(kind, success, std::move(summary), std::move(changes), std::move(checkpoint)));
        history_.Commit(std::move(document));
    } catch (const std::exception& error) {
        if (!error_.empty()) error_ += "\n";
        error_ += std::string("履歴の記録に失敗しました。") + error.what();
        throw;
    }
}
