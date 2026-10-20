#pragma once
#include "Matrix4x4.h"
#include "Quaternion.h"
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>
#include <stdexcept>
#include <memory>
#include <array>

// Pure, renderer/input independent evaluator. Seconds are outgoing segment times;
// the terminal point has duration 0. Rotations use engine XYZ radians, FOV degrees.
struct CameraPose {
    Vector3 position{}, rotation{};
    float fov = 1.0471975512f; // vertical radians, like Camera
};
struct CameraSequencePoint {
    std::string id, interpolation="CatmullRom", rotationMode="Manual";
    int order=0;
    Vector3 position{}, rotation{}, target{}, lookOffset{0,1.0f,0};
    double duration=1;
    float fov=60;
    bool playerEye=false;
    // Empty pathType retains the original interpolation evaluator exactly.
    std::string pathType, easing="Constant", handleType="Auto";
    Vector3 handleIn{},handleOut{}; // offsets relative to the resolved point (also PlayerEye)
};
struct CameraSequenceDefinition {
    std::string id;
    bool enabled=true;
    std::vector<CameraSequencePoint> points;
};

class CameraSequence {
public:
    static constexpr float DegreesToRadians=.01745329251994329577f;
    static nlohmann::json ToJson(const std::vector<CameraSequenceDefinition>& definitions) {
        auto rows=nlohmann::json::array();
        const auto vec=[](const Vector3& p) { return nlohmann::json::array({p.x,p.y,p.z}); };
        for (const auto& d:definitions) {
            auto points=nlohmann::json::array();
            for (const auto& p:d.points) {
                nlohmann::json item{{"id",p.id},{"order",p.order},{"position",vec(p.position)},{"rotation",vec(p.rotation)},
                    {"duration",p.duration},{"interpolation",p.interpolation},{"rotationMode",p.rotationMode},
                    {"lookAtOffset",vec(p.lookOffset)},{"fov",p.fov},{"playerEye",p.playerEye}};
                if (p.rotationMode=="LookAtTarget") item["lookAtTarget"]=vec(p.target);
                if (!p.pathType.empty()) { item["pathType"]=p.pathType; item["easing"]=p.easing; }
                // Handles also belong to endpoints whose outgoing segment is legacy/Linear.
                if (p.handleType!="Auto" || !p.pathType.empty()) {
                    item["handleType"]=p.handleType; item["handleIn"]=vec(p.handleIn); item["handleOut"]=vec(p.handleOut);
                }
                points.push_back(std::move(item));
            }
            rows.push_back({{"id",d.id},{"enabled",d.enabled},{"points",std::move(points)}});
        }
        return rows;
    }
    static std::vector<CameraSequenceDefinition> Parse(const nlohmann::json& data) {
        std::vector<CameraSequenceDefinition> result;
        if (!data.contains("cameraSequences")) return result;
        const auto& rows=data.at("cameraSequences");
        if (!rows.is_array() || rows.size()>128) throw std::runtime_error("cameraSequences: expected <=128 sequences");
        std::set<std::string> sequences;
        const auto vector=[](const nlohmann::json& v) {
            if (!v.is_array() || v.size()!=3) throw std::runtime_error("Camera: expected 3-vector");
            Vector3 p{v.at(0).get<float>(),v.at(1).get<float>(),v.at(2).get<float>()};
            for (float n:{p.x,p.y,p.z}) if (!std::isfinite(n) || std::abs(n)>1e6f) throw std::runtime_error("Camera: invalid vector");
            return p;
        };
        for (const auto& row:rows) {
            CameraSequenceDefinition d;
            d.id=row.at("id").get<std::string>(); d.enabled=row.value("enabled",true);
            if (d.id.empty() || !sequences.insert(d.id).second) throw std::runtime_error("Camera: empty/duplicate sequence ID");
            const auto& points=row.at("points");
            if (!points.is_array() || points.size()<2 || points.size()>1024) throw std::runtime_error("Camera: require 2..1024 points");
            std::set<std::string> ids; std::set<int> orders;
            for (const auto& item:points) {
                CameraSequencePoint p;
                p.id=item.at("id").get<std::string>();
                if (!item.at("order").is_number_integer()) throw std::runtime_error("Camera: integer order required");
                p.order=item.at("order").get<int>();
                if (p.id.empty() || !ids.insert(p.id).second || p.order<0 || !orders.insert(p.order).second)
                    throw std::runtime_error("Camera: empty/duplicate point ID or order");
                p.playerEye=item.value("playerEye",false);
                p.duration=item.at("duration").get<double>();
                p.interpolation=item.value("interpolation",p.interpolation);
                p.rotationMode=item.value("rotationMode",p.rotationMode);
                p.fov=item.value("fov",60.0f);
                p.pathType=item.value("pathType",std::string{}); p.easing=item.value("easing",p.easing);
                p.handleType=item.value("handleType",p.handleType);
                if (p.pathType!="" && p.pathType!="Linear" && p.pathType!="Bezier") throw std::runtime_error("Camera: unknown path type");
                if (p.easing!="Constant" && p.easing!="EaseIn" && p.easing!="EaseOut" && p.easing!="EaseInOut") throw std::runtime_error("Camera: unknown easing");
                if (p.handleType!="Auto" && p.handleType!="Aligned" && p.handleType!="Free") throw std::runtime_error("Camera: unknown handle type");
                if (item.contains("handleIn")) p.handleIn=vector(item.at("handleIn"));
                if (item.contains("handleOut")) p.handleOut=vector(item.at("handleOut"));
                if (p.handleType!="Auto" && (!item.contains("handleIn") || !item.contains("handleOut"))) throw std::runtime_error("Camera: manual handles require in/out offsets");
                if (p.handleType=="Aligned") {
                    const auto a=p.handleIn,b=p.handleOut;
                    const float product=std::hypot(a.x,a.y,a.z)*std::hypot(b.x,b.y,b.z);
                    const Vector3 cross{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
                    if (product>1e-10f && (a.x*b.x+a.y*b.y+a.z*b.z>product*1e-5f || std::hypot(cross.x,cross.y,cross.z)>product*1e-4f))
                        throw std::runtime_error("Camera: Aligned handles must point in opposite collinear directions");
                }
                if (!std::isfinite(p.duration) || p.duration<0 || p.duration>1200 ||
                    !std::isfinite(p.fov) || p.fov<10 || p.fov>150)
                    throw std::runtime_error("Camera: invalid duration/FOV");
                if (p.interpolation!="CatmullRom" && p.interpolation!="Linear" && p.interpolation!="SmoothStep" && p.interpolation!="Hold")
                    throw std::runtime_error("Camera: unknown interpolation");
                if (p.rotationMode!="Manual" && p.rotationMode!="LookAtPlayer" && p.rotationMode!="LookAtTarget" && p.rotationMode!="FollowTangent")
                    throw std::runtime_error("Camera: unknown rotation mode");
                if (!p.playerEye) {
                    p.position=vector(item.at("position")); p.rotation=vector(item.at("rotation"));
                    if (p.rotationMode=="LookAtTarget") p.target=vector(item.at("lookAtTarget"));
                }
                if (item.contains("lookAtOffset")) p.lookOffset=vector(item.at("lookAtOffset"));
                d.points.push_back(p);
            }
            std::sort(d.points.begin(),d.points.end(),[](const auto& a,const auto& b){return a.order<b.order;});
            double total=0;
            for (size_t i=0;i<d.points.size();++i) {
                const auto& p=d.points[i]; const bool last=i+1==d.points.size();
                if ((!last && (p.duration<.001 || p.playerEye)) || (last && p.duration!=0))
                    throw std::runtime_error("Camera: only terminal point may be PlayerEye; terminal duration=0, others >=.001");
                if (p.pathType.empty() && p.interpolation=="Hold" && !last) {
                    const auto& next=d.points[i+1];
                    if (next.playerEye || next.position.x!=p.position.x || next.position.y!=p.position.y || next.position.z!=p.position.z ||
                        next.rotation.x!=p.rotation.x || next.rotation.y!=p.rotation.y || next.rotation.z!=p.rotation.z ||
                        next.rotationMode!=p.rotationMode || next.fov!=p.fov ||
                        next.target.x!=p.target.x || next.target.y!=p.target.y || next.target.z!=p.target.z ||
                        next.lookOffset.x!=p.lookOffset.x || next.lookOffset.y!=p.lookOffset.y || next.lookOffset.z!=p.lookOffset.z)
                        throw std::runtime_error("Camera: Hold requires an identical next pose");
                }
                total+=p.duration;
            }
            if (total>3600 || (d.id=="start" && !d.points.back().playerEye)) throw std::runtime_error("Camera: start must end at PlayerEye; maximum 3600 seconds");
            result.push_back(std::move(d));
        }
        return result;
    }
    bool Start(const CameraSequenceDefinition& d,const CameraPose& eye,const Vector3& player) {
        Stop();
        if (!d.enabled || d.points.size()<2) return false;
        definition_=d; eye_=eye; player_=player;
        BuildPathCache();
        total_=0; for (const auto& p:d.points) total_+=p.duration;
        if (total_<=0) return false;
        active_=true; elapsed_=0; point_=0; pose_=Evaluate(0); return true;
    }
    void Stop() { active_=false; }
    void Skip() { if (active_) { elapsed_=total_; pose_=Evaluate(total_); active_=false; } }
    void Update(double dt) {
        if (!active_ || !std::isfinite(dt) || dt<=0) return;
        elapsed_=std::min(total_,elapsed_+dt); pose_=Evaluate(elapsed_);
        if (elapsed_>=total_) active_=false;
    }
    bool Active() const { return active_; }
    double Elapsed() const { return elapsed_; }
    double Duration() const { return total_; }
    size_t PointIndex() const { return point_; }
    const CameraPose& Pose() const { return pose_; }
    const CameraSequenceDefinition& Definition() const { return definition_; }
    CameraPose PointPose(size_t index) const {
        const auto& p=definition_.points.at(index);
        if (p.rotationMode!="FollowTangent" || p.playerEye) return Resolve(p);
        const size_t segment=std::min(index,definition_.points.size()-2);
        return {PointPosition(index),Euler(Orientation(p,PointPosition(index),SegmentTangent(segment,index==segment ? 0.f : 1.f))),p.fov*DegreesToRadians};
    }
    CameraPose Evaluate(double seconds) const {
        const auto& ps=definition_.points;
        if (ps.empty()) return eye_;
        if (seconds>=total_) { point_=ps.size()-1; return PointPose(ps.size()-1); }
        size_t i=0; double start=0;
        while (i+2<ps.size() && seconds>=start+ps[i].duration) { start+=ps[i].duration; ++i; }
        point_=i;
        const auto& a=ps[i]; const auto& b=ps[i+1];
        float t=static_cast<float>(std::clamp((seconds-start)/a.duration,0.0,1.0));
        if (!a.pathType.empty()) {
            const float progress=Ease(t,a.easing),u=ParameterAtProgress(i,progress);
            const auto position=SegmentPosition(i,u),tangent=SegmentTangent(i,u);
            return {position,Euler(a.rotationMode=="FollowTangent" ? Orientation(a,position,tangent) : Slerp(Orientation(a,position,tangent),Orientation(b,position,tangent),progress)),
                Resolve(a).fov+(Resolve(b).fov-Resolve(a).fov)*progress};
        }
        if (a.interpolation=="Hold") t=0;
        if (a.interpolation=="SmoothStep") t=t*t*(3-2*t);
        const auto pa=Resolve(a),pb=Resolve(b);
        CameraPose pose;
        pose.position=Lerp(pa.position,pb.position,t);
        if (a.interpolation=="CatmullRom") {
            // Duplicated endpoints; never interpolate past a Hold or beyond PlayerEye.
            const Vector3 p0=(i==0 || ps[i-1].interpolation=="Hold") ? pa.position : Resolve(ps[i-1]).position;
            const Vector3 p3=i+2<ps.size() ? Resolve(ps[i+2]).position : pb.position;
            pose.position=Catmull(p0,pa.position,pb.position,p3,t);
        }
        const auto tangent=SegmentTangent(i,t);
        pose.rotation=Euler(a.rotationMode=="FollowTangent" ? Orientation(a,pose.position,tangent) : Slerp(Orientation(a,pose.position,tangent),Orientation(b,pose.position,tangent),t));
        pose.fov=pa.fov+(pb.fov-pa.fov)*t;
        return pose;
    }
    static Vector3 Catmull(const Vector3& a,const Vector3& b,const Vector3& c,const Vector3& d,float t) {
        return (b*2+(c-a)*t+(a*2-b*5+c*4-d)*(t*t)+(a*(-1)+b*3-c*3+d)*(t*t*t))*.5f;
    }
    Vector3 PointPosition(size_t index) const { const auto& p=definition_.points.at(index); return p.playerEye ? eye_.position : p.position; }
    Vector3 HandleOffset(size_t index,bool outgoing) const {
        const auto& p=definition_.points.at(index);
        if (p.handleType!="Auto") return outgoing ? p.handleOut : p.handleIn;
        const auto position=PointPosition(index);
        const auto before=index ? PointPosition(index-1) : position;
        const auto after=index+1<definition_.points.size() ? PointPosition(index+1) : position;
        const auto tangent=(after-before)*(index==0 || index+1==definition_.points.size() ? 1.f/3 : 1.f/6);
        return outgoing ? tangent : tangent*(-1);
    }
    std::array<Vector3,4> SegmentControls(size_t index) const {
        const auto a=PointPosition(index),b=PointPosition(index+1);
        const auto& p=definition_.points.at(index);
        if (p.pathType=="Bezier") return {a,a+HandleOffset(index,true),b+HandleOffset(index+1,false),b};
        if (p.pathType.empty() && p.interpolation=="CatmullRom") {
            const auto before=(index==0 || definition_.points[index-1].interpolation=="Hold") ? a : PointPosition(index-1);
            const auto after=index+2<definition_.points.size() ? PointPosition(index+2) : b;
            return {a,a+(b-before)*(1.f/6),b-(after-a)*(1.f/6),b};
        }
        return {a,a+(b-a)*(1.f/3),a+(b-a)*(2.f/3),b};
    }
    Vector3 SegmentPosition(size_t index,float u) const {
        const auto& p=definition_.points.at(index);
        if (p.pathType.empty() && p.interpolation=="Hold") return PointPosition(index);
        const auto c=SegmentControls(index); const float v=1-u;
        return c[0]*(v*v*v)+c[1]*(3*v*v*u)+c[2]*(3*v*u*u)+c[3]*(u*u*u);
    }
    Vector3 SegmentTangent(size_t index,float u) const {
        const auto& p=definition_.points.at(index);
        if (p.pathType.empty() && p.interpolation=="Hold") return {};
        const auto c=SegmentControls(index); const float v=1-u;
        return (c[1]-c[0])*(3*v*v)+(c[2]-c[1])*(6*v*u)+(c[3]-c[2])*(3*u*u);
    }
    double SegmentStart(size_t index) const { double t=0; for (size_t i=0;i<index;++i) t+=definition_.points.at(i).duration; return t; }
    static float Ease(float t,const std::string& easing) {
        if (easing=="EaseIn") return t*t;
        if (easing=="EaseOut") return 1-(1-t)*(1-t);
        if (easing=="EaseInOut") return t*t*(3-2*t);
        return t;
    }
    float TimeFractionAtParameter(size_t index,float u) const {
        const auto& p=definition_.points.at(index);
        float progress=p.pathType=="Bezier" ? ProgressAtParameter(index,u) : u;
        const std::string easing=p.pathType.empty() && p.interpolation=="SmoothStep" ? "EaseInOut" : p.pathType.empty() ? "Constant" : p.easing;
        float lo=0,hi=1;
        for (int n=0;n<25;++n) { const float mid=(lo+hi)*.5f; if (Ease(mid,easing)<progress) lo=mid; else hi=mid; }
        return (lo+hi)*.5f;
    }
    double TimeAtParameter(size_t index,float u) const { return SegmentStart(index)+definition_.points.at(index).duration*TimeFractionAtParameter(index,u); }
    size_t ArcCacheRevision() const { return arcCacheRevision_; }
    CameraSequenceDefinition SplitSegment(size_t index,float u,const std::string& id) const {
        if (index+1>=definition_.points.size() || definition_.points.size()>=1024 || !std::isfinite(u) || u<=0 || u>=1 || id.empty()) throw std::runtime_error("Camera: invalid split");
        auto result=definition_; const auto& source=definition_.points[index];
        const double fraction=source.pathType.empty() && source.interpolation=="Hold" ? u : TimeFractionAtParameter(index,u);
        if (source.duration*fraction<.001 || source.duration*(1-fraction)<.001) throw std::runtime_error("Camera: split requires >=.001 seconds on both sides");
        CameraSequencePoint point=source; point.id=id; point.playerEye=false;
        const bool hold=source.pathType.empty() && source.interpolation=="Hold";
        if (!hold) {
            const auto pose=Evaluate(TimeAtParameter(index,u)); point.position=SegmentPosition(index,u); point.rotation=pose.rotation;
            if (source.rotationMode!="FollowTangent") point.rotationMode="Manual";
            point.fov=pose.fov/DegreesToRadians;
        }
        const bool bezier=source.pathType=="Bezier" || (source.pathType.empty() && source.interpolation=="CatmullRom");
        // Freeze the original endpoints before their Auto neighbourhood changes.
        for (size_t j=index;j<=index+1;++j) { result.points[j].handleIn=HandleOffset(j,false); result.points[j].handleOut=HandleOffset(j,true); result.points[j].handleType="Free"; }
        // Legacy Catmull neighbours also depend on adjacent points. Freeze their
        // exact polynomial controls before insertion, preserving the whole path.
        for (size_t j=index ? index-1 : 0;j<=std::min(index+1,definition_.points.size()-2);++j) {
            if (hold || !definition_.points[j].pathType.empty() || definition_.points[j].interpolation!="CatmullRom") continue;
            const auto controls=SegmentControls(j);
            for (size_t k=j;k<=j+1;++k) if (result.points[k].handleType=="Auto") {
                result.points[k].handleIn=HandleOffset(k,false); result.points[k].handleOut=HandleOffset(k,true); result.points[k].handleType="Free";
            }
            result.points[j].pathType="Bezier"; result.points[j].easing="Constant";
            result.points[j].handleOut=controls[1]-controls[0]; result.points[j+1].handleIn=controls[2]-controls[3];
        }
        if (bezier) {
            const auto c=SegmentControls(index);
            const auto a=Lerp(c[0],c[1],u),b=Lerp(c[1],c[2],u),c2=Lerp(c[2],c[3],u);
            const auto d=Lerp(a,b,u),e=Lerp(b,c2,u),f=Lerp(d,e,u);
            result.points[index].pathType=point.pathType="Bezier";
            result.points[index].handleOut=a-c[0]; result.points[index+1].handleIn=c2-c[3];
            point.position=f; point.handleIn=d-f; point.handleOut=e-f; point.handleType="Free";
        }
        point.duration=source.duration*(1-fraction); result.points[index].duration=source.duration*fraction;
        if (source.pathType.empty() && source.interpolation=="SmoothStep") {
            result.points[index].pathType=point.pathType="Linear";
            result.points[index].easing=point.easing="EaseInOut";
        }
        result.points.insert(result.points.begin()+static_cast<ptrdiff_t>(index+1),point);
        for (size_t j=0;j<result.points.size();++j) result.points[j].order=static_cast<int>(j);
        (void)Parse({{"cameraSequences",ToJson({result})}});
        return result;
    }
    static Quaternion Rotation(const Vector3& r) {
        const float cx=std::cos(r.x*.5f),sx=std::sin(r.x*.5f),cy=std::cos(r.y*.5f),sy=std::sin(r.y*.5f),cz=std::cos(r.z*.5f),sz=std::sin(r.z*.5f);
        return Normalize({sx*cy*cz-cx*sy*sz,cx*sy*cz+sx*cy*sz,cx*cy*sz-sx*sy*cz,cx*cy*cz+sx*sy*sz});
    }
    static Vector3 Euler(const Quaternion& q) {
        const auto m=MakeAffineMatrix({1,1,1},q,{});
        const float y=std::asin(std::clamp(-m.m[0][2],-1.0f,1.0f));
        if (std::abs(std::cos(y))<1e-5f) return {std::atan2(-m.m[2][1],m.m[1][1]),y,0};
        return {std::atan2(m.m[1][2],m.m[2][2]),y,std::atan2(m.m[0][1],m.m[0][0])};
    }
private:
    static Vector3 Lerp(const Vector3& a,const Vector3& b,float t) { return a+(b-a)*t; }
    CameraPose Resolve(const CameraSequencePoint& p) const {
        if (p.playerEye) return eye_;
        return {p.position,Euler(Orientation(p,p.position)),p.fov*DegreesToRadians};
    }
    Quaternion Orientation(const CameraSequencePoint& p,const Vector3& position,const Vector3& tangent={}) const {
        if (p.playerEye) return Rotation(eye_.rotation);
        if (p.rotationMode=="Manual") return Rotation(p.rotation);
        if (p.rotationMode=="FollowTangent") {
            if (std::hypot(tangent.x,tangent.y,tangent.z)<1e-6f) return Rotation(p.rotation);
            return Rotation({-std::atan2(tangent.y,std::hypot(tangent.x,tangent.z)),std::atan2(tangent.x,tangent.z),0});
        }
        const auto delta=(p.rotationMode=="LookAtPlayer" ? player_+p.lookOffset : p.target)-position;
        const float horizontal=std::hypot(delta.x,delta.z);
        if (horizontal<1e-6f && std::abs(delta.y)<1e-6f) return Rotation(p.rotation);
        return Rotation({-std::atan2(delta.y,horizontal),std::atan2(delta.x,delta.z),0});
    }
    struct ArcTable { std::array<double,257> lengths{}; };
    struct PathCache { nlohmann::json key; std::vector<ArcTable> tables; };
    void BuildPathCache() {
        auto key=nlohmann::json::array();
        for (size_t i=0;i<definition_.points.size();++i) {
            const auto& p=definition_.points[i]; const auto pos=PointPosition(i);
            key.push_back({p.pathType,p.interpolation,p.handleType,pos.x,pos.y,pos.z,p.handleIn.x,p.handleIn.y,p.handleIn.z,p.handleOut.x,p.handleOut.y,p.handleOut.z});
        }
        if (arcCache_ && arcCache_->key==key) return;
        auto cache=std::make_shared<PathCache>(); cache->key=std::move(key); cache->tables.resize(definition_.points.size()-1);
        for (size_t i=0;i+1<definition_.points.size();++i) if (definition_.points[i].pathType=="Bezier") {
            auto previous=SegmentPosition(i,0); auto& table=cache->tables[i];
            for (size_t j=1;j<table.lengths.size();++j) {
                const auto next=SegmentPosition(i,static_cast<float>(j)/256); const auto delta=next-previous;
                table.lengths[j]=table.lengths[j-1]+std::hypot(delta.x,delta.y,delta.z); previous=next;
            }
        }
        arcCache_=std::move(cache); ++arcCacheRevision_;
    }
    float ProgressAtParameter(size_t index,float u) const {
        const auto& lengths=arcCache_->tables.at(index).lengths;
        if (lengths.back()<1e-8) return u;
        const float scaled=std::clamp(u,0.f,1.f)*256; const auto j=std::min(static_cast<size_t>(scaled),size_t{255});
        return static_cast<float>((lengths[j]+(lengths[j+1]-lengths[j])*(scaled-static_cast<float>(j)))/lengths.back());
    }
    float ParameterAtProgress(size_t index,float progress) const {
        if (definition_.points[index].pathType!="Bezier") return progress;
        const auto& lengths=arcCache_->tables.at(index).lengths;
        if (lengths.back()<1e-8) return progress;
        const double target=lengths.back()*progress;
        const auto found=std::lower_bound(lengths.begin(),lengths.end(),target);
        if (found==lengths.begin()) return 0;
        if (found==lengths.end()) return 1;
        const auto j=static_cast<size_t>(found-lengths.begin()-1); const double span=lengths[j+1]-lengths[j];
        return (static_cast<float>(j)+(span>1e-12 ? static_cast<float>((target-lengths[j])/span) : 0))/256;
    }
    std::shared_ptr<const PathCache> arcCache_;
    size_t arcCacheRevision_=0;
    CameraSequenceDefinition definition_;
    CameraPose eye_,pose_;
    Vector3 player_{};
    double elapsed_=0,total_=0;
    mutable size_t point_=0;
    bool active_=false;
};
