#include <Windows.h>
#include <filesystem>
#include "GameApp.h"
#include "CrashHandler.h"
#include "D3DResourceLeakChecker.h"

#define UTF8_LITERAL_IMPL(value) u8##value
#define UTF8_LITERAL(value) UTF8_LITERAL_IMPL(value)

namespace {
bool SetResourceWorkingDirectory() {
    namespace fs = std::filesystem;
    if (fs::exists("resources/shaders/Object3d.VS.hlsl")) return true;
    wchar_t executable[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, executable, MAX_PATH)) {
        const fs::path directory = fs::path(executable).parent_path();
        if (fs::is_directory(directory / "resources")) {
            fs::current_path(directory);
            return true;
        }
    }
    // Development builds use /FC so this points to the source project.
    const fs::path project = fs::path(UTF8_LITERAL(__FILE__)).parent_path();
    if (fs::is_directory(project / "resources")) {
        fs::current_path(project);
        return true;
    }
    return false;
}
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    D3DResourceLeakChecker leakCheck;
    CrashHandler::Install();
    if (!SetResourceWorkingDirectory()) {
        MessageBoxW(nullptr, L"The resources folder could not be found.", L"FPS Foundation", MB_OK | MB_ICONERROR);
        return -1;
    }
    GameApp app;
    return app.Run();
}

