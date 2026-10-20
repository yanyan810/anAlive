#include "StageLoader.h"
#include <cassert>
#include <iostream>

int main() {
    using nlohmann::json;
    const auto write=[](const std::string& path,const std::string& bytes) { std::ofstream(path,std::ios::binary|std::ios::trunc)<<bytes; };
    for (const auto* id:{"stage01","stage02","stage03"}) {
        const std::string stage=id;
        const std::string source="../../resources/levels/"+stage+"/"+stage+".json";
        const auto original=CameraSequenceOverrides::Read(source);
        const std::string fixture="camera-override-"+stage+".json";
        const auto path=CameraSequenceOverrides::PathFor(fixture);
        std::filesystem::remove(path); write(fixture,original);
        StageLoader base; assert(base.Load(fixture) && base.cameraSequences.size()==1 && base.cameraOverrideWarning.empty());
        const auto baseJson=CameraSequence::ToJson(base.cameraSequences);
        assert(CameraSequence::ToJson(CameraSequence::Parse({{"cameraSequences",baseJson}}))==baseJson);
        auto changed=base.cameraSequences.front(); changed.points[2].position.x+=1.25f; changed.points[2].fov=75;
        auto extra=changed; extra.id="future-event"; extra.enabled=false;
        CameraSequenceOverrides overrides; assert(overrides.Load(path,stage));
        overrides.sequences={changed,extra}; assert(overrides.Save(path));
        const auto firstBytes=CameraSequenceOverrides::Read(path);
        StageLoader loaded; assert(loaded.Load(fixture) && loaded.cameraSequences.size()==2);
        assert(CameraSequence::ToJson(loaded.blenderCameraSequences)==baseJson);
        assert(loaded.cameraSequences.front().points[2].position.x==changed.points[2].position.x);
        assert(!loaded.cameraSequences.back().enabled);
        assert(CameraSequenceOverrides::Read(fixture)==original && CameraSequenceOverrides::Read(source)==original);
        // Failed validation and external writer conflicts preserve the last valid file.
        overrides.sequences.front().points[0].duration=-1;
        assert(!overrides.Save(path) && CameraSequenceOverrides::Read(path)==firstBytes);
        assert(overrides.Load(path,stage));
        auto other=overrides; other.sequences.back().enabled=true; assert(other.Save(path));
        const auto secondBytes=CameraSequenceOverrides::Read(path);
        assert(CameraSequenceOverrides::Read(path+".camera-backup")==firstBytes);
        assert(!overrides.Save(path) && CameraSequenceOverrides::Read(path)==secondBytes);
        // An unrelated base ID remains intact when only start is overridden.
        const auto merged=CameraSequenceOverrides::Merge({changed,extra},{base.cameraSequences.front()});
        assert(merged.size()==2 && merged.back().id==extra.id && !merged.back().enabled);
        // A Blender re-export changes the base, while the F7 sequence remains effective.
        auto exported=json::parse(original); exported["cameraSequences"][0]["points"][2]["position"][0]=999;
        write(fixture,exported.dump()); assert(loaded.Load(fixture));
        assert(loaded.cameraSequences.front().points[2].position.x==changed.points[2].position.x);
        assert(loaded.blenderCameraSequences.front().points[2].position.x==999);
        assert(overrides.Load(path,stage)); overrides.sequences.clear(); assert(overrides.Save(path));
        assert(loaded.Load(fixture) && loaded.cameraSequences.front().points[2].position.x==999);
        // Malformed/foreign overrides fall back to the valid Blender data, with a warning.
        write(path,"broken json"); assert(loaded.Load(fixture) && !loaded.cameraOverrideWarning.empty());
        assert(loaded.cameraSequences.front().points[2].position.x==999);
        write(path,json{{"version",1},{"stage","wrong-stage"},{"cameraSequences",baseJson}}.dump());
        assert(loaded.Load(fixture) && !loaded.cameraOverrideWarning.empty());
        auto many=base.cameraSequences;
        for (int i=1;i<128;++i) { auto d=many.front(); d.id="base-"+std::to_string(i); many.push_back(d); }
        exported["cameraSequences"]=CameraSequence::ToJson(many); write(fixture,exported.dump());
        write(path,json{{"version",1},{"stage",stage},{"cameraSequences",CameraSequence::ToJson({extra})}}.dump());
        assert(loaded.Load(fixture) && loaded.cameraSequences.size()==128 && !loaded.cameraOverrideWarning.empty());
        std::filesystem::remove(path);
        assert(loaded.Load(fixture) && loaded.cameraOverrideWarning.empty());
        assert(CameraSequenceOverrides::Read(source)==original);
    }
    std::cout<<"PASS: Stage01-03, JSON round trip, per-ID precedence, future IDs/disabled sequences, isolated atomic save/backup, invalid save and conflict preservation, Blender re-export retention, revert, malformed/foreign/over-limit override fallback.\n";
}
