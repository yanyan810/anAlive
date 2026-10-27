#pragma once
#include "GameApp.h"
#include "GameOverOverlay.h"
#include "ImGuiManagaer.h"
#include "WinApp.h"
#include <optional>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

inline std::optional<Vector2> SceneMenuPointer(GameApp& app) {
    RECT rect{};
#ifdef USE_IMGUI
    if (!app.ImGui()->IsSceneImageHovered() || !app.ImGui()->GetSceneImageRect(rect)) return {};
#else
    GetClientRect(app.Win()->GetHwnd(),&rect);
    POINT origin{}; ClientToScreen(app.Win()->GetHwnd(),&origin); OffsetRect(&rect,origin.x,origin.y);
#endif
    POINT cursor{};
    if (rect.right<=rect.left || rect.bottom<=rect.top || !GetCursorPos(&cursor) || !PtInRect(&rect,cursor)) return {};
    return Vector2{static_cast<float>(cursor.x-rect.left)*WinApp::kClientWidth/(rect.right-rect.left),
        static_cast<float>(cursor.y-rect.top)*WinApp::kClientHeight/(rect.bottom-rect.top)};
}

// Shared by stage death and title demos. Returns 0=Retry / 1=Title only after release.
inline std::optional<int> UpdateGameOverMenuInput(GameApp& app,GameOverOverlay& overlay) {
    auto& input=*app.GetInput();
    if (!overlay.Armed()) {
        if (!input.IsLeftMousePressed() && !input.IsKeyPressed(DIK_RETURN) && !input.IsKeyPressed(DIK_NUMPADENTER) && !input.IsKeyPressed(DIK_SPACE)) overlay.Arm();
        return {};
    }
    if (!input.HasFocus()) return {};
#ifdef USE_IMGUI
    if (ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive()) return {};
#endif
    if (input.IsKeyTrigger(DIK_UP) || input.IsKeyTrigger(DIK_DOWN) || input.IsKeyTrigger(DIK_W) || input.IsKeyTrigger(DIK_S)) overlay.Toggle();
    if (input.IsLeftMouseTrigger()) if (const auto p=SceneMenuPointer(app)) {
        const int hit=overlay.Hit(*p);
        if (hit>=0) { overlay.Select(hit); return hit; }
    }
    if (input.IsKeyTrigger(DIK_RETURN) || input.IsKeyTrigger(DIK_NUMPADENTER) || input.IsKeyTrigger(DIK_SPACE)) return overlay.Selected();
    return {};
}
