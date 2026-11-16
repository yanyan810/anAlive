#pragma once

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <set>

inline bool IsExcludedProjectPath(const std::filesystem::path& path, bool includeGameDebugSources = false) {
    static const std::set<std::wstring> excluded = {
        L".git", L".vs", L"generated", L"build", L"debug", L"release", L"x64", L"x86",
        L"externals", L"node_modules", L"packages", L"obj", L"bin"
    };
    std::filesystem::path prefix;
    for (const auto& component : path) {
        std::wstring name = component.wstring();
        std::transform(name.begin(), name.end(), name.begin(), ::towlower);
        if (excluded.contains(name) && !(includeGameDebugSources && name == L"debug" && prefix == L"game/scene")) return true;
        if (includeGameDebugSources) prefix /= name;
    }
    return false;
}
