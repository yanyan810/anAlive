#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <CommCtrl.h>
#include <ShObjIdl.h>
#include <Shellapi.h>

#include "DebugProtocol.h"
#include "../../ProjectScanPaths.h"
#include "AdapterDiagnostics.h"
#include "CoverageTracker.h"
#include "ExternalGenericAIProvider.h"
#include "GameWindowCapture.h"
#include "ScenarioRunner.h"
#include "TestReportGenerator.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <cwctype>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"DebugAIViewerWindow";
constexpr wchar_t kProjectToolsWindowClass[] = L"DebugAIProjectToolsWindow";
constexpr wchar_t kSemanticReviewWindowClass[] = L"DebugAISemanticReviewWindow";
constexpr wchar_t kTooltipProperty[] = L"DebugAIViewer.ButtonTooltip";
constexpr wchar_t kTooltipHoverStateProperty[] = L"DebugAIViewer.TooltipHoverState";
constexpr wchar_t kTooltipTextProperty[] = L"DebugAIViewer.TooltipText";
constexpr wchar_t kTooltipTextControlProperty[] = L"DebugAIViewer.TooltipTextControl";
constexpr char kPipeName[] = "\\\\.\\pipe\\DebugAI_CG5";
constexpr DWORD kPipeIoTimeoutMilliseconds = 10000;
constexpr UINT_PTR kButtonTooltipTimerId = 0xD06A;

enum ControlId {
    StartRecordingId = 1001,
    StopRecordingId,
    PlayLatestId,
    StopReplayId,
    RefreshId,
    ReplaySessionId,
    PlaySelectedReplayId,
    ShowReplayTimelineId,
    ReloadReplayListId,
    PauseReplayId,
    ResumeReplayId,
    StepReplayId,
    ReplaySpeedId,
    ApplyReplaySpeedId,
    ExecuteFirstActionId,
    AIStepId,
    AIStartId,
    LocalStartId,
    AIStopId,
    AIIntervalId,
    LocalAIIntervalId,
    AIActorModeId,
    AIVisionEnabledId,
    CaptureVisionId,
    AIGoalId,
    ProjectFolderId,
    ScanTargetsId,
    BrowseProjectId,
    ScanProjectId,
    GenerateProfileId,
    GenerateStateProfileId,
    GenerateLocalPolicyId,
    CoverageSummaryId,
    AdapterDiagnosticsId,
    ResetCoverageId,
    OpenLatestReportId,
    StatusTextId,
    OpenProjectToolsId,
    OpenSemanticReviewId,
    SemanticReviewListId,
    SemanticReviewCategoryId,
    SemanticReviewApproveId,
    SemanticReviewIgnoreId,
    SemanticReviewReloadId,
    SemanticReviewDetailsId,
    ScenarioListId,
    ReloadScenarioListId,
    StartScenarioId,
    RunAllScenariosId,
    StopScenarioId,
};

constexpr UINT kAIStatusMessage = WM_APP + 1;
constexpr UINT kReplayListRefreshMessage = WM_APP + 2;
constexpr UINT_PTR kPendingReplayStartTimerId = 1;
constexpr UINT_PTR kGameProcessWatchTimerId = 2;

HWND gStatusText = nullptr;
HWND gMainWindow = nullptr;
HWND gProjectToolsWindow = nullptr;
HWND gSemanticReviewWindow = nullptr;
HWND gReplaySessions = nullptr;
HWND gPlaySelectedReplay = nullptr;
HWND gReplaySpeed = nullptr;
DebugObservation gLastObservation;
bool gHasObservation = false;
ExternalGenericAIProvider gAIProvider;
CoverageTracker gCoverageTracker;
ScenarioRunner gScenarioRunner;
HWND gAIInterval = nullptr;
HWND gLocalAIInterval = nullptr;
HWND gAIActorMode = nullptr;
HWND gAIVisionEnabled = nullptr;
HWND gAIGoal = nullptr;
HWND gProjectFolder = nullptr;
HWND gScanTargets = nullptr;
HWND gScenarioList = nullptr;
HWND gSemanticReviewList = nullptr;
HWND gSemanticReviewCategory = nullptr;
HWND gSemanticReviewDetails = nullptr;
std::thread gAIWorker;
std::atomic_bool gAIWorkerRunning = false;
std::atomic_bool gAIStopRequested = false;
std::thread gConnectionWorker;
std::atomic_bool gConnectionWorkerRunning = false;
std::atomic_bool gConnectionStatusReady = false;
std::atomic_bool gConnectionInitialResultPending = true;
std::mutex gConnectionStatusMutex;
std::string gPendingConnectionStatus;
std::atomic_bool gAIConnectionVerified = false;
std::mutex gAIWaitMutex;
std::condition_variable gAIWaitCondition;
std::mutex gAIStatusMutex;
std::string gPendingAIStatus;
std::mutex gTransportMutex;
std::atomic<DWORD> gGameProcessId = 0;
std::atomic_bool gGameConnectionEstablished = false;
std::atomic_uint64_t gEvidenceCaptureSequence = 0;
std::mutex gGameProcessWatchMutex;
DWORD gWatchedGameProcessId = 0;
HANDLE gWatchedGameProcessHandle = nullptr;
ULONGLONG gAutoRefreshResumeTick = 0;
ULONGLONG gNextConnectionAttemptTick = 0;
std::function<std::string()> gPendingReplayStart;

enum class SemanticReviewKind { Action, StateMapping };

struct SemanticReviewEntry {
    SemanticReviewKind kind = SemanticReviewKind::Action;
    std::string actionId;
    std::string category;
    std::string genericProperty;
    std::string sourceSymbol;
    float confidence = 0.0f;
    std::vector<std::string> evidence;
};

std::vector<SemanticReviewEntry> gSemanticReviewEntries;
std::size_t gSemanticReviewHiddenDuplicates = 0;

enum class ControlledActorMode { Player, Boss, Both };

struct ReplaySessionListEntry {
    std::filesystem::path manifestPath;
    std::wstring label;
    std::filesystem::file_time_type modified{};
};

std::vector<ReplaySessionListEntry> gReplaySessionEntries;

struct ScenarioListEntry {
    std::filesystem::path path;
    std::wstring label;
};

std::vector<ScenarioListEntry> gScenarioEntries;

struct ScenarioBatchItemResult {
    std::filesystem::path scenarioPath;
    std::filesystem::path resultPath;
    std::string label;
    std::string status;
    std::string detail;
    std::size_t anomalyCount = 0;
    std::size_t anomalyErrorCount = 0;
    std::string replayVerificationStatus = "not_run";
    std::size_t replayVerificationChecked = 0;
    std::size_t replayVerificationCheckpoints = 0;
    std::size_t replayVerificationMismatches = 0;
    std::string replayVerificationDetail;
    double elapsedSeconds = 0.0;
};

struct AutomaticReplayVerificationResult {
    std::string status = "not_run";
    std::size_t checked = 0;
    std::size_t checkpoints = 0;
    std::size_t mismatches = 0;
    std::string detail;
};

void PostAIStatus(HWND window, std::string text, bool preserveResult);
bool IsPlayerActorAction(const DebugGenericAction& action);
bool ProtocolResultOk(const DebugProtocolMessage& message);

void WatchGameProcess(DWORD processId) {
    if (processId == 0) return;
    std::lock_guard lock(gGameProcessWatchMutex);
    if (gWatchedGameProcessId == processId &&
        gWatchedGameProcessHandle != nullptr) {
        return;
    }
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processId);
    if (!process) return;
    if (gWatchedGameProcessHandle) {
        CloseHandle(gWatchedGameProcessHandle);
    }
    gWatchedGameProcessId = processId;
    gWatchedGameProcessHandle = process;
}

bool WatchedGameProcessExited() {
    std::lock_guard lock(gGameProcessWatchMutex);
    return gWatchedGameProcessHandle &&
        WaitForSingleObject(gWatchedGameProcessHandle, 0) ==
            WAIT_OBJECT_0;
}

bool HasWatchedGameProcess() {
    std::lock_guard lock(gGameProcessWatchMutex);
    return gWatchedGameProcessHandle != nullptr;
}

void StopWatchingGameProcess() {
    std::lock_guard lock(gGameProcessWatchMutex);
    if (gWatchedGameProcessHandle) {
        CloseHandle(gWatchedGameProcessHandle);
        gWatchedGameProcessHandle = nullptr;
    }
    gWatchedGameProcessId = 0;
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring ReadWindowText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) return {};
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(control, text.data(), length + 1);
    text.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0u);
    return text;
}

std::filesystem::path WorkspaceConfigPath() {
    if (!gAIProvider.ConfigPath().empty()) {
        return std::filesystem::path(gAIProvider.ConfigPath()).parent_path() /
            "debug_ai.workspace.local.json";
    }
    return std::filesystem::current_path() / "Engine/DebugAI/config/debug_ai.workspace.local.json";
}

void SaveWorkspaceSettings(const std::filesystem::path& folder, const std::wstring& scanTargets = {}) {
    const auto path = WorkspaceConfigPath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    nlohmann::json previous;
    {
        std::ifstream input(path);
        previous = nlohmann::json::parse(input, nullptr, false);
        if (previous.is_discarded() || !previous.is_object()) {
            previous = nlohmann::json::object();
        }
    }
    const auto readInterval = [](HWND control, unsigned int fallback) {
        if (!control) return fallback;
        wchar_t text[32]{};
        GetWindowTextW(control, text, static_cast<int>(std::size(text)));
        wchar_t* end = nullptr;
        const unsigned long value = wcstoul(text, &end, 10);
        return end == text ? fallback
            : static_cast<unsigned int>(std::clamp(value, 60ul, 5000ul));
    };
    nlohmann::json config = {
        { "schemaVersion", 3 },
        { "projectFolder", WideToUtf8(folder.wstring()) },
        { "scanTargets", WideToUtf8(scanTargets) },
        { "apiIntervalMs", gAIInterval
            ? readInterval(gAIInterval, 2000)
            : previous.value("apiIntervalMs", 2000u) },
        { "localIntervalMs", readInterval(gLocalAIInterval, 250) },
        { "visionEnabled", gAIVisionEnabled
            ? SendMessageW(gAIVisionEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED
            : previous.value("visionEnabled", false) },
    };
    std::ofstream output(path);
    if (output) output << config.dump(2) << '\n';
}

void SaveProjectFolder(const std::filesystem::path& folder) {
    SaveWorkspaceSettings(folder, gScanTargets ? ReadWindowText(gScanTargets) : std::wstring{});
}

std::wstring LoadProjectFolder() {
    std::ifstream input(WorkspaceConfigPath());
    const auto config = nlohmann::json::parse(input, nullptr, false);
    if (config.is_discarded() || !config.is_object()) return {};
    return Utf8ToWide(config.value("projectFolder", ""));
}

std::wstring LoadScanTargets() {
    std::ifstream input(WorkspaceConfigPath());
    const auto config = nlohmann::json::parse(input, nullptr, false);
    if (config.is_discarded() || !config.is_object()) return {};
    return Utf8ToWide(config.value("scanTargets", ""));
}

struct ProjectScanResult {
    struct SourceEvidence {
        std::string kind;
        std::string symbol;
        std::string source;
        std::size_t line = 0;
        std::string excerpt;
        float confidence = 0.0f;
    };
    struct SourceFileSummary {
        std::string path;
        std::string category;
        std::string language;
        std::uint64_t bytes = 0;
        std::size_t lines = 0;
        std::size_t targetMatches = 0;
    };
    std::filesystem::path root;
    std::uint64_t bytes = 0;
    std::size_t files = 0;
    std::size_t skippedLarge = 0;
    std::size_t analysisErrors = 0;
    std::string firstAnalysisError;
    std::map<std::string, std::size_t> categories;
    std::map<std::string, std::set<std::string>> actionSources;
    std::map<std::string, std::vector<SourceEvidence>> actionEvidence;
    std::map<std::string, std::set<std::string>> declaredSymbolSources;
    std::map<std::string, std::vector<SourceEvidence>> declaredSymbolEvidence;
    std::map<std::string, std::set<std::string>> dependencySources;
    std::map<std::string, std::vector<SourceEvidence>> dependencyEvidence;
    std::map<std::string, std::set<std::string>> runtimePropertySources;
    std::map<std::string, std::vector<SourceEvidence>> runtimePropertyEvidence;
    std::map<std::string, std::set<std::string>> sceneSources;
    std::map<std::string, std::vector<SourceEvidence>> sceneEvidence;
    std::map<std::string, std::set<std::string>> inputSources;
    std::map<std::string, std::vector<SourceEvidence>> inputEvidence;
    std::vector<SourceEvidence> targetEvidence;
    std::vector<SourceFileSummary> sourceFiles;
    struct SignalCandidate {
        std::string genericProperty;
        std::string symbol;
        std::string valueKind;
        float confidence = 0.0f;
        std::set<std::string> sources;
        std::vector<SourceEvidence> evidence;
    };
    std::map<std::string, SignalCandidate> signalCandidates;
    std::map<std::string, std::set<std::string>> stateValues;
    struct AttackRangeCandidate {
        std::string symbol;
        std::string label;
        std::string source;
        double range = 0.0;
        float confidence = 0.0f;
        std::size_t line = 0;
        std::string excerpt;
    };
    std::vector<AttackRangeCandidate> attackRangeCandidates;
};

struct ActionSemanticInference {
    std::string category = "generic";
    std::vector<std::string> tags;
    std::vector<std::string> evidence;
    float confidence = 0.25f;
};

ActionSemanticInference InferActionSemantics(
    const std::string& actionId,
    const ProjectScanResult& scan);

std::size_t SourceLineNumber(
    const std::string& text,
    std::size_t position) {
    position = (std::min)(position, text.size());
    return 1 + static_cast<std::size_t>(
        std::count(text.begin(), text.begin() + position, '\n'));
}

bool LoadVisionEnabled(bool fallback) {
    std::ifstream input(WorkspaceConfigPath());
    const auto config = nlohmann::json::parse(input, nullptr, false);
    if (config.is_discarded() || !config.is_object()) return fallback;
    return config.value("visionEnabled", fallback);
}

std::string SourceLineExcerpt(
    const std::string& text,
    std::size_t position) {
    position = (std::min)(position, text.size());
    const std::size_t begin =
        position == 0 ? 0 : text.rfind('\n', position - 1) + 1;
    std::size_t end = text.find('\n', position);
    if (end == std::string::npos) end = text.size();
    std::string excerpt = text.substr(begin, end - begin);
    const auto first = excerpt.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    excerpt.erase(0, first);
    const auto last = excerpt.find_last_not_of(" \t\r");
    if (last != std::string::npos) excerpt.resize(last + 1);
    constexpr std::size_t kMaximumExcerptBytes = 180;
    if (excerpt.size() > kMaximumExcerptBytes) {
        excerpt.resize(kMaximumExcerptBytes);
        excerpt += "...";
    }
    return excerpt;
}

void AddBoundedEvidence(
    std::vector<ProjectScanResult::SourceEvidence>& destination,
    ProjectScanResult::SourceEvidence evidence,
    std::size_t maximum = 4) {
    const bool duplicate = std::any_of(
        destination.begin(), destination.end(), [&](const auto& current) {
            return current.source == evidence.source &&
                current.line == evidence.line &&
                current.symbol == evidence.symbol;
        });
    if (!duplicate && destination.size() < maximum) {
        destination.push_back(std::move(evidence));
    }
}

std::string ClassifyProjectFile(const std::filesystem::path& relative) {
    std::wstring value = relative.wstring();
    std::transform(value.begin(), value.end(), value.begin(), ::towlower);
    if (value.find(L"debugai") != std::wstring::npos || value.find(L"adapter") != std::wstring::npos) return "DebugAI/Adapter";
    if (value.find(L"input") != std::wstring::npos) return "Input";
    if (value.find(L"action") != std::wstring::npos || value.find(L"attack") != std::wstring::npos ||
        value.find(L"player") != std::wstring::npos) return "Player/Action";
    if (value.find(L"enemy") != std::wstring::npos || value.find(L"boss") != std::wstring::npos) return "Enemy/Boss";
    if (value.find(L"scene") != std::wstring::npos) return "Scene";
    if (relative.extension() == L".json") return "JSON/Config";
    return "Other Source";
}

void ExtractActionIds(
    const std::string& text,
    const std::string& source,
    ProjectScanResult& result) {
    struct Pattern {
        std::regex expression;
        float confidence;
    };
    static const std::array<Pattern, 6> patterns = {
        Pattern{ std::regex(R"re(action\.name\s*==\s*"([A-Za-z][A-Za-z0-9_.-]+)")re"), 0.98f },
        Pattern{ std::regex(R"re((?:[A-Za-z_][A-Za-z0-9_]*\.)*actionId\s*==\s*"([A-Za-z][A-Za-z0-9_.-]+)")re"), 0.98f },
        Pattern{ std::regex(R"re(actionId\s*[=:]\s*"([A-Za-z][A-Za-z0-9_.-]+)")re"), 0.98f },
        Pattern{ std::regex(R"re(actionId"\s*:\s*"([A-Za-z][A-Za-z0-9_.-]+)")re"), 0.98f },
        Pattern{ std::regex(R"re((?:add|register|execute)[A-Za-z0-9_]*Action\s*\(\s*"([A-Za-z][A-Za-z0-9_.-]+)")re", std::regex::icase), 0.88f },
        Pattern{ std::regex(R"re(\{\s*"([A-Z][A-Za-z0-9_.-]+)"\s*\})re"), 0.60f },
    };
    for (const auto& pattern : patterns) {
        for (std::sregex_iterator match(
            text.begin(), text.end(), pattern.expression), end;
            match != end; ++match) {
            const std::string actionId = (*match)[1].str();
            result.actionSources[actionId].insert(source);
            const std::size_t position =
                static_cast<std::size_t>((*match).position());
            AddBoundedEvidence(result.actionEvidence[actionId], {
                "Action", actionId, source, SourceLineNumber(text, position),
                SourceLineExcerpt(text, position), pattern.confidence,
            });
        }
    }
}

std::string LowerAscii(std::string value);

void AddAttackRangeCandidate(ProjectScanResult& result, const std::string& symbol,
    const std::string& label, const std::string& source, double range,
    float confidence, std::size_t line = 0, std::string excerpt = {}) {
    range = std::abs(range);
    if (!std::isfinite(range) || range < 0.05 || range > 100.0) return;
    result.attackRangeCandidates.push_back({
        symbol, label, source, range, confidence, line, std::move(excerpt),
    });
}

void ExtractAttackRangeCandidates(const std::string& text, const std::string& source,
    const std::string& category, ProjectScanResult& result) {
    if (category != "Player/Action") return;
    static const std::string number = R"re([-+]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))re";
    static const std::string capturedNumber = "(" + number + ")";

    // Named reach/range constants are the strongest engine-independent hints.
    const std::regex scalarPattern(
        "(?:constexpr\\s+)?(?:const\\s+)?(?:float|double)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*" +
        capturedNumber + "[fF]?");
    for (std::sregex_iterator match(text.begin(), text.end(), scalarPattern), end; match != end; ++match) {
        const std::string symbol = (*match)[1].str();
        const std::string name = LowerAscii(symbol);
        const bool rangeName = name.find("range") != std::string::npos ||
            name.find("reach") != std::string::npos || name.find("radius") != std::string::npos;
        const bool unrelated = name.find("time") != std::string::npos || name.find("sec") != std::string::npos ||
            name.find("interval") != std::string::npos || name.find("count") != std::string::npos ||
            name.find("damage") != std::string::npos || name.find("scale") != std::string::npos ||
            name.find("rate") != std::string::npos;
        if (rangeName && !unrelated) {
            const auto position =
                static_cast<std::size_t>((*match).position());
            AddAttackRangeCandidate(
                result, symbol, {}, source,
                std::stod((*match)[2].str()), 0.78f,
                SourceLineNumber(text, position),
                SourceLineExcerpt(text, position));
        }
    }

    // Hitbox half-size vectors provide a conservative reach when no explicit
    // range constant exists.
    const std::regex vectorPattern(
        "(?:constexpr\\s+)?(?:const\\s+)?(?:Vector[234]|XMFLOAT3)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*\\{\\s*" +
        capturedNumber + "[fF]?\\s*,\\s*" + capturedNumber + "[fF]?\\s*,\\s*" +
        capturedNumber + "[fF]?\\s*\\}");
    for (std::sregex_iterator match(text.begin(), text.end(), vectorPattern), end; match != end; ++match) {
        const std::string symbol = (*match)[1].str();
        const std::string name = LowerAscii(symbol);
        if (name.find("hitbox") == std::string::npos && name.find("halfsize") == std::string::npos &&
            name.find("range") == std::string::npos && name.find("reach") == std::string::npos) continue;
        const double x = std::abs(std::stod((*match)[2].str()));
        const double z = std::abs(std::stod((*match)[4].str()));
        const auto position =
            static_cast<std::size_t>((*match).position());
        AddAttackRangeCandidate(
            result, symbol, {}, source, (std::max)(x, z), 0.66f,
            SourceLineNumber(text, position),
            SourceLineExcerpt(text, position));
    }

    // Common data-driven attack definition: label, center offset, half-size.
    // Reach along the attack direction is abs(offset.x) + halfSize.x.
    const std::regex definitionPattern(
        "\\{\\s*\"([^\"]+)\"\\s*,\\s*\\{\\s*" + capturedNumber +
        "[fF]?\\s*,\\s*" + capturedNumber + "[fF]?\\s*,\\s*" + capturedNumber +
        "[fF]?\\s*\\}\\s*,\\s*\\{\\s*" + capturedNumber +
        "[fF]?\\s*,\\s*" + capturedNumber + "[fF]?\\s*,\\s*" + capturedNumber + "[fF]?\\s*\\}");
    for (std::sregex_iterator match(text.begin(), text.end(), definitionPattern), end; match != end; ++match) {
        const double offsetX = std::abs(std::stod((*match)[2].str()));
        const double halfSizeX = std::abs(std::stod((*match)[5].str()));
        const auto position =
            static_cast<std::size_t>((*match).position());
        AddAttackRangeCandidate(
            result, "attackDefinition", (*match)[1].str(), source,
            offsetX + halfSizeX, 0.90f,
            SourceLineNumber(text, position),
            SourceLineExcerpt(text, position));
    }
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string SourceLanguage(const std::filesystem::path& path) {
    std::wstring extension = path.extension().wstring();
    std::transform(
        extension.begin(), extension.end(), extension.begin(), ::towlower);
    if (extension == L".cpp" || extension == L".c" ||
        extension == L".h" || extension == L".hpp") return "C/C++";
    if (extension == L".cs") return "C#";
    if (extension == L".json") return "JSON";
    if (extension == L".lua") return "Lua";
    if (extension == L".py") return "Python";
    return "Source";
}

unsigned int LoadWorkspaceInterval(const char* name, unsigned int fallback) {
    std::ifstream input(WorkspaceConfigPath());
    const auto config = nlohmann::json::parse(input, nullptr, false);
    if (config.is_discarded() || !config.is_object()) return fallback;
    const auto value = config.value(name, fallback);
    return static_cast<unsigned int>(std::clamp<std::uint64_t>(value, 60, 5000));
}

void AddSignalCandidate(
    ProjectScanResult& result,
    const std::string& property,
    const std::string& symbol,
    const std::string& kind,
    float confidence,
    const std::string& source);

bool IsSemanticRuntimeProperty(const std::string& property) {
    const std::string name = LowerAscii(property);
    static const std::set<std::string> exact = {
        "game.phase", "game.state", "game.mode",
        "player.hp", "player.health", "player.maxhp", "player.action",
        "player.attacktype", "player.isattacking", "player.canmove",
        "player.canjump", "player.canattack", "player.onground",
        "enemy.hp", "enemy.health", "enemy.maxhp", "enemy.phase",
        "enemy.state", "enemy.intent", "enemy.threat",
        "enemy.attackactive", "enemy.attackstartup",
        "enemy.attackrange", "enemy.distancetoplayer",
    };
    if (exact.contains(name)) return true;
    const auto semanticSuffix = [&](std::string_view suffix) {
        return name.size() > suffix.size() && name.ends_with(suffix);
    };
    return semanticSuffix(".hp") || semanticSuffix(".health") ||
        semanticSuffix(".phase") || semanticSuffix(".state") ||
        semanticSuffix(".action") || semanticSuffix(".threat") ||
        semanticSuffix(".attackactive");
}

void ExtractSourceIndex(
    const std::string& text,
    const std::string& source,
    const std::string& category,
    const std::string& language,
    const std::vector<std::string>& scanTargets,
    ProjectScanResult& result,
    ProjectScanResult::SourceFileSummary& fileSummary) {
    static const std::regex includePattern(
        R"re(^\s*#\s*include\s*[<"]([^>"]+)[>"])re");
    static const std::regex typePattern(
        R"re(\b(class|struct|enum(?:\s+class)?)\s+([A-Za-z_][A-Za-z0-9_]*))re");
    static const std::regex qualifiedFunctionPattern(
        R"re(\b([A-Za-z_][A-Za-z0-9_]*)::([A-Za-z_~][A-Za-z0-9_]*)\s*\()re");
    static const std::regex functionPattern(
        R"re(\b(?:void|bool|int|float|double|auto|std::[A-Za-z0-9_:<>]+|[A-Za-z_][A-Za-z0-9_:<>]*\s*[*&]?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*\)\s*(?:const\s*)?(?:\{|override\b))re");
    static const std::regex propertyPattern(
        R"re((?:properties|parameters)\s*\[\s*"([^"]+)")re");
    static const std::regex scenePattern(
        R"re((?:ChangeScene|RequestChangeScene_?|sceneId)\s*(?:\(|=|:)\s*"([A-Za-z0-9_.-]+)")re");
    static const std::regex inputPattern(
        R"re(\b(DIK_[A-Z0-9_]+|KeyCode::[A-Za-z0-9_]+|Keys\.[A-Za-z0-9_]+)\b)re");

    std::istringstream lines(text);
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(lines, line)) {
        ++lineNumber;
        ++fileSummary.lines;
        std::string excerpt = line;
        const auto first = excerpt.find_first_not_of(" \t\r");
        if (first == std::string::npos) excerpt.clear();
        else excerpt.erase(0, first);
        if (excerpt.size() > 180) {
            excerpt.resize(180);
            excerpt += "...";
        }
        const std::string lowerLine = LowerAscii(line);
        for (const std::string& target : scanTargets) {
            if (!target.empty() &&
                lowerLine.find(target) != std::string::npos) {
                ++fileSummary.targetMatches;
                AddBoundedEvidence(result.targetEvidence, {
                    "TargetMatch", target, source, lineNumber, excerpt, 1.0f,
                }, 200);
            }
        }

        std::smatch match;
        if (std::regex_search(line, match, includePattern)) {
            const std::string dependency = match[1].str();
            result.dependencySources[dependency].insert(source);
            AddBoundedEvidence(result.dependencyEvidence[dependency], {
                "Dependency", dependency, source, lineNumber, excerpt, 1.0f,
            });
        }

        for (std::sregex_iterator found(
            line.begin(), line.end(), typePattern), end;
            found != end; ++found) {
            const std::string kind = (*found)[1].str();
            const std::string symbol = (*found)[2].str();
            const std::string key = kind + ":" + symbol;
            result.declaredSymbolSources[key].insert(source);
            AddBoundedEvidence(result.declaredSymbolEvidence[key], {
                kind, symbol, source, lineNumber, excerpt, 0.96f,
            });
            if (LowerAscii(symbol).ends_with("scene")) {
                result.sceneSources[symbol].insert(source);
                AddBoundedEvidence(result.sceneEvidence[symbol], {
                    "SceneType", symbol, source, lineNumber, excerpt, 0.88f,
                });
            }
        }

        bool foundQualifiedFunction = false;
        for (std::sregex_iterator found(
            line.begin(), line.end(), qualifiedFunctionPattern), end;
            found != end; ++found) {
            foundQualifiedFunction = true;
            const std::string symbol =
                (*found)[1].str() + "::" + (*found)[2].str();
            const std::string key = "function:" + symbol;
            result.declaredSymbolSources[key].insert(source);
            AddBoundedEvidence(result.declaredSymbolEvidence[key], {
                "function", symbol, source, lineNumber, excerpt, 0.98f,
            });
        }
        if (!foundQualifiedFunction &&
            std::regex_search(line, match, functionPattern)) {
            const std::string symbol = match[1].str();
            const std::string key = "function:" + symbol;
            result.declaredSymbolSources[key].insert(source);
            AddBoundedEvidence(result.declaredSymbolEvidence[key], {
                "function", symbol, source, lineNumber, excerpt, 0.78f,
            });
        }

        for (std::sregex_iterator found(
            line.begin(), line.end(), propertyPattern), end;
            found != end; ++found) {
            const std::string property = (*found)[1].str();
            result.runtimePropertySources[property].insert(source);
            AddBoundedEvidence(result.runtimePropertyEvidence[property], {
                "RuntimeProperty", property, source, lineNumber, excerpt, 0.99f,
            });
            if (IsSemanticRuntimeProperty(property)) {
                AddSignalCandidate(
                    result, property, property, "runtimeProperty",
                    0.995f, source);
            }
        }
        for (std::sregex_iterator found(
            line.begin(), line.end(), scenePattern), end;
            found != end; ++found) {
            const std::string scene = (*found)[1].str();
            result.sceneSources[scene].insert(source);
            AddBoundedEvidence(result.sceneEvidence[scene], {
                "SceneId", scene, source, lineNumber, excerpt, 0.96f,
            });
        }
        for (std::sregex_iterator found(
            line.begin(), line.end(), inputPattern), end;
            found != end; ++found) {
            const std::string input = (*found)[1].str();
            result.inputSources[input].insert(source);
            AddBoundedEvidence(result.inputEvidence[input], {
                "Input", input, source, lineNumber, excerpt,
                category == "Input" ? 0.98f : 0.86f,
            });
        }
    }

    // State candidates are name-based and reviewable. Attach the first actual
    // source occurrence so users can jump to evidence instead of approving a
    // symbol from its name alone.
    for (auto& [key, candidate] : result.signalCandidates) {
        if (!candidate.sources.contains(source)) continue;
        const std::size_t position = text.find(candidate.symbol);
        if (position == std::string::npos) continue;
        AddBoundedEvidence(candidate.evidence, {
            "StateMapping", candidate.symbol, source,
            SourceLineNumber(text, position),
            SourceLineExcerpt(text, position), candidate.confidence,
        });
    }

    fileSummary.language = language;
}

void AddSignalCandidate(ProjectScanResult& result, const std::string& property,
    const std::string& symbol, const std::string& kind, float confidence, const std::string& source) {
    const std::string key = property + "\n" + symbol;
    auto& candidate = result.signalCandidates[key];
    candidate.genericProperty = property;
    candidate.symbol = symbol;
    candidate.valueKind = kind;
    if (confidence > candidate.confidence) candidate.confidence = confidence;
    candidate.sources.insert(source);
}

void ExtractStateSignals(const std::string& text, const std::string& source,
    const std::string& category, ProjectScanResult& result) {
    // Extract declared fields and zero-argument getters. This intentionally uses names only;
    // generated mappings remain reviewable candidates and never execute source code.
    static const std::regex symbolPattern(
        R"re(\b(bool|float|double|int|uint\w*|size_t|[A-Za-z_][A-Za-z0-9_]*(?:State|Phase|Action|Mode))\s+((?:Get|Is|Can|Has|Did)?[A-Za-z_][A-Za-z0-9_]*)\s*(?:\(\s*\)\s*const|[_{=;]))re");
    const bool bossSource = category == "Enemy/Boss";
    const bool playerSource = category == "Player/Action" || category == "Input";
    const bool sceneSource = category == "Scene";
    const auto healthName = [](const std::string& name) {
        return name == "hp" || name == "hp_" || name == "health" ||
            name == "health_" || name == "gethp" || name == "gethealth" ||
            name.find("currenthp") != std::string::npos ||
            name.find("currenthealth") != std::string::npos ||
            name.find("hitpoints") != std::string::npos;
    };
    const auto maxHealthName = [](const std::string& name) {
        return name.find("maxhp") != std::string::npos ||
            name.find("maxhealth") != std::string::npos ||
            name.find("maximumhealth") != std::string::npos;
    };
    for (std::sregex_iterator match(text.begin(), text.end(), symbolPattern), end; match != end; ++match) {
        const std::string kind = (*match)[1].str();
        const std::string symbol = (*match)[2].str();
        const std::string name = LowerAscii(symbol);
        const std::string kindName = LowerAscii(kind);
        if (bossSource) {
            if (healthName(name))
                AddSignalCandidate(result, "enemy.hp", symbol, kind, 0.95f, source);
            if (maxHealthName(name))
                AddSignalCandidate(result, "enemy.maxHp", symbol, kind, 0.93f, source);
            if (name == "getstate" || name == "state" || name == "state_" || name == "st_")
                AddSignalCandidate(result, "enemy.intent", symbol, kind, 0.92f, source);
            if (name.find("attackactive") != std::string::npos || name.find("isattacking") != std::string::npos ||
                name.find("requestmeleeattack") != std::string::npos)
                AddSignalCandidate(result, "enemy.attackActive", symbol, kind, 0.92f, source);
            if ((name.find("startup") != std::string::npos || name.find("windup") != std::string::npos ||
                 name.find("charge") != std::string::npos) &&
                (name.find("frame") != std::string::npos || name.find("time") != std::string::npos || name.find("sec") != std::string::npos))
                AddSignalCandidate(result, "enemy.attackStartup", symbol, kind, 0.84f, source);
            if (name.find("attackrange") != std::string::npos || name.find("attackhitbox") != std::string::npos)
                AddSignalCandidate(result, "enemy.attackRange", symbol, kind, 0.88f, source);
            if (name.find("dist") != std::string::npos &&
                (name.find("player") != std::string::npos || name.find("target") != std::string::npos || name.find("chase") != std::string::npos))
                AddSignalCandidate(result, "enemy.distanceToPlayer", symbol, kind, 0.72f, source);
            if (name == "getphase" || name == "phase" || name == "phase_" ||
                kindName.find("phase") != std::string::npos)
                AddSignalCandidate(result, "enemy.phase", symbol, kind, 0.90f, source);
        }
        if (playerSource) {
            if (healthName(name))
                AddSignalCandidate(result, "player.hp", symbol, kind, 0.95f, source);
            if (maxHealthName(name))
                AddSignalCandidate(result, "player.maxHp", symbol, kind, 0.93f, source);
            if (name.find("isonground") != std::string::npos || name == "onground")
                AddSignalCandidate(result, "player.onGround", symbol, kind, 0.96f, source);
            if (name.find("canstartattack") != std::string::npos || name.find("canattack") != std::string::npos)
                AddSignalCandidate(result, "player.canAttack", symbol, kind, 0.90f, source);
            if (name.find("currentaction") != std::string::npos)
                AddSignalCandidate(result, "player.action", symbol, kind, 0.91f, source);
            if (name.find("currentattacktype") != std::string::npos || name == "attacktype_")
                AddSignalCandidate(result, "player.attackType", symbol, kind, 0.91f, source);
        }
        if (sceneSource) {
            if (name.find("phase") != std::string::npos ||
                kindName.find("phase") != std::string::npos)
                AddSignalCandidate(result, "game.phase", symbol, kind, 0.88f, source);
            if (name.find("gamestate") != std::string::npos ||
                kindName.find("gamestate") != std::string::npos)
                AddSignalCandidate(result, "game.state", symbol, kind, 0.84f, source);
            if (name.find("playerhp") != std::string::npos ||
                name.find("playerhealth") != std::string::npos)
                AddSignalCandidate(result, "player.hp", symbol, kind, 0.88f, source);
            if (name.find("enemyhp") != std::string::npos ||
                name.find("bosshealth") != std::string::npos ||
                name.find("bosshp") != std::string::npos)
                AddSignalCandidate(result, "enemy.hp", symbol, kind, 0.88f, source);
        }
    }

    // Also inspect identifier references and functions with parameters. This finds
    // helpers such as IsBossIncomingAttack(state), not only fields/getters.
    static const std::regex identifierPattern(R"re(\b([A-Za-z_][A-Za-z0-9_]*)\b)re");
    for (std::sregex_iterator match(text.begin(), text.end(), identifierPattern), end; match != end; ++match) {
        const std::string symbol = (*match)[1].str();
        const std::string name = LowerAscii(symbol);
        if (bossSource) {
            if (name.find("threat") != std::string::npos || name.find("incomingattack") != std::string::npos ||
                name.find("danger") != std::string::npos)
                AddSignalCandidate(result, "enemy.threat", symbol, "inferred", 0.86f, source);
            if (name.find("attackactive") != std::string::npos || name.find("isattacking") != std::string::npos ||
                name.find("requestmeleeattack") != std::string::npos)
                AddSignalCandidate(result, "enemy.attackActive", symbol, "bool", 0.88f, source);
            if ((name.find("distance") != std::string::npos || name.find("dist") != std::string::npos) &&
                (name.find("player") != std::string::npos || name.find("target") != std::string::npos ||
                 name.find("chase") != std::string::npos))
                AddSignalCandidate(result, "enemy.distanceToPlayer", symbol, "number", 0.78f, source);
        }
        if (playerSource) {
            if (name.find("canmove") != std::string::npos || name.find("movementenabled") != std::string::npos)
                AddSignalCandidate(result, "player.canMove", symbol, "bool", 0.90f, source);
            if (name.find("canjump") != std::string::npos || name.find("jumpenabled") != std::string::npos)
                AddSignalCandidate(result, "player.canJump", symbol, "bool", 0.90f, source);
            if (name.find("canattack") != std::string::npos || name.find("canstartattack") != std::string::npos)
                AddSignalCandidate(result, "player.canAttack", symbol, "bool", 0.90f, source);
            if (name.find("isattacking") != std::string::npos || name.find("attackactive") != std::string::npos ||
                name.find("currentattacktype") != std::string::npos)
                AddSignalCandidate(result, "player.isAttacking", symbol, "bool/inferred", 0.84f, source);
        }
    }

    static const std::regex enumPattern(
        R"re(enum\s+class\s+([A-Za-z_][A-Za-z0-9_]*(?:State|Phase|Action|Mode))\s*(?::[^\{]+)?\{([^\}]+)\})re");
    static const std::regex valuePattern(R"re(\b([A-Za-z][A-Za-z0-9_]*)\b\s*(?:=[^,]+)?(?:,|$))re");
    for (std::sregex_iterator match(text.begin(), text.end(), enumPattern), end; match != end; ++match) {
        const std::string enumName = (*match)[1].str();
        const std::string body = (*match)[2].str();
        for (std::sregex_iterator value(body.begin(), body.end(), valuePattern), valueEnd; value != valueEnd; ++value) {
            result.stateValues[enumName].insert((*value)[1].str());
        }
    }
}

bool AnalyzeProjectFolder(
    const std::filesystem::path& root,
    ProjectScanResult& result,
    const std::vector<std::string>& scanTargets = {}) {
    std::error_code rootError;
    if (!std::filesystem::is_directory(root, rootError) || rootError) return false;
    result.root = root;
    static const std::set<std::wstring> extensions = {
        L".cpp", L".c", L".h", L".hpp", L".cs", L".json", L".lua", L".py"
    };
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(root,
        std::filesystem::directory_options::skip_permission_denied, error), end;
    for (; iterator != end && result.files < 5000; iterator.increment(error)) {
        if (error) { error.clear(); continue; }
        const auto& entry = *iterator;
        if (entry.is_directory(error) && IsExcludedProjectPath(entry.path().lexically_relative(root))) {
            iterator.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file(error) || IsExcludedProjectPath(entry.path().lexically_relative(root))) continue;
        std::wstring filename = entry.path().filename().wstring();
        std::transform(filename.begin(), filename.end(), filename.begin(), ::towlower);
        if (filename == L".env" || filename.ends_with(L".user") ||
            filename.find(L"secret") != std::wstring::npos ||
            filename.find(L"credential") != std::wstring::npos ||
            filename == L"project_scan.json" ||
            filename == L"action_profile.json" ||
            filename == L"state_mapping_profile.json" ||
            filename == L"local_policy.json" ||
            filename == L"debug_ai.local.json" || filename == L"debug_ai.workspace.local.json") continue;
        std::wstring extension = entry.path().extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
        if (!extensions.contains(extension)) continue;
        const auto size = entry.file_size(error);
        if (error) { error.clear(); continue; }
        if (size > 512 * 1024) { ++result.skippedLarge; continue; }
        const auto relative = entry.path().lexically_relative(root);
        ++result.files;
        result.bytes += size;
        const std::string category = ClassifyProjectFile(relative);
        ++result.categories[category];
        ProjectScanResult::SourceFileSummary fileSummary;
        fileSummary.path = WideToUtf8(relative.wstring());
        fileSummary.category = category;
        fileSummary.language = SourceLanguage(relative);
        fileSummary.bytes = size;
        std::ifstream input(entry.path(), std::ios::binary);
        if (input) {
            try {
                std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
                const std::string& source = fileSummary.path;
                ExtractActionIds(text, source, result);
                ExtractStateSignals(text, source, category, result);
                ExtractAttackRangeCandidates(text, source, category, result);
                ExtractSourceIndex(
                    text, source, category, fileSummary.language,
                    scanTargets, result, fileSummary);
            } catch (const std::exception& exception) {
                ++result.analysisErrors;
                if (result.firstAnalysisError.empty()) {
                    result.firstAnalysisError = WideToUtf8(relative.wstring()) + ": " + exception.what();
                }
            } catch (...) {
                ++result.analysisErrors;
                if (result.firstAnalysisError.empty()) {
                    result.firstAnalysisError = WideToUtf8(relative.wstring()) + ": unknown analysis error";
                }
            }
        }
        result.sourceFiles.push_back(std::move(fileSummary));
    }
    return true;
}

std::vector<std::string> ParseScanTargets(const std::string& text);

std::filesystem::path ProjectScanIndexPath(
    const std::filesystem::path& projectRoot) {
    std::string gameId = WideToUtf8(projectRoot.filename().wstring());
    if (gameId.empty()) gameId = "game";
    return WorkspaceConfigPath().parent_path().parent_path() /
        "profiles" / gameId / "project_scan.json";
}

nlohmann::json EvidenceJson(
    const std::vector<ProjectScanResult::SourceEvidence>& evidence) {
    nlohmann::json values = nlohmann::json::array();
    for (const auto& item : evidence) {
        values.push_back({
            { "kind", item.kind },
            { "symbol", item.symbol },
            { "source", item.source },
            { "line", item.line },
            { "excerpt", item.excerpt },
            { "confidence", item.confidence },
        });
    }
    return values;
}

bool WriteProjectScanIndex(
    const ProjectScanResult& result,
    const std::vector<std::string>& scanTargets,
    std::filesystem::path& outputPath,
    std::string& errorText) {
    outputPath = ProjectScanIndexPath(result.root);
    std::string gameId = WideToUtf8(result.root.filename().wstring());
    if (gameId.empty()) gameId = "game";

    const auto indexedCollection = [](
        const auto& sources,
        const auto& evidence,
        const char* idName) {
        nlohmann::json collection = nlohmann::json::array();
        for (const auto& [id, sourceFiles] : sources) {
            nlohmann::json item = {
                { idName, id },
                { "sources", sourceFiles },
            };
            if (const auto found = evidence.find(id);
                found != evidence.end()) {
                item["evidence"] = EvidenceJson(found->second);
            } else {
                item["evidence"] = nlohmann::json::array();
            }
            collection.push_back(std::move(item));
        }
        return collection;
    };

    nlohmann::json files = nlohmann::json::array();
    for (const auto& file : result.sourceFiles) {
        files.push_back({
            { "path", file.path },
            { "category", file.category },
            { "language", file.language },
            { "bytes", file.bytes },
            { "lines", file.lines },
            { "targetMatches", file.targetMatches },
        });
    }
    nlohmann::json mappings = nlohmann::json::array();
    for (const auto& [key, candidate] : result.signalCandidates) {
        mappings.push_back({
            { "genericProperty", candidate.genericProperty },
            { "sourceSymbol", candidate.symbol },
            { "valueKind", candidate.valueKind },
            { "confidence", candidate.confidence },
            { "sources", candidate.sources },
            { "evidence", EvidenceJson(candidate.evidence) },
        });
    }
    nlohmann::json ranges = nlohmann::json::array();
    for (const auto& range : result.attackRangeCandidates) {
        ranges.push_back({
            { "symbol", range.symbol },
            { "label", range.label },
            { "source", range.source },
            { "line", range.line },
            { "excerpt", range.excerpt },
            { "range", range.range },
            { "confidence", range.confidence },
        });
    }
    nlohmann::json enumValues = nlohmann::json::object();
    for (const auto& [name, values] : result.stateValues) {
        enumValues[name] = values;
    }
    nlohmann::json actionSemantics = nlohmann::json::array();
    std::size_t semanticAutoApproved = 0;
    std::size_t semanticReviewRequired = 0;
    for (const auto& [actionId, ignored] : result.actionSources) {
        const auto semantic = InferActionSemantics(actionId, result);
        const bool autoApproved = semantic.confidence >= 0.90f;
        if (autoApproved) ++semanticAutoApproved;
        else ++semanticReviewRequired;
        actionSemantics.push_back({
            { "actionId", actionId },
            { "category", semantic.category },
            { "tags", semantic.tags },
            { "confidence", semantic.confidence },
            { "evidence", semantic.evidence },
            { "autoApproved", autoApproved },
            { "reviewRequired", !autoApproved },
        });
    }
    nlohmann::json index = {
        { "schemaVersion", 3 },
        { "gameId", gameId },
        { "generatedLocally", true },
        { "sourceFiles", std::move(files) },
        { "scanTargets", scanTargets },
        { "summary", {
            { "files", result.files },
            { "bytes", result.bytes },
            { "analysisErrors", result.analysisErrors },
            { "actions", result.actionSources.size() },
            { "semanticActionsAutoApproved", semanticAutoApproved },
            { "semanticActionsReviewRequired", semanticReviewRequired },
            { "stateMappings", result.signalCandidates.size() },
            { "attackRanges", result.attackRangeCandidates.size() },
            { "symbols", result.declaredSymbolSources.size() },
            { "dependencies", result.dependencySources.size() },
            { "runtimeProperties", result.runtimePropertySources.size() },
            { "scenes", result.sceneSources.size() },
            { "inputs", result.inputSources.size() },
            { "targetMatches", result.targetEvidence.size() },
        } },
        { "actions", indexedCollection(
            result.actionSources, result.actionEvidence, "actionId") },
        { "actionSemantics", std::move(actionSemantics) },
        { "symbols", indexedCollection(
            result.declaredSymbolSources,
            result.declaredSymbolEvidence, "symbolId") },
        { "dependencies", indexedCollection(
            result.dependencySources,
            result.dependencyEvidence, "dependency") },
        { "runtimeProperties", indexedCollection(
            result.runtimePropertySources,
            result.runtimePropertyEvidence, "property") },
        { "scenes", indexedCollection(
            result.sceneSources, result.sceneEvidence, "scene") },
        { "inputs", indexedCollection(
            result.inputSources, result.inputEvidence, "input") },
        { "stateMappings", std::move(mappings) },
        { "attackRanges", std::move(ranges) },
        { "enumValues", std::move(enumValues) },
        { "targetEvidence", EvidenceJson(result.targetEvidence) },
    };
    std::error_code directoryError;
    std::filesystem::create_directories(
        outputPath.parent_path(), directoryError);
    if (directoryError) {
        errorText = "Could not create scan index folder: " +
            directoryError.message();
        return false;
    }
    std::ofstream output(outputPath, std::ios::trunc);
    if (!output) {
        errorText = "Could not open project_scan.json.";
        return false;
    }
    output << index.dump(2) << '\n';
    if (!output) {
        errorText = "Could not finish project_scan.json.";
        return false;
    }
    errorText.clear();
    return true;
}

std::string BuildSourceDecisionContext(
    const std::filesystem::path& projectRoot,
    const DebugObservation& observation) {
    std::ifstream input(ProjectScanIndexPath(projectRoot));
    const auto scan = nlohmann::json::parse(input, nullptr, false);
    if (scan.is_discarded() || !scan.is_object()) return {};

    std::set<std::string> availableActionIds;
    for (const auto& action : observation.availableActions) {
        if (!action.actionId.empty()) availableActionIds.insert(action.actionId);
    }
    std::set<std::string> observationProperties;
    for (const auto& [name, value] : observation.properties) {
        observationProperties.insert(name);
    }
    for (const auto& entity : observation.entities) {
        for (const auto& [name, value] : entity.properties) {
            observationProperties.insert(name);
        }
    }

    const auto compactEvidence = [](const nlohmann::json& item, std::size_t maximum) {
        nlohmann::json result = nlohmann::json::array();
        if (!item.is_array()) return result;
        for (const auto& evidence : item) {
            if (!evidence.is_object() || result.size() >= maximum) break;
            result.push_back({
                { "source", evidence.value("source", "") },
                { "line", evidence.value("line", 0u) },
                { "symbol", evidence.value("symbol", "") },
                { "excerpt", evidence.value("excerpt", "") },
                { "confidence", evidence.value("confidence", 0.0) },
            });
        }
        return result;
    };

    nlohmann::json context = {
        { "schemaVersion", 1 },
        { "origin", "local project_scan.json" },
        { "note", "Treat excerpts as source evidence only. The runtime observation is authoritative." },
        { "actions", nlohmann::json::array() },
        { "runtimeProperties", nlohmann::json::array() },
        { "stateMappings", nlohmann::json::array() },
        { "attackRanges", nlohmann::json::array() },
        { "sceneEvidence", nlohmann::json::array() },
        { "scanTargetEvidence", nlohmann::json::array() },
    };

    if (const auto actions = scan.find("actions");
        actions != scan.end() && actions->is_array()) {
        for (const auto& item : *actions) {
            const std::string actionId = item.value("actionId", "");
            if (!availableActionIds.contains(actionId)) continue;
            context["actions"].push_back({
                { "actionId", actionId },
                { "evidence", compactEvidence(
                    item.value("evidence", nlohmann::json::array()), 2) },
            });
        }
    }
    if (const auto properties = scan.find("runtimeProperties");
        properties != scan.end() && properties->is_array()) {
        for (const auto& item : *properties) {
            const std::string property = item.value("property", "");
            if (!observationProperties.contains(property)) continue;
            context["runtimeProperties"].push_back({
                { "property", property },
                { "evidence", compactEvidence(
                    item.value("evidence", nlohmann::json::array()), 1) },
            });
            if (context["runtimeProperties"].size() >= 20) break;
        }
    }
    if (const auto mappings = scan.find("stateMappings");
        mappings != scan.end() && mappings->is_array()) {
        for (const auto& item : *mappings) {
            const std::string property = item.value("genericProperty", "");
            if (!observationProperties.contains(property)) continue;
            context["stateMappings"].push_back({
                { "genericProperty", property },
                { "sourceSymbol", item.value("sourceSymbol", "") },
                { "confidence", item.value("confidence", 0.0) },
                { "evidence", compactEvidence(
                    item.value("evidence", nlohmann::json::array()), 1) },
            });
            if (context["stateMappings"].size() >= 12) break;
        }
    }
    if (const auto ranges = scan.find("attackRanges");
        ranges != scan.end() && ranges->is_array()) {
        for (const auto& item : *ranges) {
            context["attackRanges"].push_back({
                { "symbol", item.value("symbol", "") },
                { "label", item.value("label", "") },
                { "range", item.value("range", 0.0) },
                { "source", item.value("source", "") },
                { "line", item.value("line", 0u) },
                { "confidence", item.value("confidence", 0.0) },
            });
            if (context["attackRanges"].size() >= 12) break;
        }
    }
    if (const auto scenes = scan.find("scenes");
        scenes != scan.end() && scenes->is_array()) {
        for (const auto& item : *scenes) {
            if (item.value("scene", "") != observation.sceneId) continue;
            context["sceneEvidence"].push_back({
                { "scene", observation.sceneId },
                { "evidence", compactEvidence(
                    item.value("evidence", nlohmann::json::array()), 3) },
            });
        }
    }
    if (const auto targets = scan.find("targetEvidence");
        targets != scan.end() && targets->is_array()) {
        context["scanTargetEvidence"] = compactEvidence(*targets, 12);
    }

    std::string serialized = context.dump();
    constexpr std::size_t kMaximumContextBytes = 24 * 1024;
    if (serialized.size() > kMaximumContextBytes) {
        context["attackRanges"] = nlohmann::json::array();
        context["scanTargetEvidence"] = nlohmann::json::array();
        serialized = context.dump();
    }
    return serialized.size() <= kMaximumContextBytes ? serialized : std::string{};
}

std::string ScanProjectFolder(
    const std::filesystem::path& root,
    const std::string& scanTargetText = {}) {
    const auto scanTargets = ParseScanTargets(scanTargetText);
    ProjectScanResult result;
    if (!AnalyzeProjectFolder(root, result, scanTargets)) {
        return "Project scan failed: folder does not exist.";
    }
    std::filesystem::path indexPath;
    std::string indexError;
    const bool indexWritten =
        WriteProjectScanIndex(result, scanTargets, indexPath, indexError);
    std::size_t semanticAutoApproved = 0;
    std::size_t semanticReviewRequired = 0;
    for (const auto& [actionId, ignored] : result.actionSources) {
        if (InferActionSemantics(actionId, result).confidence >= 0.90f)
            ++semanticAutoApproved;
        else
            ++semanticReviewRequired;
    }
    std::ostringstream output;
    output << "Project scan completed.\r\nFolder: " << WideToUtf8(root.wstring())
        << "\r\nSafe candidate files: " << result.files
        << "\r\nTotal candidate bytes: " << result.bytes
        << "\r\nLarge files skipped: " << result.skippedLarge
        << "\r\nFiles skipped after analysis error: " << result.analysisErrors;
    if (!result.firstAnalysisError.empty()) {
        output << "\r\nFirst analysis error: " << result.firstAnalysisError;
    }
    output << "\r\n\r\nClassification:";
    for (const auto& [category, count] : result.categories) output << "\r\n  " << category << ": " << count;
    output << "\r\n\r\nDiscovered Action IDs: " << result.actionSources.size()
        << "\r\nSemantic Actions auto-approved: " << semanticAutoApproved
        << "\r\nSemantic Actions needing review: " << semanticReviewRequired
        << "\r\nState mapping candidates: " << result.signalCandidates.size()
        << "\r\nAttack range candidates: " << result.attackRangeCandidates.size()
        << "\r\nDeclared code symbols: " << result.declaredSymbolSources.size()
        << "\r\nInclude dependencies: " << result.dependencySources.size()
        << "\r\nRuntime property keys: " << result.runtimePropertySources.size()
        << "\r\nScene IDs / types: " << result.sceneSources.size()
        << "\r\nInput bindings: " << result.inputSources.size()
        << "\r\nScan target evidence: " << result.targetEvidence.size();
    if (indexWritten) {
        output << "\r\n\r\nDetailed scan index: "
            << WideToUtf8(indexPath.wstring());
    } else {
        output << "\r\n\r\nScan index warning: " << indexError;
    }
    output
        << "\r\nNo source files were sent during this scan."
        << "\r\nFuture API decisions may attach a bounded subset of the recorded evidence.";
    return output.str();
}

std::filesystem::path StateProfilePath(const std::filesystem::path& projectRoot) {
    std::string gameId = WideToUtf8(projectRoot.filename().wstring());
    if (gameId.empty()) gameId = "game";
    return WorkspaceConfigPath().parent_path().parent_path() /
        "profiles" / gameId / "state_mapping_profile.json";
}

std::filesystem::path LocalPolicyPath(const std::filesystem::path& projectRoot) {
    std::string gameId = WideToUtf8(projectRoot.filename().wstring());
    if (gameId.empty()) gameId = "game";
    return WorkspaceConfigPath().parent_path().parent_path() /
        "profiles" / gameId / "local_policy.json";
}

std::vector<std::string> ParseScanTargets(const std::string& text) {
    std::vector<std::string> targets;
    std::string current;
    auto append = [&] {
        const auto first = current.find_first_not_of(" \t\r\n");
        const auto last = current.find_last_not_of(" \t\r\n");
        if (first != std::string::npos) targets.push_back(LowerAscii(current.substr(first, last - first + 1)));
        current.clear();
    };
    for (char c : text) {
        if (c == ',' || c == ';' || c == '\r' || c == '\n') append();
        else current.push_back(c);
    }
    append();
    return targets;
}

bool MatchesScanTargets(const ProjectScanResult::SignalCandidate& candidate,
    const std::vector<std::string>& targets) {
    if (targets.empty()) return true;
    std::string searchable = LowerAscii(candidate.genericProperty + " " + candidate.symbol);
    for (const auto& source : candidate.sources) searchable += " " + LowerAscii(source);
    return std::any_of(targets.begin(), targets.end(), [&](const std::string& target) {
        return searchable.find(target) != std::string::npos;
    });
}

std::string GenerateStateMappingProfile(const std::filesystem::path& root, const std::string& scanTargetText) {
    const auto scanTargets = ParseScanTargets(scanTargetText);
    ProjectScanResult result;
    if (!AnalyzeProjectFolder(root, result, scanTargets)) {
        return "State Mapping generation failed: folder does not exist.";
    }
    std::map<std::string, nlohmann::json> existingMappings;
    {
        std::ifstream existingInput(StateProfilePath(root));
        const auto existing =
            nlohmann::json::parse(existingInput, nullptr, false);
        if (!existing.is_discarded() && existing.is_object() &&
            existing.contains("mappings") &&
            existing["mappings"].is_array()) {
            for (const auto& mapping : existing["mappings"]) {
                if (!mapping.is_object()) continue;
                const std::string property =
                    mapping.value("genericProperty", "");
                const std::string symbol =
                    mapping.value("sourceSymbol", "");
                if (!property.empty() && !symbol.empty()) {
                    existingMappings[property + "\n" + symbol] = mapping;
                }
            }
        }
    }
    nlohmann::json mappings = nlohmann::json::array();
    std::size_t autoApprovedMappings = 0;
    std::size_t manuallyApprovedMappings = 0;
    std::size_t manuallyIgnoredMappings = 0;
    std::size_t reviewRequiredMappings = 0;
    for (const auto& [key, candidate] : result.signalCandidates) {
        if (!MatchesScanTargets(candidate, scanTargets)) continue;
        nlohmann::json mapping = existingMappings.contains(key)
            ? existingMappings[key] : nlohmann::json::object();
        mapping["genericProperty"] = candidate.genericProperty;
        mapping["sourceSymbol"] = candidate.symbol;
        mapping["valueKind"] = candidate.valueKind;
        mapping["confidence"] = candidate.confidence;
        mapping["sources"] = candidate.sources;
        mapping["evidence"] = EvidenceJson(candidate.evidence);
        mapping["sourceDiscovered"] = true;
        const bool previouslyApproved = mapping.value("approved", false);
        const std::string previousApprovalSource =
            mapping.value("approvalSource", std::string{});
        const bool manuallyApproved = previouslyApproved &&
            previousApprovalSource != "source_scan_high_confidence" &&
            previousApprovalSource != "pending_review";
        const bool manuallyIgnored = mapping.value("ignored", false) ||
            previousApprovalSource == "manual_ignored";
        const bool highConfidence = candidate.confidence >= 0.98f;
        mapping["approved"] = !manuallyIgnored && (manuallyApproved || highConfidence);
        mapping["autoApproved"] = !manuallyIgnored && !manuallyApproved && highConfidence;
        mapping["ignored"] = manuallyIgnored;
        mapping["approvalSource"] = manuallyIgnored
            ? "manual_ignored"
            : (manuallyApproved
            ? (previousApprovalSource.empty() ? "manual_legacy" : previousApprovalSource)
            : (highConfidence ? "source_scan_high_confidence" : "pending_review"));
        mapping["reviewRequired"] = !manuallyIgnored && !(manuallyApproved || highConfidence);
        if (mapping["reviewRequired"].get<bool>()) ++reviewRequiredMappings;
        else if (manuallyIgnored) ++manuallyIgnoredMappings;
        else if (manuallyApproved) ++manuallyApprovedMappings;
        else ++autoApprovedMappings;
        mapping["runtimeObserved"] =
            mapping.value("runtimeObserved", false);
        mappings.push_back(std::move(mapping));
        existingMappings.erase(key);
    }
    // Never erase a manually approved mapping merely because a later target
    // filter did not rediscover it. Keep it visible and mark it as stale.
    for (auto& [key, mapping] : existingMappings) {
        const std::string approvalSource = mapping.value("approvalSource", std::string{});
        const bool manuallyApproved = mapping.value("approved", false) &&
            approvalSource != "source_scan_high_confidence" &&
            approvalSource != "pending_review";
        const bool manuallyIgnored = mapping.value("ignored", false) ||
            approvalSource == "manual_ignored";
        if (!manuallyApproved && !manuallyIgnored) continue;
        mapping["sourceDiscovered"] = false;
        mapping["reviewRequired"] = false;
        if (manuallyIgnored) ++manuallyIgnoredMappings;
        else ++manuallyApprovedMappings;
        mappings.push_back(std::move(mapping));
    }
    nlohmann::json enumValues = nlohmann::json::object();
    for (const auto& [name, values] : result.stateValues) enumValues[name] = values;
    std::string gameId = WideToUtf8(root.filename().wstring());
    if (gameId.empty()) gameId = "game";
    nlohmann::json profile = {
        { "schemaVersion", 3 }, { "gameId", gameId }, { "generatedLocally", true },
        { "reviewRequired", reviewRequiredMappings > 0 },
        { "semanticSummary", {
            { "autoApproved", autoApprovedMappings },
            { "manuallyApproved", manuallyApprovedMappings },
            { "manuallyIgnored", manuallyIgnoredMappings },
            { "reviewRequired", reviewRequiredMappings },
            { "autoApprovalThreshold", 0.98 },
            { "method", "local_source_scan" },
        } },
        { "mappings", std::move(mappings) },
        { "discoveredEnumValues", std::move(enumValues) },
        { "scanTargets", scanTargets },
    };
    const auto outputPath = StateProfilePath(root);
    std::error_code error;
    std::filesystem::create_directories(outputPath.parent_path(), error);
    std::ofstream output(outputPath, std::ios::trunc);
    if (!output) return "State Mapping generation failed: output file could not be opened.";
    output << profile.dump(2) << '\n';
    return "State Mapping Profile generated locally.\r\nPath: " + WideToUtf8(outputPath.wstring()) +
        "\r\nMapping candidates: " + std::to_string(profile["mappings"].size()) +
        "\r\nHigh-confidence auto-approved: " + std::to_string(autoApprovedMappings) +
        "\r\nManually approved: " + std::to_string(manuallyApprovedMappings) +
        "\r\nManually ignored: " + std::to_string(manuallyIgnoredMappings) +
        "\r\nReview required: " + std::to_string(reviewRequiredMappings) +
        "\r\nFiles skipped after analysis error: " + std::to_string(result.analysisErrors) +
        (result.firstAnalysisError.empty() ? std::string{} :
            "\r\nFirst analysis error: " + result.firstAnalysisError) +
        "\r\nScan targets: " + (scanTargets.empty() ? std::string("automatic (all)") : scanTargetText) +
        "\r\nReview only mappings with reviewRequired=true."
        "\r\nNo source files were sent to an API.";
}

std::string GuessActionCategory(const std::string& actionId) {
    std::string value = actionId;
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto containsAny = [&](std::initializer_list<std::string_view> terms) {
        return std::any_of(terms.begin(), terms.end(), [&](std::string_view term) {
            return value.find(term) != std::string::npos;
        });
    };
    if (containsAny({ "attack", "strike", "melee", "shoot", "fire", "punch", "kick", "spell", "grab", "super" })) return "attack";
    if (containsAny({ "guard", "block", "parry", "shield", "defend" })) return "defense";
    if (containsAny({ "jump", "dodge", "dash", "evade", "roll", "teleport" })) return "mobility";
    if (containsAny({ "move", "walk", "run", "retreat", "approach", "chase", "wander", "strafe" })) return "movement";
    if (containsAny({ "wait", "idle", "noop" })) return "idle";
    if (containsAny({ "skip", "confirm", "start", "pause", "menu", "phase", "setactorstate", "restore" })) return "flow";
    return "generic";
}

std::vector<std::string> GuessActionTags(const std::string& actionId) {
    const std::string category = GuessActionCategory(actionId);
    std::vector<std::string> tags{ category };
    const std::string name = LowerAscii(actionId);
    if (category == "attack") tags.push_back("combat.attack");
    if (name.find("move") != std::string::npos) tags.push_back("movement.approach");
    if (name.find("retreat") != std::string::npos) tags.push_back("movement.retreat");
    if (name.find("dodge") != std::string::npos || name.find("retreat") != std::string::npos ||
        name.find("guard") != std::string::npos) tags.push_back("defense.evade");
    if (name.find("jump") != std::string::npos) tags.push_back("mobility.jump");
    return tags;
}

ActionSemanticInference InferActionSemantics(
    const std::string& actionId,
    const ProjectScanResult& scan) {
    ActionSemanticInference result;
    const std::string name = LowerAscii(actionId);
    std::map<std::string, int> scores;
    const auto addEvidence = [&](std::string value) {
        if (value.empty() || result.evidence.size() >= 12 ||
            std::find(result.evidence.begin(), result.evidence.end(), value) !=
                result.evidence.end()) return;
        result.evidence.push_back(std::move(value));
    };
    const auto scoreTerms = [&](
        const std::string& text,
        const std::string& category,
        std::initializer_list<std::string_view> terms,
        int score,
        const char* origin) {
        for (const std::string_view term : terms) {
            if (text.find(term) == std::string::npos) continue;
            scores[category] += score;
            addEvidence(std::string(origin) + " contains '" +
                std::string(term) + "'");
            break;
        }
    };
    const auto scoreCorpus = [&](const std::string& text, int score, const char* origin) {
        scoreTerms(text, "attack", { "attack", "hitbox", "damage", "strike", "melee", "shoot", "projectile", "punch", "kick", "spell", "grab", "super", "windup" }, score, origin);
        scoreTerms(text, "defense", { "guard", "block", "parry", "shield", "defend", "invincible" }, score, origin);
        scoreTerms(text, "mobility", { "jump", "dodge", "dash", "evade", "roll", "teleport" }, score, origin);
        scoreTerms(text, "movement", { "move", "walk", "run", "retreat", "approach", "chase", "wander", "strafe" }, score, origin);
        scoreTerms(text, "idle", { "wait", "idle", "noop" }, score, origin);
        scoreTerms(text, "flow", { "skip", "confirm", "start", "pause", "menu", "phase", "setactorstate", "restore" }, score, origin);
        scoreTerms(text, "interaction", { "interact", "use", "talk", "pickup", "open" }, score, origin);
    };

    scoreCorpus(name, 8, "actionId");
    if (const auto sources = scan.actionSources.find(actionId);
        sources != scan.actionSources.end()) {
        for (const auto& source : sources->second) {
            scoreCorpus(LowerAscii(source), 2, "source path");
        }
    }
    if (const auto evidence = scan.actionEvidence.find(actionId);
        evidence != scan.actionEvidence.end()) {
        for (const auto& item : evidence->second) {
            scoreCorpus(LowerAscii(item.excerpt), 1, "source excerpt");
        }
    }

    int bestScore = 0;
    int secondScore = 0;
    for (const auto& [category, score] : scores) {
        if (score > bestScore) {
            secondScore = bestScore;
            bestScore = score;
            result.category = category;
        } else if (score > secondScore) {
            secondScore = score;
        }
    }
    if (bestScore == 0) {
        result.category = GuessActionCategory(actionId);
        result.confidence = result.category == "generic" ? 0.25f : 0.72f;
    } else {
        const int gap = bestScore - secondScore;
        result.confidence = std::clamp(
            0.55f + static_cast<float>(bestScore) * 0.04f +
                static_cast<float>((std::min)(gap, 4)) * 0.01f,
            0.25f, 0.99f);
    }

    result.tags.push_back(result.category);
    const auto addTag = [&](const std::string& tag) {
        if (std::find(result.tags.begin(), result.tags.end(), tag) == result.tags.end())
            result.tags.push_back(tag);
    };
    if (result.category == "attack") addTag("combat.attack");
    if (name.find("melee") != std::string::npos ||
        name.find("punch") != std::string::npos ||
        name.find("kick") != std::string::npos) addTag("combat.melee");
    if (name.find("shoot") != std::string::npos ||
        name.find("fire") != std::string::npos ||
        name.find("projectile") != std::string::npos) addTag("combat.ranged");
    if (result.category == "movement") {
        if (name.find("retreat") != std::string::npos ||
            name.find("back") != std::string::npos ||
            name.find("away") != std::string::npos) addTag("movement.retreat");
        else addTag("movement.approach");
    }
    if (name.find("jump") != std::string::npos) addTag("mobility.jump");
    if (name.find("dodge") != std::string::npos ||
        name.find("evade") != std::string::npos ||
        name.find("roll") != std::string::npos ||
        name.find("retreat") != std::string::npos) addTag("defense.evade");
    if (name.find("guard") != std::string::npos ||
        name.find("block") != std::string::npos ||
        name.find("parry") != std::string::npos) addTag("defense.guard");
    return result;
}

nlohmann::json EstimateActionRangeProperties(const std::string& actionId,
    const ProjectScanResult& scan) {
    if (GuessActionCategory(actionId) != "attack") return nlohmann::json::object();
    const std::string action = LowerAscii(actionId);
    const auto relevance = [&](const ProjectScanResult::AttackRangeCandidate& candidate) {
        const std::string evidence = LowerAscii(candidate.source + " " + candidate.symbol + " " + candidate.label);
        int score = 0;
        const auto matches = [&](const char* token) {
            return action.find(token) != std::string::npos && evidence.find(token) != std::string::npos;
        };
        for (const char* token : { "neutral", "side", "up", "down", "smash" })
            if (matches(token)) score += 5;
        if (action.find("weak") != std::string::npos &&
            (evidence.find("ground neutral") != std::string::npos ||
             evidence.find("air neutral") != std::string::npos)) score += 7;
        if (action.find("tilt") != std::string::npos && evidence.find("ground side") != std::string::npos)
            score += 7;
        if (action.find("special") != std::string::npos && evidence.find("playerattacki") != std::string::npos)
            score += 2;
        return score;
    };

    int bestScore = 0;
    double estimatedRange = 3.0; // Safe fallback for an unclassified close-range attack.
    float confidence = 0.25f;
    nlohmann::json evidence = nlohmann::json::array();
    for (const auto& candidate : scan.attackRangeCandidates) {
        const int score = relevance(candidate);
        if (score <= 0) continue;
        if (score > bestScore) {
            bestScore = score;
            estimatedRange = candidate.range;
            confidence = candidate.confidence;
            evidence = nlohmann::json::array();
        } else if (score == bestScore) {
            estimatedRange = (std::max)(estimatedRange, candidate.range);
            confidence = (std::max)(confidence, candidate.confidence);
        } else {
            continue;
        }
        if (evidence.size() < 5) {
            evidence.push_back({
                { "source", candidate.source }, { "symbol", candidate.symbol },
                { "label", candidate.label }, { "range", candidate.range },
                { "confidence", candidate.confidence },
            });
        }
    }
    // Source hitboxes normally describe the attack volume, while runtime
    // distance is center-to-center. Add a small generic target-body allowance.
    if (bestScore > 0) estimatedRange += 1.0;
    estimatedRange = std::clamp(estimatedRange, 0.5, 25.0);
    return {
        { "estimatedRangeMax", estimatedRange },
        { "rangeConfidence", confidence },
        { "rangeSource", bestScore > 0 ? "source_scan" : "conservative_default" },
        { "requiresFacing", true },
        { "rangeEvidence", std::move(evidence) },
    };
}

std::filesystem::path ActionProfilePath(const std::filesystem::path& projectRoot) {
    std::string gameId = WideToUtf8(projectRoot.filename().wstring());
    if (gameId.empty()) gameId = "game";
    return WorkspaceConfigPath().parent_path().parent_path() /
        "profiles" / gameId / "action_profile.json";
}

std::filesystem::path ConfiguredProjectRoot() {
    std::ifstream input(WorkspaceConfigPath());
    const auto config = nlohmann::json::parse(input, nullptr, false);
    if (config.is_discarded() || !config.is_object()) return {};
    return std::filesystem::path(Utf8ToWide(config.value("projectFolder", "")));
}

std::filesystem::path ResolveReplayTrackPath(
    const std::filesystem::path& manifestPath,
    const std::string& storedPath) {
    if (storedPath.empty()) return {};
    const std::filesystem::path path = Utf8ToWide(storedPath);
    return (path.is_absolute() ? path : manifestPath.parent_path() / path)
        .lexically_normal();
}

std::int64_t RecordedReplayCheckpointCount(
    const std::string& manifestPathText) {
    if (manifestPathText.empty()) return -1;
    std::filesystem::path manifestPath(Utf8ToWide(manifestPathText));
    if (!manifestPath.is_absolute()) {
        const auto projectRoot = ConfiguredProjectRoot();
        if (!projectRoot.empty()) manifestPath = projectRoot / manifestPath;
    }
    std::ifstream manifestInput(manifestPath);
    const auto manifest = nlohmann::json::parse(
        manifestInput, nullptr, false);
    if (manifest.is_discarded() || !manifest.is_object() ||
        !manifest.contains("tracks") || !manifest["tracks"].is_object()) {
        return -1;
    }
    const std::string storedSummaryPath =
        manifest["tracks"].value("eventSummary", "");
    if (storedSummaryPath.empty()) return -1;
    std::ifstream summaryInput(
        ResolveReplayTrackPath(manifestPath, storedSummaryPath));
    const auto summary = nlohmann::json::parse(
        summaryInput, nullptr, false);
    if (summary.is_discarded() || !summary.is_object()) return -1;
    return summary.value("checkpointCount", std::int64_t{-1});
}

std::wstring FormatReplaySessionTime(const std::string& sessionId) {
    if (sessionId.size() < 15 || sessionId[8] != '_') {
        return Utf8ToWide(sessionId);
    }
    const auto digits = [](std::string_view value) {
        return std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return c >= '0' && c <= '9';
        });
    };
    if (!digits(std::string_view(sessionId).substr(0, 8)) ||
        !digits(std::string_view(sessionId).substr(9, 6))) {
        return Utf8ToWide(sessionId);
    }
    return Utf8ToWide(
        sessionId.substr(0, 4) + "-" + sessionId.substr(4, 2) + "-" +
        sessionId.substr(6, 2) + " " + sessionId.substr(9, 2) + ":" +
        sessionId.substr(11, 2) + ":" + sessionId.substr(13, 2));
}

std::uint64_t ReadInputReplayFrameCount(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return 0;
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t commandSize = 0;
    std::uint32_t reserved = 0;
    std::uint64_t declaredFrames = 0;
    input.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    input.read(reinterpret_cast<char*>(&version), sizeof(version));
    input.read(reinterpret_cast<char*>(&commandSize), sizeof(commandSize));
    input.read(reinterpret_cast<char*>(&reserved), sizeof(reserved));
    input.read(reinterpret_cast<char*>(&declaredFrames), sizeof(declaredFrames));
    if (!input || magic != 0x52494144 || version != 1 ||
        commandSize == 0) {
        return 0;
    }
    constexpr std::uint64_t kHeaderSize =
        sizeof(magic) + sizeof(version) + sizeof(commandSize) +
        sizeof(reserved) + sizeof(declaredFrames);
    input.seekg(0, std::ios::end);
    const std::streamoff fileSize = input.tellg();
    if (fileSize < static_cast<std::streamoff>(kHeaderSize)) return 0;
    const std::uint64_t availableFrames =
        (static_cast<std::uint64_t>(fileSize) - kHeaderSize) / commandSize;
    return declaredFrames == 0
        ? availableFrames
        : (std::min)(declaredFrames, availableFrames);
}

std::vector<ReplaySessionListEntry> LoadReplaySessionList(
    const std::filesystem::path& projectRoot) {
    std::vector<ReplaySessionListEntry> sessions;
    if (projectRoot.empty()) return sessions;
    const auto directory = projectRoot / "generated/debug_ai/player/sessions";
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) return sessions;

    std::filesystem::recursive_directory_iterator iterator(
        directory, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        const auto entry = *iterator;
        iterator.increment(error);
        if (error) {
            error.clear();
            continue;
        }
        std::error_code entryError;
        if (!entry.is_regular_file(entryError) ||
            entry.path().filename() != L"manifest.json") {
            continue;
        }
        std::ifstream input(entry.path());
        const auto manifest = nlohmann::json::parse(input, nullptr, false);
        if (manifest.is_discarded() || !manifest.is_object() ||
            manifest.value("schemaVersion", 0) != 1 ||
            manifest.value("status", "") != "complete") {
            continue;
        }
        const auto tracks = manifest.find("tracks");
        if (tracks == manifest.end() || !tracks->is_object()) continue;
        const auto trackPath = [&](const char* key) {
            return ResolveReplayTrackPath(
                entry.path(), tracks->value(key, ""));
        };
        const auto hasTrack = [](const std::filesystem::path& path) {
            std::error_code trackError;
            return !path.empty() && std::filesystem::is_regular_file(path, trackError);
        };
        const auto inputPath = trackPath("playerInput");
        const auto actorPath = trackPath("actorActions");
        const bool hasInput = hasTrack(inputPath);
        const bool hasActors = hasTrack(actorPath);
        if (!hasInput && !hasActors) continue;

        const std::string sessionId = manifest.value(
            "sessionId", entry.path().parent_path().filename().string());
        const std::string scene = manifest.value("sceneId", "");
        const std::string phase = manifest.value("phase", "");
        std::uint64_t frameCount =
            manifest.value("inputFrameCount", std::uint64_t{ 0 });
        if (frameCount == 0 && hasInput) {
            frameCount = ReadInputReplayFrameCount(inputPath);
        }
        std::wostringstream label;
        label << FormatReplaySessionTime(sessionId)
            << L" | " << Utf8ToWide(scene.empty() ? "-" : scene);
        if (!phase.empty()) label << L" / " << Utf8ToWide(phase);
        label << L" | " << frameCount << L" frames | ";
        if (hasInput && hasActors) label << L"Input + Actor";
        else if (hasInput) label << L"Input";
        else label << L"Actor";
        std::string coverageStoredPath = manifest.value("coveragePath", "");
        if (coverageStoredPath.empty()) {
            coverageStoredPath = tracks->value("coverage", "");
        }
        if (hasTrack(ResolveReplayTrackPath(entry.path(), coverageStoredPath))) {
            label << L" + Coverage";
        }

        auto manifestPath = std::filesystem::absolute(entry.path(), entryError);
        if (entryError) {
            entryError.clear();
            manifestPath = entry.path();
        }
        const auto modified = entry.last_write_time(entryError);
        sessions.push_back({
            std::move(manifestPath),
            label.str(),
            entryError ? std::filesystem::file_time_type{} : modified,
        });
    }
    std::stable_sort(sessions.begin(), sessions.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.modified > rhs.modified;
    });
    constexpr std::size_t kMaximumDisplayedSessions = 200;
    if (sessions.size() > kMaximumDisplayedSessions) {
        sessions.resize(kMaximumDisplayedSessions);
    }
    return sessions;
}

void RefreshReplaySessionList(bool showResult) {
    if (!gReplaySessions) return;
    std::filesystem::path previousSelection;
    const LRESULT previousIndex = SendMessageW(gReplaySessions, CB_GETCURSEL, 0, 0);
    if (previousIndex != CB_ERR &&
        static_cast<std::size_t>(previousIndex) < gReplaySessionEntries.size()) {
        previousSelection =
            gReplaySessionEntries[static_cast<std::size_t>(previousIndex)].manifestPath;
    }

    const auto projectRoot = ConfiguredProjectRoot();
    gReplaySessionEntries = LoadReplaySessionList(projectRoot);
    SendMessageW(gReplaySessions, CB_RESETCONTENT, 0, 0);
    std::size_t selection = 0;
    if (gReplaySessionEntries.empty()) {
        const wchar_t* message = projectRoot.empty()
            ? L"(Select a Game Project Folder)"
            : L"(No completed replay sessions)";
        SendMessageW(gReplaySessions, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(message));
    } else {
        for (std::size_t i = 0; i < gReplaySessionEntries.size(); ++i) {
            const auto& session = gReplaySessionEntries[i];
            SendMessageW(gReplaySessions, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(session.label.c_str()));
            if (!previousSelection.empty() &&
                session.manifestPath == previousSelection) {
                selection = i;
            }
        }
    }
    SendMessageW(gReplaySessions, CB_SETCURSEL,
        static_cast<WPARAM>(selection), 0);
    if (gPlaySelectedReplay) {
        EnableWindow(gPlaySelectedReplay, !gReplaySessionEntries.empty());
    }
    if (showResult && gStatusText) {
        std::wostringstream result;
        result << L"Replay list refreshed.\r\nCompleted sessions: "
            << gReplaySessionEntries.size() << L"\r\nFolder: "
            << (projectRoot / "generated/debug_ai/player/sessions").wstring();
        SetWindowTextW(gStatusText, result.str().c_str());
    }
}

std::string GenerateActionProfile(const std::filesystem::path& root) {
    ProjectScanResult result;
    if (!AnalyzeProjectFolder(root, result)) return "Action Profile generation failed: folder does not exist.";
    std::map<std::string, nlohmann::json> existingActions;
    {
        std::ifstream existingInput(ActionProfilePath(root));
        const auto existingProfile = nlohmann::json::parse(existingInput, nullptr, false);
        if (!existingProfile.is_discarded() && existingProfile.is_object() &&
            existingProfile.contains("actions") && existingProfile["actions"].is_array()) {
            for (const auto& entry : existingProfile["actions"]) {
                if (entry.is_object() && !entry.value("actionId", "").empty())
                    existingActions[entry.value("actionId", "")] = entry;
            }
        }
    }
    nlohmann::json actions = nlohmann::json::array();
    std::size_t semanticAutoApproved = 0;
    std::size_t semanticManuallyApproved = 0;
    std::size_t semanticManuallyIgnored = 0;
    std::size_t semanticReviewRequired = 0;
    for (const auto& [actionId, sources] : result.actionSources) {
        nlohmann::json entry = existingActions.contains(actionId)
            ? existingActions[actionId] : nlohmann::json::object();
        const ActionSemanticInference semantic =
            InferActionSemantics(actionId, result);
        const bool manuallyApproved =
            entry.value("semanticApprovalSource", std::string{}) == "manual" &&
            entry.value("semanticApproved", false);
        const bool manuallyIgnored = entry.value("semanticIgnored", false) ||
            entry.value("semanticApprovalSource", std::string{}) == "manual_ignored";
        entry["actionId"] = actionId;
        if (!manuallyApproved && !manuallyIgnored) {
            entry["category"] = semantic.category;
            entry["tags"] = semantic.tags;
        }
        const bool autoApproved = !manuallyApproved && !manuallyIgnored && semantic.confidence >= 0.90f;
        entry["semanticApproved"] = !manuallyIgnored && (manuallyApproved || autoApproved);
        entry["semanticIgnored"] = manuallyIgnored;
        entry["semanticApprovalSource"] = manuallyIgnored
            ? "manual_ignored"
            : (manuallyApproved ? "manual" : (autoApproved ? "source_scan_high_confidence" : "pending_review"));
        entry["semanticReviewRequired"] = !manuallyIgnored && !(manuallyApproved || autoApproved);
        entry["semanticInference"] = {
            { "category", semantic.category },
            { "tags", semantic.tags },
            { "confidence", semantic.confidence },
            { "evidence", semantic.evidence },
            { "method", "local_source_scan" },
        };
        if (entry["semanticReviewRequired"].get<bool>()) ++semanticReviewRequired;
        else if (manuallyIgnored) ++semanticManuallyIgnored;
        else if (manuallyApproved) ++semanticManuallyApproved;
        else ++semanticAutoApproved;
        entry["enabled"] = entry.value("enabled", true);
        entry["sourceDiscovered"] = true;
        entry["runtimeObserved"] = entry.value("runtimeObserved", false);
        entry["availableNow"] = entry.value("availableNow", false);
        entry["verified"] = entry.value("verified", false);
        entry["enabledForLocalAI"] = entry.value("enabledForLocalAI", false);
        entry["sources"] = sources;
        entry["evidence"] = result.actionEvidence.contains(actionId)
            ? EvidenceJson(result.actionEvidence.at(actionId))
            : nlohmann::json::array();
        nlohmann::json properties = entry.value("properties", nlohmann::json::object());
        const auto estimated = EstimateActionRangeProperties(actionId, result);
        for (auto property = estimated.begin(); property != estimated.end(); ++property)
            properties[property.key()] = property.value();
        entry["properties"] = std::move(properties);
        actions.push_back(std::move(entry));
        existingActions.erase(actionId);
    }
    for (auto& [actionId, entry] : existingActions) {
        const ActionSemanticInference semantic =
            InferActionSemantics(actionId, result);
        const bool manuallyApproved =
            entry.value("semanticApprovalSource", std::string{}) == "manual" &&
            entry.value("semanticApproved", false);
        const bool manuallyIgnored = entry.value("semanticIgnored", false) ||
            entry.value("semanticApprovalSource", std::string{}) == "manual_ignored";
        if (!manuallyApproved && !manuallyIgnored) {
            entry["category"] = semantic.category;
            entry["tags"] = semantic.tags;
        }
        const bool autoApproved = !manuallyApproved && !manuallyIgnored && semantic.confidence >= 0.90f;
        entry["semanticApproved"] = !manuallyIgnored && (manuallyApproved || autoApproved);
        entry["semanticIgnored"] = manuallyIgnored;
        entry["semanticApprovalSource"] = manuallyIgnored
            ? "manual_ignored"
            : (manuallyApproved ? "manual" : (autoApproved ? "source_scan_high_confidence" : "pending_review"));
        entry["semanticReviewRequired"] = !manuallyIgnored && !(manuallyApproved || autoApproved);
        entry["semanticInference"] = {
            { "category", semantic.category },
            { "tags", semantic.tags },
            { "confidence", semantic.confidence },
            { "evidence", semantic.evidence },
            { "method", "local_source_scan" },
        };
        if (entry["semanticReviewRequired"].get<bool>()) ++semanticReviewRequired;
        else if (manuallyIgnored) ++semanticManuallyIgnored;
        else if (manuallyApproved) ++semanticManuallyApproved;
        else ++semanticAutoApproved;
        actions.push_back(std::move(entry));
    }
    std::string gameId = WideToUtf8(root.filename().wstring());
    if (gameId.empty()) gameId = "game";
    const auto outputPath = ActionProfilePath(root);
    std::error_code error;
    std::filesystem::create_directories(outputPath.parent_path(), error);
    nlohmann::json profile = {
        { "schemaVersion", 2 },
        { "gameId", gameId },
        { "generatedLocally", true },
        { "sourceFileCount", result.files },
        { "semanticSummary", {
            { "autoApproved", semanticAutoApproved },
            { "manuallyApproved", semanticManuallyApproved },
            { "manuallyIgnored", semanticManuallyIgnored },
            { "reviewRequired", semanticReviewRequired },
            { "autoApprovalThreshold", 0.90 },
            { "method", "local_source_scan" },
        } },
        { "actions", std::move(actions) },
    };
    std::ofstream output(outputPath);
    if (!output) return "Action Profile generation failed: output file could not be opened.";
    output << profile.dump(2) << '\n';
    return "Action Profile generated locally.\r\nPath: " + WideToUtf8(outputPath.wstring()) +
        "\r\nActions discovered: " + std::to_string(result.actionSources.size()) +
        "\r\nSemantic auto-approved: " + std::to_string(semanticAutoApproved) +
        "\r\nSemantic manually approved: " + std::to_string(semanticManuallyApproved) +
        "\r\nSemantic manually ignored: " + std::to_string(semanticManuallyIgnored) +
        "\r\nSemantic review required: " + std::to_string(semanticReviewRequired) +
        "\r\nAttack range candidates: " + std::to_string(result.attackRangeCandidates.size()) +
        "\r\nReview only entries with semanticReviewRequired=true."
        "\r\nNo source files were sent to an API.";
}

std::filesystem::path ScenarioDirectory(const std::filesystem::path& projectRoot) {
    std::string gameId = WideToUtf8(projectRoot.filename().wstring());
    if (gameId.empty()) gameId = "game";
    return WorkspaceConfigPath().parent_path().parent_path() /
        "scenarios" / Utf8ToWide(gameId);
}

void RefreshScenarioList(bool showResult) {
    if (!gScenarioList) return;
    std::filesystem::path selectedPath;
    const LRESULT previous = SendMessageW(gScenarioList, CB_GETCURSEL, 0, 0);
    if (previous != CB_ERR && static_cast<std::size_t>(previous) < gScenarioEntries.size())
        selectedPath = gScenarioEntries[static_cast<std::size_t>(previous)].path;
    gScenarioEntries.clear();
    SendMessageW(gScenarioList, CB_RESETCONTENT, 0, 0);
    const auto projectRoot = ConfiguredProjectRoot();
    const auto directory = ScenarioDirectory(projectRoot);
    std::error_code error;
    if (!projectRoot.empty() && std::filesystem::is_directory(directory, error)) {
        for (std::filesystem::directory_iterator iterator(directory,
            std::filesystem::directory_options::skip_permission_denied, error), end;
            iterator != end; iterator.increment(error)) {
            if (error) { error.clear(); continue; }
            if (!iterator->is_regular_file(error) || iterator->path().extension() != L".json") continue;
            std::ifstream input(iterator->path());
            const auto scenario = nlohmann::json::parse(input, nullptr, false);
            if (scenario.is_discarded() || !scenario.is_object()) continue;
            const std::string name = scenario.value("name", WideToUtf8(iterator->path().stem().wstring()));
            const std::string actor = scenario.value("actor", "Player");
            ScenarioListEntry entry;
            entry.path = iterator->path();
            entry.label = Utf8ToWide(name + " | " + actor);
            gScenarioEntries.push_back(std::move(entry));
        }
    }
    std::sort(gScenarioEntries.begin(), gScenarioEntries.end(),
        [](const ScenarioListEntry& left, const ScenarioListEntry& right) {
            return left.label < right.label;
        });
    LRESULT selected = CB_ERR;
    for (std::size_t index = 0; index < gScenarioEntries.size(); ++index) {
        SendMessageW(gScenarioList, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(gScenarioEntries[index].label.c_str()));
        if (!selectedPath.empty() && gScenarioEntries[index].path == selectedPath)
            selected = static_cast<LRESULT>(index);
    }
    if (selected == CB_ERR && !gScenarioEntries.empty()) selected = 0;
    if (selected != CB_ERR) SendMessageW(gScenarioList, CB_SETCURSEL, selected, 0);
    if (showResult && gStatusText) {
        std::ostringstream result;
        result << "Scenario list reloaded.\r\nFolder: " << WideToUtf8(directory.wstring())
            << "\r\nScenarios: " << gScenarioEntries.size();
        SetWindowTextW(gStatusText, Utf8ToWide(result.str()).c_str());
    }
}

bool UpdateActionProfileFromRuntime(const DebugObservation& observation) {
    const auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) return false;
    const auto path = ActionProfilePath(projectRoot);
    std::ifstream input(path);
    auto profile = nlohmann::json::parse(input, nullptr, false);
    if (profile.is_discarded() || !profile.is_object()) return false;
    if (!profile.contains("actions") || !profile["actions"].is_array()) profile["actions"] = nlohmann::json::array();

    std::set<std::string> available;
    for (const auto& action : observation.availableActions) available.insert(action.actionId);
    std::set<std::string> matched;
    for (auto& action : profile["actions"]) {
        if (!action.is_object()) continue;
        const std::string actionId = action.value("actionId", "");
        const bool isAvailable = available.contains(actionId);
        const bool wasObserved = action.value("runtimeObserved", false);
        action["sourceDiscovered"] = action.value("sourceDiscovered", true);
        action["availableNow"] = isAvailable;
        action["runtimeObserved"] = wasObserved || isAvailable;
        action["verified"] = action.value("verified", false) || isAvailable;
        action["enabledForLocalAI"] = (wasObserved || isAvailable) && action.value("enabled", true);
        if (!action.contains("tags") || !action["tags"].is_array()) action["tags"] = GuessActionTags(actionId);
        if (isAvailable) matched.insert(actionId);
    }
    for (const auto& actionId : available) {
        if (matched.contains(actionId)) continue;
        profile["actions"].push_back({
            { "actionId", actionId }, { "category", GuessActionCategory(actionId) },
            { "tags", GuessActionTags(actionId) },
            { "enabled", true }, { "sourceDiscovered", false },
            { "runtimeObserved", true }, { "availableNow", true },
            { "verified", true }, { "enabledForLocalAI", true },
            { "sources", nlohmann::json::array() }, { "properties", nlohmann::json::object() },
        });
    }
    profile.erase("projectFolder");
    profile["lastRuntimeScene"] = observation.sceneId;
    profile["lastRuntimeFrame"] = observation.frameNumber;
    std::ofstream output(path, std::ios::trunc);
    if (!output) return false;
    output << profile.dump(2) << '\n';
    return true;
}

bool ActionCanHitTarget(const DebugGenericAction& action) {
    if (GuessActionCategory(action.actionId) != "attack") return true;
    const auto found = action.parameters.find(DebugActionParameter::CanHitTarget);
    if (found == action.parameters.end()) return false;
    const auto* value = std::get_if<bool>(&found->second);
    return value && *value;
}

void EnrichObservationWithActionHitEstimates(DebugObservation& observation) {
    const auto projectRoot = ConfiguredProjectRoot();
    nlohmann::json profile;
    if (!projectRoot.empty()) {
        std::ifstream input(ActionProfilePath(projectRoot));
        profile = nlohmann::json::parse(input, nullptr, false);
    }
    std::map<std::string, nlohmann::json> propertiesByAction;
    if (!profile.is_discarded() && profile.is_object() &&
        profile.contains("actions") && profile["actions"].is_array()) {
        for (const auto& entry : profile["actions"]) {
            if (!entry.is_object() || !entry.contains("properties") || !entry["properties"].is_object()) continue;
            const std::string actionId = entry.value("actionId", "");
            if (!actionId.empty()) propertiesByAction[actionId] = entry["properties"];
        }
    }

    double targetDistance = -1.0;
    if (const auto found = observation.properties.find("enemy.distanceToPlayer");
        found != observation.properties.end()) {
        if (const auto* value = std::get_if<double>(&found->second)) targetDistance = *value;
        else if (const auto* value = std::get_if<std::int64_t>(&found->second)) targetDistance = static_cast<double>(*value);
    }
    DebugVec3 playerPosition{};
    DebugVec3 playerForward{};
    bool hasPlayerPosition = false;
    bool hasPlayerForward = false;
    if (const auto found = observation.properties.find("player.position"); found != observation.properties.end()) {
        if (const auto* value = std::get_if<DebugVec3>(&found->second)) {
            playerPosition = *value;
            hasPlayerPosition = true;
        }
    }
    if (const auto found = observation.properties.find("player.forward"); found != observation.properties.end()) {
        if (const auto* value = std::get_if<DebugVec3>(&found->second)) {
            playerForward = *value;
            hasPlayerForward = true;
        }
    }
    std::string nearestId;
    if (const auto found = observation.properties.find("enemy.nearestId"); found != observation.properties.end()) {
        if (const auto* value = std::get_if<std::string>(&found->second)) nearestId = *value;
    }
    const DebugEntity* target = nullptr;
    for (const auto& entity : observation.entities) {
        if ((!nearestId.empty() && entity.id == nearestId) ||
            (nearestId.empty() && entity.category == "Enemy")) {
            target = &entity;
            break;
        }
    }
    bool facingTarget = true;
    bool facingKnown = false;
    if (target && hasPlayerPosition && hasPlayerForward) {
        const double dx = target->position.x - playerPosition.x;
        const double dz = target->position.z - playerPosition.z;
        const double targetLength = std::sqrt(dx * dx + dz * dz);
        const double forwardLength = std::sqrt(playerForward.x * playerForward.x + playerForward.z * playerForward.z);
        if (targetLength > 0.0001 && forwardLength > 0.0001) {
            facingTarget = (dx * playerForward.x + dz * playerForward.z) /
                (targetLength * forwardLength) > 0.0;
            facingKnown = true;
        }
    }

    for (auto& action : observation.availableActions) {
        if (GuessActionCategory(action.actionId) != "attack") continue;
        const auto properties = propertiesByAction.find(action.actionId);
        const nlohmann::json* values = properties == propertiesByAction.end() ? nullptr : &properties->second;
        double estimatedRange = values ? values->value("estimatedRangeMax", 3.0) : 3.0;
        if (values) estimatedRange = (std::max)(estimatedRange, values->value("verifiedRangeMax", 0.0));
        estimatedRange = std::clamp(estimatedRange, 0.5, 100.0);
        const double confidence = values ? values->value("rangeConfidence", 0.25) : 0.25;
        const bool requiresFacing = values ? values->value("requiresFacing", true) : true;
        const bool inRange = targetDistance >= 0.0 && targetDistance <= estimatedRange;
        const bool canHit = inRange && (!requiresFacing || !facingKnown || facingTarget);
        action.parameters[DebugActionParameter::CanHitTarget] = canHit;
        action.parameters[DebugActionParameter::EstimatedRange] = estimatedRange;
        action.parameters[DebugActionParameter::TargetDistance] = targetDistance;
        action.parameters[DebugActionParameter::RangeConfidence] = confidence;
        action.parameters[DebugActionParameter::RequiresFacing] = requiresFacing;
        action.parameters["facingTarget"] = facingTarget;
        action.parameters["facingKnown"] = facingKnown;
    }
}

const DebugGenericAction* FindAvailableActionByProfileTag(
    const DebugObservation& observation, const std::string& requiredTag) {
    const auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) return nullptr;
    std::ifstream input(ActionProfilePath(projectRoot));
    const auto profile = nlohmann::json::parse(input, nullptr, false);
    if (profile.is_discarded() || !profile.is_object() ||
        !profile.contains("actions") || !profile["actions"].is_array()) return nullptr;
    for (const auto& entry : profile["actions"]) {
        if (!entry.is_object() || !entry.contains("tags") || !entry["tags"].is_array()) continue;
        bool hasTag = false;
        for (const auto& tag : entry["tags"]) {
            if (tag.is_string() && tag.get<std::string>() == requiredTag) {
                hasTag = true;
                break;
            }
        }
        if (!hasTag) continue;
        const std::string actionId = entry.value("actionId", "");
        const auto found = std::find_if(observation.availableActions.begin(), observation.availableActions.end(),
            [&](const DebugGenericAction& action) { return action.actionId == actionId; });
        if (found != observation.availableActions.end()) return &*found;
    }
    return nullptr;
}

bool UpdateStateProfileFromRuntime(const DebugObservation& observation) {
    const auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) return false;
    const auto path = StateProfilePath(projectRoot);
    std::ifstream input(path);
    auto profile = nlohmann::json::parse(input, nullptr, false);
    if (profile.is_discarded() || !profile.is_object() ||
        !profile.contains("mappings") || !profile["mappings"].is_array()) return false;
    const auto hasProperty = [&](const std::string& name) {
        if (observation.properties.contains(name)) return true;
        return std::any_of(observation.entities.begin(), observation.entities.end(),
            [&](const DebugEntity& entity) { return entity.properties.contains(name); });
    };
    for (auto& mapping : profile["mappings"]) {
        if (!mapping.is_object()) continue;
        const std::string property = mapping.value("genericProperty", "");
        if (!property.empty() && hasProperty(property)) mapping["runtimeObserved"] = true;
    }
    profile["lastRuntimeScene"] = observation.sceneId;
    profile["lastRuntimeFrame"] = observation.frameNumber;
    std::ofstream output(path, std::ios::trunc);
    if (!output) return false;
    output << profile.dump(2) << '\n';
    return true;
}

std::string DebugValueText(const DebugValue& value) {
    return std::visit([](const auto& item) -> std::string {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) return "";
        else if constexpr (std::is_same_v<T, bool>) return item ? "true" : "false";
        else if constexpr (std::is_same_v<T, std::string>) return item;
        else if constexpr (std::is_same_v<T, DebugVec3>) {
            std::ostringstream text;
            text << std::fixed << std::setprecision(2) << "(" << item.x << ", " << item.y << ", " << item.z << ")";
            return text.str();
        } else return std::to_string(item);
    }, value);
}

std::string PropertyText(const DebugPropertyMap& properties, const char* name, const char* fallback = "-") {
    const auto found = properties.find(name);
    return found == properties.end() ? fallback : DebugValueText(found->second);
}

std::filesystem::path SaveLatestProtocolResponse(const std::string& response) {
    auto root = ConfiguredProjectRoot();
    if (root.empty()) root = std::filesystem::current_path();
    const auto path = root / "generated/debug_ai/viewer/latest_response.json";
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::trunc);
    if (output) output << response << '\n';
    return path;
}

std::string FormatProtocolResponse(const std::string& response) {
    DebugProtocolMessage message;
    if (!DebugProtocolJson::TryParse(response, message)) return response;
    const auto rawPath = SaveLatestProtocolResponse(response);
    std::ostringstream output;
    output << "Game connection: connected\r\n"
        << "Result: " << PropertyText(message.properties, "ok", "true") << "\r\n"
        << "Message: " << PropertyText(message.properties, "message") << "\r\n";
    const std::string replayNote = PropertyText(message.properties, "lastError", "");
    if (!replayNote.empty()) output << "Replay note: " << replayNote << "\r\n";
    if (message.properties.contains("recording")) {
        output << "Recording: " << PropertyText(message.properties, "recording")
            << "  Replay: " << PropertyText(message.properties, "replaying") << "\r\n";
        if (PropertyText(message.properties, "replaying", "false") == "true") {
            output << "Playback: "
                << (PropertyText(message.properties, "replayPaused", "false") == "true"
                    ? "paused" : "running")
                << "  Speed: "
                << PropertyText(message.properties, "replaySpeed", "1")
                << "x  Replay frame: "
                << PropertyText(message.properties, "frame", "0")
                << "\r\n";
        }
    }
    const std::string replaySessionId =
        PropertyText(message.properties, "replaySessionId", "");
    const std::string replayManifestPath =
        PropertyText(message.properties, "replayManifestPath", "");
    const std::string replayInitialObservationPath =
        PropertyText(message.properties, "replayInitialObservationPath", "");
    if (PropertyText(message.properties, "replayQueued", "false") == "true") {
        output << "Replay queued: loading scene "
            << PropertyText(message.properties, "replayTargetScene", "-") << "\r\n";
    }
    if (!replaySessionId.empty()) output << "Replay session: " << replaySessionId << "\r\n";
    if (!replayManifestPath.empty()) output << "Replay manifest: " << replayManifestPath << "\r\n";
    const std::string validationStatus =
        PropertyText(message.properties, "replayValidationStatus", "");
    if (!replayManifestPath.empty() &&
        PropertyText(message.properties, "recording", "false") != "true" &&
        !validationStatus.empty()) {
        output << "Replay verification: " << validationStatus;
        if (validationStatus != "unavailable") {
            output << "  checked "
                << PropertyText(message.properties, "replayValidationChecked", "0")
                << "/"
                << PropertyText(message.properties, "replayValidationCheckpoints", "0")
                << "  mismatches "
                << PropertyText(message.properties, "replayValidationMismatches", "0");
        }
        output << "\r\n";
        if (validationStatus == "diverged") {
            output << "  First mismatch: "
                << PropertyText(
                    message.properties,
                    "replayValidationFirstMismatchFrame",
                    "0")
                << "F\r\n";
            std::string detail = PropertyText(
                message.properties, "replayValidationFirstDetail", "");
            if (detail.empty()) {
                detail = PropertyText(
                    message.properties, "replayValidationDetail", "");
            }
            if (!detail.empty()) output << "  Detail: " << detail << "\r\n";
        } else if (validationStatus == "unavailable") {
            std::int64_t recordedCheckpointCount = -1;
            const auto checkpointProperty =
                message.properties.find("eventCheckpointCount");
            if (checkpointProperty != message.properties.end()) {
                if (const auto* value = std::get_if<std::int64_t>(
                    &checkpointProperty->second)) {
                    recordedCheckpointCount = *value;
                }
            }
            const std::int64_t manifestCheckpointCount =
                RecordedReplayCheckpointCount(replayManifestPath);
            if (manifestCheckpointCount >= 0) {
                recordedCheckpointCount = manifestCheckpointCount;
            }
            if (recordedCheckpointCount > 0) {
                output << "  Not verified yet. Play this replay to check its "
                    << recordedCheckpointCount << " recorded checkpoints.\r\n";
            } else {
                output << "  No verification checkpoints were recorded.\r\n";
            }
        } else if (validationStatus == "interrupted") {
            output << "  Replay ended before every verification checkpoint was reached.\r\n";
        }
    }
    if (!replayInitialObservationPath.empty()) {
        output << "Initial observation: " << replayInitialObservationPath << "\r\n";
        if (PropertyText(message.properties, "replayInitialStateRestored", "false") == "true") {
            output << "Initial state: restored before replay\r\n";
        }
        const std::string restoreWarning =
            PropertyText(message.properties, "replayRestoreWarning", "");
        if (!restoreWarning.empty()) {
            output << "Initial state warning: " << restoreWarning << "\r\n";
        }
    }
    const std::string replayPath = PropertyText(message.properties, "path", "");
    if (!replayPath.empty() && replayManifestPath.empty()) {
        output << "Replay file: " << replayPath << "\r\n";
    }
    const std::string playerReplayPath = PropertyText(message.properties, "playerReplayPath", "");
    const std::string actorReplayPath = PropertyText(message.properties, "actorReplayPath", "");
    if (!playerReplayPath.empty()) output << "  Player input: " << playerReplayPath << "\r\n";
    if (!actorReplayPath.empty()) output << "  Actor actions: " << actorReplayPath << "\r\n";
    const std::string eventLogPath = PropertyText(message.properties, "eventLogPath", "");
    const std::string eventSummaryPath = PropertyText(message.properties, "eventSummaryPath", "");
    if (!eventLogPath.empty()) {
        output << "Event timeline (" << PropertyText(message.properties, "eventCount", "0")
            << "): " << eventLogPath << "\r\n";
    }
    if (!eventSummaryPath.empty()) output << "Event summary: " << eventSummaryPath << "\r\n";
    const std::string lastEvent = PropertyText(message.properties, "lastEvent", "");
    if (!lastEvent.empty()) output << "Latest event: " << lastEvent << "\r\n";
    if (message.properties.contains("anomalyRulesLoaded")) {
        output << "Anomaly rules: "
            << (PropertyText(message.properties, "anomalyRulesLoaded", "false") == "true"
                ? "loaded" : "unavailable")
            << " (" << PropertyText(message.properties, "anomalyRuleCount", "0") << ")"
            << "  Detections: " << PropertyText(message.properties, "anomalyCount", "0")
            << "  Errors: " << PropertyText(message.properties, "anomalyErrorCount", "0")
            << "\r\n";
        const std::string anomalyLast =
            PropertyText(message.properties, "anomalyLast", "");
        if (!anomalyLast.empty()) output << "Latest anomaly: " << anomalyLast << "\r\n";
        const std::string anomalyRuleError =
            PropertyText(message.properties, "anomalyRuleError", "");
        if (!anomalyRuleError.empty()) {
            output << "Anomaly rule warning: " << anomalyRuleError << "\r\n";
        }
    }
    if (message.observation) {
        const auto& observation = *message.observation;
        output << "\r\nGame State\r\n"
            << "  Scene: " << observation.sceneId
            << "  Phase: " << PropertyText(observation.properties, "game.phase")
            << "  Frame: " << observation.frameNumber
            << "  FPS: " << PropertyText(observation.properties, "fps") << "\r\n"
            << "  Player HP: " << PropertyText(observation.properties, "player.hp")
            << "  Position: " << PropertyText(observation.properties, "player.position") << "\r\n"
            << "  Player: action=" << PropertyText(observation.properties, "player.action")
            << "  attacking=" << PropertyText(observation.properties, "player.isAttacking")
            << "  canMove=" << PropertyText(observation.properties, "player.canMove")
            << "  canJump=" << PropertyText(observation.properties, "player.canJump")
            << "  canAttack=" << PropertyText(observation.properties, "player.canAttack") << "\r\n"
            << "  Enemy HP: " << PropertyText(observation.properties, "enemy.hp")
            << "  Count: " << PropertyText(observation.properties, "enemy.count")
            << "  Entities: " << observation.entities.size() << "\r\n"
            << "  Enemy: threat=" << PropertyText(observation.properties, "enemy.threat")
            << "  attackActive=" << PropertyText(observation.properties, "enemy.attackActive")
            << "  distance=" << PropertyText(observation.properties, "enemy.distanceToPlayer")
            << "  nearest=" << PropertyText(observation.properties, "enemy.nearestId") << "\r\n"
            << "\r\nAvailable Actions (" << observation.availableActions.size() << "):\r\n";
        const auto printActions = [&](bool player, const char* label) {
            output << "  " << label << ": ";
            bool first = true;
            for (const auto& action : observation.availableActions) {
                if (IsPlayerActorAction(action) != player) continue;
                if (!first) output << ", ";
                output << action.actionId;
                first = false;
            }
            if (first) output << "(none)";
            output << "\r\n";
        };
        printActions(true, "Player");
        printActions(false, "Boss/Other");
    }
    output << "\r\nDetailed JSON: " << WideToUtf8(rawPath.wstring());
    return output.str();
}

bool BrowseForProjectFolder(HWND owner, std::filesystem::path& selected) {
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog)))) return false;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    const HRESULT shown = dialog->Show(owner);
    IShellItem* item = nullptr;
    PWSTR path = nullptr;
    const bool ok = SUCCEEDED(shown) && SUCCEEDED(dialog->GetResult(&item)) &&
        SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path));
    if (ok) selected = path;
    if (path) CoTaskMemFree(path);
    if (item) item->Release();
    dialog->Release();
    return ok;
}

bool SendProtocolMessage(const DebugProtocolMessage& requestMessage, std::string& response) {
    std::lock_guard transportLock(gTransportMutex);
    response.clear();
    if (!WaitNamedPipeA(kPipeName, 250)) {
        gGameProcessId = 0;
        response = "Game connection: disconnected\r\nStart CG2_Setup.exe to connect.";
        return false;
    }

    HANDLE pipe = CreateFileA(
        kPipeName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        gGameProcessId = 0;
        response = "Game connection: disconnected";
        return false;
    }
    ULONG serverProcessId = 0;
    if (GetNamedPipeServerProcessId(pipe, &serverProcessId)) {
        gGameProcessId = serverProcessId;
        WatchGameProcess(serverProcessId);
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
    const std::string request = DebugProtocolJson::Serialize(requestMessage);
    HANDLE writeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    OVERLAPPED writeOverlapped{};
    writeOverlapped.hEvent = writeEvent;
    DWORD written = 0;
    bool writeOk = WriteFile(pipe, request.data(), static_cast<DWORD>(request.size()), &written, &writeOverlapped) != FALSE;
    const bool writePending = !writeOk && GetLastError() == ERROR_IO_PENDING;
    if (writePending) {
        writeOk = WaitForSingleObject(writeEvent, kPipeIoTimeoutMilliseconds) == WAIT_OBJECT_0 &&
            GetOverlappedResult(pipe, &writeOverlapped, &written, FALSE) != FALSE;
    }
    if (!writeOk) {
        if (writePending) {
            CancelIoEx(pipe, &writeOverlapped);
            WaitForSingleObject(writeEvent, INFINITE);
        }
        CloseHandle(writeEvent);
        CloseHandle(pipe);
        response = "Game connection: send timeout or failure.";
        return false;
    }
    CloseHandle(writeEvent);

    std::vector<char> buffer(1024 * 1024);
    HANDLE readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    OVERLAPPED readOverlapped{};
    readOverlapped.hEvent = readEvent;
    DWORD read = 0;
    bool success = ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size() - 1), &read, &readOverlapped) != FALSE;
    const bool readPending = !success && GetLastError() == ERROR_IO_PENDING;
    DWORD readFailure = success ? ERROR_SUCCESS : GetLastError();
    if (readPending) {
        const DWORD waitResult = WaitForSingleObject(readEvent, kPipeIoTimeoutMilliseconds);
        success = waitResult == WAIT_OBJECT_0 &&
            GetOverlappedResult(pipe, &readOverlapped, &read, FALSE) != FALSE;
        readFailure = success ? ERROR_SUCCESS
            : (waitResult == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
    }
    if (!success && readPending) {
        CancelIoEx(pipe, &readOverlapped);
        WaitForSingleObject(readEvent, INFINITE);
    }
    CloseHandle(readEvent);
    CloseHandle(pipe);
    if (!success) {
        response = "Game connection: response failed (Windows error " +
            std::to_string(readFailure) + "). The game may be starting, stopped, or paused in the debugger.";
        return false;
    }
    response.assign(buffer.data(), read);
    gGameConnectionEstablished = true;
    DebugProtocolMessage responseMessage;
    if (DebugProtocolJson::TryParse(response, responseMessage)) {
        gCoverageTracker.Configure(ConfiguredProjectRoot(), responseMessage.gameId);
        const auto countProperty = [&](const char* name) -> std::size_t {
            const auto found = responseMessage.properties.find(name);
            if (found == responseMessage.properties.end()) return 0;
            if (const auto* value = std::get_if<std::int64_t>(&found->second)) {
                return *value > 0 ? static_cast<std::size_t>(*value) : 0;
            }
            if (const auto* value = std::get_if<double>(&found->second)) {
                return *value > 0.0 ? static_cast<std::size_t>(*value) : 0;
            }
            return 0;
        };
        const auto anomalyLast = responseMessage.properties.find("anomalyLast");
        const auto* anomalyLastText = anomalyLast == responseMessage.properties.end()
            ? nullptr : std::get_if<std::string>(&anomalyLast->second);
        gScenarioRunner.RecordAnomalyStatus(
            countProperty("anomalyCount"),
            countProperty("anomalyErrorCount"),
            anomalyLastText ? *anomalyLastText : std::string{});
        if (responseMessage.observation) {
            gCoverageTracker.Observe(*responseMessage.observation);
            gScenarioRunner.Observe(*responseMessage.observation);
        }
        bool actionAccepted = true;
        if (const auto found = responseMessage.properties.find("ok");
            found != responseMessage.properties.end()) {
            if (const auto* ok = std::get_if<bool>(&found->second)) actionAccepted = *ok;
        }
        if (actionAccepted &&
            requestMessage.messageType == DebugProtocolMessageType::ExecuteAction &&
            requestMessage.action) {
            const std::uint64_t frameNumber = responseMessage.observation
                ? responseMessage.observation->frameNumber : 0;
            gCoverageTracker.RecordExecutedAction(*requestMessage.action, frameNumber);
            gScenarioRunner.RecordExecutedAction(*requestMessage.action, frameNumber);
        }
    }
    return true;
}

DebugProtocolMessage MakeRequest(const char* command) {
    static std::atomic_uint64_t sequence = 0;
    DebugProtocolMessage request;
    request.gameId = "DebugAIViewer";
    request.gameVersion = "0.1.0";
    request.sessionId = "viewer";
    request.sequence = sequence.fetch_add(1) + 1;
    if (strcmp(command, "status") == 0) {
        request.messageType = DebugProtocolMessageType::StatusRequest;
    } else {
        request.messageType = DebugProtocolMessageType::ControlCommand;
        request.properties["command"] = std::string(command);
    }
    return request;
}

bool SendCommand(const char* command, std::string& response) {
    return SendProtocolMessage(MakeRequest(command), response);
}

bool SendCheckedCommand(const char* command, std::string& response) {
    if (!SendCommand(command, response)) return false;
    DebugProtocolMessage result;
    if (!DebugProtocolJson::TryParse(response, result)) return false;
    const auto found = result.properties.find("ok");
    return found != result.properties.end() &&
        std::get_if<bool>(&found->second) && *std::get_if<bool>(&found->second);
}

std::string PlaySelectedReplayCore(const std::filesystem::path& manifestPath) {
    DebugProtocolMessage request = MakeRequest("play_latest");
    request.properties["manifestPath"] = WideToUtf8(manifestPath.wstring());
    std::string response;
    return SendProtocolMessage(request, response)
        ? FormatProtocolResponse(response)
        : response;
}

std::string SetReplaySpeedCore(double speed) {
    DebugProtocolMessage request = MakeRequest("set_replay_speed");
    request.properties["speed"] = speed;
    std::string response;
    return SendProtocolMessage(request, response)
        ? FormatProtocolResponse(response)
        : response;
}

std::size_t ProtocolCountProperty(
    const DebugProtocolMessage& message,
    const char* name) {
    const auto found = message.properties.find(name);
    if (found == message.properties.end()) return 0;
    if (const auto* value = std::get_if<std::int64_t>(&found->second)) {
        return *value > 0 ? static_cast<std::size_t>(*value) : 0;
    }
    if (const auto* value = std::get_if<double>(&found->second)) {
        return *value > 0.0 ? static_cast<std::size_t>(*value) : 0;
    }
    return 0;
}

bool ProtocolBoolProperty(
    const DebugProtocolMessage& message,
    const char* name) {
    const auto found = message.properties.find(name);
    if (found == message.properties.end()) return false;
    const auto* value = std::get_if<bool>(&found->second);
    return value && *value;
}

std::string FormattedLineValue(
    const std::string& text,
    const std::string& label) {
    const auto position = text.find(label);
    if (position == std::string::npos) return {};
    std::size_t begin = position + label.size();
    while (begin < text.size() &&
        (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    std::size_t end = text.find_first_of("\r\n", begin);
    if (end == std::string::npos) end = text.size();
    return text.substr(begin, end - begin);
}

AutomaticReplayVerificationResult VerifyReplayAutomatically(
    HWND window,
    const std::filesystem::path& manifestPath,
    double timeoutSeconds) {
    AutomaticReplayVerificationResult result;
    if (manifestPath.empty()) {
        result.status = "unavailable";
        result.detail = "replay manifest path is missing";
        return result;
    }

    // Verified playback deliberately uses real-time simulation. Speeds above
    // 1x can change physics results, so 1x is the fastest deterministic mode.
    std::string speedResponse;
    DebugProtocolMessage speedRequest = MakeRequest("set_replay_speed");
    speedRequest.properties["speed"] = 1.0;
    SendProtocolMessage(speedRequest, speedResponse);

    DebugProtocolMessage playRequest = MakeRequest("play_latest");
    playRequest.properties["manifestPath"] = WideToUtf8(manifestPath.wstring());
    std::string response;
    if (!SendProtocolMessage(playRequest, response)) {
        result.status = "start_failed";
        result.detail = response;
        return result;
    }
    DebugProtocolMessage status;
    if (!DebugProtocolJson::TryParse(response, status) || !ProtocolResultOk(status)) {
        result.status = "start_failed";
        result.detail = DebugProtocolJson::TryParse(response, status)
            ? PropertyText(status.properties, "message", "replay start was rejected")
            : "replay start returned an invalid response";
        return result;
    }

    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<std::int64_t>(timeoutSeconds * 1000.0));
    bool playbackStarted = ProtocolBoolProperty(status, "replaying");
    ULONGLONG nextProgressUpdate = 0;
    unsigned int consecutiveStatusFailures = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        if (gAIStopRequested) {
            std::string stopResponse;
            SendCommand("stop_replay", stopResponse);
            result.status = "stopped";
            result.detail = "verification was stopped by the user";
            return result;
        }

        const std::string validation =
            PropertyText(status.properties, "replayValidationStatus", "unavailable");
        result.checked = ProtocolCountProperty(status, "replayValidationChecked");
        result.checkpoints = ProtocolCountProperty(status, "replayValidationCheckpoints");
        result.mismatches = ProtocolCountProperty(status, "replayValidationMismatches");
        const bool queued = ProtocolBoolProperty(status, "replayQueued");
        const bool replaying = ProtocolBoolProperty(status, "replaying");
        playbackStarted = playbackStarted || replaying;

        if (!queued && !replaying &&
            (playbackStarted || validation == "passed" ||
                validation == "diverged" || validation == "interrupted")) {
            result.status = validation;
            result.detail = PropertyText(
                status.properties, "replayValidationFirstDetail", "");
            if (result.detail.empty()) {
                result.detail = PropertyText(
                    status.properties, "replayValidationDetail", "");
            }
            if (result.status == "unavailable") {
                result.detail = "no verification checkpoints were recorded";
            }
            return result;
        }
        if (!queued && !replaying && validation == "unavailable") {
            result.status = "unavailable";
            result.detail = "no verification checkpoints were recorded";
            return result;
        }

        const ULONGLONG now = GetTickCount64();
        if (now >= nextProgressUpdate) {
            nextProgressUpdate = now + 500;
            std::ostringstream progress;
            progress << "Automatic replay verification\r\n"
                << "Status: " << (queued ? "loading scene" :
                    (replaying ? "playing" : validation))
                << "  checked " << result.checked << '/' << result.checkpoints
                << "  mismatches " << result.mismatches;
            PostAIStatus(window, progress.str(), true);
        }

        std::unique_lock waitLock(gAIWaitMutex);
        gAIWaitCondition.wait_for(
            waitLock, std::chrono::milliseconds(100),
            [] { return gAIStopRequested.load(); });
        if (gAIStopRequested) continue;
        std::string statusResponse;
        if (!SendCommand("status", statusResponse) ||
            !DebugProtocolJson::TryParse(statusResponse, status)) {
            if (++consecutiveStatusFailures >= 20) {
                result.status = "connection_failed";
                result.detail = "game status could not be read during replay verification";
                return result;
            }
            continue;
        }
        consecutiveStatusFailures = 0;
    }

    std::string stopResponse;
    SendCommand("stop_replay", stopResponse);
    result.status = "timeout";
    result.detail = "verification exceeded " +
        std::to_string(static_cast<int>(timeoutSeconds)) + " seconds";
    return result;
}

std::string FormatReplayTimelineCore(
    const std::filesystem::path& manifestPath) {
    std::ifstream manifestInput(manifestPath);
    const auto manifest =
        nlohmann::json::parse(manifestInput, nullptr, false);
    if (manifest.is_discarded() || !manifest.is_object()) {
        return "Timeline could not be opened: invalid replay manifest.";
    }
    const auto tracks = manifest.find("tracks");
    if (tracks == manifest.end() || !tracks->is_object()) {
        return "Timeline could not be opened: manifest has no tracks.";
    }
    const auto timelinePath = ResolveReplayTrackPath(
        manifestPath, tracks->value("eventTimeline", ""));
    std::ifstream timelineInput(timelinePath);
    if (!timelineInput) {
        return "Timeline is not available for this replay.\r\nPath: " +
            WideToUtf8(timelinePath.wstring());
    }
    std::uint64_t frameCount =
        manifest.value("inputFrameCount", std::uint64_t{ 0 });
    if (frameCount == 0) {
        frameCount = ReadInputReplayFrameCount(ResolveReplayTrackPath(
            manifestPath, tracks->value("playerInput", "")));
    }

    std::ostringstream output;
    output << "Replay Event Timeline\r\n"
        << "Session: " << manifest.value("sessionId", "") << "\r\n"
        << "Scene: " << manifest.value("sceneId", "")
        << " / " << manifest.value("phase", "") << "\r\n"
        << "Frames: " << frameCount << "\r\n"
        << "Manifest: " << WideToUtf8(manifestPath.wstring())
        << "\r\n\r\n";

    constexpr std::size_t kMaximumDisplayedEvents = 800;
    std::size_t eventCount = 0;
    std::size_t displayedCount = 0;
    std::size_t checkpointCount = 0;
    std::size_t invalidLineCount = 0;
    std::string line;
    while (std::getline(timelineInput, line)) {
        if (line.empty()) continue;
        DebugProtocolMessage event;
        if (!DebugProtocolJson::TryParse(line, event)) {
            ++invalidLineCount;
            continue;
        }
        const std::string type =
            PropertyText(event.properties, "event.type", "");
        if (type.empty()) continue;
        ++eventCount;
        if (type == "ReplayCheckpoint") {
            ++checkpointCount;
            continue;
        }
        if (displayedCount >= kMaximumDisplayedEvents) continue;

        const std::string actor =
            PropertyText(event.properties, "event.actorId", "");
        const std::string before =
            PropertyText(event.properties, "before", "");
        const std::string after =
            PropertyText(event.properties, "after", "");
        const std::string message =
            PropertyText(event.properties, "event.message", "");
        std::string description;
        if (type == "PlayerStateChanged") {
            if (after == "Attack") {
                const std::string attackType =
                    PropertyText(
                        event.properties, "player.attackType", "");
                description = "Player Attack";
                if (!attackType.empty() && attackType != "None") {
                    description += " " + attackType;
                }
            } else {
                description = "Player " + after;
            }
            const std::string direction =
                PropertyText(event.properties, "direction", "");
            if (after == "Move" && !direction.empty()) {
                description += " direction=" + direction;
            }
        } else if (type == "ActionExecuted" && event.action) {
            description = actor + " Action " + event.action->actionId;
            const std::string source =
                PropertyText(event.properties, "event.source", "");
            if (!source.empty()) description += " source=" + source;
        } else if (type == "ActorStateChanged") {
            description = actor + " State " + before + " -> " + after;
        } else if (type == "ActorPhaseChanged") {
            description = actor + " Phase " + before + " -> " + after;
        } else if (type == "PhaseChanged" || type == "SceneChanged") {
            description = type + " " + before + " -> " + after;
        } else if (type == "PlayerDamaged" ||
            type == "EntityDamaged") {
            description = actor + " Damaged " +
                PropertyText(event.properties, "before", "?") + " -> " +
                PropertyText(event.properties, "after", "?") +
                " amount=" +
                PropertyText(event.properties, "amount", "?");
        } else if (type == "EntitySpawned") {
            description = actor + " Spawned " +
                PropertyText(event.properties, "category", "") + "/" +
                PropertyText(event.properties, "type", "");
        } else if (type == "EntityDespawned") {
            description = actor + " Despawned";
        } else if (type == "SessionStarted" ||
            type == "SessionEnded" ||
            type == "ObservationStarted") {
            description = type;
        } else {
            description = type;
            if (!actor.empty()) description += " actor=" + actor;
            if (!message.empty()) description += " - " + message;
        }
        output << std::setw(6) << event.sequence << "F  "
            << description << "\r\n";
        ++displayedCount;
    }
    output << "\r\nEvents: " << eventCount
        << "  Displayed: " << displayedCount
        << "  Verification checkpoints: " << checkpointCount;
    if (eventCount - checkpointCount > displayedCount) {
        output << "\r\n"
            << (eventCount - checkpointCount - displayedCount)
            << " events omitted from the Viewer display.";
    }
    if (invalidLineCount > 0) {
        output << "\r\nInvalid lines skipped: " << invalidLineCount;
    }
    return output.str();
}

bool BuildOfflinePolicyObservation(DebugObservation& observation) {
    const auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) return false;
    std::ifstream input(ActionProfilePath(projectRoot));
    const auto profile = nlohmann::json::parse(input, nullptr, false);
    if (profile.is_discarded() || !profile.is_object() ||
        !profile.contains("actions") || !profile["actions"].is_array()) return false;

    observation.sceneId = "OfflineProfile";
    observation.properties["game.phase"] = std::string("Battle");
    observation.properties["player.canMove"] = true;
    observation.properties["player.canJump"] = true;
    observation.properties["player.canAttack"] = true;
    observation.properties["player.isAttacking"] = false;
    observation.properties["enemy.threat"] = false;
    observation.properties["enemy.attackActive"] = false;
    observation.properties["enemy.distanceToPlayer"] = 6.0;
    observation.properties["observation.source"] = std::string("action_profile.json");
    for (const auto& entry : profile["actions"]) {
        if (!entry.is_object() || !entry.value("enabled", true)) continue;
        const bool usable = entry.value("enabledForLocalAI", false) ||
            entry.value("verified", false) || entry.value("runtimeObserved", false);
        const std::string actionId = entry.value("actionId", "");
        if (!usable || actionId.empty()) continue;
        DebugGenericAction action;
        action.actionId = actionId;
        action.parameters[DebugActionParameter::Direction] = DebugVec3{};
        action.parameters[DebugActionParameter::CoordinateSpace] = std::string(DebugCoordinateSpace::World);
        action.parameters[DebugActionParameter::DurationFrames] = static_cast<std::int64_t>(1);
        action.parameters[DebugActionParameter::TargetId] = std::string{};
        observation.availableActions.push_back(std::move(action));
    }
    return !observation.availableActions.empty();
}

std::string GenerateLocalPolicyCore() {
    std::string response;
    DebugObservation observation;
    bool usedRuntimeObservation = false;
    DebugProtocolMessage status;
    if (SendCommand("status", response) && DebugProtocolJson::TryParse(response, status) && status.observation) {
        observation = *status.observation;
        usedRuntimeObservation = true;
    } else if (!BuildOfflinePolicyObservation(observation)) {
        return "Local Policy generation failed: the game is disconnected and no verified Action Profile is available.\r\n"
            "Run Scan Project and Generate Action Profile once, or generate while GameScene is connected.";
    }
    EnrichObservationWithActionHitEstimates(observation);
    std::string policyJson;
    std::string reason;
    if (!gAIProvider.GenerateLocalPolicy(observation, policyJson, reason)) {
        return std::string("Local Policy generation failed (") + gAIProvider.Name() + "): " +
            gAIProvider.LastStatus();
    }
    auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) return "Local Policy generation failed: select a Game Project Folder first.";
    const auto path = LocalPolicyPath(projectRoot);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::trunc);
    if (!output) return "Local Policy generation failed: output file could not be opened.";
    output << policyJson << '\n';
    return "Local Policy generated from AI Goal.\r\nPath: " + WideToUtf8(path.wstring()) +
        "\r\nObservation source: " +
            (usedRuntimeObservation ? std::string("connected game") : std::string("offline Action Profile")) +
        "\r\nProvider: " + gAIProvider.Name() + "\r\nReason: " + reason +
        "\r\nStart Local will use this policy without API calls.";
}

void ExecuteAndDisplay(const char* command) {
    std::string response;
    const bool connected = SendCommand(command, response);
    if (connected) {
        DebugProtocolMessage message;
        if (DebugProtocolJson::TryParse(response, message) && message.observation) {
            gLastObservation = *message.observation;
            gHasObservation = true;
        }
    }
    std::wstring display = connected
        ? L"Game connection: connected\r\n\r\n" + Utf8ToWide(response)
        : Utf8ToWide(response);
    SetWindowTextW(gStatusText, display.c_str());
}

std::string ExecuteCommandCore(const char* command) {
    std::string response;
    const bool connected = SendCommand(command, response);
    if (connected) {
        DebugProtocolMessage message;
        if (DebugProtocolJson::TryParse(response, message) && message.observation) {
            EnrichObservationWithActionHitEstimates(*message.observation);
            UpdateActionProfileFromRuntime(*message.observation);
            UpdateStateProfileFromRuntime(*message.observation);
        }
    }
    return connected ? FormatProtocolResponse(response) : response;
}

void ExecuteFirstAvailableAction() {
    std::string response;
    if (!SendCommand("status", response)) {
        ExecuteAndDisplay("status");
        return;
    }
    DebugProtocolMessage status;
    if (!DebugProtocolJson::TryParse(response, status) || !status.observation ||
        status.observation->availableActions.empty()) {
        SetWindowTextW(gStatusText, L"No available generic action was received.");
        return;
    }
    DebugProtocolMessage request;
    request.gameId = "DebugAIViewer";
    request.gameVersion = "0.1.0";
    request.sessionId = "viewer";
    request.messageType = DebugProtocolMessageType::ExecuteAction;
    request.sequence = status.sequence + 1;
    request.action = status.observation->availableActions.front();
    if (SendProtocolMessage(request, response)) {
        DebugProtocolMessage result;
        if (DebugProtocolJson::TryParse(response, result) && result.observation) {
            gLastObservation = *result.observation;
            gHasObservation = true;
        }
        SetWindowTextW(gStatusText, Utf8ToWide(response).c_str());
    }
}

std::string ExecuteFirstAvailableActionCore() {
    std::string response;
    if (!SendCommand("status", response)) return response;
    DebugProtocolMessage status;
    if (!DebugProtocolJson::TryParse(response, status) || !status.observation ||
        status.observation->availableActions.empty()) {
        return "No available generic action was received.";
    }
    UpdateActionProfileFromRuntime(*status.observation);
    UpdateStateProfileFromRuntime(*status.observation);
    EnrichObservationWithActionHitEstimates(*status.observation);
    DebugProtocolMessage request = MakeRequest("execute_action");
    request.messageType = DebugProtocolMessageType::ExecuteAction;
    request.action = status.observation->availableActions.front();
    SendProtocolMessage(request, response);
    return FormatProtocolResponse(response);
}

std::string GenericActionActorId(const DebugGenericAction& action) {
    const auto found = action.parameters.find(DebugActionParameter::ActorId);
    if (found != action.parameters.end()) {
        if (const auto* value = std::get_if<std::string>(&found->second); value && !value->empty()) {
            return *value;
        }
    }
    return "player";
}

bool IsPlayerActorAction(const DebugGenericAction& action) {
    return GenericActionActorId(action) == "player";
}

DebugObservation ObservationForActor(const DebugObservation& source, ControlledActorMode mode) {
    DebugObservation result = source;
    if (mode == ControlledActorMode::Both) return result;
    const bool wantPlayer = mode == ControlledActorMode::Player;
    std::erase_if(result.availableActions, [&](const DebugGenericAction& action) {
        return IsPlayerActorAction(action) != wantPlayer;
    });
    return result;
}

const char* ControlledActorLabel(ControlledActorMode mode) {
    if (mode == ControlledActorMode::Boss) return "Boss";
    if (mode == ControlledActorMode::Both) return "Both";
    return "Player";
}

std::filesystem::path LatestVisionCapturePath(
    const std::filesystem::path& projectRoot) {
    const auto root = projectRoot.empty()
        ? std::filesystem::current_path()
        : projectRoot;
    return root / "generated/debug_ai/viewer/latest_frame.png";
}

bool SaveLatestVisionCapture(
    const std::filesystem::path& projectRoot,
    const std::vector<unsigned char>& pngBytes,
    std::filesystem::path& path) {
    path = LatestVisionCapturePath(projectRoot);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(
        reinterpret_cast<const char*>(pngBytes.data()),
        static_cast<std::streamsize>(pngBytes.size()));
    return static_cast<bool>(output);
}

std::string CapturePendingScenarioEvidence(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& scenarioPath,
    HWND viewerWindow) {
    std::string reason;
    if (!gScenarioRunner.ConsumeEvidenceRequest(reason)) return {};
    if (gGameProcessId.load() == 0) {
        std::string response;
        SendCommand("status", response);
    }

    GameWindowCaptureResult capture;
    if (!CaptureGameProcessWindow(
        gGameProcessId.load(), 1280, capture, viewerWindow)) {
        gScenarioRunner.RecordEvidence(
            {}, reason, 0, 0, capture.error);
        return "Evidence screenshot failed: " + capture.error;
    }

    const auto directory = projectRoot / "generated/debug_ai/evidence" /
        scenarioPath.stem();
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        const std::string message =
            "evidence directory could not be created";
        gScenarioRunner.RecordEvidence({}, reason, capture.width, capture.height, message);
        return "Evidence screenshot failed: " + message;
    }
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto sequence = gEvidenceCaptureSequence.fetch_add(1) + 1;
    const auto path = directory /
        (std::to_string(stamp) + "_" + std::to_string(sequence) + ".png");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        const std::string message = "evidence PNG could not be opened";
        gScenarioRunner.RecordEvidence({}, reason, capture.width, capture.height, message);
        return "Evidence screenshot failed: " + message;
    }
    output.write(
        reinterpret_cast<const char*>(capture.pngBytes.data()),
        static_cast<std::streamsize>(capture.pngBytes.size()));
    if (!output) {
        const std::string message = "evidence PNG could not be written";
        gScenarioRunner.RecordEvidence({}, reason, capture.width, capture.height, message);
        return "Evidence screenshot failed: " + message;
    }
    gScenarioRunner.RecordEvidence(
        path, reason, capture.width, capture.height);
    return "Evidence screenshot saved.\r\nReason: " + reason +
        "\r\nPath: " + WideToUtf8(path.wstring()) +
        "\r\nNo API was called.";
}

std::string CaptureVisionPreviewCore(
    const std::filesystem::path& projectRoot,
    HWND viewerWindow) {
    if (gGameProcessId.load() == 0) {
        std::string response;
        SendCommand("status", response);
    }
    GameWindowCaptureResult capture;
    if (!CaptureGameProcessWindow(
        gGameProcessId.load(),
        gAIProvider.VisionMaximumWidth(),
        capture,
        viewerWindow)) {
        return "Vision capture failed: " + capture.error;
    }
    std::filesystem::path path;
    if (!SaveLatestVisionCapture(projectRoot, capture.pngBytes, path)) {
        return "Vision capture succeeded, but latest_frame.png could not be saved.";
    }
    return "Vision capture ready.\r\nWindow: " +
        WideToUtf8(capture.windowTitle) +
        "\r\nSize: " + std::to_string(capture.width) + "x" +
        std::to_string(capture.height) +
        "\r\nPath: " + WideToUtf8(path.wstring()) +
        "\r\nNo API was called.";
}

bool ConfigureCoverageFromCurrentProject() {
    const auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) return false;
    return gCoverageTracker.Configure(
        projectRoot,
        WideToUtf8(projectRoot.filename().wstring()));
}

std::string FormatCoverageSummaryCore() {
    if (!ConfigureCoverageFromCurrentProject()) {
        return "Coverage is not configured. Select a Game Project Folder first.";
    }
    gCoverageTracker.ReloadProfiles();
    return gCoverageTracker.FormatSummary();
}

std::string ResetCoverageCore() {
    if (!ConfigureCoverageFromCurrentProject()) {
        return "Coverage reset failed: select a Game Project Folder first.";
    }
    gCoverageTracker.ReloadProfiles();
    return gCoverageTracker.Reset();
}

std::string OpenLatestTestReportCore() {
    const auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) {
        return "Test report could not be opened: select a Game Project Folder first.";
    }
    const auto reportPath = TestReportGenerator::FindLatestHtml(projectRoot);
    if (reportPath.empty()) {
        return "No test report was found. Run all scenarios once to generate it.";
    }
    const HINSTANCE opened = ShellExecuteW(
        nullptr, L"open", reportPath.c_str(), nullptr,
        reportPath.parent_path().c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(opened) <= 32) {
        return "Test report could not be opened.\r\nPath: " +
            WideToUtf8(reportPath.wstring());
    }
    return "Opened latest test report.\r\nPath: " +
        WideToUtf8(reportPath.wstring());
}

std::string RunAdapterDiagnosticsCore(
    const std::filesystem::path& projectRoot) {
    if (projectRoot.empty()) {
        return "Adapter diagnosis failed: select a Game Project Folder first.";
    }
    std::string response;
    if (!SendCommand("status", response)) return response;
    DebugProtocolMessage status;
    std::string parseError;
    if (!DebugProtocolJson::TryParse(response, status, &parseError)) {
        return "Adapter diagnosis failed: game response could not be parsed.\r\n" +
            parseError;
    }
    return AdapterDiagnostics::Run(projectRoot, status).message;
}

bool ProtocolResultOk(const DebugProtocolMessage& message) {
    const auto found = message.properties.find("ok");
    if (found == message.properties.end()) return true;
    const auto* value = std::get_if<bool>(&found->second);
    return value != nullptr && *value;
}

std::filesystem::path ResolveProjectOutputPath(const std::string& pathText) {
    if (pathText.empty()) return {};
    const std::filesystem::path path(Utf8ToWide(pathText));
    if (path.is_absolute()) return path.lexically_normal();
    const auto root = ConfiguredProjectRoot();
    return (root.empty() ? path : root / path).lexically_normal();
}

std::string StartRecordingWithCoverageCore() {
    std::string response;
    if (!SendCommand("start_recording", response)) return response;
    DebugProtocolMessage message;
    if (!DebugProtocolJson::TryParse(response, message)) return response;
    const std::string formatted = FormatProtocolResponse(response);
    if (!ProtocolResultOk(message)) return formatted;

    gCoverageTracker.Configure(ConfiguredProjectRoot(), message.gameId);
    const std::string sessionId =
        PropertyText(message.properties, "replaySessionId", "");
    std::string coverage = gCoverageTracker.BeginReplaySession(sessionId);
    if (message.observation) gCoverageTracker.Observe(*message.observation);
    return formatted + "\r\n" + coverage;
}

std::string StopRecordingWithCoverageCore() {
    std::string response;
    if (!SendCommand("stop_recording", response)) return response;
    DebugProtocolMessage message;
    if (!DebugProtocolJson::TryParse(response, message)) return response;
    const std::string formatted = FormatProtocolResponse(response);
    const auto manifestPath = ResolveProjectOutputPath(
        PropertyText(message.properties, "replayManifestPath", ""));
    if (manifestPath.empty()) {
        return ProtocolResultOk(message)
            ? formatted + "\r\nCoverage finalization skipped: replay manifest path is missing."
            : formatted;
    }

    std::ifstream manifestInput(manifestPath);
    const auto manifest = nlohmann::json::parse(manifestInput, nullptr, false);
    const bool completedReplay = manifest.is_object() &&
        manifest.value("status", std::string{}) == "complete";
    if (!ProtocolResultOk(message) && !completedReplay) return formatted;

    gCoverageTracker.Configure(ConfiguredProjectRoot(), message.gameId);
    if (!ProtocolResultOk(message)) {
        const std::string existingCoverage =
            CoverageTracker::FormatReplaySummary(manifestPath);
        if (existingCoverage.find("unavailable for this session") == std::string::npos) {
            return formatted +
                "\r\nThe replay had already stopped automatically. "
                "Its completed coverage was found.\r\n" + existingCoverage;
        }
    }
    const std::string automaticStopNote = ProtocolResultOk(message)
        ? std::string{}
        : std::string(
            "\r\nThe replay had already stopped automatically at the scene end. "
            "The saved session was found, so coverage finalization continued.\r\n");
    return formatted + automaticStopNote + "\r\n" +
        gCoverageTracker.FinalizeReplaySession(manifestPath);
}

bool StopScenarioRuntimeActivity(
    DebugProtocolMessage& status,
    std::string& error) {
    std::string response;
    if (!SendCommand("status", response)) {
        error = response;
        return false;
    }
    if (!DebugProtocolJson::TryParse(response, status) ||
        !ProtocolResultOk(status)) {
        error = "Scenario preparation could not read the current game status.";
        return false;
    }
    const auto boolProperty = [&](const char* name) {
        const auto found = status.properties.find(name);
        const auto* value = found == status.properties.end()
            ? nullptr : std::get_if<bool>(&found->second);
        return value && *value;
    };
    // Game scenes may start an automatic replay recording before a scenario
    // batch begins. The batch owns separate recordings for each scenario, so
    // close any pre-existing session before restoring the shared baseline.
    if (boolProperty("replaying")) {
        std::string stopResponse;
        if (!SendCheckedCommand("stop_replay", stopResponse)) {
            error = "Scenario batch could not stop the active replay.\r\n" + stopResponse;
            return false;
        }
    }
    if (boolProperty("recording")) {
        std::string stopResponse;
        if (!SendCheckedCommand("stop_recording", stopResponse)) {
            error = "Scenario batch could not stop the existing automatic recording.\r\n" +
                stopResponse;
            return false;
        }
    }
    error.clear();
    return true;
}

bool CaptureScenarioBaseline(
    DebugObservation& observation,
    std::string& error) {
    DebugProtocolMessage status;
    if (!StopScenarioRuntimeActivity(status, error)) return false;
    if (!status.observation) {
        error = "The current scene does not expose a DebugObservation. "
            "Add sceneId to the scenario so the Viewer can load its target scene.";
        return false;
    }
    observation = *status.observation;
    error.clear();
    return true;
}

bool EnsureScenarioSceneLoaded(
    const std::string& targetSceneId,
    std::string& error) {
    DebugProtocolMessage status;
    if (!StopScenarioRuntimeActivity(status, error)) return false;
    if (targetSceneId.empty()) {
        if (status.observation) return true;
        error = "Scenario has no sceneId and the current scene has no DebugObservation.";
        return false;
    }
    if (status.observation && status.observation->sceneId == targetSceneId) {
        return true;
    }

    DebugProtocolMessage request = MakeRequest("load_scene");
    request.properties["sceneId"] = targetSceneId;
    std::string response;
    if (!SendProtocolMessage(request, response)) {
        error = response;
        return false;
    }
    DebugProtocolMessage loadResult;
    if (!DebugProtocolJson::TryParse(response, loadResult) ||
        !ProtocolResultOk(loadResult)) {
        error = DebugProtocolJson::TryParse(response, loadResult)
            ? PropertyText(loadResult.properties, "message", "scene load request failed")
            : "scene load request returned an invalid response";
        return false;
    }

    constexpr auto kSceneLoadTimeout = std::chrono::seconds(20);
    const auto deadline = std::chrono::steady_clock::now() + kSceneLoadTimeout;
    while (!gAIStopRequested && std::chrono::steady_clock::now() < deadline) {
        {
            std::unique_lock lock(gAIWaitMutex);
            gAIWaitCondition.wait_for(
                lock, std::chrono::milliseconds(100),
                [] { return gAIStopRequested.load(); });
        }
        if (gAIStopRequested) break;
        std::string statusResponse;
        if (!SendCommand("status", statusResponse)) continue;
        DebugProtocolMessage current;
        if (!DebugProtocolJson::TryParse(statusResponse, current) ||
            !ProtocolResultOk(current) || !current.observation ||
            current.observation->sceneId != targetSceneId) {
            continue;
        }
        // Scene entry may have opened an automatic recording. Close it before
        // the scenario restores its baseline and starts its own recording.
        return StopScenarioRuntimeActivity(current, error);
    }
    error = gAIStopRequested
        ? "Scene loading was stopped by the user."
        : "Timed out waiting for scene: " + targetSceneId;
    return false;
}

bool RestoreScenarioBaseline(
    const DebugObservation& observation,
    std::string& error) {
    DebugProtocolMessage request = MakeRequest("restore_observation");
    request.observation = observation;
    std::string response;
    if (!SendProtocolMessage(request, response)) {
        error = response;
        return false;
    }
    DebugProtocolMessage message;
    if (!DebugProtocolJson::TryParse(response, message) ||
        !ProtocolResultOk(message)) {
        error = DebugProtocolJson::TryParse(response, message)
            ? PropertyText(message.properties, "message", "observation restore failed")
            : "observation restore returned an invalid response";
        return false;
    }
    error.clear();
    return true;
}

const char* ScenarioStatusName(ScenarioRunner::Status status) {
    switch (status) {
    case ScenarioRunner::Status::Ready: return "ready";
    case ScenarioRunner::Status::Running: return "running";
    case ScenarioRunner::Status::Passed: return "passed";
    case ScenarioRunner::Status::Failed: return "failed";
    case ScenarioRunner::Status::Stopped: return "stopped";
    default: return "idle";
    }
}

std::filesystem::path SaveScenarioBatchResult(
    const std::filesystem::path& projectRoot,
    const std::vector<ScenarioBatchItemResult>& items,
    bool stopped,
    double elapsedSeconds) {
    const auto directory = projectRoot / "generated/debug_ai/scenarios/results";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto path = directory /
        ("batch_" + std::to_string(stamp) + ".result.json");
    std::size_t passed = 0;
    std::size_t failed = 0;
    std::size_t anomalies = 0;
    std::size_t anomalyErrors = 0;
    std::size_t replayVerified = 0;
    std::size_t replayVerificationFailed = 0;
    nlohmann::json scenarios = nlohmann::json::array();
    for (const auto& item : items) {
        if (item.status == "passed") ++passed;
        else if (item.status != "stopped") ++failed;
        anomalies += item.anomalyCount;
        anomalyErrors += item.anomalyErrorCount;
        if (item.replayVerificationStatus == "passed") ++replayVerified;
        else if (item.replayVerificationStatus != "disabled") {
            ++replayVerificationFailed;
        }
        scenarios.push_back({
            { "label", item.label },
            { "status", item.status },
            { "detail", item.detail },
            { "anomalyCount", item.anomalyCount },
            { "anomalyErrorCount", item.anomalyErrorCount },
            { "replayVerificationStatus", item.replayVerificationStatus },
            { "replayVerificationChecked", item.replayVerificationChecked },
            { "replayVerificationCheckpoints", item.replayVerificationCheckpoints },
            { "replayVerificationMismatches", item.replayVerificationMismatches },
            { "replayVerificationDetail", item.replayVerificationDetail },
            { "elapsedSeconds", item.elapsedSeconds },
            { "scenarioPath", WideToUtf8(item.scenarioPath.wstring()) },
            { "resultPath", WideToUtf8(item.resultPath.wstring()) },
        });
    }
    const std::string status = stopped
        ? "stopped" : (failed == 0 && passed == items.size() ? "passed" : "failed");
    nlohmann::json result = {
        { "schemaVersion", 1 },
        { "type", "scenarioBatch" },
        { "status", status },
        { "elapsedSeconds", elapsedSeconds },
        { "scenarioCount", items.size() },
        { "passed", passed },
        { "failed", failed },
        { "anomalyCount", anomalies },
        { "anomalyErrorCount", anomalyErrors },
        { "replayVerified", replayVerified },
        { "replayVerificationFailed", replayVerificationFailed },
        { "scenarios", std::move(scenarios) },
    };
    std::ofstream output(path, std::ios::trunc);
    if (!output) return {};
    output << result.dump(2) << '\n';
    return path;
}

std::string FormatScenarioBatchResult(
    const std::vector<ScenarioBatchItemResult>& items,
    const std::filesystem::path& resultPath,
    bool stopped,
    double elapsedSeconds) {
    std::size_t passed = 0;
    std::size_t failed = 0;
    std::size_t anomalies = 0;
    std::size_t anomalyErrors = 0;
    std::size_t replayVerified = 0;
    std::size_t replayVerificationFailed = 0;
    std::ostringstream output;
    output << "Scenario batch finished\r\n";
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        if (item.status == "passed") ++passed;
        else if (item.status != "stopped") ++failed;
        anomalies += item.anomalyCount;
        anomalyErrors += item.anomalyErrorCount;
        if (item.replayVerificationStatus == "passed") ++replayVerified;
        else if (item.replayVerificationStatus != "disabled") {
            ++replayVerificationFailed;
        }
        output << "  [" << (item.status == "passed" ? "PASS" : "FAIL") << "] "
            << item.label << " (" << std::fixed << std::setprecision(1)
            << item.elapsedSeconds << "s)\r\n";
        output << "         Anomalies: " << item.anomalyCount
            << "  Errors: " << item.anomalyErrorCount << "\r\n";
        output << "         Replay: " << item.replayVerificationStatus;
        if (item.replayVerificationCheckpoints > 0) {
            output << "  checked " << item.replayVerificationChecked << '/'
                << item.replayVerificationCheckpoints << "  mismatches "
                << item.replayVerificationMismatches;
        }
        output << "\r\n";
        if (!item.replayVerificationDetail.empty()) {
            output << "         Replay detail: "
                << item.replayVerificationDetail << "\r\n";
        }
        if (!item.detail.empty()) output << "         " << item.detail << "\r\n";
    }
    output << "Result: " << (stopped ? "stopped" : (failed == 0 ? "passed" : "failed"))
        << "  Passed: " << passed << '/' << items.size()
        << "  Failed: " << failed
        << "  Anomalies: " << anomalies
        << "  Errors: " << anomalyErrors
        << "  Replay verified: " << replayVerified << '/' << items.size()
        << "  Replay failures: " << replayVerificationFailed
        << "  Time: " << std::fixed << std::setprecision(1) << elapsedSeconds << "s\r\n";
    if (!resultPath.empty()) {
        output << "Batch result JSON: " << WideToUtf8(resultPath.wstring());
    } else {
        output << "Batch result JSON could not be saved.";
    }
    return output.str();
}

std::string FormatSelectedReplayCoverageCore(
    const std::filesystem::path& manifestPath) {
    std::string summary = CoverageTracker::FormatReplaySummary(manifestPath);
    if (summary.find("unavailable for this session") == std::string::npos) {
        return summary;
    }
    if (!ConfigureCoverageFromCurrentProject()) return summary;
    return gCoverageTracker.FinalizeReplaySession(manifestPath) +
        "\r\n\r\nCoverage was generated from the saved replay timeline.";
}

std::string ExecuteAIStepCore(
    HWND window,
    ControlledActorMode actorMode,
    const std::filesystem::path& projectRoot,
    bool visionEnabled,
    bool* outExecuted = nullptr) {
    if (outExecuted) *outExecuted = false;
    const auto startedAt = std::chrono::steady_clock::now();
    PostAIStatus(window, "AI Step 1/5: requesting game state...", true);
    std::string response;
    if (!SendCommand("status", response)) {
        return response;
    }
    DebugProtocolMessage status;
    if (!DebugProtocolJson::TryParse(response, status) || !status.observation) {
        return "AI Step: no generic observation was received.";
    }
    UpdateActionProfileFromRuntime(*status.observation);
    UpdateStateProfileFromRuntime(*status.observation);
    EnrichObservationWithActionHitEstimates(*status.observation);
    DebugObservation decisionObservation = ObservationForActor(*status.observation, actorMode);
    if (decisionObservation.availableActions.empty()) {
        return std::string("AI Step: no actions are available for actor ") +
            ControlledActorLabel(actorMode) + ".";
    }

    const bool pauseForInitialConnection = !gAIConnectionVerified.load();
    if (pauseForInitialConnection) {
        std::string pauseResponse;
        if (!SendCheckedCommand("pause_simulation", pauseResponse)) {
            return "AI Step failed: the game could not be paused.\r\n" + pauseResponse;
        }
    }
    const std::string sourceContext =
        BuildSourceDecisionContext(projectRoot, decisionObservation);
    GameWindowCaptureResult capture;
    std::filesystem::path capturePath;
    const bool captured = visionEnabled && CaptureGameProcessWindow(
        gGameProcessId.load(),
        gAIProvider.VisionMaximumWidth(),
        capture,
        window);
    if (captured) {
        SaveLatestVisionCapture(projectRoot, capture.pngBytes, capturePath);
    }
    gAIProvider.SetDecisionContext(
        sourceContext,
        captured ? "image/png" : "",
        captured ? Base64Encode(capture.pngBytes) : "");
    PostAIStatus(window, std::string("AI Step 2/5: ") +
        (pauseForInitialConnection ? "game paused for initial connection; calling " : "calling ") +
        gAIProvider.Name() +
        " for " + ControlledActorLabel(actorMode) + " with " +
        std::to_string(decisionObservation.availableActions.size()) + " available actions" +
        (sourceContext.empty() ? "" : ", source context") +
        (captured ? ", and one screenshot" : "") + "..." +
        (visionEnabled && !captured ? "\r\nVision warning: " + capture.error : ""), true);
    DebugGenericAction selected;
    std::string reason;
    if (!gAIProvider.ChooseAction(decisionObservation, selected, reason)) {
        if (pauseForInitialConnection) {
            std::string ignoredResumeResponse;
            SendCheckedCommand("resume_simulation", ignoredResumeResponse);
        }
        const std::string message = std::string("AI Step failed (") + gAIProvider.Name() + "): " +
            gAIProvider.LastStatus();
        return message;
    }
    if (GuessActionCategory(selected.actionId) == "attack" && !ActionCanHitTarget(selected)) {
        if (const auto* approach = FindAvailableActionByProfileTag(
            decisionObservation, "movement.approach")) {
            selected = *approach;
            selected.parameters[DebugActionParameter::Direction] = DebugVec3{ 0.0, 0.0, 1.0 };
            selected.parameters[DebugActionParameter::CoordinateSpace] =
                std::string(DebugCoordinateSpace::TargetRelative);
            if (const auto target = status.observation->properties.find("enemy.nearestId");
                target != status.observation->properties.end()) {
                if (const auto* targetId = std::get_if<std::string>(&target->second))
                    selected.parameters[DebugActionParameter::TargetId] = *targetId;
            }
            selected.parameters[DebugActionParameter::DurationFrames] = static_cast<std::int64_t>(12);
            reason = "API attack rejected because canHitTarget=false; approaching target instead";
        }
    }
    selected.parameters[DebugActionParameter::Source] = std::string("API");

    if (pauseForInitialConnection) {
        PostAIStatus(window, "AI Step 3/5: selected " + selected.actionId + "; resuming game...", true);
        std::string resumeResponse;
        if (!SendCheckedCommand("resume_simulation", resumeResponse)) {
            return "AI Step failed: the game could not be resumed. It will auto-resume within 20 seconds.\r\n" +
                resumeResponse;
        }
    } else {
        PostAIStatus(window, "AI Step 3/5: selected " + selected.actionId + "; game remained running.", true);
    }
    PostAIStatus(window, "AI Step 4/5: sending " + selected.actionId + " to the game...", true);
    DebugProtocolMessage request = MakeRequest("execute_action");
    request.messageType = DebugProtocolMessageType::ExecuteAction;
    request.action = selected;
    if (!SendProtocolMessage(request, response)) {
        return response;
    }
    if (outExecuted) *outExecuted = true;
    gAIConnectionVerified = true;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startedAt).count();
    const auto parameter = [&](const char* name) {
        const auto found = selected.parameters.find(name);
        return found == selected.parameters.end() ? std::string("-") : DebugValueText(found->second);
    };
    return std::string("AI Step 5/5: completed\r\nProvider: ") + gAIProvider.Name() +
        "\r\nActor: " + GenericActionActorId(selected) +
        "\r\nAction: " + selected.actionId +
        "\r\nParameters: direction=" + parameter(DebugActionParameter::Direction) +
        ", space=" + parameter(DebugActionParameter::CoordinateSpace) +
        ", durationFrames=" + parameter(DebugActionParameter::DurationFrames) +
        "\r\nReason: " + reason +
        "\r\nSource scan context: " + (sourceContext.empty() ? "not available" : "attached") +
        "\r\nVision: " + (captured
            ? "attached " + std::to_string(capture.width) + "x" +
                std::to_string(capture.height) +
                (capturePath.empty()
                    ? std::string{}
                    : " (" + WideToUtf8(capturePath.wstring()) + ")")
            : (visionEnabled ? "capture unavailable - " + capture.error : "disabled")) +
        "\r\n\r\n" +
        FormatProtocolResponse(response) +
        "\r\nElapsed: " + std::to_string(elapsed) + " ms";
}

struct LocalPolicyConfig {
    struct Condition {
        std::string property;
        std::string operation;
        nlohmann::json value;
        double enter = 0.0;
        double exit = 0.0;
    };
    struct Rule {
        std::string id;
        int priority = 0;
        std::string conditionMode = "all";
        std::vector<Condition> conditions;
        std::vector<std::string> actionIds;
        std::vector<std::string> actionTags;
        std::string selection = "first";
        std::int64_t durationFrames = 1;
        bool interruptCurrent = false;
        bool repeatWhileMatched = false;
        int maxConsecutive = 0;
        std::vector<std::string> recoveryActionTags;
        std::int64_t recoveryDurationFrames = 1;
        bool hasDirection = false;
        DebugVec3 direction{};
        std::string coordinateSpace = DebugCoordinateSpace::World;
    };
    std::vector<Rule> rules;
    std::map<std::string, std::set<std::string>> actionTags;
    std::vector<std::string> threatActions{ "DodgeAway", "Jump", "Retreat", "Guard", "Wait" };
    std::string approachAction = "Move";
    std::vector<std::string> attackActions;
    std::string idleAction = "Wait";
    double attackDistance = 5.0;
    std::int64_t approachDurationFrames = 16;
    std::int64_t evadeDurationFrames = 10;
    std::int64_t attackDurationFrames = 8;
    double closeRangeEnterDistance = 3.0;
    double closeRangeExitDistance = 4.0;
    std::int64_t closeRangeWaitFrames = 4;
    bool preferLeastUsedAttack = true;
    bool loadedFromFile = false;
};

LocalPolicyConfig LoadLocalPolicyConfig() {
    LocalPolicyConfig config;
    const auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) return config;
    std::ifstream input(LocalPolicyPath(projectRoot));
    const auto policy = nlohmann::json::parse(input, nullptr, false);
    if (policy.is_discarded() || !policy.is_object()) return config;
    std::ifstream actionInput(ActionProfilePath(projectRoot));
    const auto actionProfile = nlohmann::json::parse(actionInput, nullptr, false);
    if (!actionProfile.is_discarded() && actionProfile.is_object() &&
        actionProfile.contains("actions") && actionProfile["actions"].is_array()) {
        for (const auto& action : actionProfile["actions"]) {
            if (!action.is_object()) continue;
            const std::string id = action.value("actionId", "");
            if (id.empty() || !action.contains("tags") || !action["tags"].is_array()) continue;
            for (const auto& tag : action["tags"]) if (tag.is_string()) config.actionTags[id].insert(tag.get<std::string>());
        }
    }
    const auto readActions = [&](const char* name, std::vector<std::string>& output) {
        const auto found = policy.find(name);
        if (found == policy.end() || !found->is_array()) return;
        std::vector<std::string> values;
        for (const auto& value : *found) if (value.is_string()) values.push_back(value.get<std::string>());
        if (!values.empty()) output = std::move(values);
    };
    readActions("threatActions", config.threatActions);
    readActions("attackActions", config.attackActions);
    config.approachAction = policy.value("approachAction", config.approachAction);
    config.idleAction = policy.value("idleAction", config.idleAction);
    config.attackDistance = std::clamp(policy.value("attackDistance", config.attackDistance), 0.0, 100.0);
    config.closeRangeEnterDistance = std::clamp(
        policy.value("closeRangeEnterDistance", config.attackDistance), 0.0, 99.0);
    config.closeRangeExitDistance = std::clamp(
        policy.value("closeRangeExitDistance", config.closeRangeEnterDistance + 1.0),
        config.closeRangeEnterDistance + 0.1, 100.0);
    config.closeRangeWaitFrames = std::clamp<std::int64_t>(
        policy.value("closeRangeWaitFrames", config.closeRangeWaitFrames), 1, 60);
    config.approachDurationFrames = std::clamp<std::int64_t>(
        policy.value("approachDurationFrames", config.approachDurationFrames), 1, 60);
    config.evadeDurationFrames = std::clamp<std::int64_t>(
        policy.value("evadeDurationFrames", config.evadeDurationFrames), 1, 60);
    config.attackDurationFrames = std::clamp<std::int64_t>(
        policy.value("attackDurationFrames", config.attackDurationFrames), 1, 60);
    config.preferLeastUsedAttack = policy.value("preferLeastUsedAttack", true);
    if (const auto rules = policy.find("rules"); rules != policy.end() && rules->is_array()) {
        for (const auto& sourceRule : *rules) {
            if (!sourceRule.is_object()) continue;
            LocalPolicyConfig::Rule rule;
            rule.id = sourceRule.value("id", "rule_" + std::to_string(config.rules.size()));
            rule.priority = sourceRule.value("priority", 0);
            rule.conditionMode = sourceRule.value("conditionMode", "all");
            rule.selection = sourceRule.value("selection", "first");
            rule.durationFrames = std::clamp<std::int64_t>(sourceRule.value("durationFrames", 1), 1, 600);
            rule.interruptCurrent = sourceRule.value("interruptCurrent", false);
            rule.repeatWhileMatched = sourceRule.value("repeatWhileMatched", false);
            rule.maxConsecutive = std::clamp(sourceRule.value("maxConsecutive", 0), 0, 100);
            rule.recoveryDurationFrames = std::clamp<std::int64_t>(
                sourceRule.value("recoveryDurationFrames", 1), 1, 600);
            const auto readStrings = [&](const char* name, std::vector<std::string>& destination) {
                const auto values = sourceRule.find(name);
                if (values == sourceRule.end() || !values->is_array()) return;
                for (const auto& value : *values) if (value.is_string()) destination.push_back(value.get<std::string>());
            };
            readStrings("actionIds", rule.actionIds);
            readStrings("actionTags", rule.actionTags);
            readStrings("recoveryActionTags", rule.recoveryActionTags);
            // Profiles generated before repeatWhileMatched existed already tag
            // sustained movement as movement.approach. Preserve smooth movement
            // without requiring users to regenerate those profiles.
            if (!sourceRule.contains("repeatWhileMatched")) {
                rule.repeatWhileMatched = std::find(rule.actionTags.begin(), rule.actionTags.end(),
                    "movement.approach") != rule.actionTags.end();
                for (const auto& actionId : rule.actionIds) {
                    const auto tags = config.actionTags.find(actionId);
                    if (tags != config.actionTags.end() && tags->second.contains("movement.approach")) {
                        rule.repeatWhileMatched = true;
                        break;
                    }
                }
            }
            // Backward compatibility: older generated attack rules had no
            // cadence, so they could monopolize every available decision.
            if (!sourceRule.contains("maxConsecutive")) {
                bool isAttackRule = std::find(rule.actionTags.begin(), rule.actionTags.end(),
                    "combat.attack") != rule.actionTags.end();
                for (const auto& actionId : rule.actionIds) {
                    const auto tags = config.actionTags.find(actionId);
                    if (tags != config.actionTags.end() && tags->second.contains("combat.attack")) {
                        isAttackRule = true;
                        break;
                    }
                }
                if (isAttackRule) {
                    rule.maxConsecutive = 2;
                    rule.recoveryActionTags = { "movement.retreat" };
                    rule.recoveryDurationFrames = (std::max<std::int64_t>)(12, rule.durationFrames);
                }
            }
            if (const auto conditions = sourceRule.find("conditions");
                conditions != sourceRule.end() && conditions->is_array()) {
                for (const auto& sourceCondition : *conditions) {
                    if (!sourceCondition.is_object()) continue;
                    LocalPolicyConfig::Condition condition;
                    condition.property = sourceCondition.value("property", "");
                    condition.operation = sourceCondition.value("operator", "equals");
                    if (sourceCondition.contains("value")) condition.value = sourceCondition["value"];
                    condition.enter = sourceCondition.value("enter", 0.0);
                    condition.exit = sourceCondition.value("exit", condition.enter);
                    if (!condition.property.empty()) rule.conditions.push_back(std::move(condition));
                }
            }
            if (const auto direction = sourceRule.find("direction");
                direction != sourceRule.end() && direction->is_object()) {
                rule.hasDirection = true;
                rule.direction = { direction->value("x", 0.0), direction->value("y", 0.0), direction->value("z", 0.0) };
            }
            rule.coordinateSpace = sourceRule.value("coordinateSpace", DebugCoordinateSpace::World);
            if (!rule.conditions.empty() && (!rule.actionIds.empty() || !rule.actionTags.empty()))
                config.rules.push_back(std::move(rule));
        }
        std::stable_sort(config.rules.begin(), config.rules.end(),
            [](const auto& left, const auto& right) { return left.priority > right.priority; });
    }
    config.loadedFromFile = true;
    return config;
}

struct LocalPolicyState {
    LocalPolicyConfig config;
    std::map<std::string, std::uint64_t> useCount;
    std::size_t fallbackIndex = 0;
    std::map<std::string, bool> ruleActive;
    std::map<std::string, int> consecutiveRuleExecutions;
    std::uint64_t actionLockUntilFrame = 0;
    int activePriority = -1;
    std::string activeRuleId;
    std::string activeActionId;
    bool closeRangeHoldActive = false;
    struct PendingAttackSample {
        bool active = false;
        bool sawAttackActive = false;
        std::string actionId;
        double distance = -1.0;
        double enemyHpBefore = -1.0;
        std::uint64_t deadlineFrame = 0;
    } pendingAttack;
};

constexpr bool NextCloseRangeHoldState(
    bool active, double distance, double enterDistance, double exitDistance) {
    if (distance < 0.0) return false;
    return active ? distance < exitDistance : distance <= enterDistance;
}

static_assert(!NextCloseRangeHoldState(false, 5.0, 3.0, 4.0));
static_assert(NextCloseRangeHoldState(false, 3.0, 3.0, 4.0));
static_assert(NextCloseRangeHoldState(true, 3.5, 3.0, 4.0));
static_assert(!NextCloseRangeHoldState(true, 4.0, 3.0, 4.0));

bool BoolProperty(const DebugPropertyMap& properties, const char* name, bool fallback) {
    const auto found = properties.find(name);
    if (found == properties.end()) return fallback;
    if (const auto* value = std::get_if<bool>(&found->second)) return *value;
    return fallback;
}

double NumberProperty(const DebugPropertyMap& properties, const char* name, double fallback) {
    const auto found = properties.find(name);
    if (found == properties.end()) return fallback;
    if (const auto* value = std::get_if<double>(&found->second)) return *value;
    if (const auto* value = std::get_if<std::int64_t>(&found->second)) return static_cast<double>(*value);
    return fallback;
}

std::string StringProperty(const DebugPropertyMap& properties, const char* name) {
    const auto found = properties.find(name);
    if (found == properties.end()) return {};
    if (const auto* value = std::get_if<std::string>(&found->second)) return *value;
    return {};
}

void RecordActionRangeSample(const std::string& actionId, double distance, bool hit) {
    if (actionId.empty() || distance < 0.0) return;
    const auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) return;
    const auto path = ActionProfilePath(projectRoot);
    std::ifstream input(path);
    auto profile = nlohmann::json::parse(input, nullptr, false);
    if (profile.is_discarded() || !profile.is_object() ||
        !profile.contains("actions") || !profile["actions"].is_array()) return;
    for (auto& action : profile["actions"]) {
        if (!action.is_object() || action.value("actionId", "") != actionId) continue;
        if (!action.contains("properties") || !action["properties"].is_object())
            action["properties"] = nlohmann::json::object();
        auto& properties = action["properties"];
        properties["runtimeSamples"] = properties.value("runtimeSamples", 0) + 1;
        if (hit) {
            properties["runtimeHitSamples"] = properties.value("runtimeHitSamples", 0) + 1;
            properties["verifiedRangeMax"] = (std::max)(
                properties.value("verifiedRangeMax", 0.0), distance);
            properties["rangeConfidence"] = (std::max)(
                properties.value("rangeConfidence", 0.25), 0.85);
            properties["runtimeRangeVerified"] = true;
        } else {
            properties["runtimeMissSamples"] = properties.value("runtimeMissSamples", 0) + 1;
            const double previous = properties.value("nearestMissRange", -1.0);
            properties["nearestMissRange"] = previous < 0.0 ? distance : (std::min)(previous, distance);
        }
        break;
    }
    std::ofstream output(path, std::ios::trunc);
    if (output) output << profile.dump(2) << '\n';
}

bool PolicyConditionMatches(const LocalPolicyConfig::Condition& condition,
    const DebugPropertyMap& properties, bool ruleWasActive) {
    const auto found = properties.find(condition.property);
    if (found == properties.end()) return false;
    if (condition.operation == "equals" || condition.operation == "notEquals") {
        bool equal = false;
        if (condition.value.is_boolean()) {
            if (const auto* value = std::get_if<bool>(&found->second)) equal = *value == condition.value.get<bool>();
        } else if (condition.value.is_string()) {
            if (const auto* value = std::get_if<std::string>(&found->second)) equal = *value == condition.value.get<std::string>();
        } else if (condition.value.is_number()) {
            equal = std::abs(NumberProperty(properties, condition.property.c_str(),
                std::numeric_limits<double>::quiet_NaN()) - condition.value.get<double>()) < 0.0001;
        }
        return condition.operation == "equals" ? equal : !equal;
    }
    const double number = NumberProperty(properties, condition.property.c_str(),
        std::numeric_limits<double>::quiet_NaN());
    if (std::isnan(number)) return false;
    const double expected = condition.value.is_number() ? condition.value.get<double>() : 0.0;
    if (condition.operation == "greaterThan") return number > expected;
    if (condition.operation == "greaterOrEqual") return number >= expected;
    if (condition.operation == "lessThan") return number < expected;
    if (condition.operation == "lessOrEqual") return number <= expected;
    if (condition.operation == "hysteresisAbove")
        return ruleWasActive ? number > condition.exit : number > condition.enter;
    return false;
}

std::string ExecuteLocalContinuousStep(LocalPolicyState& policy, unsigned int intervalMilliseconds) {
    std::string response;
    if (!SendCommand("status", response)) return response;
    DebugProtocolMessage status;
    if (!DebugProtocolJson::TryParse(response, status) || !status.observation ||
        status.observation->availableActions.empty()) {
        return "Local continuous step: no available action.";
    }
    UpdateActionProfileFromRuntime(*status.observation);
    UpdateStateProfileFromRuntime(*status.observation);
    EnrichObservationWithActionHitEstimates(*status.observation);
    DebugObservation observation = ObservationForActor(
        *status.observation, ControlledActorMode::Player);
    if (observation.availableActions.empty()) {
        return "Local continuous step: no player action is available.";
    }
    const auto& actions = observation.availableActions;
    const auto findAction = [&](const std::string& id) -> const DebugGenericAction* {
        const auto found = std::find_if(actions.begin(), actions.end(),
            [&](const DebugGenericAction& action) { return action.actionId == id; });
        return found == actions.end() ? nullptr : &*found;
    };
    const auto firstAvailable = [&](std::initializer_list<const char*> ids) -> const DebugGenericAction* {
        for (const char* id : ids) if (const auto* action = findAction(id)) return action;
        return nullptr;
    };

    const bool threat = BoolProperty(observation.properties, "enemy.threat", false);
    const bool attackActive = BoolProperty(observation.properties, "enemy.attackActive", false);
    const bool canMove = BoolProperty(observation.properties, "player.canMove", true);
    const bool canJump = BoolProperty(observation.properties, "player.canJump", true);
    const bool canAttack = BoolProperty(observation.properties, "player.canAttack", true);
    const bool isAttacking = BoolProperty(observation.properties, "player.isAttacking", false);
    const double distance = NumberProperty(observation.properties, "enemy.distanceToPlayer", -1.0);
    const double enemyHp = NumberProperty(observation.properties, "enemy.hp", -1.0);
    const std::string nearestId = StringProperty(observation.properties, "enemy.nearestId");
    const auto& config = policy.config;
    policy.closeRangeHoldActive = NextCloseRangeHoldState(
        policy.closeRangeHoldActive, distance,
        config.closeRangeEnterDistance, config.closeRangeExitDistance);
    const auto isApproachAction = [&](const std::string& actionId) {
        if (actionId == config.approachAction) return true;
        const auto tags = config.actionTags.find(actionId);
        return tags != config.actionTags.end() &&
            tags->second.contains("movement.approach");
    };

    if (policy.pendingAttack.active) {
        if (isAttacking) policy.pendingAttack.sawAttackActive = true;
        const bool hit = enemyHp >= 0.0 && policy.pendingAttack.enemyHpBefore >= 0.0 &&
            enemyHp < policy.pendingAttack.enemyHpBefore;
        const bool finishedWithoutHit = !isAttacking && policy.pendingAttack.sawAttackActive;
        const bool timedOut = observation.frameNumber >= policy.pendingAttack.deadlineFrame;
        if (hit || finishedWithoutHit || timedOut) {
            RecordActionRangeSample(policy.pendingAttack.actionId,
                policy.pendingAttack.distance, hit);
            policy.pendingAttack = {};
        }
    }

    const DebugGenericAction* choice = nullptr;
    const LocalPolicyConfig::Rule* selectedRule = nullptr;
    bool selectedRecovery = false;
    bool closeRangeHoldChoice = false;
    std::string reason;
    for (const auto& rule : config.rules) {
        const bool wasActive = policy.ruleActive[rule.id];
        std::size_t matchedConditions = 0;
        for (const auto& condition : rule.conditions) {
            if (PolicyConditionMatches(condition, observation.properties, wasActive)) ++matchedConditions;
        }
        const bool matched = rule.conditionMode == "any"
            ? matchedConditions > 0
            : matchedConditions == rule.conditions.size();
        policy.ruleActive[rule.id] = matched;
        if (!matched || selectedRule) continue;
        const bool approachRule =
            std::find(rule.actionTags.begin(), rule.actionTags.end(),
                "movement.approach") != rule.actionTags.end() ||
            std::any_of(rule.actionIds.begin(), rule.actionIds.end(),
                [&](const std::string& actionId) { return isApproachAction(actionId); });
        if (policy.closeRangeHoldActive && approachRule) {
            policy.ruleActive[rule.id] = false;
            continue;
        }

        std::vector<const DebugGenericAction*> candidates;
        for (const auto& actionId : rule.actionIds) {
            if (const auto* action = findAction(actionId)) {
                if (GuessActionCategory(action->actionId) != "attack" || ActionCanHitTarget(*action))
                    candidates.push_back(action);
            }
        }
        if (candidates.empty() && !rule.actionTags.empty()) {
            for (const auto& action : actions) {
                const auto tags = config.actionTags.find(action.actionId);
                if (tags == config.actionTags.end()) continue;
                const bool hasTag = std::any_of(rule.actionTags.begin(), rule.actionTags.end(),
                    [&](const std::string& tag) { return tags->second.contains(tag); });
                if (hasTag && (GuessActionCategory(action.actionId) != "attack" || ActionCanHitTarget(action)))
                    candidates.push_back(&action);
            }
        }
        if (candidates.empty()) continue;

        if (rule.maxConsecutive > 0 &&
            policy.consecutiveRuleExecutions[rule.id] >= rule.maxConsecutive && canMove) {
            std::vector<const DebugGenericAction*> recoveryCandidates;
            for (const auto& action : actions) {
                const auto tags = config.actionTags.find(action.actionId);
                if (tags == config.actionTags.end()) continue;
                const bool hasRecoveryTag = std::any_of(rule.recoveryActionTags.begin(),
                    rule.recoveryActionTags.end(),
                    [&](const std::string& tag) { return tags->second.contains(tag); });
                if (hasRecoveryTag) recoveryCandidates.push_back(&action);
            }
            if (!recoveryCandidates.empty()) {
                choice = recoveryCandidates.front();
                selectedRule = &rule;
                selectedRecovery = true;
                reason = "maximum consecutive actions reached; adjusting spacing";
                break;
            }
        }
        choice = candidates.front();
        if (rule.selection == "leastUsed") {
            for (const auto* candidate : candidates) {
                if (policy.useCount[candidate->actionId] < policy.useCount[choice->actionId]) choice = candidate;
            }
        }
        selectedRule = &rule;
        reason = "generic rule " + rule.id + " matched";
    }

    if (!choice && !canMove && !canJump && !canAttack) {
        choice = findAction(config.idleAction);
        if (!choice) choice = firstAvailable({ "Wait", "Guard" });
        reason = "player cannot currently act";
    } else if (!choice && (threat || attackActive)) {
        for (const auto& actionId : config.threatActions) {
            if (!canJump && (actionId == "DodgeAway" || actionId == "Jump")) continue;
            if (!canMove && (actionId == "Retreat" || actionId == "Move")) continue;
            if (const auto* action = findAction(actionId)) { choice = action; break; }
        }
        if (!choice) choice = firstAvailable({ "Guard", "Wait" });
        reason = attackActive ? "enemy attack is active" : "incoming enemy threat";
    } else if (!choice && policy.closeRangeHoldActive) {
        if (canAttack && !isAttacking) {
            for (const auto& action : actions) {
                const bool listed = config.attackActions.empty()
                    ? GuessActionCategory(action.actionId) == "attack"
                    : std::find(config.attackActions.begin(), config.attackActions.end(),
                        action.actionId) != config.attackActions.end();
                if (!listed) continue;
                if (!choice || !config.preferLeastUsedAttack ||
                    policy.useCount[action.actionId] < policy.useCount[choice->actionId]) {
                    choice = &action;
                }
                if (choice && !config.preferLeastUsedAttack) break;
            }
        }
        if (!choice) choice = firstAvailable({ "Wait", "Guard" });
        closeRangeHoldChoice = choice != nullptr;
        reason = choice && GuessActionCategory(choice->actionId) == "attack"
            ? "contact range; attacking without additional approach movement"
            : "contact range; holding position until spacing is stable";
    } else if (!choice && canAttack && !isAttacking && (distance < 0.0 || distance <= config.attackDistance)) {
        for (const auto& action : actions) {
            const bool listed = config.attackActions.empty()
                ? GuessActionCategory(action.actionId) == "attack"
                : std::find(config.attackActions.begin(), config.attackActions.end(), action.actionId) != config.attackActions.end();
            if (!listed || !ActionCanHitTarget(action)) continue;
            if (!choice || !config.preferLeastUsedAttack ||
                policy.useCount[action.actionId] < policy.useCount[choice->actionId]) choice = &action;
            if (choice && !config.preferLeastUsedAttack) break;
        }
        reason = "safe attack range; selecting least-used attack";
    } else if (!choice && canMove && !policy.closeRangeHoldActive &&
        distance >= config.closeRangeExitDistance) {
        choice = findAction(config.approachAction);
        if (!choice) choice = firstAvailable({ "Move" });
        reason = "enemy is outside attack range; approaching target";
    }
    if (!choice && canMove && !policy.closeRangeHoldActive) {
        choice = findAction(config.approachAction);
        if (!choice) choice = FindAvailableActionByProfileTag(observation, "movement.approach");
        if (choice) reason = "no attack can currently hit; adjusting range or facing";
    }
    if (!choice && canAttack && !isAttacking) {
        for (const auto& action : actions) {
            if (GuessActionCategory(action.actionId) == "attack" && ActionCanHitTarget(action)) {
                choice = &action;
                break;
            }
        }
        reason = "using an available attack";
    }
    if (!choice && policy.closeRangeHoldActive) {
        return "Generic Local Policy\r\nAction: none (holding position)"
            "\r\nReason: contact range has no safe idle or attack Action"
            "\r\nDistance: " + std::to_string(distance) +
            "\r\nAPI call: skipped\r\n\r\n" + FormatProtocolResponse(response);
    }
    if (!choice) {
        choice = firstAvailable({ "Wait", "Guard" });
        if (!choice) choice = &actions[policy.fallbackIndex++ % actions.size()];
        reason = "safe fallback";
    }

    const bool repeatWhileMatched = selectedRule
        ? selectedRule->repeatWhileMatched
        : choice->actionId == config.approachAction;
    if (observation.frameNumber < policy.actionLockUntilFrame) {
        const bool mayInterrupt = selectedRule && selectedRule->interruptCurrent &&
            selectedRule->priority > policy.activePriority;
        const bool mayRefresh = repeatWhileMatched &&
            choice->actionId == policy.activeActionId &&
            (!selectedRule || selectedRule->id == policy.activeRuleId);
        const bool mayStopApproach = policy.closeRangeHoldActive &&
            isApproachAction(policy.activeActionId);
        if (!mayInterrupt && !mayRefresh && !mayStopApproach) {
            return "Generic Local Policy\r\nAction lock: " + policy.activeRuleId +
                " continues until frame " + std::to_string(policy.actionLockUntilFrame) +
                "\r\nCurrent frame: " + std::to_string(observation.frameNumber) +
                "\r\nAPI call: skipped\r\n\r\n" + FormatProtocolResponse(response);
        }
    }

    DebugGenericAction selected = *choice;
    selected.parameters[DebugActionParameter::ActorId] = std::string("player");
    selected.parameters[DebugActionParameter::Source] = std::string("Local");
    ++policy.useCount[selected.actionId];
    std::int64_t selectedDuration = 1;
    if (selectedRule) {
        selectedDuration = selectedRecovery
            ? selectedRule->recoveryDurationFrames
            : selectedRule->durationFrames;
        selected.parameters[DebugActionParameter::DurationFrames] = selectedDuration;
        selected.parameters[DebugActionParameter::TargetId] = nearestId;
        if (selectedRule->hasDirection) {
            selected.parameters[DebugActionParameter::Direction] = selectedRule->direction;
            selected.parameters[DebugActionParameter::CoordinateSpace] = selectedRule->coordinateSpace;
        }
    } else if (closeRangeHoldChoice) {
        const bool attackChoice = GuessActionCategory(selected.actionId) == "attack";
        selectedDuration = attackChoice
            ? config.attackDurationFrames : config.closeRangeWaitFrames;
        selected.parameters[DebugActionParameter::DurationFrames] = selectedDuration;
        selected.parameters[DebugActionParameter::TargetId] = nearestId;
    } else if (selected.actionId == config.approachAction ||
        (config.actionTags.contains(selected.actionId) &&
         config.actionTags.at(selected.actionId).contains("movement.approach"))) {
        selected.parameters[DebugActionParameter::Direction] = DebugVec3{ 0.0, 0.0, 1.0 };
        selected.parameters[DebugActionParameter::CoordinateSpace] =
            std::string(DebugCoordinateSpace::TargetRelative);
        selected.parameters[DebugActionParameter::TargetId] = nearestId;
        selected.parameters[DebugActionParameter::DurationFrames] = config.approachDurationFrames;
        selectedDuration = config.approachDurationFrames;
    } else if (std::find(config.threatActions.begin(), config.threatActions.end(), selected.actionId) !=
        config.threatActions.end()) {
        selected.parameters[DebugActionParameter::TargetId] = nearestId;
        selected.parameters[DebugActionParameter::DurationFrames] = config.evadeDurationFrames;
        selectedDuration = config.evadeDurationFrames;
    } else if (GuessActionCategory(selected.actionId) == "attack") {
        selected.parameters[DebugActionParameter::TargetId] = nearestId;
        selected.parameters[DebugActionParameter::DurationFrames] = config.attackDurationFrames;
        selectedDuration = config.attackDurationFrames;
    }
    if (repeatWhileMatched) {
        // Keep a continuous control active until the next decision can renew it.
        // The margin also covers pipe/status latency and variable frame times.
        const double observedFps = std::clamp(NumberProperty(observation.properties, "fps", 60.0), 10.0, 360.0);
        const auto decisionFrames = static_cast<std::int64_t>(
            std::ceil(observedFps * static_cast<double>(intervalMilliseconds) / 1000.0)) + 4;
        selectedDuration = std::clamp<std::int64_t>((std::max)(selectedDuration, decisionFrames), 1, 600);
        selected.parameters[DebugActionParameter::DurationFrames] = selectedDuration;
    }
    DebugProtocolMessage request = MakeRequest("execute_action");
    request.messageType = DebugProtocolMessageType::ExecuteAction;
    request.action = selected;
    if (!SendProtocolMessage(request, response)) return response;
    if (GuessActionCategory(selected.actionId) == "attack" && !policy.pendingAttack.active) {
        policy.pendingAttack.active = true;
        policy.pendingAttack.actionId = selected.actionId;
        policy.pendingAttack.distance = distance;
        policy.pendingAttack.enemyHpBefore = enemyHp;
        policy.pendingAttack.deadlineFrame = observation.frameNumber + 120;
    }
    if (selectedRule && selectedRule->maxConsecutive > 0) {
        if (selectedRecovery) {
            policy.consecutiveRuleExecutions[selectedRule->id] = 0;
        } else {
            ++policy.consecutiveRuleExecutions[selectedRule->id];
        }
    }
    policy.actionLockUntilFrame = observation.frameNumber + static_cast<std::uint64_t>(selectedDuration);
    policy.activePriority = selectedRule ? selectedRule->priority : 0;
    policy.activeRuleId = selectedRule ? selectedRule->id : "compatibility_fallback";
    policy.activeActionId = selected.actionId;
    return "Generic Local Policy\r\nAction: " + selected.actionId +
        "\r\nReason: " + reason +
        "\r\nThreat: " + (threat ? "true" : "false") +
        "  Attack active: " + (attackActive ? "true" : "false") +
        "  Distance: " + std::to_string(distance) +
        "  Close hold: " + (policy.closeRangeHoldActive ? "true" : "false") +
        "\r\nPolicy source: " + (config.loadedFromFile ? "AI-generated local_policy.json" : "built-in default") +
        "\r\nAPI call: skipped\r\n\r\n" + FormatProtocolResponse(response);
}

struct BossLocalPolicyState {
    std::map<std::string, std::size_t> useCount;
    std::uint64_t nextDecisionFrame = 0;
    std::string activeAction;
};

std::string ExecuteBossLocalContinuousStep(BossLocalPolicyState& policy) {
    std::string response;
    if (!SendCommand("status", response)) return response;
    DebugProtocolMessage status;
    if (!DebugProtocolJson::TryParse(response, status) || !status.observation) {
        return "Boss Local Policy: no game observation was received.";
    }
    DebugObservation observation = ObservationForActor(
        *status.observation, ControlledActorMode::Boss);
    if (observation.availableActions.empty()) {
        return "Boss Local Policy: no boss action is available in the current scene or phase.";
    }
    if (observation.frameNumber < policy.nextDecisionFrame) {
        return "Boss Local Policy\r\nAction: " + policy.activeAction +
            " (native sequence continues)\r\nNext decision frame: " +
            std::to_string(policy.nextDecisionFrame) + "\r\nAPI call: skipped";
    }

    const DebugGenericAction* choice = &observation.availableActions.front();
    for (const auto& candidate : observation.availableActions) {
        if (policy.useCount[candidate.actionId] < policy.useCount[choice->actionId]) {
            choice = &candidate;
        }
    }
    DebugGenericAction selected = *choice;
    selected.parameters[DebugActionParameter::Source] = std::string("Local");
    selected.parameters[DebugActionParameter::DurationFrames] = static_cast<std::int64_t>(1);

    DebugProtocolMessage request = MakeRequest("execute_action");
    request.messageType = DebugProtocolMessageType::ExecuteAction;
    request.action = selected;
    if (!SendProtocolMessage(request, response)) return response;

    ++policy.useCount[selected.actionId];
    policy.activeAction = selected.actionId;
    // Let the boss's native state sequence play out before selecting a new
    // high-level move. This is based on observation frames, not engine time.
    policy.nextDecisionFrame = observation.frameNumber + 90;
    return "Boss Local Policy\r\nActor: " + GenericActionActorId(selected) +
        "\r\nAction: " + selected.actionId +
        "\r\nReason: least-used boss action; native sequence runs for 90 frames" +
        "\r\nAPI call: skipped\r\n\r\n" + FormatProtocolResponse(response);
}

std::string ExecuteLocalActors(
    LocalPolicyState& playerPolicy,
    BossLocalPolicyState& bossPolicy,
    ControlledActorMode mode,
    unsigned int intervalMilliseconds) {
    if (mode == ControlledActorMode::Boss) return ExecuteBossLocalContinuousStep(bossPolicy);
    if (mode == ControlledActorMode::Both) {
        return ExecuteLocalContinuousStep(playerPolicy, intervalMilliseconds) +
            "\r\n\r\n---------------- Actor: Boss ----------------\r\n" +
            ExecuteBossLocalContinuousStep(bossPolicy);
    }
    return ExecuteLocalContinuousStep(playerPolicy, intervalMilliseconds);
}

void PostAIStatus(HWND window, std::string text, bool preserveResult = false) {
    {
        std::lock_guard lock(gAIStatusMutex);
        gPendingAIStatus = std::move(text);
    }
    PostMessageW(window, kAIStatusMessage, preserveResult ? 1 : 0, 0);
}

unsigned int ReadIntervalMilliseconds(HWND control, unsigned int fallback) {
    if (!control) return fallback;
    wchar_t text[32]{};
    GetWindowTextW(control, text, static_cast<int>(std::size(text)));
    wchar_t* end = nullptr;
    const unsigned long value = wcstoul(text, &end, 10);
    return end == text ? fallback
        : static_cast<unsigned int>(std::clamp(value, 60ul, 5000ul));
}

ControlledActorMode ReadControlledActorMode() {
    if (!gAIActorMode) return ControlledActorMode::Player;
    const LRESULT selection = SendMessageW(gAIActorMode, CB_GETCURSEL, 0, 0);
    if (selection == 1) return ControlledActorMode::Boss;
    if (selection == 2) return ControlledActorMode::Both;
    return ControlledActorMode::Player;
}

void ApplyViewerGoal() {
    const int length = GetWindowTextLengthW(gAIGoal);
    if (length <= 0) return;
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(gAIGoal, text.data(), length + 1);
    text.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0u);
    gAIProvider.SetGoal(WideToUtf8(text));
}

enum class AIWorkerMode { ApiStep, ApiContinuous, LocalContinuous };

bool LocalAIRuntimeReady(
    ControlledActorMode actorMode,
    std::string& waitingMessage) {
    std::string response;
    if (!SendCommand("status", response)) {
        waitingMessage = "ローカルAI待機中\r\nゲームへの接続を待っています。\r\n"
            "停止ボタンを押すまで自動で再確認します。";
        return false;
    }
    DebugProtocolMessage status;
    if (!DebugProtocolJson::TryParse(response, status) || !status.observation) {
        waitingMessage = "ローカルAI待機中\r\n"
            "現在のSceneはDebugObservationを提供していません。\r\n"
            "対応するゲームSceneへ入ると自動的に操作を開始します。";
        return false;
    }
    const DebugObservation actorObservation =
        ObservationForActor(*status.observation, actorMode);
    if (actorObservation.availableActions.empty()) {
        waitingMessage = "ローカルAI待機中\r\nScene: " +
            status.observation->sceneId +
            "\r\n現在のSceneまたはPhaseで使用可能なActionを待っています。";
        return false;
    }
    waitingMessage.clear();
    return true;
}

void StartAIWorker(HWND window, AIWorkerMode mode) {
    if (gAIWorkerRunning.exchange(true)) {
        SetWindowTextW(gStatusText, L"AI is already running.");
        return;
    }
    // Ignore a status request that may already be in flight. Its stale
    // disconnected/connected text must not overwrite AI progress.
    gAutoRefreshResumeTick = GetTickCount64() + 60000;
    if (gAIWorker.joinable()) gAIWorker.join();
    const bool continuous = mode != AIWorkerMode::ApiStep;
    const bool useApi = mode != AIWorkerMode::LocalContinuous;
    if (useApi) ApplyViewerGoal();
    gAIStopRequested = false;
    const unsigned int apiInterval = ReadIntervalMilliseconds(gAIInterval, 2000);
    const unsigned int localInterval = ReadIntervalMilliseconds(gLocalAIInterval, 250);
    const unsigned int interval = mode == AIWorkerMode::LocalContinuous
        ? localInterval : apiInterval;
    const ControlledActorMode actorMode = ReadControlledActorMode();
    const std::filesystem::path projectRoot = gProjectFolder
        ? std::filesystem::path(ReadWindowText(gProjectFolder))
        : ConfiguredProjectRoot();
    const std::wstring scanTargets = gScanTargets
        ? ReadWindowText(gScanTargets) : LoadScanTargets();
    const bool visionEnabled = gAIVisionEnabled &&
        SendMessageW(gAIVisionEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
    SaveWorkspaceSettings(
        projectRoot,
        scanTargets);
    SetWindowTextW(gStatusText, mode == AIWorkerMode::ApiStep ? L"APIステップを開始しています..."
        : (mode == AIWorkerMode::ApiContinuous ? L"API連続実行を開始しています..." :
            L"ローカルAIを開始しています。対応Sceneまでは待機します..."));
    gAIWorker = std::thread([
        window, mode, continuous, useApi, interval, localInterval, actorMode,
        projectRoot, visionEnabled] {
        constexpr auto kRateLimitCooldown = std::chrono::seconds(60);
        LocalPolicyState localPolicy;
        localPolicy.config = LoadLocalPolicyConfig();
        BossLocalPolicyState bossLocalPolicy;
        bool bothBossTurn = false;
        auto apiCooldownUntil = std::chrono::steady_clock::time_point{};
        do {
            const auto now = std::chrono::steady_clock::now();
            unsigned int waitMilliseconds = interval;
            if (useApi && continuous && now < apiCooldownUntil) {
                waitMilliseconds = localInterval;
                const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
                    apiCooldownUntil - now).count() + 1;
                PostAIStatus(window, "API: rate-limit cooldown (" +
                    std::to_string(remaining) + "s remaining).\r\nAPI call: skipped\r\n\r\n" +
                    ExecuteLocalActors(localPolicy, bossLocalPolicy, actorMode, localInterval), true);
            } else if (useApi) {
                bool apiExecuted = false;
                ControlledActorMode decisionActor = actorMode;
                if (actorMode == ControlledActorMode::Both) {
                    decisionActor = bothBossTurn
                        ? ControlledActorMode::Boss : ControlledActorMode::Player;
                    bothBossTurn = !bothBossTurn;
                }
                const std::string apiResult = ExecuteAIStepCore(
                    window,
                    decisionActor,
                    projectRoot,
                    visionEnabled,
                    &apiExecuted);
                if (continuous && !apiExecuted && !gAIStopRequested) {
                    const bool rateLimited = apiResult.find("HTTP error 429") != std::string::npos ||
                        apiResult.find("RESOURCE_EXHAUSTED") != std::string::npos;
                    if (rateLimited) {
                        apiCooldownUntil = std::chrono::steady_clock::now() + kRateLimitCooldown;
                        waitMilliseconds = localInterval;
                    }
                    PostAIStatus(window, "API decision failed; using one local fallback.\r\n\r\n" +
                        (rateLimited ? std::string("Rate limit detected. API calls paused for 60 seconds.\r\n") : std::string{}) +
                        apiResult + "\r\n\r\n" + ExecuteLocalActors(
                            localPolicy, bossLocalPolicy, decisionActor, localInterval), true);
                } else {
                    PostAIStatus(window, apiResult, true);
                }
            } else {
                std::string waitingMessage;
                if (!LocalAIRuntimeReady(actorMode, waitingMessage)) {
                    PostAIStatus(window, std::move(waitingMessage), true);
                } else {
                    PostAIStatus(window, ExecuteLocalActors(
                        localPolicy, bossLocalPolicy, actorMode, interval), true);
                }
            }
            if (!continuous || gAIStopRequested) break;
            std::unique_lock lock(gAIWaitMutex);
            gAIWaitCondition.wait_for(lock, std::chrono::milliseconds(waitMilliseconds),
                [] { return gAIStopRequested.load(); });
        } while (!gAIStopRequested);
        gAIWorkerRunning = false;
        if (continuous && gAIStopRequested) PostAIStatus(window,
            mode == AIWorkerMode::LocalContinuous ?
                "ローカルAIを停止しました。" : "API連続実行を停止しました。", true);
    });
}

void StopAIWorker() {
    gAIStopRequested = true;
    gAIWaitCondition.notify_all();
}

ControlledActorMode ScenarioActorMode(const std::string& actor) {
    if (actor == "Boss") return ControlledActorMode::Boss;
    if (actor == "Both") return ControlledActorMode::Both;
    return ControlledActorMode::Player;
}

void StartScenarioWorker(HWND window, bool runAll) {
    const LRESULT selected = gScenarioList
        ? SendMessageW(gScenarioList, CB_GETCURSEL, 0, 0) : CB_ERR;
    if ((!runAll && selected == CB_ERR) ||
        gScenarioEntries.empty() ||
        (!runAll &&
            static_cast<std::size_t>(selected) >= gScenarioEntries.size())) {
        SetWindowTextW(gStatusText,
            L"Select a scenario first. Use Reload after adding a JSON file.");
        return;
    }
    if (gAIWorkerRunning.exchange(true)) {
        SetWindowTextW(gStatusText,
            L"Another AI, scenario, or Viewer operation is already running.");
        return;
    }
    if (gAIWorker.joinable()) gAIWorker.join();
    const auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) {
        gAIWorkerRunning = false;
        SetWindowTextW(gStatusText,
            L"Scenario start failed: select a Game Project Folder first.");
        return;
    }
    std::vector<ScenarioListEntry> scenarios;
    if (runAll) {
        scenarios = gScenarioEntries;
    } else {
        scenarios.push_back(gScenarioEntries[static_cast<std::size_t>(selected)]);
    }
    const unsigned int localInterval =
        ReadIntervalMilliseconds(gLocalAIInterval, 250);
    gAIStopRequested = false;
    gAutoRefreshResumeTick = GetTickCount64() + 60000;
    SetWindowTextW(gStatusText,
        runAll ? L"Capturing the batch baseline..." : L"Scenario started...");
    gAIWorker = std::thread([
        window, localInterval, projectRoot, scenarios = std::move(scenarios), runAll] {
        const auto batchStartedAt = std::chrono::steady_clock::now();
        std::map<std::string, DebugObservation> sceneBaselines;
        std::vector<ScenarioBatchItemResult> batchItems;
        std::string lastSingleResult;
        for (std::size_t scenarioIndex = 0;
            scenarioIndex < scenarios.size() && !gAIStopRequested;
            ++scenarioIndex) {
            const auto& scenario = scenarios[scenarioIndex];
            ScenarioBatchItemResult item;
            item.scenarioPath = scenario.path;
            item.label = WideToUtf8(scenario.label);
            const auto scenarioStartedAt = std::chrono::steady_clock::now();

            std::string error;
            if (!gScenarioRunner.Load(
                scenario.path,
                ActionProfilePath(projectRoot),
                projectRoot / "generated/debug_ai/scenarios/results",
                error)) {
                item.status = "load_failed";
                item.detail = error;
                item.elapsedSeconds = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - scenarioStartedAt).count();
                batchItems.push_back(std::move(item));
                PostAIStatus(window,
                    "Scenario could not start: " + error, true);
                continue;
            }

            const std::string targetSceneId = gScenarioRunner.TargetSceneId();
            PostAIStatus(window,
                (runAll
                    ? "Scenario batch " + std::to_string(scenarioIndex + 1) + "/" +
                        std::to_string(scenarios.size()) + "\r\n"
                    : std::string{}) +
                "Preparing scene: " +
                (targetSceneId.empty() ? std::string("current") : targetSceneId), true);
            if (!EnsureScenarioSceneLoaded(targetSceneId, error)) {
                item.status = "scene_load_failed";
                item.detail = error;
                item.elapsedSeconds = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - scenarioStartedAt).count();
                batchItems.push_back(std::move(item));
                continue;
            }

            if (runAll) {
                const std::string baselineKey = targetSceneId.empty()
                    ? std::string("<current>") : targetSceneId;
                auto baseline = sceneBaselines.find(baselineKey);
                if (baseline == sceneBaselines.end()) {
                    DebugObservation captured;
                    if (!CaptureScenarioBaseline(captured, error)) {
                        item.status = "baseline_failed";
                        item.detail = error;
                        item.elapsedSeconds = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - scenarioStartedAt).count();
                        batchItems.push_back(std::move(item));
                        continue;
                    }
                    baseline = sceneBaselines.emplace(baselineKey, std::move(captured)).first;
                }
                if (!RestoreScenarioBaseline(baseline->second, error)) {
                    item.status = "restore_failed";
                    item.detail = error;
                    item.elapsedSeconds = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - scenarioStartedAt).count();
                    batchItems.push_back(std::move(item));
                    continue;
                }
            }

            std::string anomalyResetResponse;
            if (!SendCheckedCommand("reset_anomalies", anomalyResetResponse)) {
                item.status = "anomaly_reset_failed";
                item.detail = anomalyResetResponse;
                item.elapsedSeconds = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - scenarioStartedAt).count();
                batchItems.push_back(std::move(item));
                continue;
            }

            if (!gScenarioRunner.Start(error)) {
                item.status = "start_failed";
                item.detail = error;
                item.elapsedSeconds = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - scenarioStartedAt).count();
                batchItems.push_back(std::move(item));
                continue;
            }

            LocalPolicyState playerPolicy;
            playerPolicy.config = LoadLocalPolicyConfig();
            BossLocalPolicyState bossPolicy;
            const ControlledActorMode actorMode =
                ScenarioActorMode(gScenarioRunner.ActorMode());
            const bool autoRecord = gScenarioRunner.AutoRecord();
            std::string replayStart;
            bool scenarioRecordingStarted = false;
            if (autoRecord) {
                replayStart = StartRecordingWithCoverageCore();
                scenarioRecordingStarted =
                    replayStart.find("Recording: true") != std::string::npos ||
                    replayStart.find("recording started") != std::string::npos;
            }
            while (gScenarioRunner.IsRunning() && !gAIStopRequested) {
                const std::string step = ExecuteLocalActors(
                    playerPolicy, bossPolicy, actorMode, localInterval);
                const std::string evidenceStatus = CapturePendingScenarioEvidence(
                    projectRoot, scenario.path, window);
                const bool actionUnavailable =
                    step.find("no available action") != std::string::npos ||
                    step.find("no player action is available") != std::string::npos ||
                    step.find("no boss action is available") != std::string::npos ||
                    step.find("no game observation") != std::string::npos;
                if (actionUnavailable && !targetSceneId.empty()) {
                    std::string sceneResponse;
                    DebugProtocolMessage sceneStatus;
                    if (!SendCommand("status", sceneResponse) ||
                        !DebugProtocolJson::TryParse(sceneResponse, sceneStatus) ||
                        !sceneStatus.observation ||
                        sceneStatus.observation->sceneId != targetSceneId) {
                        gScenarioRunner.Fail(
                            "Target scene ended or changed before all goals passed: " +
                            targetSceneId);
                    }
                }
                const std::string batchPrefix = runAll
                    ? "Scenario batch " + std::to_string(scenarioIndex + 1) + "/" +
                        std::to_string(scenarios.size()) + "\r\n"
                    : std::string{};
                PostAIStatus(window,
                    batchPrefix + gScenarioRunner.FormatProgress() +
                    "\r\nLast local decision:\r\n" + step +
                    (evidenceStatus.empty()
                        ? std::string{}
                        : "\r\n\r\n" + evidenceStatus), true);
                if (!gScenarioRunner.IsRunning() || gAIStopRequested) break;
                std::unique_lock lock(gAIWaitMutex);
                gAIWaitCondition.wait_for(
                    lock, std::chrono::milliseconds(localInterval),
                    [] { return gAIStopRequested.load(); });
            }
            if (gAIStopRequested) gScenarioRunner.RequestStop();
            if (!gAIStopRequested) {
                CapturePendingScenarioEvidence(projectRoot, scenario.path, window);
            }
            const std::string replayStop = scenarioRecordingStarted
                ? StopRecordingWithCoverageCore()
                : (autoRecord ? replayStart : std::string("Automatic replay recording disabled."));
            // The recording stop response includes the final gameplay
            // observation. Freeze its anomaly counts before replay playback so
            // the same recorded warning is not counted a second time.
            gScenarioRunner.FinishExecutionObservation();
            AutomaticReplayVerificationResult verification;
            if (gAIStopRequested) {
                verification.status = "stopped";
                verification.detail = "verification was not started because the batch was stopped";
            } else if (!gScenarioRunner.VerifyReplay()) {
                verification.status = "disabled";
            } else if (!scenarioRecordingStarted) {
                verification.status = "unavailable";
                verification.detail = "automatic replay recording did not start";
            } else {
                const auto manifestPath = ResolveProjectOutputPath(
                    FormattedLineValue(replayStop, "Replay manifest:"));
                PostAIStatus(window,
                    (runAll
                        ? "Scenario batch " + std::to_string(scenarioIndex + 1) + "/" +
                            std::to_string(scenarios.size()) + "\r\n"
                        : std::string{}) +
                    "Starting automatic replay verification...", true);
                verification = VerifyReplayAutomatically(
                    window, manifestPath,
                    gScenarioRunner.ReplayVerificationTimeoutSeconds());
            }
            gScenarioRunner.RecordReplayVerification(
                verification.status,
                verification.checked,
                verification.checkpoints,
                verification.mismatches,
                verification.detail);
            if (!gAIStopRequested) {
                CapturePendingScenarioEvidence(projectRoot, scenario.path, window);
            }
            item.status = ScenarioStatusName(gScenarioRunner.CurrentStatus());
            item.detail = gScenarioRunner.FailureReason();
            item.anomalyCount = gScenarioRunner.AnomalyCount();
            item.anomalyErrorCount = gScenarioRunner.AnomalyErrorCount();
            item.replayVerificationStatus = verification.status;
            item.replayVerificationChecked = verification.checked;
            item.replayVerificationCheckpoints = verification.checkpoints;
            item.replayVerificationMismatches = verification.mismatches;
            item.replayVerificationDetail = verification.detail;
            std::ostringstream replayVerificationSummary;
            replayVerificationSummary << replayStop
                << "\r\nAutomatic replay verification: " << verification.status;
            if (verification.checkpoints > 0) {
                replayVerificationSummary << " checked " << verification.checked << '/'
                    << verification.checkpoints << " mismatches "
                    << verification.mismatches;
            }
            if (!verification.detail.empty()) {
                replayVerificationSummary << "\r\nVerification detail: "
                    << verification.detail;
            }
            const std::string replaySummary = replayVerificationSummary.str();
            const std::string result = gScenarioRunner.Finalize(replaySummary);
            item.resultPath = gScenarioRunner.ResultPath();
            item.elapsedSeconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - scenarioStartedAt).count();
            batchItems.push_back(std::move(item));
            lastSingleResult = result + "\r\n\r\n" +
                gScenarioRunner.FormatProgress() +
                "\r\nReplay / Coverage:\r\n" + replaySummary;
        }

        if (runAll) {
            const double elapsedSeconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - batchStartedAt).count();
            const auto resultPath = SaveScenarioBatchResult(
                projectRoot, batchItems, gAIStopRequested, elapsedSeconds);
            std::string status = FormatScenarioBatchResult(
                batchItems, resultPath, gAIStopRequested, elapsedSeconds);
            try {
                const auto report =
                    TestReportGenerator::Generate(projectRoot, resultPath);
                status += "\r\n\r\n" + report.message;
            } catch (const std::exception& exception) {
                status += "\r\n\r\nTest report generation failed safely.\r\nError: ";
                status += exception.what();
            } catch (...) {
                status += "\r\n\r\nTest report generation failed safely: unknown error";
            }
            PostAIStatus(window, std::move(status), true);
        } else if (!lastSingleResult.empty()) {
            PostAIStatus(window, std::move(lastSingleResult), true);
        }
        PostMessageW(window, kReplayListRefreshMessage, 0, 0);
        gAIWorkerRunning = false;
    });
}

void StopScenarioWorker() {
    gScenarioRunner.RequestStop();
    StopAIWorker();
}

void StartOneShotWorker(HWND window, std::function<std::string()> task,
    const wchar_t* startingText = nullptr, bool preserveResult = true) {
    if (gAIWorkerRunning.exchange(true)) {
        SetWindowTextW(
            gStatusText,
            L"Another Viewer operation is still running. Wait for it to finish or press Stop.");
        return;
    }
    if (gAIWorker.joinable()) gAIWorker.join();
    gAIStopRequested = false;
    if (startingText) SetWindowTextW(gStatusText, startingText);
    gAIWorker = std::thread([window, task = std::move(task), preserveResult] {
        std::string result;
        try {
            result = task();
        } catch (const std::exception& exception) {
            result = std::string("Operation failed safely. Viewer remains open.\r\nError: ") +
                exception.what();
        } catch (...) {
            result = "Operation failed safely. Viewer remains open.\r\nError: unknown exception";
        }
        PostAIStatus(window, std::move(result), preserveResult);
        gAIWorkerRunning = false;
    });
}

void StartReplayWorker(
    HWND window,
    std::function<std::string()> task,
    const wchar_t* startingText) {
    if (gAIWorkerRunning) {
        gPendingReplayStart = std::move(task);
        StopAIWorker();
        SetWindowTextW(
            gStatusText,
            L"Stopping the active AI before starting replay...");
        SetTimer(window, kPendingReplayStartTimerId, 50, nullptr);
        return;
    }
    StartOneShotWorker(window, std::move(task), startingText);
}

void StartCommandWorker(HWND window, const char* command,
    const wchar_t* startingText = nullptr, bool preserveResult = true) {
    const std::string commandCopy = command;
    StartOneShotWorker(window, [commandCopy] { return ExecuteCommandCore(commandCopy.c_str()); },
        startingText, preserveResult);
}

void StartConnectionWorker(HWND window, const wchar_t* startingText = nullptr) {
    if (gConnectionWorkerRunning.exchange(true)) return;
    if (gConnectionWorker.joinable()) gConnectionWorker.join();
    if (startingText) SetWindowTextW(gStatusText, startingText);
    gConnectionWorker = std::thread([window] {
        std::string result;
        try {
            result = ExecuteCommandCore("status");
        } catch (const std::exception& exception) {
            result = std::string("Game connection check failed safely.\r\nError: ") +
                exception.what();
        } catch (...) {
            result = "Game connection check failed safely.\r\nError: unknown exception";
        }
        {
            std::lock_guard lock(gConnectionStatusMutex);
            gPendingConnectionStatus = result;
        }
        gConnectionStatusReady = true;
        gConnectionWorkerRunning = false;
    });
}

const wchar_t* ButtonTooltipText(int id) {
    switch (id) {
    case StartRecordingId:
        return L"操作とゲーム内イベントの記録を開始します。ゲームSceneへ移動すると本記録が始まります。";
    case StopRecordingId:
        return L"現在の記録を終了し、リプレイセッションとして保存します。";
    case PlayLatestId:
        return L"保存日時が最も新しいリプレイを最初から再生します。必要なら対象Sceneを自動で読み込みます。";
    case StopReplayId:
        return L"再生中またはScene読み込み待ちのリプレイを停止します。";
    case RefreshId:
        return L"ゲームとの接続状態、現在のScene、プレイヤーや敵の状態を再取得します。";
    case PlaySelectedReplayId:
        return L"左の一覧で選択したリプレイを最初から再生します。";
    case ShowReplayTimelineId:
        return L"選択したリプレイで発生したAction、被弾、状態変化などを時系列で表示します。";
    case ReloadReplayListId:
        return L"保存済みリプレイの一覧をディスクから読み直します。";
    case PauseReplayId:
        return L"リプレイ再生を現在のフレームで一時停止します。";
    case ResumeReplayId:
        return L"一時停止しているリプレイの再生を再開します。";
    case StepReplayId:
        return L"一時停止中のリプレイを1フレームだけ進めます。";
    case ApplyReplaySpeedId:
        return L"左で選択した再生速度を現在のリプレイへ適用します。検証には1xを推奨します。";
    case LocalStartId:
        return L"ローカルAIを連続実行します。未対応Sceneでは待機し、ゲームSceneへ移動すると自動で開始します。";
    case AIStopId:
        return L"実行中または待機中のローカルAIを停止します。";
    case ReloadScenarioListId:
        return L"利用可能なシナリオJSONの一覧をディスクから読み直します。";
    case StartScenarioId:
        return L"一覧で選択したシナリオテストを1件実行し、結果とリプレイを保存します。";
    case RunAllScenariosId:
        return L"登録されているすべてのシナリオテストを順番に実行し、レポートを生成します。";
    case StopScenarioId:
        return L"実行中のシナリオテストまたは一括実行を停止します。";
    case OpenProjectToolsId:
        return L"ゲームフォルダーの指定、ソース解析、各プロファイル生成を行う画面を開きます。";
    case OpenSemanticReviewId:
        return L"ソース解析で意味を確定できなかったActionや状態候補を確認・承認します。";
    case AdapterDiagnosticsId:
        return L"ゲーム側Adapter、通信、Observation、Actionなどの接続準備状況を診断します。";
    case CoverageSummaryId:
        return L"これまで確認できたAction、Scene、Phase、状態と未確認項目を表示します。";
    case OpenLatestReportId:
        return L"直近に生成されたシナリオテストのHTMLレポートを開きます。";
    case ResetCoverageId:
        return L"現在のカバレッジをアーカイブしてから集計を初期化し、新しい計測を開始します。";
    case BrowseProjectId:
        return L"解析対象となるゲームプロジェクトのフォルダーを選択します。";
    case ScanProjectId:
        return L"ソースと設定をローカル解析し、Action、状態、Sceneなどの候補を収集します。APIには送信しません。";
    case GenerateProfileId:
        return L"解析結果からActionの分類とパラメーター候補をまとめたプロファイルを生成します。";
    case GenerateStateProfileId:
        return L"解析結果からHP、Phase、敵の予兆などに使える状態プロパティ候補を生成します。";
    case GenerateLocalPolicyId:
        return L"設定済みAPIを呼び出し、AI目標と解析結果から繰り返し使えるローカルAI用の行動方針を生成します。";
    case SemanticReviewApproveId:
        return L"選択中の候補と分類を正しいものとしてプロファイルへ保存します。";
    case SemanticReviewIgnoreId:
        return L"選択中の候補を使用しないものとしてプロファイルへ保存します。";
    case SemanticReviewReloadId:
        return L"判断が必要な候補をプロファイルから読み直します。";
    default:
        return nullptr;
    }
}

HWND EnsureButtonTooltip(HWND parent) {
    if (!parent) return nullptr;
    if (const HANDLE value = GetPropW(parent, kTooltipProperty)) {
        return static_cast<HWND>(value);
    }
    HWND tooltip = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"STATIC",
        L"",
        WS_POPUP | WS_BORDER | SS_LEFT | SS_NOPREFIX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        430,
        40,
        parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (!tooltip) return nullptr;
    HWND textControl = CreateWindowW(
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
        8,
        6,
        414,
        26,
        tooltip,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (!textControl) {
        DestroyWindow(tooltip);
        return nullptr;
    }
    SendMessageW(textControl, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SetPropW(tooltip, kTooltipTextControlProperty, textControl);
    SetWindowPos(tooltip, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_HIDEWINDOW);
    if (!SetPropW(parent, kTooltipProperty, tooltip)) {
        DestroyWindow(tooltip);
        return nullptr;
    }
    return tooltip;
}

void AddButtonTooltip(HWND parent, HWND button, const wchar_t* text) {
    if (!button || !text || !*text) return;
    HWND tooltip = EnsureButtonTooltip(parent);
    if (!tooltip) return;
    SetPropW(button, kTooltipTextProperty,
        reinterpret_cast<HANDLE>(const_cast<wchar_t*>(text)));
    if (!SetWindowSubclass(button, [](HWND control, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) -> LRESULT {
        const auto activateTooltip = [control](bool activate) {
            HWND parent = GetParent(control);
            HWND tooltip = parent
                ? static_cast<HWND>(GetPropW(parent, kTooltipProperty))
                : nullptr;
            if (!tooltip) return;
            if (!activate) {
                ShowWindow(tooltip, SW_HIDE);
                return;
            }
            const auto* text = reinterpret_cast<const wchar_t*>(
                GetPropW(control, kTooltipTextProperty));
            if (!text || !*text) return;
            HWND textControl = static_cast<HWND>(
                GetPropW(tooltip, kTooltipTextControlProperty));
            if (!textControl) return;
            SetWindowTextW(textControl, text);

            RECT buttonRect{};
            GetWindowRect(control, &buttonRect);
            constexpr int tooltipWidth = 430;
            constexpr int horizontalPadding = 8;
            constexpr int topPadding = 6;
            constexpr int bottomPadding = 8;
            const int textWidth = tooltipWidth - horizontalPadding * 2;
            int textHeight = 20;
            if (HDC deviceContext = GetDC(textControl)) {
                const HFONT font = reinterpret_cast<HFONT>(
                    SendMessageW(textControl, WM_GETFONT, 0, 0));
                const HGDIOBJ previousFont = font
                    ? SelectObject(deviceContext, font) : nullptr;
                RECT measured{ 0, 0, textWidth, 0 };
                DrawTextW(deviceContext, text, -1, &measured,
                    DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
                textHeight = (std::max)(20,
                    static_cast<int>(measured.bottom - measured.top));
                if (previousFont) SelectObject(deviceContext, previousFont);
                ReleaseDC(textControl, deviceContext);
            }
            const int tooltipHeight = std::clamp(
                topPadding + textHeight + bottomPadding, 34, 180);
            SetWindowPos(textControl, nullptr,
                horizontalPadding, topPadding, textWidth, textHeight,
                SWP_NOZORDER | SWP_NOACTIVATE);
            int x = buttonRect.left;
            int y = buttonRect.bottom + 6;
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);
            const HMONITOR monitor = MonitorFromWindow(
                control, MONITOR_DEFAULTTONEAREST);
            if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
                const int workLeft = static_cast<int>(monitorInfo.rcWork.left);
                const int workTop = static_cast<int>(monitorInfo.rcWork.top);
                const int workRight = static_cast<int>(monitorInfo.rcWork.right);
                const int workBottom = static_cast<int>(monitorInfo.rcWork.bottom);
                x = std::clamp(x, workLeft,
                    (std::max)(workLeft, workRight - tooltipWidth));
                if (y + tooltipHeight > workBottom) {
                    y = buttonRect.top - tooltipHeight - 6;
                }
                y = (std::max)(y, workTop);
            }
            SetWindowPos(tooltip, HWND_TOPMOST, x, y,
                tooltipWidth, tooltipHeight,
                SWP_NOACTIVATE | SWP_SHOWWINDOW);
            RedrawWindow(tooltip, nullptr, nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        };
        switch (message) {
        case WM_MOUSEMOVE: {
            if (!GetPropW(control, kTooltipHoverStateProperty)) {
                SetPropW(control, kTooltipHoverStateProperty,
                    reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(1)));
                SetTimer(control, kButtonTooltipTimerId, 1000, nullptr);
            }
            TRACKMOUSEEVENT tracking{};
            tracking.cbSize = sizeof(tracking);
            tracking.dwFlags = TME_LEAVE;
            tracking.hwndTrack = control;
            TrackMouseEvent(&tracking);
            break;
        }
        case WM_TIMER:
            if (wParam != kButtonTooltipTimerId) break;
            KillTimer(control, kButtonTooltipTimerId);
            SetPropW(control, kTooltipHoverStateProperty,
                reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(2)));
            activateTooltip(true);
            return 0;
        case WM_MOUSELEAVE:
        case WM_LBUTTONDOWN:
            KillTimer(control, kButtonTooltipTimerId);
            RemovePropW(control, kTooltipHoverStateProperty);
            activateTooltip(false);
            break;
        case WM_NCDESTROY:
            KillTimer(control, kButtonTooltipTimerId);
            RemovePropW(control, kTooltipHoverStateProperty);
            RemovePropW(control, kTooltipTextProperty);
            activateTooltip(false);
            break;
        default:
            break;
        }
        return DefSubclassProc(control, message, wParam, lParam);
    }, 1, 0)) {
        RemovePropW(button, kTooltipTextProperty);
    }
}

void DestroyButtonTooltip(HWND parent) {
    HWND tooltip = static_cast<HWND>(RemovePropW(parent, kTooltipProperty));
    if (tooltip && IsWindow(tooltip)) DestroyWindow(tooltip);
}

HWND AddButton(HWND parent, const wchar_t* text, int id, int x, int y, int width) {
    HWND button = CreateWindowW(
        L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, width, 34,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),
        nullptr);
    AddButtonTooltip(parent, button, ButtonTooltipText(id));
    return button;
}

void AppendSemanticEvidence(
    std::vector<std::string>& destination,
    const nlohmann::json& evidence) {
    if (!evidence.is_array()) return;
    for (const auto& item : evidence) {
        if (destination.size() >= 12) break;
        if (item.is_string()) {
            destination.push_back(item.get<std::string>());
            continue;
        }
        if (!item.is_object()) continue;
        std::ostringstream line;
        const std::string source = item.value("source", "");
        const std::size_t sourceLine = item.value("line", std::size_t{});
        const std::string excerpt = item.value("excerpt", "");
        if (!source.empty()) line << source;
        if (sourceLine > 0) line << ':' << sourceLine;
        if (!excerpt.empty()) {
            if (line.tellp() > 0) line << " - ";
            line << excerpt;
        }
        if (line.tellp() > 0) destination.push_back(line.str());
    }
}

void UpdateSemanticReviewDetails() {
    if (!gSemanticReviewList || !gSemanticReviewDetails) return;
    const LRESULT selected = SendMessageW(gSemanticReviewList, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR ||
        static_cast<std::size_t>(selected) >= gSemanticReviewEntries.size()) {
        const std::wstring message = gSemanticReviewEntries.empty()
            ? L"No semantic items require review.\r\n"
                L"Candidates already covered by an approved runtime property are hidden: " +
                std::to_wstring(gSemanticReviewHiddenDuplicates)
            : L"Select an item to inspect its evidence.\r\n"
                L"Approved-runtime duplicates hidden: " +
                std::to_wstring(gSemanticReviewHiddenDuplicates);
        SetWindowTextW(gSemanticReviewDetails, message.c_str());
        EnableWindow(gSemanticReviewCategory, FALSE);
        return;
    }
    const auto& entry = gSemanticReviewEntries[static_cast<std::size_t>(selected)];
    std::ostringstream details;
    if (entry.kind == SemanticReviewKind::Action) {
        details << "Action: " << entry.actionId
            << "\r\nSuggested category: " << entry.category;
        EnableWindow(gSemanticReviewCategory, TRUE);
        const std::wstring category = Utf8ToWide(entry.category);
        const LRESULT categoryIndex = SendMessageW(
            gSemanticReviewCategory, CB_FINDSTRINGEXACT, -1,
            reinterpret_cast<LPARAM>(category.c_str()));
        SendMessageW(gSemanticReviewCategory, CB_SETCURSEL,
            categoryIndex == CB_ERR ? 0 : categoryIndex, 0);
    } else {
        details << "State Mapping: " << entry.genericProperty
            << " <- " << entry.sourceSymbol;
        EnableWindow(gSemanticReviewCategory, FALSE);
    }
    details << "\r\nConfidence: " << std::fixed << std::setprecision(2)
        << entry.confidence << "\r\n\r\nEvidence:";
    if (entry.evidence.empty()) details << "\r\n  (none)";
    for (const auto& evidence : entry.evidence) details << "\r\n  " << evidence;
    if (gSemanticReviewHiddenDuplicates > 0) {
        details << "\r\n\r\nApproved-runtime duplicates hidden: "
            << gSemanticReviewHiddenDuplicates;
    }
    SetWindowTextW(gSemanticReviewDetails, Utf8ToWide(details.str()).c_str());
}

void LoadSemanticReviewEntries() {
    gSemanticReviewEntries.clear();
    gSemanticReviewHiddenDuplicates = 0;
    if (!gSemanticReviewList) return;
    SendMessageW(gSemanticReviewList, LB_RESETCONTENT, 0, 0);
    const auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) {
        SetWindowTextW(gSemanticReviewDetails,
            L"Select a Game Project Folder in Project Tools first.");
        return;
    }

    std::ifstream actionInput(ActionProfilePath(projectRoot));
    const auto actionProfile = nlohmann::json::parse(actionInput, nullptr, false);
    if (!actionProfile.is_discarded() && actionProfile.is_object() &&
        actionProfile.contains("actions") && actionProfile["actions"].is_array()) {
        for (const auto& action : actionProfile["actions"]) {
            if (!action.is_object() || !action.value("semanticReviewRequired", false)) continue;
            SemanticReviewEntry entry;
            entry.kind = SemanticReviewKind::Action;
            entry.actionId = action.value("actionId", "");
            entry.category = action.value("category", "generic");
            if (action.contains("semanticInference") && action["semanticInference"].is_object()) {
                const auto& inference = action["semanticInference"];
                entry.confidence = inference.value("confidence", 0.0f);
                AppendSemanticEvidence(entry.evidence,
                    inference.value("evidence", nlohmann::json::array()));
            }
            const std::wstring label = L"[Action] " + Utf8ToWide(entry.actionId) +
                L" -> " + Utf8ToWide(entry.category);
            gSemanticReviewEntries.push_back(std::move(entry));
            SendMessageW(gSemanticReviewList, LB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(label.c_str()));
        }
    }

    std::ifstream stateInput(StateProfilePath(projectRoot));
    const auto stateProfile = nlohmann::json::parse(stateInput, nullptr, false);
    if (!stateProfile.is_discarded() && stateProfile.is_object() &&
        stateProfile.contains("mappings") && stateProfile["mappings"].is_array()) {
        std::set<std::string> approvedRuntimeProperties;
        for (const auto& mapping : stateProfile["mappings"]) {
            if (!mapping.is_object() || !mapping.value("approved", false)) continue;
            const std::string property = mapping.value("genericProperty", "");
            const std::string symbol = mapping.value("sourceSymbol", "");
            if (!property.empty() && (mapping.value("runtimeObserved", false) || symbol == property))
                approvedRuntimeProperties.insert(property);
        }
        for (const auto& mapping : stateProfile["mappings"]) {
            if (!mapping.is_object() || !mapping.value("reviewRequired", false)) continue;
            if (approvedRuntimeProperties.contains(mapping.value("genericProperty", ""))) {
                ++gSemanticReviewHiddenDuplicates;
                continue;
            }
            SemanticReviewEntry entry;
            entry.kind = SemanticReviewKind::StateMapping;
            entry.genericProperty = mapping.value("genericProperty", "");
            entry.sourceSymbol = mapping.value("sourceSymbol", "");
            entry.confidence = mapping.value("confidence", 0.0f);
            AppendSemanticEvidence(entry.evidence,
                mapping.value("evidence", nlohmann::json::array()));
            const std::wstring label = L"[State] " + Utf8ToWide(entry.genericProperty) +
                L" <- " + Utf8ToWide(entry.sourceSymbol);
            gSemanticReviewEntries.push_back(std::move(entry));
            SendMessageW(gSemanticReviewList, LB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(label.c_str()));
        }
    }
    if (!gSemanticReviewEntries.empty())
        SendMessageW(gSemanticReviewList, LB_SETCURSEL, 0, 0);
    UpdateSemanticReviewDetails();
}

void RecalculateActionSemanticSummary(nlohmann::json& profile) {
    std::size_t automatic = 0, manual = 0, ignored = 0, review = 0;
    for (const auto& action : profile.value("actions", nlohmann::json::array())) {
        if (!action.is_object()) continue;
        const std::string source = action.value("semanticApprovalSource", "");
        if (action.value("semanticReviewRequired", false)) ++review;
        else if (action.value("semanticIgnored", false) || source == "manual_ignored") ++ignored;
        else if (source == "manual") ++manual;
        else if (action.value("semanticApproved", false)) ++automatic;
    }
    profile["semanticSummary"] = {
        { "autoApproved", automatic }, { "manuallyApproved", manual },
        { "manuallyIgnored", ignored }, { "reviewRequired", review },
        { "autoApprovalThreshold", 0.90 }, { "method", "local_source_scan" },
    };
}

void RecalculateStateSemanticSummary(nlohmann::json& profile) {
    std::size_t automatic = 0, manual = 0, ignored = 0, review = 0;
    for (const auto& mapping : profile.value("mappings", nlohmann::json::array())) {
        if (!mapping.is_object()) continue;
        const std::string source = mapping.value("approvalSource", "");
        if (mapping.value("reviewRequired", false)) ++review;
        else if (mapping.value("ignored", false) || source == "manual_ignored") ++ignored;
        else if (source == "manual" || source == "manual_legacy") ++manual;
        else if (mapping.value("approved", false)) ++automatic;
    }
    profile["reviewRequired"] = review > 0;
    profile["semanticSummary"] = {
        { "autoApproved", automatic }, { "manuallyApproved", manual },
        { "manuallyIgnored", ignored }, { "reviewRequired", review },
        { "autoApprovalThreshold", 0.98 }, { "method", "local_source_scan" },
    };
}

bool SaveSelectedSemanticReview(bool approve, std::string& resultMessage) {
    if (!gSemanticReviewList) return false;
    const LRESULT selected = SendMessageW(gSemanticReviewList, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR ||
        static_cast<std::size_t>(selected) >= gSemanticReviewEntries.size()) {
        resultMessage = "Select a semantic review item first.";
        return false;
    }
    const auto selectedEntry = gSemanticReviewEntries[static_cast<std::size_t>(selected)];
    const auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) {
        resultMessage = "Semantic Review failed: project folder is not configured.";
        return false;
    }
    if (selectedEntry.kind == SemanticReviewKind::Action) {
        const auto path = ActionProfilePath(projectRoot);
        std::ifstream input(path);
        auto profile = nlohmann::json::parse(input, nullptr, false);
        if (profile.is_discarded() || !profile.is_object() ||
            !profile.contains("actions") || !profile["actions"].is_array()) {
            resultMessage = "Semantic Review failed: Action Profile is invalid.";
            return false;
        }
        bool found = false;
        for (auto& action : profile["actions"]) {
            if (!action.is_object() || action.value("actionId", "") != selectedEntry.actionId) continue;
            found = true;
            if (approve) {
                const std::string category = WideToUtf8(ReadWindowText(gSemanticReviewCategory));
                action["category"] = category.empty() ? selectedEntry.category : category;
                action["tags"] = nlohmann::json::array({ action["category"] });
                action["semanticApproved"] = true;
                action["semanticIgnored"] = false;
                action["semanticApprovalSource"] = "manual";
            } else {
                action["semanticApproved"] = false;
                action["semanticIgnored"] = true;
                action["semanticApprovalSource"] = "manual_ignored";
            }
            action["semanticReviewRequired"] = false;
            break;
        }
        if (!found) {
            resultMessage = "Semantic Review failed: Action was not found.";
            return false;
        }
        RecalculateActionSemanticSummary(profile);
        std::ofstream output(path, std::ios::trunc);
        if (!output) {
            resultMessage = "Semantic Review failed: Action Profile could not be saved.";
            return false;
        }
        output << profile.dump(2) << '\n';
        resultMessage = std::string(approve ? "Approved Action: " : "Ignored Action candidate: ") +
            selectedEntry.actionId;
        return true;
    }

    const auto path = StateProfilePath(projectRoot);
    std::ifstream input(path);
    auto profile = nlohmann::json::parse(input, nullptr, false);
    if (profile.is_discarded() || !profile.is_object() ||
        !profile.contains("mappings") || !profile["mappings"].is_array()) {
        resultMessage = "Semantic Review failed: State Mapping Profile is invalid.";
        return false;
    }
    bool found = false;
    for (auto& mapping : profile["mappings"]) {
        if (!mapping.is_object() ||
            mapping.value("genericProperty", "") != selectedEntry.genericProperty ||
            mapping.value("sourceSymbol", "") != selectedEntry.sourceSymbol) continue;
        found = true;
        mapping["approved"] = approve;
        mapping["autoApproved"] = false;
        mapping["ignored"] = !approve;
        mapping["approvalSource"] = approve ? "manual" : "manual_ignored";
        mapping["reviewRequired"] = false;
        break;
    }
    if (!found) {
        resultMessage = "Semantic Review failed: State Mapping was not found.";
        return false;
    }
    RecalculateStateSemanticSummary(profile);
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        resultMessage = "Semantic Review failed: State Mapping Profile could not be saved.";
        return false;
    }
    output << profile.dump(2) << '\n';
    resultMessage = std::string(approve ? "Approved State Mapping: " : "Ignored State Mapping candidate: ") +
        selectedEntry.genericProperty + " <- " + selectedEntry.sourceSymbol;
    return true;
}

LRESULT CALLBACK ProjectToolsWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        gProjectToolsWindow = window;
        CreateWindowW(L"STATIC", L"プロジェクト / プロファイル設定", WS_CHILD | WS_VISIBLE,
            20, 15, 250, 24, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"STATIC", L"ゲームプロジェクト:", WS_CHILD | WS_VISIBLE,
            20, 48, 160, 22, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        const std::wstring savedProjectFolder = LoadProjectFolder();
        gProjectFolder = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", savedProjectFolder.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 20, 70, 450, 27, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ProjectFolderId)), GetModuleHandleW(nullptr), nullptr);
        AddButton(window, L"参照...", BrowseProjectId, 480, 67, 80);
        AddButton(window, L"ソース解析", ScanProjectId, 570, 67, 90);
        CreateWindowW(L"STATIC", L"解析対象（空欄 = 自動）:", WS_CHILD | WS_VISIBLE,
            20, 110, 300, 22, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        const std::wstring savedScanTargets = LoadScanTargets();
        gScanTargets = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", savedScanTargets.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 20, 132, 640, 27, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ScanTargetsId)), GetModuleHandleW(nullptr), nullptr);
        AddButton(window, L"操作プロファイル生成", GenerateProfileId, 20, 175, 190);
        AddButton(window, L"状態マッピング生成", GenerateStateProfileId, 220, 175, 190);
        AddButton(window, L"ローカル方針生成", GenerateLocalPolicyId, 420, 175, 190);
        AddButton(window, L"意味確認...", OpenSemanticReviewId, 20, 218, 190);
        CreateWindowW(L"STATIC", L"結果はメイン画面の下部に表示されます。",
            WS_CHILD | WS_VISIBLE, 220, 226, 390, 22, window, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case BrowseProjectId: {
            std::filesystem::path selected;
            if (BrowseForProjectFolder(window, selected)) {
                SetWindowTextW(gProjectFolder, selected.c_str());
                SaveProjectFolder(selected);
                RefreshReplaySessionList(false);
                SetWindowTextW(gStatusText, L"Project folder saved locally.");
            }
            return 0;
        }
        case ScanProjectId: {
            const std::filesystem::path folder(ReadWindowText(gProjectFolder));
            const std::wstring targets = ReadWindowText(gScanTargets);
            SaveWorkspaceSettings(folder, targets);
            const std::string targetText = WideToUtf8(targets);
            StartOneShotWorker(gMainWindow, [folder, targetText] {
                return ScanProjectFolder(folder, targetText);
            }, L"Scanning project files...");
            return 0;
        }
        case GenerateProfileId: {
            const std::filesystem::path folder(ReadWindowText(gProjectFolder));
            SaveProjectFolder(folder);
            StartOneShotWorker(gMainWindow, [folder] { return GenerateActionProfile(folder); },
                L"Generating Action Profile locally...");
            return 0;
        }
        case GenerateStateProfileId: {
            const std::filesystem::path folder(ReadWindowText(gProjectFolder));
            const std::wstring targets = ReadWindowText(gScanTargets);
            SaveWorkspaceSettings(folder, targets);
            const std::string targetText = WideToUtf8(targets);
            StartOneShotWorker(gMainWindow, [folder, targetText] {
                return GenerateStateMappingProfile(folder, targetText);
            }, L"Generating State Mapping Profile locally...");
            return 0;
        }
        case GenerateLocalPolicyId:
            ApplyViewerGoal();
            StartOneShotWorker(gMainWindow, GenerateLocalPolicyCore,
                L"Generating Local Policy from AI Goal...");
            return 0;
        case OpenSemanticReviewId:
            PostMessageW(gMainWindow, WM_COMMAND, OpenSemanticReviewId, 0);
            return 0;
        default: break;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        DestroyButtonTooltip(window);
        gProjectToolsWindow = nullptr;
        gProjectFolder = nullptr;
        gScanTargets = nullptr;
        return 0;
    default: break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK SemanticReviewWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        gSemanticReviewWindow = window;
        CreateWindowW(L"STATIC", L"意味確認 - 判断が必要な候補だけを表示",
            WS_CHILD | WS_VISIBLE, 20, 15, 500, 24, window, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        gSemanticReviewList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
            20, 48, 640, 210, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(SemanticReviewListId)),
            GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"STATIC", L"操作分類:", WS_CHILD | WS_VISIBLE,
            20, 272, 120, 22, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        gSemanticReviewCategory = CreateWindowW(L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            140, 268, 180, 180, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(SemanticReviewCategoryId)),
            GetModuleHandleW(nullptr), nullptr);
        for (const wchar_t* category : { L"generic", L"attack", L"movement", L"mobility",
            L"defense", L"idle", L"flow", L"interaction" }) {
            SendMessageW(gSemanticReviewCategory, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(category));
        }
        AddButton(window, L"選択項目を承認", SemanticReviewApproveId, 335, 264, 145);
        AddButton(window, L"候補を無視", SemanticReviewIgnoreId, 490, 264, 135);
        AddButton(window, L"再読込", SemanticReviewReloadId, 20, 307, 90);
        gSemanticReviewDetails = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            20, 350, 640, 190, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(SemanticReviewDetailsId)),
            GetModuleHandleW(nullptr), nullptr);
        LoadSemanticReviewEntries();
        PostMessageW(window, WM_COMMAND, SemanticReviewReloadId, 0);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case SemanticReviewListId:
            if (HIWORD(wParam) == LBN_SELCHANGE) UpdateSemanticReviewDetails();
            return 0;
        case SemanticReviewApproveId:
        case SemanticReviewIgnoreId: {
            std::string result;
            const bool approve = LOWORD(wParam) == SemanticReviewApproveId;
            SaveSelectedSemanticReview(approve, result);
            SetWindowTextW(gStatusText, Utf8ToWide(result).c_str());
            LoadSemanticReviewEntries();
            return 0;
        }
        case SemanticReviewReloadId:
            LoadSemanticReviewEntries();
            return 0;
        default: break;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        DestroyButtonTooltip(window);
        gSemanticReviewWindow = nullptr;
        gSemanticReviewList = nullptr;
        gSemanticReviewCategory = nullptr;
        gSemanticReviewDetails = nullptr;
        gSemanticReviewEntries.clear();
        return 0;
    default: break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void ShowProjectToolsWindow(HWND owner) {
    if (gProjectToolsWindow && IsWindow(gProjectToolsWindow)) {
        ShowWindow(gProjectToolsWindow, SW_SHOWNORMAL);
        SetForegroundWindow(gProjectToolsWindow);
        return;
    }
    CreateWindowExW(WS_EX_TOOLWINDOW, kProjectToolsWindowClass,
        L"DebugAI プロジェクト設定", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 700, 310, owner, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (gProjectToolsWindow) ShowWindow(gProjectToolsWindow, SW_SHOWNORMAL);
}

void ShowSemanticReviewWindow(HWND owner) {
    if (gSemanticReviewWindow && IsWindow(gSemanticReviewWindow)) {
        LoadSemanticReviewEntries();
        ShowWindow(gSemanticReviewWindow, SW_SHOWNORMAL);
        SetForegroundWindow(gSemanticReviewWindow);
        return;
    }
    CreateWindowExW(WS_EX_TOOLWINDOW, kSemanticReviewWindowClass,
        L"DebugAI 意味確認", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 700, 600, owner, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (gSemanticReviewWindow) ShowWindow(gSemanticReviewWindow, SW_SHOWNORMAL);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        gMainWindow = window;
        gAIProvider.Configure();
        const std::wstring configuredLocalInterval = std::to_wstring(
            LoadWorkspaceInterval("localIntervalMs", 250));
        CreateWindowW(L"STATIC", L"DebugAI Viewer", WS_CHILD | WS_VISIBLE,
            20, 16, 300, 28, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        AddButton(window, L"記録開始", StartRecordingId, 20, 55, 120);
        AddButton(window, L"記録停止", StopRecordingId, 150, 55, 120);
        AddButton(window, L"最新を再生", PlayLatestId, 280, 55, 120);
        AddButton(window, L"再生停止", StopReplayId, 410, 55, 120);
        AddButton(window, L"更新", RefreshId, 540, 55, 110);
        CreateWindowW(L"STATIC", L"リプレイ:", WS_CHILD | WS_VISIBLE,
            20, 104, 70, 24, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        gReplaySessions = CreateWindowW(L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            90, 96, 285, 260, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ReplaySessionId)),
            GetModuleHandleW(nullptr), nullptr);
        gPlaySelectedReplay = AddButton(
            window, L"選択を再生", PlaySelectedReplayId, 385, 96, 110);
        AddButton(window, L"履歴", ShowReplayTimelineId, 505, 96, 70);
        AddButton(window, L"再読込", ReloadReplayListId, 585, 96, 65);
        CreateWindowW(L"STATIC", L"再生操作:", WS_CHILD | WS_VISIBLE,
            20, 149, 70, 24, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        AddButton(window, L"一時停止", PauseReplayId, 90, 141, 90);
        AddButton(window, L"再開", ResumeReplayId, 190, 141, 80);
        AddButton(window, L"1F進む", StepReplayId, 280, 141, 90);
        CreateWindowW(L"STATIC", L"速度:", WS_CHILD | WS_VISIBLE,
            380, 149, 50, 24, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        gReplaySpeed = CreateWindowW(L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            430, 145, 80, 160, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ReplaySpeedId)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(gReplaySpeed, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"0.25x"));
        SendMessageW(gReplaySpeed, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"0.5x"));
        SendMessageW(gReplaySpeed, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"1x"));
        SendMessageW(gReplaySpeed, CB_SETCURSEL, 0, 2);
        AddButton(window, L"適用", ApplyReplaySpeedId, 520, 141, 80);
        CreateWindowW(L"STATIC", L"ローカルAI", WS_CHILD | WS_VISIBLE,
            20, 194, 95, 24, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        AddButton(window, L"開始", LocalStartId, 115, 186, 90);
        AddButton(window, L"停止", AIStopId, 215, 186, 80);
        CreateWindowW(L"STATIC", L"間隔(ms):", WS_CHILD | WS_VISIBLE,
            315, 194, 80, 24, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        gLocalAIInterval = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", configuredLocalInterval.c_str(),
            WS_CHILD | WS_VISIBLE | ES_NUMBER, 395, 190, 70, 26, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(LocalAIIntervalId)), GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"STATIC", L"操作対象:", WS_CHILD | WS_VISIBLE,
            480, 194, 70, 24, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        gAIActorMode = CreateWindowW(L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            550, 190, 110, 180, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(AIActorModeId)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(gAIActorMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"プレイヤー"));
        SendMessageW(gAIActorMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"ボス"));
        SendMessageW(gAIActorMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"両方"));
        SendMessageW(gAIActorMode, CB_SETCURSEL, 0, 0);
        CreateWindowW(L"STATIC", L"シナリオ:", WS_CHILD | WS_VISIBLE,
            20, 237, 70, 24, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        gScenarioList = CreateWindowW(L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            90, 233, 235, 220, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ScenarioListId)),
            GetModuleHandleW(nullptr), nullptr);
        AddButton(window, L"再読込", ReloadScenarioListId, 335, 228, 70);
        AddButton(window, L"選択実行", StartScenarioId, 415, 228, 90);
        AddButton(window, L"全件実行", RunAllScenariosId, 515, 228, 85);
        AddButton(window, L"停止", StopScenarioId, 610, 228, 50);
        const std::wstring configuredGoal = Utf8ToWide(gAIProvider.Goal());
        CreateWindowW(L"STATIC", L"AI目標 / 指示:", WS_CHILD | WS_VISIBLE,
            20, 278, 170, 22, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        gAIGoal = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", configuredGoal.c_str(),
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
            20, 300, 640, 68, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(AIGoalId)), GetModuleHandleW(nullptr), nullptr);
        AddButton(window, L"プロジェクト設定...", OpenProjectToolsId, 20, 379, 160);
        AddButton(window, L"意味確認...", OpenSemanticReviewId, 190, 379, 130);
        AddButton(window, L"接続診断", AdapterDiagnosticsId, 330, 379, 120);
        AddButton(window, L"カバレッジ", CoverageSummaryId, 20, 421, 130);
        AddButton(window, L"レポート", OpenLatestReportId, 160, 421, 100);
        AddButton(window, L"カバレッジ初期化", ResetCoverageId, 270, 421, 170);
        gStatusText = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"ゲーム接続を確認しています...",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            20, 463, 640, 237,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(StatusTextId)),
            GetModuleHandleW(nullptr),
            nullptr);
        RefreshReplaySessionList(false);
        RefreshScenarioList(false);
        StartConnectionWorker(window, L"ゲーム接続を確認しています...");
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case StartRecordingId:
            StartOneShotWorker(
                window,
                StartRecordingWithCoverageCore,
                L"Starting replay and coverage recording...");
            return 0;
        case StopRecordingId:
            StartOneShotWorker(window, [window] {
                const std::string result = StopRecordingWithCoverageCore();
                PostMessageW(window, kReplayListRefreshMessage, 0, 0);
                return result;
            }, L"Finalizing replay and coverage...");
            return 0;
        case PlayLatestId:
            StartReplayWorker(window, [] {
                return ExecuteCommandCore("play_latest");
            }, L"Starting replay...");
            return 0;
        case StopReplayId: StartCommandWorker(window, "stop_replay", L"Stopping replay..."); return 0;
        case RefreshId: StartCommandWorker(window, "status", L"Refreshing..."); return 0;
        case PlaySelectedReplayId: {
            const LRESULT selected = SendMessageW(gReplaySessions, CB_GETCURSEL, 0, 0);
            if (selected == CB_ERR ||
                static_cast<std::size_t>(selected) >= gReplaySessionEntries.size()) {
                SetWindowTextW(gStatusText, L"Select a completed replay session first.");
                return 0;
            }
            const auto manifestPath =
                gReplaySessionEntries[static_cast<std::size_t>(selected)].manifestPath;
            StartReplayWorker(window, [manifestPath] {
                return PlaySelectedReplayCore(manifestPath);
            }, L"Starting selected replay...");
            return 0;
        }
        case ShowReplayTimelineId: {
            const LRESULT selected =
                SendMessageW(gReplaySessions, CB_GETCURSEL, 0, 0);
            if (selected == CB_ERR ||
                static_cast<std::size_t>(selected) >=
                    gReplaySessionEntries.size()) {
                SetWindowTextW(
                    gStatusText,
                    L"Select a completed replay session first.");
                return 0;
            }
            const auto manifestPath =
                gReplaySessionEntries[
                    static_cast<std::size_t>(selected)].manifestPath;
            StartOneShotWorker(window, [manifestPath] {
                return FormatReplayTimelineCore(manifestPath);
            }, L"Loading replay timeline...");
            return 0;
        }
        case ReloadReplayListId:
            RefreshReplaySessionList(true);
            return 0;
        case ReplaySessionId: {
            if (HIWORD(wParam) != CBN_SELCHANGE) return 0;
            const LRESULT selected =
                SendMessageW(gReplaySessions, CB_GETCURSEL, 0, 0);
            if (selected == CB_ERR ||
                static_cast<std::size_t>(selected) >= gReplaySessionEntries.size()) {
                return 0;
            }
            const auto manifestPath =
                gReplaySessionEntries[static_cast<std::size_t>(selected)].manifestPath;
            StartOneShotWorker(window, [manifestPath, window] {
                const std::string result =
                    FormatSelectedReplayCoverageCore(manifestPath);
                PostMessageW(window, kReplayListRefreshMessage, 0, 0);
                return result;
            }, L"Loading selected replay coverage...");
            return 0;
        }
        case PauseReplayId:
            StartCommandWorker(window, "pause_replay", L"Pausing replay...");
            return 0;
        case ResumeReplayId:
            StartCommandWorker(window, "resume_replay", L"Resuming replay...");
            return 0;
        case StepReplayId:
            StartCommandWorker(window, "step_replay", L"Advancing one replay frame...");
            return 0;
        case ApplyReplaySpeedId: {
            const LRESULT selected =
                SendMessageW(gReplaySpeed, CB_GETCURSEL, 0, 0);
            constexpr double speeds[] = { 0.25, 0.5, 1.0 };
            if (selected == CB_ERR ||
                static_cast<std::size_t>(selected) >=
                    sizeof(speeds) / sizeof(speeds[0])) {
                SetWindowTextW(gStatusText, L"Select a replay speed first.");
                return 0;
            }
            const double speed = speeds[static_cast<std::size_t>(selected)];
            StartOneShotWorker(window, [speed] {
                return SetReplaySpeedCore(speed);
            }, L"Changing replay speed...");
            return 0;
        }
        case ExecuteFirstActionId:
            StartOneShotWorker(window, ExecuteFirstAvailableActionCore, L"Executing first action..."); return 0;
        case AIStepId: StartAIWorker(window, AIWorkerMode::ApiStep); return 0;
        case AIStartId: StartAIWorker(window, AIWorkerMode::ApiContinuous); return 0;
        case LocalStartId: StartAIWorker(window, AIWorkerMode::LocalContinuous); return 0;
        case AIStopId:
            if (gScenarioRunner.IsRunning()) StopScenarioWorker();
            else StopAIWorker();
            return 0;
        case ReloadScenarioListId:
            RefreshScenarioList(true);
            return 0;
        case StartScenarioId:
            StartScenarioWorker(window, false);
            return 0;
        case RunAllScenariosId:
            StartScenarioWorker(window, true);
            return 0;
        case StopScenarioId:
            StopScenarioWorker();
            return 0;
        case CaptureVisionId: {
            const std::filesystem::path projectRoot = gProjectFolder
                ? std::filesystem::path(ReadWindowText(gProjectFolder))
                : ConfiguredProjectRoot();
            StartOneShotWorker(window, [projectRoot, window] {
                return CaptureVisionPreviewCore(projectRoot, window);
            }, L"Capturing the connected game window...");
            return 0;
        }
        case OpenProjectToolsId:
            ShowProjectToolsWindow(window);
            return 0;
        case OpenSemanticReviewId:
            ShowSemanticReviewWindow(window);
            return 0;
        case CoverageSummaryId:
            StartOneShotWorker(window, FormatCoverageSummaryCore,
                L"Building runtime coverage summary...");
            return 0;
        case AdapterDiagnosticsId: {
            const std::filesystem::path projectRoot = gProjectFolder
                ? std::filesystem::path(ReadWindowText(gProjectFolder))
                : ConfiguredProjectRoot();
            StartOneShotWorker(window, [projectRoot] {
                return RunAdapterDiagnosticsCore(projectRoot);
            }, L"Checking the connected game Adapter...");
            return 0;
        }
        case OpenLatestReportId:
            StartOneShotWorker(window, OpenLatestTestReportCore,
                L"Opening latest test report...");
            return 0;
        case ResetCoverageId:
            StartOneShotWorker(window, ResetCoverageCore,
                L"Archiving and resetting runtime coverage...");
            return 0;
        default: break;
        }
        break;

    case kReplayListRefreshMessage:
        RefreshReplaySessionList(false);
        return 0;

    case WM_TIMER:
        if (wParam == kGameProcessWatchTimerId) {
            if (WatchedGameProcessExited()) {
                ShowWindow(window, SW_HIDE);
                DestroyWindow(window);
                return 0;
            }
            const ULONGLONG now = GetTickCount64();
            if (gGameConnectionEstablished && !HasWatchedGameProcess()) {
                WatchGameProcess(gGameProcessId.load());
            }
            const bool connectionStatusReady = gConnectionStatusReady.exchange(false);
            const bool showConnectionStatus = connectionStatusReady &&
                (gGameConnectionEstablished ||
                    gConnectionInitialResultPending.exchange(false));
            if (connectionStatusReady && showConnectionStatus && !gAIWorkerRunning) {
                std::string status;
                {
                    std::lock_guard lock(gConnectionStatusMutex);
                    status = gPendingConnectionStatus;
                }
                SetWindowTextW(gStatusText, Utf8ToWide(status).c_str());
            }
            if (!gGameConnectionEstablished && !gConnectionWorkerRunning &&
                now >= gNextConnectionAttemptTick) {
                gNextConnectionAttemptTick = now + 1000;
                StartConnectionWorker(window);
            }
            return 0;
        }
        if (wParam == kPendingReplayStartTimerId) {
            if (!gPendingReplayStart) {
                KillTimer(window, kPendingReplayStartTimerId);
                return 0;
            }
            if (!gAIWorkerRunning) {
                KillTimer(window, kPendingReplayStartTimerId);
                auto task = std::move(gPendingReplayStart);
                gPendingReplayStart = {};
                StartOneShotWorker(
                    window, std::move(task), L"Starting queued replay...");
            }
            return 0;
        }
        return 0;

    case kAIStatusMessage: {
        const ULONGLONG now = GetTickCount64();
        if (wParam == 0 && now < gAutoRefreshResumeTick) {
            return 0;
        }
        if (wParam != 0) gAutoRefreshResumeTick = now + 15000;
        std::string status;
        {
            std::lock_guard lock(gAIStatusMutex);
            status = gPendingAIStatus;
        }
        SetWindowTextW(gStatusText, Utf8ToWide(status).c_str());
        return 0;
    }

    case WM_DESTROY:
        DestroyButtonTooltip(window);
        gScenarioRunner.RequestStop();
        if (gProjectToolsWindow && IsWindow(gProjectToolsWindow))
            DestroyWindow(gProjectToolsWindow);
        if (gSemanticReviewWindow && IsWindow(gSemanticReviewWindow))
            DestroyWindow(gSemanticReviewWindow);
        gMainWindow = nullptr;
        KillTimer(window, kPendingReplayStartTimerId);
        KillTimer(window, kGameProcessWatchTimerId);
        StopWatchingGameProcess();
        gPendingReplayStart = {};
        StopAIWorker();
        if (gConnectionWorker.joinable()) gConnectionWorker.join();
        if (gAIWorker.joinable()) gAIWorker.join();
        gCoverageTracker.Save();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&commonControls);

    std::wstring arguments = commandLine ? commandLine : L"";
#if defined(DEBUGAI_VIEWER_DISABLED)
    if (arguments.empty()) return 0;
#endif
    constexpr std::wstring_view diagnosticPrefix = L"--scan-diagnostic";
    if (arguments.starts_with(diagnosticPrefix)) {
        arguments.erase(0, diagnosticPrefix.size());
        const auto first = arguments.find_first_not_of(L" \t");
        if (first != std::wstring::npos) arguments.erase(0, first);
        const auto last = arguments.find_last_not_of(L" \t");
        if (last != std::wstring::npos) arguments.erase(last + 1);
        if (arguments.size() >= 2 && arguments.front() == L'"' && arguments.back() == L'"') {
            arguments = arguments.substr(1, arguments.size() - 2);
        }
        const std::filesystem::path root(arguments);
        std::string diagnostic;
        int resultCode = 0;
        try {
            diagnostic = "SCAN\r\n" + ScanProjectFolder(root) +
                "\r\n\r\nACTION PROFILE\r\n" + GenerateActionProfile(root) +
                "\r\n\r\nSTATE MAPPING\r\n" + GenerateStateMappingProfile(root, "");
        } catch (const std::exception& exception) {
            diagnostic = std::string("Scan diagnostic failed safely: ") + exception.what();
            resultCode = 2;
        } catch (...) {
            diagnostic = "Scan diagnostic failed safely: unknown exception";
            resultCode = 3;
        }
        const auto outputPath = std::filesystem::current_path() /
            "generated/debug_ai/viewer/scan_diagnostic.txt";
        std::error_code error;
        std::filesystem::create_directories(outputPath.parent_path(), error);
        std::ofstream output(outputPath, std::ios::trunc);
        if (output) {
            output << "Root: " << WideToUtf8(root.wstring()) << "\n" << diagnostic << '\n';
        }
        return resultCode;
    }
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&windowClass)) {
        return 1;
    }
    WNDCLASSW projectToolsClass = windowClass;
    projectToolsClass.lpfnWndProc = ProjectToolsWindowProc;
    projectToolsClass.lpszClassName = kProjectToolsWindowClass;
    if (!RegisterClassW(&projectToolsClass)) return 1;
    WNDCLASSW semanticReviewClass = windowClass;
    semanticReviewClass.lpfnWndProc = SemanticReviewWindowProc;
    semanticReviewClass.lpszClassName = kSemanticReviewWindowClass;
    if (!RegisterClassW(&semanticReviewClass)) return 1;

    HWND window = CreateWindowExW(
        0,
        kWindowClass,
        L"DebugAI Viewer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 700, 760,
        nullptr, nullptr, instance, nullptr);
    if (!window) {
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);
    SetTimer(window, kGameProcessWatchTimerId, 500, nullptr);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (SUCCEEDED(comResult)) CoUninitialize();
    return static_cast<int>(message.wParam);
}
