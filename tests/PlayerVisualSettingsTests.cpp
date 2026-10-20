#include "StageLoader.h"
#include <cassert>
#include <iostream>
#include <limits>
using nlohmann::json;
int main() {
    assert(PlayerVisualSettings::FromStage(json::object()).yawOffsetDegrees==0);
    for (float value:{-180.f,-90.f,0.f,90.f,180.f}) {
        const auto s=PlayerVisualSettings::FromStage({{"playerVisual",{{"yawOffsetDegrees",value}}}});
        assert(s.yawOffsetDegrees==value);
        assert(std::abs(s.WorldYaw(.7f)-(.7f+value*.01745329251994329577f))<1e-6f);
    }
    for (const auto& value:std::vector<json>{true,"90",nullptr,181,-181}) {
        bool rejected=false;
        try { (void)PlayerVisualSettings::FromStage({{"playerVisual",{{"yawOffsetDegrees",value}}}}); }
        catch (const std::exception&) { rejected=true; } assert(rejected);
    }
    for (const auto& visual:std::vector<json>{nullptr,3,json::array()}) {
        bool rejected=false; try { (void)PlayerVisualSettings::FromStage({{"playerVisual",visual}}); }
        catch (const std::exception&) { rejected=true; } assert(rejected);
    }
    PlayerVisualSettings settings; settings.yawOffsetDegrees=-33.25f;
    for (const auto* stage:{"stage01","stage02","stage03"}) {
        const auto path=std::string("../../resources/levels/")+stage+"/"+stage+".json";
        const auto original=PlayerVisualSettings::ReadBytes(path);
        StageLoader loader; assert(loader.Load(path) && loader.playerVisualWarning.empty() && loader.playerVisual.yawOffsetDegrees==90);
        const auto edited=settings.UpdatedStageBytes(original,stage);
        auto before=json::parse(original),after=json::parse(edited);
        assert(after.at("playerVisual").at("yawOffsetDegrees")==-33.25f);
        before.erase("playerVisual"); after.erase("playerVisual"); assert(before==after);
        auto exact=original;
        const std::string old="{\"yawOffsetDegrees\":90}";
        exact.replace(exact.find(old),old.size(),"{\"yawOffsetDegrees\":-33.25}");
        assert(edited==exact); // all unrelated bytes, including camera sequences, stay identical
        const std::string fixture="player-visual-stage.json";
        { std::ofstream file(fixture,std::ios::binary); file<<original; }
        std::string error;
        assert(settings.SaveStage(fixture,stage,original,error));
        assert(PlayerVisualSettings::ReadBytes(fixture)==edited);
        assert(PlayerVisualSettings::ReadBytes(fixture+".player-visual-backup")==original);
        assert(loader.Load(fixture) && loader.playerVisual.yawOffsetDegrees==-33.25f);
        assert(!settings.SaveStage(fixture,stage,original,error)); // stale draft cannot overwrite external data
        assert(PlayerVisualSettings::ReadBytes(fixture)==edited);
        auto invalid=json::parse(original); invalid["playerVisual"]["yawOffsetDegrees"]="bad";
        { std::ofstream file(fixture); file<<invalid; }
        assert(loader.Load(fixture) && !loader.playerVisualWarning.empty() && loader.playerVisual.yawOffsetDegrees==0);
        invalid.erase("playerVisual"); { std::ofstream file(fixture); file<<invalid; }
        assert(loader.Load(fixture) && loader.playerVisual.yawOffsetDegrees==0);
        const auto legacy=PlayerVisualSettings::ReadBytes(fixture);
        assert(settings.SaveStage(fixture,stage,legacy,error));
        assert(loader.Load(fixture) && loader.playerVisual.yawOffsetDegrees==-33.25f);
    }
    // Nested keys, escaped strings and future model properties must survive a field update.
    const std::string complex=R"({ "stage":{"id":"test"}, "notes":"escaped \"playerVisual\": { }", "nested":{"playerVisual":[1,{"x":2}]}, "playerVisual":{"yawOffsetDegrees":0,"futureModel":{"path":"final.gltf","forward":"+Z"}}, "tail": 1.2000e2 })";
    const auto result=settings.UpdatedStageBytes(complex,"test");
    const auto parsed=json::parse(result);
    assert(parsed["playerVisual"]["futureModel"]["forward"]=="+Z");
    assert(result.ends_with(", \"tail\": 1.2000e2 }"));
    const auto duplicate=R"({"stage":{"id":"test"},"playerVisual":{},"playerVisual":{}})";
    bool rejected=false; try { (void)settings.UpdatedStageBytes(duplicate,"test"); } catch (const std::exception&) { rejected=true; } assert(rejected);
    std::cout<<"PASS: Player Visual JSON/defaults/range, stage loader, isolated byte-preserving save/reload, backups, conflict guard, legacy and malformed fallback, escaped/nested/future fields\n";
}
