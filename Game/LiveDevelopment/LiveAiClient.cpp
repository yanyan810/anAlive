#include "LiveAiClient.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

#pragma comment(lib, "winhttp.lib")

namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
constexpr size_t kResponseLimit = 96 * 1024;

struct HttpHandle {
    HINTERNET value;
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
};

struct HttpExchange {
    std::mutex mutex;
    std::condition_variable changed;
    DWORD status = 0, error = 0, bytes = 0;
    std::array<char, 8192> buffer{};
    std::string body;

    static void CALLBACK Notify(HINTERNET, DWORD_PTR context, DWORD status, void* info, DWORD length) {
        if (!context) return;
        auto* owner = reinterpret_cast<std::shared_ptr<HttpExchange>*>(context);
        if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
            delete owner;
            return;
        }
        auto self = *owner;
        {
            std::lock_guard lock(self->mutex);
            self->status = status;
            if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
                self->error = static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError;
            if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) self->bytes = length;
        }
        self->changed.notify_one();
    }

    void Wait(BOOL started, DWORD expected, Clock::time_point deadline, const std::atomic_bool& cancelled) {
        if (!started && GetLastError() != ERROR_IO_PENDING) Fail(GetLastError());
        std::unique_lock lock(mutex);
        while (!status && !cancelled && Clock::now() < deadline)
            changed.wait_until(lock, std::min(deadline, Clock::now() + std::chrono::milliseconds(20)));
        if (cancelled) throw std::runtime_error("問い合わせをキャンセルしました。");
        if (Clock::now() >= deadline) Fail(ERROR_WINHTTP_TIMEOUT);
        if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) Fail(error);
        if (status != expected) throw std::runtime_error("Ollama HTTP の応答状態が無効です。");
        status = 0;
    }

    [[noreturn]] static void Fail(DWORD error) {
        if (error == ERROR_WINHTTP_TIMEOUT) throw std::runtime_error("Ollama の推論がタイムアウトしました。");
        throw std::runtime_error("Ollama に接続できないか通信が中断されました (WinHTTP " + std::to_string(error) + ")。");
    }
};

INTERNET_PORT LocalPort(std::string_view endpoint) {
    constexpr std::string_view prefix = "http://127.0.0.1:";
    if (!endpoint.starts_with(prefix)) throw std::runtime_error("接続先は http://127.0.0.1:<port> を指定してください。");
    endpoint.remove_prefix(prefix.size());
    unsigned port = 0;
    const auto end = endpoint.data() + endpoint.size();
    const auto result = std::from_chars(endpoint.data(), end, port);
    if (result.ec != std::errc{} || result.ptr != end || port == 0 || port > 65535)
        throw std::runtime_error("Ollama のポート番号が無効です。");
    return static_cast<INTERNET_PORT>(port);
}

Json ParseObject(std::string_view bytes) {
    // Both the HTTP envelope and the model payload are untrusted.
    std::array<std::vector<std::string>, 17> keys;
    auto value = Json::parse(bytes, [&](int depth, Json::parse_event_t event, Json& parsed) {
        if (depth < 0 || depth > 16) throw std::runtime_error("応答 JSON の階層が深すぎます。");
        if (event == Json::parse_event_t::object_start) keys[depth].clear();
        if (event == Json::parse_event_t::key) {
            auto& existing = keys.at(depth - 1);
            const auto& key = parsed.get_ref<const std::string&>();
            if (existing.size() >= 64 || std::ranges::find(existing, key) != existing.end())
                throw std::runtime_error("応答 JSON に重複キーまたは過剰な項目があります。");
            existing.push_back(key);
        }
        return true;
    });
    if (!value.is_object()) throw std::runtime_error("応答が JSON オブジェクトではありません。");
    return value;
}

Json ChatRequest(const LiveAiClient::Settings& settings, const Json& context, const std::string& prompt,
    const std::optional<LiveAiClient::Repair>& repair) {
    const Json schema = {
        {"type", "object"}, {"additionalProperties", false},
        {"required", {"operation", "scene", "enemyId", "baseGeneration", "lua", "reason", "requiresApproval"}},
        {"properties", {
            {"operation", {{"type", "string"}, {"enum", {"set_enemy_speed_lua", "unsupported"}}}},
            {"scene", {{"type", "string"}}}, {"enemyId", {{"type", "string"}}},
            {"baseGeneration", {{"type", "string"}}}, {"lua", {{"type", "string"}, {"maxLength", LiveAiClient::kLuaMaxCharacters}}},
            {"reason", {{"type", "string"}, {"minLength", 1}, {"maxLength", LiveAiClient::kReasonMaxCharacters}}},
            {"requiresApproval", {{"type", "boolean"}}}
        }}
    };
    std::string instructions = R"(Generate raw Lua in the lua field of one JSON object. Follow the examples below. Copy scene, enemyId and baseGeneration exactly from context. For a supported request, set reason to "速度倍率を変更".
Lua starts with local enemy = ... and uses an empty global environment. Use local variables, arithmetic, comparisons, if/then/end and return. No function calls, global variables, libraries or Markdown.
enemy is a read-only copy. String fields: id, type, state. Number fields: hp, maxHp, distance, speedMultiplier (previous multiplier). Pursuit is state "Chase".
Return the absolute multiplier as a finite number from 0.0 to 3.0 on every path. Return 1.0 for normal speed. Never write enemy fields or multiply the previous speedMultiplier. Guard maxHp > 0 before dividing.

Example for HP <= 50%:
local enemy = ...
if enemy.maxHp > 0 and enemy.hp / enemy.maxHp <= 0.5 then return 1.5 end
return 1.0

Example for pursuit:
local enemy = ...
if enemy.state == "Chase" then return 2.0 end
return 1.0

For supported requests, set the JSON operation field to "set_enemy_speed_lua". That identifier is JSON metadata and must never appear in Lua. For unsupported requests, use operation "unsupported", an empty lua field, and one short Japanese sentence explaining the limitation in reason.
)";
    auto message = Json{{"context", context}, {"instruction", prompt}};
    if (repair) {
        instructions += "Repair the rejected candidate. Inspect repair.lua and its diagnostic; follow repair.requirement and preserve the original instruction and scope.";
        message["repair"] = {{"lua", repair->source}, {"errorType", repair->errorType},
            {"error", repair->error}, {"requirement", repair->requirement}};
    }
    return {{"model", settings.model}, {"stream", false}, {"format", schema},
        {"options", {{"temperature", 0}, {"num_ctx", 8192}, {"num_predict", 600}, {"repeat_penalty", 1.15}}},
        {"messages", Json::array({
            {{"role", "system"}, {"content", instructions}},
            {{"role", "user"}, {"content", message.dump(-1, ' ', false, Json::error_handler_t::replace)}}
        })}};
}

std::string Request(const LiveAiClient::Settings& settings, std::string body, const std::atomic_bool& cancelled,
    bool& connected, Clock::time_point overallDeadline = Clock::time_point::max(), const wchar_t* resource = L"/api/chat") {
    if (cancelled) throw std::runtime_error("問い合わせをキャンセルしました。");
    const auto deadline = std::min(overallDeadline, Clock::now() + std::chrono::seconds(settings.timeoutSeconds));
    const auto port = LocalPort(settings.endpoint);
    const auto timeout = settings.timeoutSeconds * 1000;
    const HttpHandle session{WinHttpOpen(L"YanEngine Live Development/1", WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC)};
    if (!session.value || !WinHttpSetTimeouts(session.value, 1000, std::min(timeout, 3000), timeout, timeout))
        HttpExchange::Fail(GetLastError());
    const HttpHandle connection{WinHttpConnect(session.value, L"127.0.0.1", port, 0)};
    if (!connection.value) HttpExchange::Fail(GetLastError());
    auto exchange = std::make_shared<HttpExchange>();
    auto owner = std::make_unique<std::shared_ptr<HttpExchange>>(exchange);
    const HttpHandle request{WinHttpOpenRequest(connection.value, L"POST", resource, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)};
    if (!request.value) HttpExchange::Fail(GetLastError());
    auto context = reinterpret_cast<DWORD_PTR>(owner.get());
    DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION | WINHTTP_DISABLE_COOKIES;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)) ||
        !WinHttpSetOption(request.value, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)))
        HttpExchange::Fail(GetLastError());
    if (WinHttpSetStatusCallback(request.value, HttpExchange::Notify,
        WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE | WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE |
        WINHTTP_CALLBACK_FLAG_READ_COMPLETE | WINHTTP_CALLBACK_FLAG_REQUEST_ERROR |
        WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK) HttpExchange::Fail(GetLastError());
    owner.release(); // Released by the final HANDLE_CLOSING callback.
    exchange->body = std::move(body);
    const auto bytes = static_cast<DWORD>(exchange->body.size());
    if (cancelled) throw std::runtime_error("問い合わせをキャンセルしました。");
    exchange->Wait(WinHttpSendRequest(request.value, L"Content-Type: application/json\r\n", DWORD(-1),
        exchange->body.data(), bytes, bytes, context), WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, deadline, cancelled);
    exchange->Wait(WinHttpReceiveResponse(request.value, nullptr), WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, deadline, cancelled);
    connected = true;
    DWORD code = 0, size = sizeof(code);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX)) HttpExchange::Fail(GetLastError());
    if (code == 404) throw std::runtime_error("Ollama のモデルまたは API が見つかりません。モデル名と ollama list を確認してください。");
    if (code != 200) throw std::runtime_error("Ollama HTTP エラー " + std::to_string(code));
    std::string response;
    do {
        exchange->Wait(WinHttpReadData(request.value, exchange->buffer.data(), static_cast<DWORD>(exchange->buffer.size()),
            nullptr), WINHTTP_CALLBACK_STATUS_READ_COMPLETE, deadline, cancelled);
        if (response.size() + exchange->bytes > kResponseLimit) throw std::runtime_error("Ollama の応答が上限を超えました。");
        response.append(exchange->buffer.data(), exchange->bytes);
    } while (exchange->bytes);
    return response;
}

void CheckSettings(const LiveAiClient::Settings& settings) {
    LocalPort(settings.endpoint);
    if (settings.model.empty() || settings.model.size() > 128 ||
        settings.model.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._:/-") != std::string::npos ||
        settings.model.ends_with("-cloud") || settings.timeoutSeconds < 1 || settings.timeoutSeconds > 120)
        throw std::runtime_error("ローカルモデル名またはタイムアウト (1〜120 秒) が無効です。");
}

Json Content(std::string_view response, double* loadMs = nullptr, double* inferenceMs = nullptr) {
    const auto document = ParseObject(response);
    if (loadMs) *loadMs += document.value("load_duration", 0.0) / 1000000.0;
    if (inferenceMs) *inferenceMs += (document.value("prompt_eval_duration", 0.0) + document.value("eval_duration", 0.0)) / 1000000.0;
    if (document.value("done_reason", "") == "length")
        throw std::runtime_error("Ollama の応答が出力上限で打ち切られました。");
    if (document.at("done") != true)
        throw std::runtime_error("Ollama の応答が完了していません。");
    return ParseObject(document.at("message").at("content").get_ref<const std::string&>());
}
}

struct LiveAiClient::Job {
    std::atomic_bool cancelled = false;
    std::atomic_bool ready = false;
    Result result;
    std::mutex progressMutex;
    ProjectResearch::Progress progress;
};

LiveAiClient::~LiveAiClient() { Cancel(); }

void LiveAiClient::Start(const Settings& settings, const Json& context, const std::string& prompt,
    const std::optional<Repair>& repair) {
    if (Busy()) throw std::runtime_error("AI は生成中です。");
    CheckSettings(settings);
    if (prompt.empty() || prompt.size() > 2048) throw std::runtime_error("指示は UTF-8 で 1〜2048 バイトです。");
    auto job = std::make_shared<Job>();
    std::thread worker([job, settings, context, prompt, repair] {
        try {
            const auto response = Request(settings, ChatRequest(settings, context, prompt, repair).dump(),
                job->cancelled, job->result.connected);
            job->result.proposal = Content(response);
        } catch (const std::exception& e) {
            job->result.error = e.what();
        }
        job->ready.store(true, std::memory_order_release);
    });
    worker.detach();
    job_ = std::move(job);
}

void LiveAiClient::StartResearch(Settings settings, std::filesystem::path root, ProjectResearch::Limits limits,
    std::string question, std::vector<ProjectResearch::Turn> history) {
    if (Busy()) throw std::runtime_error("AI は処理中です。");
    ProjectResearch::Validate(limits);
    CheckSettings(settings);
    auto job = std::make_shared<Job>();
    std::thread worker([job, settings = std::move(settings), root = std::move(root), limits,
        question = std::move(question), history = std::move(history)] {
        try {
            const auto deadline = Clock::now() + std::chrono::seconds(limits.seconds);
            const auto check = [&] {
                if (job->cancelled) throw std::runtime_error("調査をキャンセルしました。");
                if (Clock::now() >= deadline) throw std::runtime_error("プロジェクト調査がタイムアウトしました。");
            };
            if (!std::filesystem::is_regular_file(root / "CG2_Setup.vcxproj"))
                throw std::runtime_error("YanEngine のプロジェクトルートで実行してください。");
            // Ask for local model metadata before transmitting any repository text.
            const auto model = ParseObject(Request(settings, Json{{"model", settings.model}}.dump(), job->cancelled,
                job->result.connected, deadline, L"/api/show"));
            if (model.contains("remote_host") || model.contains("remote_model") || !model.contains("details") ||
                !model.at("details").is_object() || model.at("details").value("format", "") != "gguf")
                throw std::runtime_error("プロジェクト調査にはローカルの GGUF モデルが必要です。");
            ProjectResearch::SourceReader source(root, check);
            double loadMs = 0, inferenceMs = 0;
            const auto ask = [&](Json request) {
                check();
                request["model"] = settings.model;
                return Content(Request(settings, request.dump(), job->cancelled, job->result.connected, deadline), &loadMs, &inferenceMs);
            };
            const auto report = [&](const ProjectResearch::Progress& progress) {
                check();
                const std::lock_guard lock(job->progressMutex);
                job->progress = progress;
            };
            auto turn = ProjectResearch::Investigate(source, limits, question, history, ask, report);
            turn.model = settings.model;
            turn.quantization = model.at("details").value("quantization_level", "");
            turn.loadMs = loadMs;
            turn.inferenceMs = inferenceMs;
            job->result.research = std::move(turn);
        } catch (const std::exception& error) {
            job->result.error = error.what();
        }
        job->ready.store(true, std::memory_order_release);
    });
    worker.detach();
    job_ = std::move(job);
}

ProjectResearch::Progress LiveAiClient::ResearchProgress() const {
    if (!job_) return {};
    const std::lock_guard lock(job_->progressMutex);
    return job_->progress;
}

void LiveAiClient::Cancel() noexcept {
    if (job_) job_->cancelled = true;
    job_.reset();
}

std::optional<LiveAiClient::Result> LiveAiClient::Poll() {
    if (!job_ || !job_->ready.load(std::memory_order_acquire)) return std::nullopt;
    auto job = std::move(job_);
    return std::move(job->result);
}
