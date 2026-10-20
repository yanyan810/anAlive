#pragma once
#include "CameraSequence.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

// F7 owns this sibling file. Blender continues to own the exported stage JSON.
class CameraSequenceOverrides {
public:
    std::string stage,error;
    std::vector<CameraSequenceDefinition> sequences;
    static std::string PathFor(const std::string& stagePath) {
        auto path=std::filesystem::path(stagePath); path.replace_extension(".camera_sequences.json"); return path.string();
    }
    static std::string Read(const std::string& path) {
        std::ifstream file(path,std::ios::binary);
        if (!file) throw std::runtime_error("Cannot read camera override: "+path);
        return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    }
    nlohmann::json Json() const { return {{"version",1},{"stage",stage},{"cameraSequences",CameraSequence::ToJson(sequences)}}; }
    bool Load(const std::string& path,const std::string& expectedStage) {
        try {
            CameraSequenceOverrides next; next.stage=expectedStage;
            next.existed_=std::filesystem::exists(path);
            if (next.existed_) {
                next.bytes_=Read(path);
                const auto data=nlohmann::json::parse(next.bytes_);
                if (data.at("version")!=1 || data.at("stage")!=expectedStage || !data.contains("cameraSequences"))
                    throw std::runtime_error("Camera override version/stage/array mismatch");
                next.sequences=CameraSequence::Parse(data);
            }
            *this=std::move(next); return true;
        } catch (const std::exception& e) { error=e.what(); return false; }
    }
    bool Save(const std::string& path) {
        const auto temporary=path+".camera-tmp";
        try {
            (void)CameraSequence::Parse(Json()); // same validator as the game
            const auto unchanged=[&] { return std::filesystem::exists(path)==existed_ && (!existed_ || Read(path)==bytes_); };
            if (!unchanged()) throw std::runtime_error("Camera override changed externally. Reload before saving.");
            const auto bytes=Json().dump(2)+'\n';
            { std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
                if (!file || !(file<<bytes) || !file.flush()) throw std::runtime_error("Cannot write camera override temporary file"); }
            CameraSequenceOverrides checked;
            if (!checked.Load(temporary,stage)) throw std::runtime_error(checked.error);
            if (!unchanged()) throw std::runtime_error("Camera override changed during validation");
            if (existed_) std::filesystem::copy_file(path,path+".camera-backup",std::filesystem::copy_options::overwrite_existing);
#ifdef _WIN32
            if (!MoveFileExW(std::filesystem::path(temporary).c_str(),std::filesystem::path(path).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot replace camera override: "+std::to_string(GetLastError()));
#else
            std::filesystem::rename(temporary,path);
#endif
            existed_=true; bytes_=bytes; error.clear(); return true;
        } catch (const std::exception& e) {
            error=e.what(); std::error_code ignored; std::filesystem::remove(temporary,ignored); return false;
        }
    }
    static std::vector<CameraSequenceDefinition> Merge(std::vector<CameraSequenceDefinition> base,const std::vector<CameraSequenceDefinition>& overrides) {
        for (const auto& d:overrides) {
            const auto it=std::find_if(base.begin(),base.end(),[&](const auto& row){return row.id==d.id;});
            if (it==base.end()) base.push_back(d); else *it=d;
        }
        return base;
    }
private:
    bool existed_=false;
    std::string bytes_;
};
