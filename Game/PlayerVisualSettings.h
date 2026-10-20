#pragma once
#include <nlohmann/json.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

// Stage-owned presentation data. Never changes Player/FPS rotation or enemy placement.
struct PlayerVisualSettings {
    float yawOffsetDegrees=0;
    static PlayerVisualSettings FromStage(const nlohmann::json& stage) {
        PlayerVisualSettings result;
        if (!stage.contains("playerVisual")) return result;
        const auto& value=stage.at("playerVisual");
        if (!value.is_object()) throw std::runtime_error("playerVisual must be an object");
        if (value.contains("yawOffsetDegrees")) {
            const auto& yaw=value.at("yawOffsetDegrees");
            if (!yaw.is_number()) throw std::runtime_error("playerVisual.yawOffsetDegrees must be a number");
            result.yawOffsetDegrees=yaw.get<float>();
        }
        if (!std::isfinite(result.yawOffsetDegrees) || std::abs(result.yawOffsetDegrees)>180)
            throw std::runtime_error("playerVisual.yawOffsetDegrees must be between -180 and 180 degrees");
        return result;
    }
    float WorldYaw(float playerYaw) const { return playerYaw+yawOffsetDegrees*.01745329251994329577f; }
    static std::string ReadBytes(const std::string& path) {
        std::ifstream file(path,std::ios::binary);
        if (!file) throw std::runtime_error("Cannot read stage: "+path);
        return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    }
    // Surgical root-field update: even whitespace/number spelling in camera paths,
    // spawn settings and unknown stage fields remains byte-for-byte unchanged.
    std::string UpdatedStageBytes(const std::string& original,const std::string& expectedStage) const {
        const auto data=nlohmann::json::parse(original);
        if (data.at("stage").at("id")!=expectedStage) throw std::runtime_error("Player Visual stage ID mismatch");
        auto value=data.value("playerVisual",nlohmann::json::object());
        if (!value.is_object()) throw std::runtime_error("playerVisual must be an object");
        if (!std::isfinite(yawOffsetDegrees) || std::abs(yawOffsetDegrees)>180) throw std::runtime_error("Invalid Player Visual yaw");
        value["yawOffsetDegrees"]=yawOffsetDegrees;
        (void)FromStage(nlohmann::json{{"playerVisual",value}});
        const auto replacement=value.dump();
        size_t targetStart=std::string::npos,targetEnd=0;
        size_t cursor=original.find('{')+1;
        while (cursor<original.size()) {
            SkipSpace(original,cursor);
            if (original[cursor]=='}') break;
            const size_t keyStart=cursor;
            SkipString(original,cursor);
            const auto key=nlohmann::json::parse(original.substr(keyStart,cursor-keyStart)).get<std::string>();
            SkipSpace(original,cursor); ++cursor; // colon; the full document was validated above
            SkipSpace(original,cursor);
            const size_t start=cursor;
            SkipValue(original,cursor);
            if (key=="playerVisual") {
                if (targetStart!=std::string::npos) throw std::runtime_error("Duplicate playerVisual fields");
                targetStart=start; targetEnd=cursor;
            }
            SkipSpace(original,cursor);
            if (original[cursor]==',') ++cursor;
        }
        auto bytes=original;
        if (targetStart!=std::string::npos) { bytes.replace(targetStart,targetEnd-targetStart,replacement); return bytes; }
        const size_t end=original.find_last_of('}');
        const size_t last=original.find_last_not_of(" \t\r\n",end-1);
        bytes.insert(last+1,",\n  \"playerVisual\": "+replacement);
        return bytes;
    }
    bool SaveStage(const std::string& path,const std::string& expectedStage,const std::string& expectedBytes,std::string& error) const {
        const auto temporary=path+".player-visual-tmp";
        try {
            if (ReadBytes(path)!=expectedBytes) throw std::runtime_error("Stage changed externally. Reload Player Visual before saving.");
            const auto bytes=UpdatedStageBytes(expectedBytes,expectedStage);
            { std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
                if (!file || !(file<<bytes) || !file.flush()) throw std::runtime_error("Cannot write temporary Player Visual settings"); }
            const auto checked=nlohmann::json::parse(ReadBytes(temporary));
            (void)FromStage(checked);
            if (ReadBytes(path)!=expectedBytes) throw std::runtime_error("Stage changed externally during save");
            std::filesystem::copy_file(path,path+".player-visual-backup",std::filesystem::copy_options::overwrite_existing);
#ifdef _WIN32
            if (!MoveFileExW(std::filesystem::path(temporary).c_str(),std::filesystem::path(path).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot replace stage Player Visual settings: "+std::to_string(GetLastError()));
#else
            std::filesystem::rename(temporary,path);
#endif
            error.clear(); return true;
        } catch (const std::exception& e) {
            error=e.what(); std::error_code ignored; std::filesystem::remove(temporary,ignored); return false;
        }
    }
private:
    static void SkipSpace(const std::string& s,size_t& i) {
        while (i<s.size() && (s[i]==' ' || s[i]=='\t' || s[i]=='\r' || s[i]=='\n')) ++i;
    }
    static void SkipString(const std::string& s,size_t& i) {
        ++i;
        while (i<s.size()) {
            const char c=s[i++];
            if (c=='\\') ++i;
            else if (c=='"') return;
        }
    }
    static void SkipValue(const std::string& s,size_t& i) {
        if (s[i]=='"') { SkipString(s,i); return; }
        if (s[i]=='{' || s[i]=='[') {
            int depth=0;
            do {
                const char c=s[i];
                if (c=='"') { SkipString(s,i); continue; }
                if (c=='{' || c=='[') ++depth;
                if (c=='}' || c==']') --depth;
                ++i;
            } while (depth>0 && i<s.size());
        } else while (i<s.size() && s[i]!=',' && s[i]!='}' && s[i]!=' ' && s[i]!='\r' && s[i]!='\n' && s[i]!='\t') ++i;
    }
};
