#pragma once
#include <chrono>
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

// CPU stages can also be recorded by preload workers. No GPU access here.
namespace AssetLoading {
inline bool Option(const char* name, bool fallback=true) {
    char value[16]{};
    const auto length=GetEnvironmentVariableA(name,value,sizeof(value));
    return length && length<sizeof(value) ? std::string(value)!="0" : fallback;
}
struct Event { std::string stage,path; double milliseconds; };
inline std::mutex profileMutex;
inline std::vector<Event> events;
inline bool profiling=Option("YAN_LOAD_PROFILE",false);
class Timer {
    std::string stage_,path_;
    std::chrono::steady_clock::time_point start_=std::chrono::steady_clock::now();
public:
    Timer(std::string stage,std::string path="") : stage_(std::move(stage)),path_(std::move(path)) {}
    ~Timer() {
        if(!profiling) return;
        const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start_).count();
        std::lock_guard lock(profileMutex);
        if(events.size()<10000) events.push_back({stage_,path_,elapsed});
    }
};
inline void SaveProfile(const std::filesystem::path& path) {
    std::lock_guard lock(profileMutex);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file<<"stage,path,milliseconds\n";
    for(const auto& e:events) {
        file<<'"'<<e.stage<<"\",\"";
        for(char c:e.path) { if(c=='"') file<<'"'; file<<c; }
        file<<"\","<<e.milliseconds<<'\n';
    }
}
}
