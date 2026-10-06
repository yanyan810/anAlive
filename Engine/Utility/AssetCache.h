#pragma once
#include "AssetLoadProfile.h"
#include "StringUtility.h"
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <span>
#include <Windows.h>

namespace AssetLoading {
inline uint64_t Hash(std::span<const uint8_t> bytes) {
    uint64_t result=14695981039346656037ull;
    const auto* data=bytes.data();
    for(size_t i=0;i<bytes.size();++i) { result^=data[i]; result*=1099511628211ull; }
    return result;
}
inline std::string Key(const std::string& text) {
    std::ostringstream s;
    s<<std::hex<<std::setw(16)<<std::setfill('0')<<Hash({reinterpret_cast<const uint8_t*>(text.data()),text.size()});
    return s.str();
}
inline std::filesystem::path Path(const std::string& utf8) { return std::filesystem::path(StringUtility::ConvertString(utf8)); }
inline std::string Canonical(const std::string& path) {
    return StringUtility::ConvertString(std::filesystem::weakly_canonical(Path(path)).wstring());
}
inline std::string FileStamp(const std::string& path) {
    const auto p=Path(path);
    if(!std::filesystem::is_regular_file(p)) return Canonical(path)+":missing";
    return Canonical(path)+":"+std::to_string(std::filesystem::file_size(p))+":"+
        std::to_string(std::filesystem::last_write_time(p).time_since_epoch().count());
}
inline std::filesystem::path CachePath(const std::string& kind,const std::string& key,const std::string& ext) {
    auto root=std::filesystem::path("generated/asset-cache");
    char space[128]{}; const auto length=GetEnvironmentVariableA("YAN_ASSET_CACHE_NAMESPACE",space,sizeof(space));
    const std::string name(space);
    if(length && length<sizeof(space) && name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos)
        root/=name;
    return root/kind/(Key(key)+ext);
}
inline std::filesystem::path Temporary(const std::filesystem::path& target) {
    return target.wstring()+L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetCurrentThreadId())+L".tmp";
}
inline bool Publish(const std::filesystem::path& temp,const std::filesystem::path& target) {
    if(MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return true;
    std::error_code error; std::filesystem::remove(temp,error); return false;
}
// Local derived data is optional. Never make a cache error an asset-load error.
inline bool CacheEnabled() { return Option("YAN_ASSET_CACHE"); }
inline bool LoadBlob(const std::filesystem::path& target,std::vector<uint8_t>& data) {
    try {
        std::ifstream file(target,std::ios::binary|std::ios::ate); if(!file) return false;
        const auto size=file.tellg(); if(size<16 || size>512*1024*1024) return false;
        file.seekg(0); uint64_t header[2]{}; file.read(reinterpret_cast<char*>(header),sizeof(header));
        if(header[0]!=0x59414e4444533031ull) return false;
        data.resize(static_cast<size_t>(size)-sizeof(header)); file.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(data.size()));
        return file && Hash(data)==header[1];
    } catch(const std::exception&) { return false; }
}
inline void SaveBlob(const std::filesystem::path& target,std::span<const uint8_t> data) {
    try {
        std::filesystem::create_directories(target.parent_path()); const auto temp=Temporary(target);
        const uint64_t header[]{0x59414e4444533031ull,Hash(data)};
        { std::ofstream file(temp,std::ios::binary); file.write(reinterpret_cast<const char*>(header),sizeof(header));
          file.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size())); if(!file) return; }
        Publish(temp,target);
    } catch(const std::exception&) {}
}
}
