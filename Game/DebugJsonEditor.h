#pragma once
#if defined(_DEBUG) || defined(USE_IMGUI)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <iterator>
#include <cmath>

class DebugJsonEditor {
public:
    nlohmann::json document;
    std::string error, path;
    bool dirty=false;
    static void SetNumber(nlohmann::json& value,double number) {
        if (!std::isfinite(number)) return;
        // A JSON integer may represent a floating gameplay field (e.g. position=3).
        // Preserve integral types when possible; production validators reject fractional counts.
        if (value.is_number_integer() && std::trunc(number)==number && number>=-9.0e15 && number<=9.0e15)
            value=static_cast<int64_t>(number);
        else value=number;
    }
    bool Open(const std::string& file) {
        try {
            const auto bytes=Read(file);
            auto parsed=nlohmann::json::parse(bytes);
            path=file; original_=bytes; document=std::move(parsed); dirty=false; error.clear(); return true;
        } catch (const std::exception& e) { error=e.what(); return false; }
    }
    // Validate a sibling temporary file through the production loaders before replacing anything.
    bool Save(const std::function<std::string(const std::string&)>& validate) {
        std::filesystem::path temporary;
        try {
            if (path.empty()) throw std::runtime_error("Select a JSON file first");
            if (Read(path)!=original_) throw std::runtime_error("File changed externally. Reload before saving.");
            const auto bytes=document.dump(2)+'\n';
            temporary=std::filesystem::path(path).concat(".debug-tmp");
            { std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
                if (!file || !(file<<bytes) || !file.flush()) throw std::runtime_error("Cannot write temporary JSON"); }
            const auto validation=validate(temporary.string());
            if (!validation.empty()) throw std::runtime_error(validation);
            if (Read(path)!=original_) throw std::runtime_error("File changed externally during validation");
            std::filesystem::copy_file(path,path+".debug-backup",std::filesystem::copy_options::overwrite_existing);
            if (!MoveFileExW(temporary.c_str(),std::filesystem::path(path).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot replace JSON (Windows error "+std::to_string(GetLastError())+")");
            original_=bytes; dirty=false; error.clear(); return true;
        } catch (const std::exception& e) {
            error=e.what(); std::error_code ignored;
            if (!temporary.empty()) std::filesystem::remove(temporary,ignored);
            return false;
        }
    }
private:
    std::string original_;
    static std::string Read(const std::string& path) {
        std::ifstream file(path,std::ios::binary);
        if (!file) throw std::runtime_error("Cannot open "+path);
        return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    }
};
#endif
