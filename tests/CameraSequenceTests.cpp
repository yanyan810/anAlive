#include "StageLoader.h"
#include <iostream>
#include <cassert>
#include <limits>
using nlohmann::json;
static void Near(const Vector3& a,const Vector3& b,float epsilon=.001f) { assert(StageLength(a-b)<epsilon); }
static void RotationNear(const Vector3& a,const Vector3& b) {
    const auto am=Matrix4x4::RotateXYZ(a.x,a.y,a.z),bm=Matrix4x4::RotateXYZ(b.x,b.y,b.z);
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) assert(std::abs(am.m[i][j]-bm.m[i][j])<.0001f);
}
int main() {
    for (const auto& r:std::vector<Vector3>{{.3f,.4f,-.7f},{1.1f,2.9f,.9f},{0,3.1415926f,0},{.7f,1.5707963f,.2f}})
        RotationNear(r,CameraSequence::Euler(CameraSequence::Rotation(r)));
    for (const auto* stage:{"stage01","stage02","stage03"}) {
        const auto path=std::string("../../resources/levels/")+stage+"/"+stage+".json";
        StageLoader loader; assert(loader.Load(path)); assert(loader.cameraWarning.empty()); assert(loader.cameraSequences.size()==1);
        const auto& def=loader.cameraSequences[0];
        CameraPose eye{loader.playerPosition+Vector3{0,1.6f,0},loader.playerRotation,1.04719755f};
        {
            CameraSequence s; auto disabled=def; disabled.enabled=false;
            assert(!s.Start(disabled,eye,loader.playerPosition));
            const CameraPose moved{{40,9,-22},{.3f,2.8f,.1f},.9f};
            assert(s.Start(def,moved,{40,7.4f,-22})); s.Skip();
            Near(s.Pose().position,moved.position); RotationNear(s.Pose().rotation,moved.rotation); assert(s.Pose().fov==moved.fov);
        }
        for (int fps:{30,60,144}) {
            CameraSequence s; assert(s.Start(def,eye,loader.playerPosition)); assert(std::abs(s.Duration()-4)<.0001);
            const auto first=s.Pose();
            s.Update(-1); s.Update(std::numeric_limits<double>::quiet_NaN()); assert(s.Elapsed()==0);
            s.Update(.25); Near(first.position,s.Pose().position);
            s.Update(.25); Near(first.position,s.Pose().position);
            auto previous=s.Pose();
            for (int i=0;i<fps*4 && s.Active();++i) {
                s.Update(1.0/fps); const auto p=s.Pose();
                assert(std::isfinite(p.position.x) && std::isfinite(p.rotation.y));
                assert(StageLength(p.position-previous.position)<.5f);
                assert(std::abs(Dot(CameraSequence::Rotation(p.rotation),CameraSequence::Rotation(previous.rotation)))>.95f);
                previous=p;
            }
            assert(!s.Active()); Near(s.Pose().position,eye.position); RotationNear(s.Pose().rotation,eye.rotation); assert(s.Pose().fov==eye.fov);
            assert(s.Start(def,eye,loader.playerPosition)); s.Update(1.37); s.Skip(); Near(s.Pose().position,eye.position);
            s.Skip(); assert(!s.Active() && s.Elapsed()==s.Duration());
            assert(s.Start(def,eye,loader.playerPosition)); s.Update(100); assert(!s.Active()); Near(s.Pose().position,eye.position);
            s.Stop(); assert(!s.Active());
        }
        CameraSequence sampler; assert(sampler.Start(def,eye,loader.playerPosition));
        // Sweep the authored curve against stage collision with near-plane margin.
        for (int i=0;i<1000;++i) {
            auto a=sampler.Evaluate(4.0*i/1000).position,b=sampler.Evaluate(4.0*(i+1)/1000).position;
            StageHit hit;
            if (loader.collision.Raycast(a,b-a,StageLength(b-a),hit,.12f)) {
                std::cerr<<stage<<" camera collision at "<<4.0*i/1000<<"s: "<<a.x<<","<<a.y<<","<<a.z<<std::endl;
                assert(false);
            }
        }
        // Blender exports a reference sample for identical engine-space poses.
        const auto reference="../camera-tests/"+std::string(stage)+"-samples.json";
        std::ifstream input(reference);
        if (input) {
            json samples; input>>samples;
            for (const auto& sample:samples) {
                const auto p=sampler.Evaluate(sample.at("time").get<double>());
                const auto& pos=sample.at("position"); const auto& rot=sample.at("rotation");
                Near(p.position,{pos[0],pos[1],pos[2]}); RotationNear(p.rotation,{rot[0],rot[1],rot[2]});
                assert(std::abs(p.fov-sample.at("fov").get<float>()*CameraSequence::DegreesToRadians)<.0001f);
            }
        }
    }
    {
        std::ifstream file("../camera-tests/mixed-reference.json");
        if (file) {
            json data; file>>data;
            const auto defs=CameraSequence::Parse(data);
            CameraSequence s; assert(s.Start(defs[0],{{3,1.6f,-6},{},1.04719755f},{3,0,-6}));
            for (const auto& sample:data.at("samples")) {
                const auto p=s.Evaluate(sample.at("time").get<double>());
                const auto& pos=sample.at("position"); const auto& rot=sample.at("rotation");
                Near(p.position,{pos[0],pos[1],pos[2]}); RotationNear(p.rotation,{rot[0],rot[1],rot[2]});
            }
        }
    }
    json valid; std::ifstream("../../resources/levels/stage01/stage01.json")>>valid;
    const auto reject=[&](const json& bad) {
        bool failed=false; try { (void)CameraSequence::Parse(bad); } catch (const std::exception&) { failed=true; } assert(failed);
        std::ofstream("camera-invalid.json")<<bad;
        StageLoader loader; assert(loader.Load("camera-invalid.json") && !loader.cameraWarning.empty() && loader.cameraSequences.empty());
    };
    auto bad=valid; bad["cameraSequences"]=json::object(); reject(bad);
    bad=valid; bad["cameraSequences"][0]["points"][1]["order"]=0; reject(bad);
    bad=valid; bad["cameraSequences"][0]["points"][1]["duration"]=-1; reject(bad);
    bad=valid; bad["cameraSequences"][0]["points"].back()["playerEye"]=false; reject(bad);
    bad=valid; bad["cameraSequences"][0]["points"][2]["interpolation"]="Unknown"; reject(bad);
    bad=valid; bad["cameraSequences"][0]["points"][2]["fov"]=0; reject(bad);
    bad=valid; bad["cameraSequences"][0]["points"][2]["rotationMode"]="LookAtTarget"; reject(bad);
    bad=valid; bad["cameraSequences"][0]["points"][2]["position"]={1,2}; reject(bad);
    bad=valid; bad["cameraSequences"][0]["points"].back()["duration"]=1; reject(bad);
    valid.erase("cameraSequences"); assert(CameraSequence::Parse(valid).empty());
    std::cout<<"PASS: camera curves/rotation/FOV/handoff, 30/60/144FPS, large/invalid dt, skip/restart, collision sweep, Blender parity when available, malformed camera fallback and legacy JSON\n";
}
