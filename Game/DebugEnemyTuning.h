#pragma once
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
#include "DebugJsonEditor.h"
#include "EnemyDefinition.h"
// Save only the live dimension fields; retain all other types and tuning values.
inline bool SaveEnemyDimensions(const std::string& path,const EnemyDefinition& value,std::string& error) {
    DebugJsonEditor editor;
    if (!editor.Open(path)) { error=editor.error; return false; }
    EnemyDefinitions definitions;
    if (!definitions.Load(path)) { error=definitions.Error(); return false; }
    if (!definitions.Find(value.id)) { error="Enemy ID not found: "+value.id; return false; }
    for (auto& item : editor.document.at("enemies")) {
        if (item.at("id").get<std::string>()!=value.id) continue;
        const auto v=value.visualScaleMultiplier;
        item["visualScale"]={v.x,v.y,v.z};
        item["collisionRadius"]=value.collisionRadius;
        item["collisionHeight"]=value.collisionHeight;
    }
    const bool saved=editor.Save([](const std::string& file) {
        EnemyDefinitions next; return next.Load(file) ? std::string{} : next.Error();
    });
    error=editor.error; return saved;
}
inline bool LoadEnemyDimensions(const std::string& path,EnemyDefinition& value,std::string& error) {
    EnemyDefinitions definitions;
    if (!definitions.Load(path)) { error=definitions.Error(); return false; }
    const auto* source=definitions.Find(value.id);
    if (!source) { error="Enemy ID not found: "+value.id; return false; }
    value.visualScaleMultiplier=source->visualScaleMultiplier;
    value.collisionRadius=source->collisionRadius; value.collisionHeight=source->collisionHeight;
    error.clear(); return true;
}
#endif
