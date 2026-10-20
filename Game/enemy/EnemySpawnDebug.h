#pragma once
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
#include "EnemySpawnSystem.h"
#include "StageProgress.h"
#include "StageWorld.h"
#include "Matrix4x4.h"
#include "imgui.h"

inline void DrawEnemySpawnDebug(const EnemySpawnSystem& system, const Matrix4x4& vp,
    ImVec2 minimum, ImVec2 maximum, bool showSpawns = true, const std::vector<GoalTrigger>* goals = nullptr, const std::vector<StageCollider>* colliders = nullptr) {
    auto* draw = ImGui::GetForegroundDrawList();
    draw->PushClipRect(minimum, maximum, true);
    struct Clip { float x, y, z, w; };
    const auto clip = [&](Vector3 p) -> Clip {
        return {p.x*vp.m[0][0]+p.y*vp.m[1][0]+p.z*vp.m[2][0]+vp.m[3][0],
            p.x*vp.m[0][1]+p.y*vp.m[1][1]+p.z*vp.m[2][1]+vp.m[3][1],
            p.x*vp.m[0][2]+p.y*vp.m[1][2]+p.z*vp.m[2][2]+vp.m[3][2],
            p.x*vp.m[0][3]+p.y*vp.m[1][3]+p.z*vp.m[2][3]+vp.m[3][3]};
    };
    const auto project = [&](Clip p) -> ImVec2 {
        return {minimum.x+(p.x/p.w+1)*.5f*(maximum.x-minimum.x),
            minimum.y+(1-p.y/p.w)*.5f*(maximum.y-minimum.y)};
    };
    const auto line = [&](Vector3 from, Vector3 to, ImU32 color) {
        auto a = clip(from), b = clip(to);
        if (a.z < 0 && b.z < 0) return;
        if ((a.z < 0) != (b.z < 0)) {
            const float t = a.z/(a.z-b.z);
            const Clip intersection{a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, 0, a.w+(b.w-a.w)*t};
            if (a.z < 0) a = intersection; else b = intersection;
        }
        if (a.w > 1e-5f && b.w > 1e-5f) draw->AddLine(project(a), project(b), color, 1.5f);
    };
    const auto box = [&](Vector3 center, Vector3 size, const std::string& label, ImU32 color) {
        Vector3 corners[8];
        for (int i = 0; i < 8; ++i) corners[i] = center + Vector3{
            ((i&1) ? .5f : -.5f)*size.x, ((i&2) ? .5f : -.5f)*size.y, ((i&4) ? .5f : -.5f)*size.z};
        for (int i = 0; i < 8; ++i) for (int bit : {1,2,4})
            if (!(i&bit)) line(corners[i], corners[i|bit], color);
        const auto anchor = clip(center + Vector3{0,size.y*.5f,0});
        if (anchor.z >= 0 && anchor.w > 1e-5f) draw->AddText(project(anchor), color, label.c_str());
    };
    if (showSpawns) for (const auto& trigger : system.Triggers()) {
        const ImU32 color = trigger.active ? IM_COL32(255,180,40,255) : trigger.activated ?
            IM_COL32(130,130,130,255) : IM_COL32(40,220,255,255);
        box(trigger.position, trigger.size, trigger.id, color);
        for (const auto& id : trigger.spawnPointIds)
            if (const auto* point = system.FindPoint(id)) line(trigger.position, point->position, color);
    }
    if (showSpawns) for (const auto& point : system.Points()) box(point.position, {.5f,.5f,.5f}, point.id, IM_COL32(80,255,120,255));
    if (goals) for (const auto& goal : *goals)
        box(goal.position, goal.size, "GOAL: " + goal.id, IM_COL32(255,90,225,255));
    if (colliders) for (const auto& collider : *colliders) {
        Vector3 corners[8];
        for (int i=0;i<8;++i) corners[i]=StagePoint({(i&1) ? collider.local.max.x : collider.local.min.x,
            (i&2) ? collider.local.max.y : collider.local.min.y,(i&4) ? collider.local.max.z : collider.local.min.z},collider.world);
        for (int i=0;i<8;++i) for (int bit : {1,2,4}) if (!(i&bit)) line(corners[i],corners[i|bit],IM_COL32(255,230,60,255));
    }
    draw->PopClipRect();
}
#endif
