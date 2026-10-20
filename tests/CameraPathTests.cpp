#include "StageLoader.h"
#include <cassert>
#include <iostream>

static void Near(Vector3 a,Vector3 b,float epsilon=.0001f) { assert(StageLength(a-b)<epsilon); }
static CameraSequence Evaluator(const CameraSequenceDefinition& d) {
    CameraSequence s; assert(s.Start(d,{{14,1.6f,1},{0,.4f,0},1.04719755f},{})); return s;
}
int main() {
    CameraSequenceDefinition d; d.id="start";
    for (int i=0;i<4;++i) {
        CameraSequencePoint p; p.id="p"+std::to_string(i); p.order=i; p.position={static_cast<float>(i*4),1,0};
        p.pathType=i==1 ? "Bezier" : "Linear"; p.interpolation="Linear";
        p.duration=i==3 ? 0 : i==1 ? 3 : 2; p.playerEye=i==3; p.handleType="Free";
        d.points.push_back(p);
    }
    d.points[1].handleOut={1,4,2}; d.points[2].handleIn={-2,3,-1};
    const auto parsed=CameraSequence::Parse({{"cameraSequences",CameraSequence::ToJson({d})}}).front();
    assert(CameraSequence::ToJson({parsed})==CameraSequence::ToJson(CameraSequence::Parse({{"cameraSequences",CameraSequence::ToJson({parsed})}})));
    auto s=Evaluator(d);
    for (size_t segment:{size_t{0},size_t{1},size_t{2}}) for (float split:{.13f,.37f,.83f}) {
        const auto divided=s.SplitSegment(segment,split,"inserted"); const auto t=Evaluator(divided);
        assert(std::abs(t.Duration()-s.Duration())<1e-6 && divided.points.back().playerEye);
        for (int j=0;j<=100;++j) {
            const float u=static_cast<float>(j)/100;
            Near(t.SegmentPosition(segment,u),s.SegmentPosition(segment,u*split));
            Near(t.SegmentPosition(segment+1,u),s.SegmentPosition(segment,split+(1-split)*u));
        }
        for (size_t i=0;i<3;++i) if (i!=segment) for (int j=0;j<=30;++j)
            Near(s.SegmentPosition(i,static_cast<float>(j)/30),t.SegmentPosition(i+(i>segment ? 1 : 0),static_cast<float>(j)/30));
    }
    // Arc-length evaluation gives constant travel speed, including strongly curved handles.
    float shortest=1e9f,longest=0; auto previous=s.Evaluate(2).position;
    for (int j=1;j<=300;++j) {
        const auto p=s.Evaluate(2+3.0*j/300).position; const float step=StageLength(p-previous);
        shortest=std::min(shortest,step); longest=std::max(longest,step); previous=p;
    }
    assert(longest/shortest<1.015f);
    const auto revision=s.ArcCacheRevision();
    for (int i=0;i<100;++i) (void)s.Evaluate(i*.04);
    assert(s.ArcCacheRevision()==revision);
    assert(s.Start(d,{{14,1.6f,1},{0,.4f,0},1.04719755f},{})); assert(s.ArcCacheRevision()==revision);
    auto changed=d; changed.points[1].fov=80; changed.points[1].duration=4;
    assert(s.Start(changed,{{14,1.6f,1},{0,.4f,0},1.04719755f},{})); assert(s.ArcCacheRevision()==revision);
    changed.points[1].handleOut.y+=1; assert(s.Start(changed,{{14,1.6f,1},{0,.4f,0},1.04719755f},{})); assert(s.ArcCacheRevision()==revision+1);
    for (const auto* easing:{"Constant","EaseIn","EaseOut","EaseInOut"}) {
        auto eased=d; eased.points[1].easing=easing; const auto e=Evaluator(eased);
        for (float u:{.01f,.17f,.5f,.93f}) Near(e.Evaluate(e.TimeAtParameter(1,u)).position,e.SegmentPosition(1,u),.0002f);
        const float t=e.TimeFractionAtParameter(1,.5f); assert(t>0 && t<1);
    }
    auto follow=d; follow.points[1].rotationMode="FollowTangent"; const auto f=Evaluator(follow);
    for (float u:{0.f,.1f,.5f,.9f}) {
        const auto pose=f.Evaluate(f.TimeAtParameter(1,u)); const auto r=Matrix4x4::RotateXYZ(pose.rotation.x,pose.rotation.y,pose.rotation.z);
        const Vector3 forward{r.m[2][0],r.m[2][1],r.m[2][2]},tangent=f.SegmentTangent(1,u);
        assert(StageDot(forward,tangent)/StageLength(tangent)>.9999f);
    }
    const auto followSplit=f.SplitSegment(1,.4f,"follow-insert"); assert(followSplit.points[2].rotationMode=="FollowTangent");
    auto zero=d; zero.points[1].rotationMode="FollowTangent"; zero.points[2].position=zero.points[1].position; zero.points[1].handleOut={}; zero.points[2].handleIn={};
    const auto stationary=Evaluator(zero).Evaluate(3); assert(std::isfinite(stationary.rotation.x) && std::isfinite(stationary.rotation.y));
    auto automatic=d; for (auto& p:automatic.points) p.handleType="Auto";
    const auto a=Evaluator(automatic); const auto splitAuto=Evaluator(a.SplitSegment(1,.42f,"auto-split"));
    for (int j=0;j<=100;++j) { const float u=static_cast<float>(j)/100; Near(splitAuto.SegmentPosition(1,u),a.SegmentPosition(1,u*.42f)); Near(splitAuto.SegmentPosition(2,u),a.SegmentPosition(1,.42f+.58f*u)); }
    // Splitting legacy Catmull curves must also freeze affected neighbour polynomials.
    for (const auto* stage:{"stage01","stage02","stage03"}) {
        StageLoader level; assert(level.Load(std::string("../../resources/levels/")+stage+"/"+stage+".json"));
        CameraSequence old; assert(old.Start(level.cameraSequences.front(),{level.playerPosition+Vector3{0,1.6f,0},level.playerRotation,1.04719755f},level.playerPosition));
        CameraSequence held;
        assert(held.Start(old.SplitSegment(0,.3f,"hold-insert"),{level.playerPosition+Vector3{0,1.6f,0},level.playerRotation,1.04719755f},level.playerPosition));
        for (int j=0;j<=200;++j) {
            const auto before=old.Evaluate(old.Duration()*j/200),after=held.Evaluate(held.Duration()*j/200);
            Near(before.position,after.position); Near(before.rotation,after.rotation); assert(std::abs(before.fov-after.fov)<.00001f);
        }
        const auto split=old.SplitSegment(2,.37f,"legacy-insert"); CameraSequence edited;
        assert(edited.Start(split,{level.playerPosition+Vector3{0,1.6f,0},level.playerRotation,1.04719755f},level.playerPosition));
        for (size_t i=0;i+1<old.Definition().points.size();++i) for (int j=0;j<=30;++j) {
            const float u=static_cast<float>(j)/30;
            if (i==2) { Near(edited.SegmentPosition(2,u),old.SegmentPosition(2,u*.37f)); Near(edited.SegmentPosition(3,u),old.SegmentPosition(2,.37f+u*.63f)); }
            else Near(edited.SegmentPosition(i+(i>2 ? 1 : 0),u),old.SegmentPosition(i,u));
        }
    }
    const auto reject=[&](nlohmann::json json) { bool failed=false; try { (void)CameraSequence::Parse(json); } catch (const std::exception&) { failed=true; } assert(failed); };
    auto data=nlohmann::json{{"cameraSequences",CameraSequence::ToJson({d})}};
    auto bad=data; bad["cameraSequences"][0]["points"][1]["pathType"]="Paint"; reject(bad);
    bad=data; bad["cameraSequences"][0]["points"][1]["handleIn"]={1,2}; reject(bad);
    bad=data; bad["cameraSequences"][0]["points"][1]["easing"]="Bad"; reject(bad);
    bad=data; bad["cameraSequences"][0]["points"][1]["handleType"]="Aligned";
    bad["cameraSequences"][0]["points"][1]["handleIn"]={1,0,0}; reject(bad);
    bad=data; bad["cameraSequences"][0]["points"].back()["playerEye"]=false; reject(bad);
    std::cout<<"PASS: mixed Linear/Bezier, de Casteljau shape/whole-path invariance, duration/PlayerEye, JSON handles round trip, constant arc speed, cache reuse/invalidation, easing/time inverse, Follow Tangent/fallback, Auto split, legacy Stage01-03 split preservation and validation.\n";
}
