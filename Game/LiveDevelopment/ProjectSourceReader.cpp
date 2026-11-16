#include "ProjectResearch.h"
#include "DebugAI/ProjectScanPaths.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>

#pragma comment(lib, "bcrypt.lib")

namespace ProjectResearch {
namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
constexpr size_t kFileBytes = 2 * 1024 * 1024;
constexpr size_t kScanBytes = 24 * 1024 * 1024;
constexpr size_t kScanEntries = 6000;
constexpr size_t kExcerptBytes = 4500;

struct Handle {
    HANDLE value;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

class SourceUnavailable : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::string Lower(std::string_view text) {
    std::string value(text);
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return value;
}

std::string Utf8(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return {bytes.begin(), bytes.end()};
}

std::wstring FinalPath(HANDLE handle) {
    const auto needed = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
    if (needed == 0 || needed > 32768) throw SourceUnavailable("参照先を確認できません。");
    std::wstring path(needed, L'\0');
    const auto written = GetFinalPathNameByHandleW(handle, path.data(), needed, FILE_NAME_NORMALIZED);
    if (!written || written >= needed) throw SourceUnavailable("参照先を確認できません。");
    path.resize(written);
    return path;
}

bool FileExtension(const fs::path& path) {
    static constexpr std::string_view extensions[] = {
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".inl", ".lua", ".json",
        ".cmake", ".sln", ".slnx", ".vcxproj", ".filters", ".props", ".targets", ".md"
    };
    return Lower(Utf8(path.filename())) == "cmakelists.txt" ||
        std::ranges::find(extensions, Lower(Utf8(path.extension()))) != std::end(extensions);
}

bool AllowedComponents(const fs::path& path) {
    if (path.empty() || path.has_root_path() || IsExcludedProjectPath(path, true)) return false;
    for (const auto& part : path) {
        const auto name = Lower(Utf8(part));
        if (name.empty() || name.front() == '.' || name.back() == '.' || name.back() == ' ' ||
            name.find_first_of(":<>|\"?*") != std::string::npos ||
            name.find_first_of("\r\n\t") != std::string::npos ||
            name.find("secret") != std::string::npos || name.find("credential") != std::string::npos ||
            name.find("token") != std::string::npos || name.find("apikey") != std::string::npos ||
            name.find("api_key") != std::string::npos || name.find("password") != std::string::npos ||
            name.find(".local") != std::string::npos || name.ends_with(".user") ||
            name == "cache" || name == "caches" || name == "dumps" || name == "archive" ||
            name == "development" || name == "out" || name == "output" || name == "logs" ||
            name == "profiles" || name == "cmakeuserpresets.json" || name == "project_scan.json") return false;
        const auto stem = name.substr(0, name.find('.'));
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            ((stem.starts_with("com") || stem.starts_with("lpt")) && stem.size() == 4 &&
                stem.back() >= '0' && stem.back() <= '9')) return false;
    }
    return true;
}

fs::path RelativePath(std::string_view raw) {
    if (raw.empty() || raw.size() > 512 || raw.find('\0') != std::string_view::npos)
        throw SourceUnavailable("プロジェクト相対パスを指定してください。");
    for (unsigned char c : raw) if (c < 32 || c == 127) throw SourceUnavailable("パスに無効な文字があります。");
    fs::path path = fs::path(std::u8string(raw.begin(), raw.end()));
    if (!AllowedComponents(path))
        throw SourceUnavailable("対象外のパスです。");
    return path.lexically_normal();
}

bool Sensitive(std::string_view text) {
    const auto lower = Lower(text);
    if (lower.find("private key" "-----") != std::string::npos) return true;
    // Detect recognizable credentials even in an otherwise allowlisted source file.
    constexpr std::string_view prefixes[] = {"sk" "-", "ghp" "_", "github" "_pat_", "gho" "_", "xoxb" "-", "glpat" "-", "akia", "aiza"};
    for (const auto prefix : prefixes) {
        size_t at = lower.find(prefix);
        while (at != std::string::npos) {
            size_t end = at + prefix.size();
            while (end < lower.size() && (std::isalnum(static_cast<unsigned char>(lower[end])) || lower[end] == '_' || lower[end] == '-')) ++end;
            if (end - at - prefix.size() >= 14) return true;
            at = lower.find(prefix, end);
        }
    }
    constexpr std::string_view labels[] = {
        "apikey", "api_key", "api-key", "password", "credential", "secret", "access_token", "refresh_token",
        "accesstoken", "refreshtoken", "client_secret", "clientsecret", "private_key", "authorization", "bearer",
        "token", "aws_access_key_id"
    };
    for (const auto label : labels) {
        for (auto at = lower.find(label); at != std::string::npos; at = lower.find(label, at + label.size())) {
            const auto tail = lower.substr(at + label.size(), 160);
            const auto assignment = tail.find_first_not_of(" \"'\t\r\n");
            if (assignment == std::string::npos) continue;
            if (tail[assignment] != '=' && tail[assignment] != ':') {
                if (label == "bearer" && assignment > 0 && assignment < 4) {
                    const auto end = tail.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_.-", assignment);
                    if (std::min(tail.size(), end) - assignment >= 16) return true;
                }
                continue;
            }
            const auto start = tail.find_first_not_of(" \t\r\n", assignment + 1);
            if (start == std::string::npos) continue;
            if (tail[start] == '"' || tail[start] == '\'') {
                const auto end = tail.find(tail[start], start + 1);
                if (end == std::string::npos || end > start + 1) return true;
            } else {
                const auto end = tail.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_.-", start);
                if (std::min(tail.size(), end) - start >= 10) return true;
            }
        }
    }
    return false;
}

std::string Hash(std::string& bytes) {
    std::array<UCHAR, 32> digest{};
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(bytes.data()),
        static_cast<ULONG>(bytes.size()), digest.data(), static_cast<ULONG>(digest.size())) < 0)
        throw SourceUnavailable("ファイルの版を確認できません。");
    constexpr auto hex = "0123456789abcdef";
    std::string hash;
    for (auto byte : digest) { hash += hex[byte >> 4]; hash += hex[byte & 15]; }
    return hash;
}

std::vector<std::string_view> Lines(std::string_view bytes) {
    if (bytes.starts_with("\xef\xbb\xbf")) bytes.remove_prefix(3);
    std::vector<std::string_view> lines;
    while (!bytes.empty()) {
        const auto newline = bytes.find('\n');
        auto line = bytes.substr(0, newline);
        if (line.ends_with('\r')) line.remove_suffix(1);
        lines.push_back(line);
        if (newline == std::string_view::npos) break;
        if (lines.size() >= 180000) throw SourceUnavailable("行数が上限を超えています。");
        bytes.remove_prefix(newline + 1);
    }
    return lines;
}

std::string Excerpt(const std::vector<std::string_view>& lines, int first, int count, int& last) {
    std::string result;
    last = first - 1;
    for (int i = first; i <= static_cast<int>(lines.size()) && i < first + count; ++i) {
        auto line = lines[i - 1];
        const bool shortened = line.size() > 1000;
        if (shortened) {
            size_t pos = 1000;
            while ((static_cast<unsigned char>(line[pos]) & 0xc0) == 0x80) --pos;
            line = line.substr(0, pos);
        }
        const auto entry = std::to_string(i) + " | " + std::string(line) + (shortened ? " …\n" : "\n");
        if (result.size() + entry.size() > kExcerptBytes) break;
        result += entry;
        last = i;
    }
    return result;
}

bool TokenMatch(std::string_view lower, std::string_view symbol) {
    const auto word = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; };
    for (auto p = lower.find(symbol); p != std::string_view::npos; p = lower.find(symbol, p + 1)) {
        const auto end = p + symbol.size();
        if ((p == 0 || !word(lower[p - 1])) && (end == lower.size() || !word(lower[end]))) return true;
    }
    return false;
}
}

struct SourceReader::File {
    std::string bytes;
};

SourceReader::SourceReader(const fs::path& root, std::function<void()> check) :
    root_(fs::canonical(root)), check_(std::move(check)) {
    const Handle handle{CreateFileW(root_.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (handle.value == INVALID_HANDLE_VALUE || !fs::is_directory(root_))
        throw SourceUnavailable("プロジェクトルートを開けません。");
    physicalRoot_ = FinalPath(handle.value);
    if (!physicalRoot_.ends_with(L'\\')) physicalRoot_ += L'\\';
}

SourceReader::File SourceReader::Load(const fs::path& relative) const {
    check_();
    if (!AllowedComponents(relative) || !FileExtension(relative)) throw SourceUnavailable("対象外のファイルです。");
    auto target = root_;
    for (const auto& part : relative) {
        target /= part;
        const auto attributes = GetFileAttributesW(target.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
            throw SourceUnavailable("ファイルがありません。またはリンクを含みます。");
    }
    // Following the final name is safe only after checking the opened handle itself.
    const Handle handle{CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    BY_HANDLE_FILE_INFORMATION info{};
    if (handle.value == INVALID_HANDLE_VALUE || GetFileType(handle.value) != FILE_TYPE_DISK ||
        !GetFileInformationByHandle(handle.value, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) ||
        info.nNumberOfLinks != 1)
        throw SourceUnavailable("通常のソースファイルを開けません。");
    if (info.nFileSizeHigh || info.nFileSizeLow > kFileBytes) throw SourceUnavailable("ファイルサイズが上限を超えています。");
    const auto physical = FinalPath(handle.value);
    const auto prefix = static_cast<int>(physicalRoot_.size());
    if (physical.size() <= physicalRoot_.size() ||
        CompareStringOrdinal(physical.data(), prefix, physicalRoot_.data(), prefix, TRUE) != CSTR_EQUAL)
        throw SourceUnavailable("プロジェクト外の参照です。");
    const auto finalRelative = fs::path(physical.substr(physicalRoot_.size()));
    if (!AllowedComponents(finalRelative) || !FileExtension(finalRelative)) throw SourceUnavailable("参照先は対象外です。");
    File file;
    file.bytes.resize(info.nFileSizeLow);
    DWORD read = 0;
    if (!ReadFile(handle.value, file.bytes.data(), info.nFileSizeLow, &read, nullptr) || read != info.nFileSizeLow)
        throw SourceUnavailable("ソースを読み取れません。");
    if (file.bytes.find('\0') != std::string::npos ||
        (!file.bytes.empty() && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, file.bytes.data(),
            static_cast<int>(file.bytes.size()), nullptr, 0) == 0) || Sensitive(file.bytes))
        throw SourceUnavailable("UTF-8テキストの条件を満たさないか、機密情報の候補を検出しました。");
    return file;
}

Evidence SourceReader::Read(std::string_view raw, int first, int count) const {
    if (first < 1 || first > 180000 || count < 1 || count > 120)
        throw SourceUnavailable("行範囲が無効です。");
    const auto path = RelativePath(raw);
    auto file = Load(path);
    const auto lines = Lines(file.bytes);
    if (first > static_cast<int>(lines.size())) throw SourceUnavailable("指定行がファイルの範囲外です。");
    Evidence evidence{Utf8(path), Hash(file.bytes), first, first, {}};
    evidence.excerpt = Excerpt(lines, first, count, evidence.last);
    return evidence;
}

std::optional<Evidence> SourceReader::FindRange(std::string_view raw, std::string_view marker, int count, int before) const {
    try {
        const auto path = RelativePath(raw);
        auto file = Load(path);
        const auto lines = Lines(file.bytes);
        const auto match = std::ranges::find_if(lines, [&](std::string_view line) { return line.find(marker) != std::string_view::npos; });
        if (match == lines.end()) return std::nullopt;
        const int first = std::max(1, static_cast<int>(match - lines.begin()) + 1 - before);
        Evidence evidence{Utf8(path), Hash(file.bytes), first, first, {}};
        evidence.excerpt = Excerpt(lines, first, count, evidence.last);
        return evidence;
    } catch (const SourceUnavailable&) {
        return std::nullopt;
    }
}

bool SourceReader::StillCurrent(const Evidence& evidence) const {
    try {
        if (evidence.first < 1 || evidence.last < evidence.first || evidence.last > 180000 ||
            evidence.last - evidence.first >= 120 || evidence.excerpt.empty()) return false;
        const auto current = Read(evidence.path, evidence.first, evidence.last - evidence.first + 1);
        return current.path == evidence.path && current.version == evidence.version &&
            current.first == evidence.first && current.last == evidence.last && current.excerpt == evidence.excerpt;
    } catch (const SourceUnavailable&) {
        return false;
    }
}

Json SourceReader::Execute(const Json& operation, const Limits& limits, std::string_view focus) const {
    try {
        const auto& kind = operation.at("operation");
        const auto& rawPath = operation.at("path").get_ref<const std::string&>();
        const auto encode = [](const Evidence& evidence) -> Json {
            return {{"path", evidence.path}, {"sha256", evidence.version}, {"first", evidence.first},
                {"last", evidence.last}, {"excerpt", evidence.excerpt}};
        };
        if (kind == "read_file") {
            const auto& start = operation.at("first");
            const auto& count = operation.at("lines");
            if (!start.is_number_integer() || !count.is_number_integer() || start < 1 || start > 180000 || count < 1)
                throw SourceUnavailable("行範囲が無効です。");
            return Json::array({encode(Read(rawPath, start.get<int>(), count > limits.lines ? limits.lines : count.get<int>()))});
        }
        if (kind != "search_files" && kind != "search_text" && kind != "find_symbol")
            throw SourceUnavailable("未許可の調査操作です。");
        const auto query = Lower(operation.at("query").get_ref<const std::string&>());
        const auto focusText = Lower(focus);
        const auto extension = Lower(operation.at("extension").get_ref<const std::string&>());
        const bool filenames = kind == "search_files";
        const bool symbols = kind == "find_symbol";
        if ((query.empty() && (!filenames || (rawPath.empty() && extension.empty()))) ||
            query.size() > 100 || query.find('\0') != std::string::npos || extension.size() > 12)
            throw SourceUnavailable("検索キーワードが無効です。");
        const auto relative = rawPath.empty() ? fs::path{} : RelativePath(rawPath);
        const auto directory = root_ / relative;
        const bool singleFile = !relative.empty() && FileExtension(relative) && !filenames;
        if (!singleFile) {
            auto actual = root_;
            for (const auto& component : relative) {
                actual /= component;
                const auto attributes = GetFileAttributesW(actual.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    throw SourceUnavailable("検索ディレクトリがありません。または対象外です。");
            }
        }
        struct Match { int score; Json result; };
        std::vector<Match> matches;
        const auto add = [&](int score, Json result) {
            if (matches.size() < static_cast<size_t>(limits.results)) {
                matches.push_back({score, std::move(result)});
            } else {
                auto lowest = std::ranges::min_element(matches, {}, &Match::score);
                if (score > lowest->score) *lowest = {score, std::move(result)};
            }
        };
        size_t scannedBytes = 0;
        const auto record = [&](const fs::path& path) {
            if (!FileExtension(path) || (!extension.empty() && Lower(Utf8(path.extension())) != extension)) return;
            const auto name = Utf8(path);
            const auto lowerName = Lower(name);
            if (filenames && lowerName.find(query) == std::string::npos) return;
            try {
                auto file = Load(path);
                scannedBytes += file.bytes.size();
                const auto sourceDir = lowerName.starts_with("game/") || lowerName.starts_with("engine/");
                const int nameScore = (sourceDir ? 8 : 0) + (Lower(Utf8(path.stem())) == query ? 20 :
                    (lowerName.find(query) != std::string::npos ? 10 : 0));
                const auto lines = Lines(file.bytes);
                int suggestedFirst = 0;
                if (!focusText.empty()) {
                    const auto focusLine = std::ranges::find_if(lines, [&](std::string_view line) {
                        line.remove_prefix(std::min(line.size(), line.find_first_not_of(" \t")));
                        const auto lower = Lower(line);
                        return lower.starts_with(focusText) || lower.starts_with("inline " + focusText);
                    });
                    if (focusLine != lines.end()) suggestedFirst = std::max(1, static_cast<int>(focusLine - lines.begin()) - 2);
                }
                const int fileScore = nameScore + (suggestedFirst ? 24 : 0);
                if (filenames) {
                    add(fileScore, {{"path", name}, {"suggestedFirst", suggestedFirst ? suggestedFirst : 1}});
                    return;
                }
                std::string version;
                int fileHits = 0;
                for (size_t i = 0; i < lines.size(); ++i) {
                    const auto line = Lower(lines[i]);
                    if (symbols ? !TokenMatch(line, query) : line.find(query) == std::string::npos) continue;
                    if (version.empty()) version = Hash(file.bytes);
                    const bool declaration = symbols && (line.find("class " + query) != std::string::npos ||
                        line.find("struct " + query) != std::string::npos || line.find(query + "(") != std::string::npos);
                    const int first = static_cast<int>(i) + 1;
                    int last = first;
                    auto value = encode({name, version, first, first, Excerpt(lines, first, 1, last)});
                    value["matchType"] = "text_match";
                    value["suggestedFirst"] = suggestedFirst ? suggestedFirst : std::max(1, first - 2);
                    if (declaration) value["hint"] = "possible_declaration_or_call";
                    const int qualified = line.find("::" + query + "(") != std::string::npos ? 10 : 0;
                    add(fileScore + qualified + (declaration ? 5 : 0), std::move(value));
                    if (++fileHits >= limits.results) break;
                }
            } catch (const SourceUnavailable&) {
                // Excluded content never contributes a filename or excerpt.
            }
        };
        bool partial = false;
        if (singleFile) {
            record(relative);
        } else {
            std::error_code error;
            fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied, error), end;
            size_t entries = 0;
            for (; it != end; it.increment(error)) {
                check_();
                if (++entries > kScanEntries || scannedBytes >= kScanBytes) { partial = true; break; }
                const auto path = it->path().lexically_relative(root_);
                const auto attrs = GetFileAttributesW(it->path().c_str());
                if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) ||
                    !AllowedComponents(path)) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) record(path);
                if (error) error.clear();
            }
        }
        std::ranges::stable_sort(matches, std::greater{}, &Match::score);
        Json results = Json::array();
        for (auto& match : matches) {
            if (results.size() >= static_cast<size_t>(limits.results)) break;
            results.push_back(std::move(match.result));
        }
        if (partial) return {{"matches", std::move(results)}, {"partial", true}};
        return results;
    } catch (const SourceUnavailable& error) {
        return {{"error", error.what()}};
    }
}
}
