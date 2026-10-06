#pragma once
#include "scene/Main/ClothShowroomScene.h"
#include "DirectXTex.h"
#include "ImGuiManagaer.h"
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

// Test-only CPU skinning of the actual skirt triangles. The original leg radii
// are a constant body proxy across all A/B variants, not a mesh-collision guarantee.
class ClothSkirtProbes {
    struct Influence { size_t joint; float weight; };
    struct Vertex { Vector3 bind; std::vector<Influence> influences; bool leg=false,shorts=false,skirt=false; };
    struct Joint { std::string name; Matrix4x4 inverseBind; };
    std::vector<Vertex> vertices_;
    std::vector<Joint> joints_;
    bool body_=false;
    bool face_=false;
    static Vector3 Point(Vector3 p,const Matrix4x4& m) {
        return {p.x*m.m[0][0]+p.y*m.m[1][0]+p.z*m.m[2][0]+m.m[3][0],
            p.x*m.m[0][1]+p.y*m.m[1][1]+p.z*m.m[2][1]+m.m[3][1],
            p.x*m.m[0][2]+p.y*m.m[1][2]+p.z*m.m[2][2]+m.m[3][2]};
    }
public:
    enum class Part { Default, Hair, Face, Torso };
    struct Stats { size_t probes=0,inside=0; float maximum=0; };
    struct Crossings {
        size_t legs=0,shortsOnly=0,outsideColliders=0,outsideSampleEnvelope=0,nearSegments=0,offSegments=0,forehead=0;
        std::vector<size_t> foreheadTriangles;
    };
    explicit ClothSkirtProbes(const Model& model,bool body=false,bool drawersOnly=false,Part part=Part::Default) : body_(body),face_(part==Part::Face) {
        std::vector<int> indices(model.GetVertexCount(),-1);
        for(const auto& mesh:model.GetModelData().meshes) if(part==Part::Hair ? mesh.name=="髮" :
            part==Part::Face ? mesh.name=="颜" :
            part==Part::Torso ? (mesh.name=="肌" || mesh.name=="Mt_SakurabaEma_Body") : drawersOnly ?
            mesh.name=="Mt_SakurabaEma_Clothes02" : body ?
            (mesh.name=="肌" || mesh.name=="裤" || mesh.name=="袜" || mesh.name=="腿环" ||
             mesh.name=="Mt_SakurabaEma_Body" || mesh.name=="Mt_SakurabaEma_Clothes02") :
            (mesh.name=="裙" || mesh.name=="Mt_SakurabaEma_Clothes01" || mesh.name=="Mt_SakurabaEma_Clothes01_Stencil")) {
            for(size_t i=0;i<mesh.vertices.size();++i) {
                indices[mesh.startVertex+i]=static_cast<int>(vertices_.size());
                const auto p=mesh.vertices[i].position;
                vertices_.push_back({{p.x,p.y,p.z},{}});
                // A drawer-only diagnostic uses the same body classification
                // path as legs to report its contact-envelope gaps too.
                vertices_.back().shorts=!drawersOnly && (mesh.name=="裤" || mesh.name=="Mt_SakurabaEma_Clothes02");
                vertices_.back().skirt=mesh.name=="裙";
                if(part==Part::Face) vertices_.back().leg=true;
            }
        }
        for(const auto& [name,data]:model.GetSkinClusterData()) {
            const size_t joint=joints_.size(); bool used=false;
            for(const auto& weight:data.vertexWeights) if(weight.vertexIndex<indices.size() && indices[weight.vertexIndex]>=0) {
                vertices_[indices[weight.vertexIndex]].influences.push_back({joint,weight.weight}); used=true;
                if(part==Part::Torso ? (name=="下半身" || name=="上半身" || name=="上半身2") :
                    (name.find("足D")!=std::string::npos || name.find("ひざD")!=std::string::npos || name=="下半身"))
                    vertices_[indices[weight.vertexIndex]].leg=true;
                if((part==Part::Hair ? (name.starts_with("HairSL_") || name.starts_with("HairSR_") || name.starts_with("hair1-") || name.starts_with("hair2-")) :
                    (name.starts_with("Skt_") || name.starts_with("裙_"))) && weight.weight>.001f)
                    vertices_[indices[weight.vertexIndex]].skirt=true;
            }
            if(used) joints_.push_back({name,data.inverseBindPoseMatrix});
        }
        std::vector<Vertex> selected;
        for(size_t i=0;i+2<vertices_.size();i+=3) {
            const bool keep=body_ ? (vertices_[i].leg || vertices_[i+1].leg || vertices_[i+2].leg) :
                (vertices_[i].skirt || vertices_[i+1].skirt || vertices_[i+2].skirt);
            if(keep) for(size_t k=0;k<3;++k) selected.push_back(std::move(vertices_[i+k]));
        }
        vertices_=std::move(selected);
    }
    std::vector<Vector3> Skin(Object3d& object) const {
        std::vector<Matrix4x4> skin;
        for(const auto& joint:joints_) {
            Matrix4x4 world; object.TryGetJointWorldMatrix(joint.name,world);
            skin.push_back(Matrix4x4::Multiply(joint.inverseBind,world));
        }
        std::vector<Vector3> positions;
        for(const auto& vertex:vertices_) {
            Vector3 p{};
            for(const auto& influence:vertex.influences) p+=Point(vertex.bind,skin[influence.joint])*influence.weight;
            positions.push_back(p);
        }
        return positions;
    }
    size_t TriangleCount() const { return vertices_.size()/3; }
    float Width(Object3d& object,float minimumBindY=-100,float maximumBindY=100) const {
        float lo=100,hi=-100;
        const auto positions=Skin(object);
        for(size_t i=0;i<positions.size();++i) if(vertices_[i].bind.y>=minimumBindY && vertices_[i].bind.y<=maximumBindY) {
            lo=std::min(lo,positions[i].x); hi=std::max(hi,positions[i].x);
        }
        return hi-lo;
    }
    Stats Measure(Object3d& object,const ClothComponent& cloth) const {
        const auto positions=Skin(object);
        Stats stats;
        const auto probe=[&](Vector3 point) {
            ++stats.probes; float penetration=0;
            for(const auto& binding:cloth.colliders) if(binding.name.find("Leg")!=std::string::npos) {
                const auto& collider=binding.collider; const auto delta=point-collider.ClosestPoint(point);
                const float radius=(binding.name.find("Upper")!=std::string::npos ? .63f : .43f)*object.GetScale().x;
                penetration=std::max(penetration,radius-std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z));
            }
            if(penetration>.0001f) ++stats.inside;
            stats.maximum=std::max(stats.maximum,penetration);
        };
        for(size_t i=0;i+2<positions.size();i+=3) {
            const auto a=positions[i],b=positions[i+1],c=positions[i+2];
            probe(a); probe(b); probe(c); probe((a+b)*.5f); probe((b+c)*.5f); probe((c+a)*.5f); probe((a+b+c)*(1.0f/3));
        }
        return stats;
    }
    Crossings CountIntersections(Object3d& object,const ClothSkirtProbes& body,const ClothComponent& component) const {
        // Read-only discrete geometry diagnostic, never used by the physics solver.
        struct Triangle { Vector3 a,b,c,lo,hi; bool shorts; };
        const auto triangles=[](const ClothSkirtProbes& probes,const std::vector<Vector3>& points) {
            std::vector<Triangle> result;
            for(size_t i=0;i+2<points.size();i+=3) {
                if(probes.body_ && !probes.vertices_[i].leg && !probes.vertices_[i+1].leg && !probes.vertices_[i+2].leg) continue;
                const auto a=points[i],b=points[i+1],c=points[i+2];
                result.push_back({a,b,c,{std::min({a.x,b.x,c.x}),std::min({a.y,b.y,c.y}),std::min({a.z,b.z,c.z})},
                    {std::max({a.x,b.x,c.x}),std::max({a.y,b.y,c.y}),std::max({a.z,b.z,c.z})},probes.vertices_[i].shorts});
            }
            return result;
        };
        const auto skirt=triangles(*this,Skin(object)),legs=triangles(body,body.Skin(object));
        // Dense ema clothing needs a broad phase in this test-only triangle
        // diagnostic too. Preserve original pair order after collecting cells.
        using Cell=std::array<int,3>;
        std::map<Cell,std::vector<size_t>> cells;
        const auto cell=[](Vector3 p) { return Cell{static_cast<int>(std::floor(p.x/.05f)),
            static_cast<int>(std::floor(p.y/.05f)),static_cast<int>(std::floor(p.z/.05f))}; };
        for(size_t index=0;index<legs.size();++index) {
            const auto lo=cell(legs[index].lo),hi=cell(legs[index].hi);
            for(int x=lo[0];x<=hi[0];++x) for(int y=lo[1];y<=hi[1];++y) for(int z=lo[2];z<=hi[2];++z)
                cells[{x,y,z}].push_back(index);
        }
        std::vector<size_t> stamps(legs.size(),0),candidates;
        size_t stamp=0;
        const auto dot=[](Vector3 a,Vector3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; };
        const auto intersects=[&](Vector3 a,Vector3 b,const Triangle& tri,Vector3& hit) {
            const auto direction=b-a,e1=tri.b-tri.a,e2=tri.c-tri.a;
            const auto p=Matrix4x4::Cross(direction,e2);
            const float det=dot(e1,p);
            if(std::abs(det)<1e-10f) return false; // Coplanar/shared seam contacts are not counted.
            const float inv=1/det;
            const auto s=a-tri.a; const float u=dot(s,p)*inv;
            if(u<0 || u>1) return false;
            const auto q=Matrix4x4::Cross(s,e1); const float v=dot(direction,q)*inv;
            if(v<0 || u+v>1) return false;
            const float t=dot(e2,q)*inv;
            if(t<=.00001f || t>=.99999f) return false;
            hit=a+direction*t; return true;
        };
        Crossings crossings;
        Matrix4x4 inverseHead=Matrix4x4::MakeIdentity4x4();
        if(body.face_) { Matrix4x4 head; object.TryGetJointWorldMatrix("頭",head); inverseHead=Matrix4x4::Inverse(head); }
        for(size_t triangle=0;triangle<skirt.size();++triangle) {
            const auto& cloth=skirt[triangle];
            bool skinHit=false,shortsHit=false;
            candidates.clear(); ++stamp;
            const auto lo=cell(cloth.lo),hi=cell(cloth.hi);
            for(int x=lo[0];x<=hi[0];++x) for(int y=lo[1];y<=hi[1];++y) for(int z=lo[2];z<=hi[2];++z) {
                const auto found=cells.find({x,y,z}); if(found==cells.end()) continue;
                for(const auto index:found->second) if(stamps[index]!=stamp) { stamps[index]=stamp; candidates.push_back(index); }
            }
            std::sort(candidates.begin(),candidates.end());
            for(const auto index:candidates) {
                const auto& leg=legs[index];
                if(cloth.lo.x>leg.hi.x || cloth.hi.x<leg.lo.x || cloth.lo.y>leg.hi.y || cloth.hi.y<leg.lo.y || cloth.lo.z>leg.hi.z || cloth.hi.z<leg.lo.z) continue;
                Vector3 hit;
                if(!intersects(cloth.a,cloth.b,leg,hit) && !intersects(cloth.b,cloth.c,leg,hit) && !intersects(cloth.c,cloth.a,leg,hit) &&
                    !intersects(leg.a,leg.b,cloth,hit) && !intersects(leg.b,leg.c,cloth,hit) && !intersects(leg.c,leg.a,cloth,hit)) continue;
                if(leg.shorts) { shortsHit=true; continue; }
                skinHit=true; ++crossings.legs;
                if(body.face_) {
                    const auto local=Point(hit,inverseHead);
                    // Exclude the original scalp/root overlap; inspect the forehead surface.
                    if(local.y>.65f && local.y<1.85f && local.z<-.55f) {
                        ++crossings.forehead; crossings.foreheadTriangles.push_back(triangle);
                    }
                }
                float gap=100;
                for(const auto& binding:component.colliders) if(binding.collider.enabled && (binding.name.find("Leg")!=std::string::npos || binding.name.find("Drawer")!=std::string::npos || binding.name=="Hip")) {
                    const auto& c=binding.collider; const auto d=hit-c.ClosestPoint(hit);
                    gap=std::min(gap,std::sqrt(dot(d,d))-c.radius);
                }
                if(gap>=0) ++crossings.outsideColliders;
                const auto& solver=component.groups[0].solver;
                if(gap>solver.settings.collisionSampleRadius) ++crossings.outsideSampleEnvelope;
                else {
                    float distance2=100;
                    for(const auto& segment:solver.collisionSegments) {
                        if(segment.horizontal && !solver.settings.enableHorizontalCollisionSamples) continue;
                        const auto a=solver.particles[segment.a].position,axis=solver.particles[segment.b].position-a;
                        const float length2=dot(axis,axis);
                        const float t=length2>1e-10f ? std::clamp(dot(hit-a,axis)/length2,0.0f,1.0f) : 0;
                        const auto d=hit-(a+axis*t); distance2=std::min(distance2,dot(d,d));
                    }
                    if(distance2<=solver.settings.collisionSampleRadius*solver.settings.collisionSampleRadius) ++crossings.nearSegments;
                    else ++crossings.offSegments;
                }
                break;
            }
            if(!skinHit && shortsHit) ++crossings.shortsOnly;
        }
        return crossings;
    }
};

inline void RunClothPerformanceTests(GameApp& app) {
    std::filesystem::create_directories("generated/cloth-tests");
    app.Scenes().Change(app,"ClothShowroom");
    auto& scene=*dynamic_cast<ClothShowroomScene*>(app.Scenes().Current());
    std::ofstream csv("generated/cloth-tests/performance.csv");
    csv<<"model,physics,update_ms,cloth_ms,draw_and_present_ms\n";
    for(int model=0;model<2;++model) {
        scene.SelectModel(model,true);
        auto& component=scene.Cloth();
        double clothMs=0;
        scene.Character().SetPoseModifier([&](Model::Skeleton& pose,const Matrix4x4& world,float dt) {
            const auto begin=std::chrono::steady_clock::now();
            component.Update(pose,world,dt);
            clothMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        });
        for(bool enabled:{false,true}) {
            component.enabled=enabled; scene.motion=1; scene.gaitAmplitude=1.35f; scene.time=0; component.Reset();
            for(int i=0;i<30;++i) scene.Update(app,1.0f/60);
            clothMs=0;
            const auto begin=std::chrono::steady_clock::now();
            for(int i=0;i<240;++i) scene.Update(app,1.0f/60);
            const double update=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/240;
            const double cloth=clothMs/240;
            const auto drawBegin=std::chrono::steady_clock::now();
            for(int i=0;i<30;++i) { app.ImGui()->Begin(); app.Draw(); }
            app.Dx()->WaitForGPU();
            const double draw=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-drawBegin).count()/30;
            csv<<model<<','<<enabled<<','<<update<<','<<cloth<<','<<draw<<std::endl;
        }
        // Restore a callback that outlives this iteration before references expire.
        auto* saved=&component;
        scene.Character().SetPoseModifier([saved](Model::Skeleton& pose,const Matrix4x4& world,float dt) { saved->Update(pose,world,dt); });
        component.enabled=true;
    }
    std::ofstream("generated/cloth-tests/performance-result.txt")<<"PASS: real model Walk CPU updates and GPU draw/present timings\n";
}

template<class Capture> inline void RunClothMotionContactTests(GameApp& app,ClothShowroomScene& scene,const Capture& capture) {
    const auto check=[](bool ok,const char* message) { if(!ok) throw std::runtime_error(message); };
    const auto length=[](Vector3 v) { return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z); };
    std::ofstream log("generated/cloth-tests/motion-contact-metrics.txt");
    for(int model=0;model<2;++model) {
        scene.SelectModel(model,true);
        auto& object=scene.Character(); auto& cloth=scene.Cloth();
        if(model==0 && AssetLoading::Option("YAN_CLOTH_LEGACY_HEAD",false)) {
            for(auto& binding:cloth.colliders) if(binding.name=="Forehead") binding.collider.enabled=false;
            cloth.groups[1].solver.settings.collisionSamplesPerSegment=0;
            cloth.groups[1].solver.settings.dampingRelativeToAnimation=false;
            cloth.groups[1].solver.settings.maxSwingAngleDegrees=180;
            cloth.groups[1].solver.settings.gravity={0,-2.5f,0};
            cloth.groups[1].solver.settings.damping=.18f; cloth.groups[1].solver.settings.stiffness=.85f;
        }
        ClothSkirtProbes skirt(*object.GetModel()),torso(*object.GetModel(),true,false,ClothSkirtProbes::Part::Torso);
        ClothSkirtProbes hair(*object.GetModel(),false,false,ClothSkirtProbes::Part::Hair),face(*object.GetModel(),true,false,ClothSkirtProbes::Part::Face);
        scene.yaw=scene.cameraYaw=0; scene.gaitAmplitude=1.35f; scene.moving=true; scene.paused=false;
        scene.cameraFollowsCharacter=true;
        cloth.showParticles=cloth.showConstraints=cloth.showCollisionSamples=cloth.showColliders=false;
        scene.motion=2; scene.time=0; cloth.enabled=false; scene.Update(app,0);
        const auto bindHairContact=model==0 ? hair.CountIntersections(object,face,cloth) : ClothSkirtProbes::Crossings{};
        const size_t bindHair=bindHairContact.legs;
        const size_t bindTorso=skirt.CountIntersections(object,torso,cloth).legs;
        const auto extraForehead=[&](const ClothSkirtProbes::Crossings& contact) {
            return static_cast<size_t>(std::count_if(contact.foreheadTriangles.begin(),contact.foreheadTriangles.end(),[&](size_t triangle) {
                return !std::binary_search(bindHairContact.foreheadTriangles.begin(),bindHairContact.foreheadTriangles.end(),triangle);
            }));
        };
        if(model==0) {
            scene.cameraTargetHeight=1.8f; scene.cameraDistance=1.3f; scene.Update(app,0);
            capture(L"generated/cloth-tests/anby-hair-bind.png",false);
            scene.cameraTargetHeight=1; scene.cameraDistance=4.6f;
        }
        cloth.enabled=true; cloth.Reset();
        size_t runHair=0,runTorso=0,transitionHair=0,transitionTorso=0,runForehead=0,transitionForehead=0,runExtra=0,transitionExtra=0,maxExtra=0;
        std::ofstream contacts("generated/cloth-tests/motion-model"+std::to_string(model)+"-contacts.csv");
        contacts<<"trial,frame,hair_face_crossings,hair_forehead_crossings,skirt_body_crossings,additional_forehead_triangles\n";
        for(int frame=0;frame<240;++frame) {
            scene.Update(app,1.0f/60);
            if(frame%12==0) {
                const auto contact=model==0 ? hair.CountIntersections(object,face,cloth) : ClothSkirtProbes::Crossings{};
                const auto body=skirt.CountIntersections(object,torso,cloth).legs;
                runHair+=contact.legs; runForehead+=contact.forehead; runTorso+=body;
                runExtra+=extraForehead(contact);
                contacts<<-1<<','<<frame<<','<<contact.legs<<','<<contact.forehead<<','<<body<<','<<extraForehead(contact)<<std::endl;
            }
        }
        float firstLegJump=0;
        for(int trial=0;trial<4;++trial) {
            // World-space travel is sideways at yaw 0, forward/backward at +/-90.
            scene.yaw=trial==1 ? 1.5707963f : trial==3 ? -1.5707963f : 0;
            scene.motion=1; scene.time=0; cloth.Reset();
            for(int frame=0;frame<40+trial*25;++frame) scene.Update(app,1.0f/60);
            Matrix4x4 before; check(object.TryGetJointWorldMatrix("左ひざD",before),"Transition leg missing");
            scene.motion=2; // Same motion selection as the UI, without resetting cloth.
            for(int frame=0;frame<120;++frame) {
                scene.Update(app,1.0f/60);
                if(frame==0) {
                    Matrix4x4 after; object.TryGetJointWorldMatrix("左ひざD",after);
                    firstLegJump=std::max(firstLegJump,length({after.m[3][0]-before.m[3][0],after.m[3][1]-before.m[3][1],after.m[3][2]-before.m[3][2]}));
                }
                if(frame%12==0) {
                    const auto contact=model==0 ? hair.CountIntersections(object,face,cloth) : ClothSkirtProbes::Crossings{};
                    const auto body=skirt.CountIntersections(object,torso,cloth).legs;
                    transitionHair+=contact.legs; transitionForehead+=contact.forehead; transitionTorso+=body;
                    const auto extra=extraForehead(contact); transitionExtra+=extra;
                    contacts<<trial<<','<<frame<<','<<contact.legs<<','<<contact.forehead<<','<<body<<','<<extra<<std::endl;
                    if(model==0 && extra>maxExtra) {
                        maxExtra=extra; const float savedYaw=scene.cameraYaw;
                        scene.cameraYaw=-scene.yaw; scene.cameraTargetHeight=1.8f; scene.cameraDistance=1.3f; scene.Update(app,0);
                        capture(L"generated/cloth-tests/anby-transition-hair-worst.png",false);
                        scene.cameraYaw=savedYaw; scene.cameraTargetHeight=1; scene.cameraDistance=4.6f;
                    }
                }
                if(trial==2 && (frame==0 || frame==6 || frame==55)) {
                    for(int view=0;view<3;++view) {
                        scene.cameraYaw=view==0 ? 0 : view==1 ? 1.5707963f : .78f;
                        const auto filename=L"generated/cloth-tests/motion-model"+std::to_wstring(model)+L"-frame"+std::to_wstring(frame)+L"-view"+std::to_wstring(view);
                        scene.cameraDistance=4.6f; scene.cameraTargetHeight=1; scene.Update(app,0); capture(filename+L".png",false);
                        if(model==0) {
                            scene.cameraTargetHeight=1.8f; scene.cameraDistance=1.3f; scene.Update(app,0); capture(filename+L"-hair.png",false);
                        }
                    }
                    scene.cameraTargetHeight=1; scene.cameraDistance=4.6f;
                }
            }
        }
        log<<"model="<<model<<" bindHair="<<bindHair<<" bindTorso="<<bindTorso<<" runHair="<<runHair<<" runTorso="<<runTorso
            <<" transitionHair="<<transitionHair<<" transitionTorso="<<transitionTorso<<" firstLegJump="<<firstLegJump
            <<" bindForehead="<<bindHairContact.forehead<<" runForehead="<<runForehead<<" transitionForehead="<<transitionForehead
            <<" additionalRunForehead="<<runExtra<<" additionalTransitionForehead="<<transitionExtra<<" maxAdditionalForehead="<<maxExtra<<std::endl;
        check(firstLegJump<.1f,"Walk/Run selection jumped the leg phase");
        if(model==1) check(runTorso==0 && transitionTorso==0,"ema coat intersected the body during Run or Walk/Run selection");
        if(model==0 && !AssetLoading::Option("YAN_CLOTH_LEGACY_HEAD",false))
            check(runExtra<=40 && transitionExtra<=80 && maxExtra<=5,"Anby fringe folded into the forehead during Run or motion selection");
        scene.gaitAmplitude=1; scene.cameraYaw=scene.yaw=0;
    }
    scene.cameraFollowsCharacter=false;
}

inline void RunClothRuntimeTests(GameApp& app) {
    const auto check=[](bool ok,const char* message) { if(!ok) throw std::runtime_error(message); };
    const auto length=[](Vector3 v) { return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z); };
    std::filesystem::create_directories("generated/cloth-tests");
    app.Scenes().Change(app,"ClothShowroom");
    auto& scene=*dynamic_cast<ClothShowroomScene*>(app.Scenes().Current());
    check(scene.IsCharacterLoaded(0) && !scene.IsCharacterLoaded(1),"Showroom created both GPU model instances before selection");
    std::ofstream log("generated/cloth-tests/metrics.txt");
    const auto capture=[&](const std::wstring& path,bool ui=false) {
        app.ImGui()->Begin(); app.Draw(); app.Dx()->WaitForGPU();
        DirectX::ScratchImage image;
        auto* resource=app.Render()->GetOffscreen()->GetResource();
        auto state=D3D12_RESOURCE_STATE_RENDER_TARGET;
        if(ui) {
            resource=app.Dx()->swapChainResources[1-app.Dx()->swapChain->GetCurrentBackBufferIndex()].Get();
            state=D3D12_RESOURCE_STATE_PRESENT;
        }
        check(SUCCEEDED(DirectX::CaptureTexture(app.Dx()->GetCommandQueue(),resource,false,image,state,state)),"Cloth GPU capture failed");
        check(SUCCEEDED(DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path.c_str())),"Cloth PNG failed");
    };
    if(AssetLoading::Option("YAN_CLOTH_MOTION_TEST_ONLY",false)) {
        RunClothMotionContactTests(app,scene,capture);
        std::ofstream("generated/cloth-tests/result.txt")<<"PASS: motion contact diagnostics\n";
        return;
    }
    for(int model=0;model<2;++model) {
        if(AssetLoading::Option("YAN_EMA_TEST_ONLY",false) && model==0) continue;
        scene.SelectModel(model);
        const auto loadDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
        while(!scene.IsCharacterLoaded(model) && std::chrono::steady_clock::now()<loadDeadline) {
            scene.Update(app,0); std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(scene.IsCharacterLoaded(model),"Asynchronous Showroom model selection timed out");
        auto& cloth=scene.Cloth(); auto& object=scene.Character();
        check(cloth.error.empty() && cloth.groups.size()>=2,"Cloth profile/bone binding failed");
        check(cloth.colliders.size()==(model==0 ? 7u : 11u),"Body collider bindings missing");
        const auto original=object.GetModel()->GetSkeleton().joints;
        size_t contacts=0,sampleContacts=0; float displacement=0, maxPenetration=0, maxStretch=0,maxSamplePenetration=0;
        if(model==1) check(cloth.groups[0].solver.settings.collisionSamplesPerSegment==2 &&
            cloth.groups[0].solver.settings.enableHorizontalCollisionSamples,"ema skirt samples missing");
        for(int motion=0;motion<3;++motion) {
            scene.motion=motion; scene.time=0; cloth.Reset();
            for(int i=0;i<240;++i) {
                scene.Update(app,1.0f/60);
                for(const auto& group:cloth.groups) {
                    contacts+=group.solver.contacts;
                    sampleContacts+=group.solver.sampleContacts;
                    for(const auto& sample:group.solver.collisionSamples) {
                        const auto point=group.solver.particles[sample.a].position*(1-sample.t)+group.solver.particles[sample.b].position*sample.t;
                        check(length(point-sample.position)<.0001f,"Collision sample debug position is stale");
                        for(const auto& binding:cloth.colliders) {
                            const auto& c=binding.collider;
                            maxSamplePenetration=std::max(maxSamplePenetration,c.radius+group.solver.settings.collisionSampleRadius-length(point-c.ClosestPoint(point)));
                        }
                    }
                    for(const auto& p:group.solver.particles) {
                        check(std::isfinite(p.position.x)&&std::isfinite(p.position.y)&&std::isfinite(p.position.z),"Nonfinite cloth particle");
                        displacement=std::max(displacement,length(p.position-p.target));
                        if(p.inverseMass==0) check(length(p.position-p.target)<.001f,"Pinned cloth root drift");
                        else for(const auto& binding:cloth.colliders) {
                            const auto& c=binding.collider;
                            maxPenetration=std::max(maxPenetration,c.radius+p.radius-length(p.position-c.ClosestPoint(p.position)));
                        }
                    }
                    for(const auto& c:group.solver.constraints) if(c.stiffness==1)
                        maxStretch=std::max(maxStretch,std::abs(length(group.solver.particles[c.a].position-group.solver.particles[c.b].position)-c.restLength));
                    for(const auto& chain:group.chains) for(size_t bone=0;bone<chain.joints.size();++bone) {
                        Matrix4x4 renderedJoint;
                        check(object.TryGetJointWorldMatrix(original[chain.joints[bone]].name,renderedJoint),"Simulated bone missing in rendered pose");
                        check(length(Vector3{renderedJoint.m[3][0],renderedJoint.m[3][1],renderedJoint.m[3][2]}-group.solver.particles[chain.particles[bone]].position)<.0001f,"Physics result not applied to skinning pose");
                    }
                }
                Matrix4x4 matrix;
                check(object.TryGetJointWorldMatrix("左足D",matrix),"Leg pose missing");
                const auto& capsule=cloth.colliders[0].collider;
                check(length(capsule.a-Vector3{matrix.m[3][0],matrix.m[3][1],matrix.m[3][2]})<.0001f,"Leg collider did not follow animated bone");
                if(i==90 || i==180) {
                    const auto filename=L"generated/cloth-tests/model"+std::to_wstring(model)+L"-motion"+std::to_wstring(motion)+L"-frame"+std::to_wstring(i)+L".png";
                    capture(filename);
                }
            }
        }
        log<<"model="<<model<<" contacts="<<contacts<<" sampleContacts="<<sampleContacts<<" maxDisplacement="<<displacement<<" maxPenetration="<<maxPenetration<<" maxSamplePenetration="<<maxSamplePenetration<<" maxStructuralError="<<maxStretch<<std::endl;
        check(contacts>0 && displacement>.02f,"Character cloth did not move/collide");
        check(maxPenetration<.004f,"Cloth particle remained inside body collider");
        check(maxStretch<.01f,"Cloth structural constraints stretched excessively");
        check(maxSamplePenetration<.004f,"Collision samples remained inside body collider");
        if(model==0) {
            check(sampleContacts>0,"Anby segment samples never contacted the legs");
            // Missing JSON fields retain the old topology/solver and do not create samples.
            std::ifstream profile("resources/physics/anby.json"); nlohmann::json legacy; profile>>legacy;
            for(auto& group:legacy["groups"]) {
                group.erase("collisionSamplesPerSegment"); group.erase("collisionSampleRadius"); group.erase("enableHorizontalCollisionSamples");
                group.erase("dampingRelativeToAnimation"); group.erase("maxSwingAngleDegrees");
            }
            const std::string legacyPath="generated/cloth-tests/legacy-profile.json";
            { std::ofstream file(legacyPath); file<<legacy.dump(2); }
            ClothComponent compatible;
            check(compatible.Load(legacyPath,object.GetModel()->GetSkeleton()),"Legacy physics JSON failed to load");
            check(compatible.groups[0].solver.collisionSamples.empty() && compatible.groups[0].solver.particles.size()==cloth.groups[0].solver.particles.size(),"Sample compatibility changed bones or particle count");
            check(!compatible.groups[1].solver.settings.dampingRelativeToAnimation && compatible.groups[1].solver.settings.maxSwingAngleDegrees==180,"Missing hair settings changed legacy damping/swing behavior");
            ClothSkirtProbes probes(*object.GetModel()),bodyProbes(*object.GetModel(),true);
            const auto originalSettings=cloth.groups[0].solver.settings;
            std::vector<float> originalRadii; for(const auto& binding:cloth.colliders) originalRadii.push_back(binding.modelRadius);
            std::ofstream comparison("generated/cloth-tests/segment-comparison.csv");
            comparison<<"variant,frame,mesh_probes,inside_original_leg_proxy,max_mesh_penetration,sample_projections,skirt_triangles_crossing_leg_mesh,shorts_only,outside_leg_capsules,outside_sample_envelope,near_segments,off_segments\n";
            const std::array<std::string,4> variants{"legacy","padding-only","vertical","vertical-horizontal"};
            std::array<size_t,4> inside{},crossings{},outsideCounts{},envelopeCounts{},nearCounts{},offCounts{},shortsCounts{}; std::array<float,4> maximum{};
            scene.gaitAmplitude=1.35f; scene.motion=1;
            for(size_t variant=0;variant<variants.size();++variant) {
                auto& settings=cloth.groups[0].solver.settings;
                settings.collisionSamplesPerSegment=variant>=2 ? 2 : 0;
                settings.enableHorizontalCollisionSamples=variant==3;
                for(size_t i=0;i<cloth.colliders.size();++i) {
                    auto& binding=cloth.colliders[i]; binding.modelRadius=originalRadii[i];
                    if(variant==0 && binding.name.find("Leg")!=std::string::npos)
                        binding.modelRadius=binding.name.find("Upper")!=std::string::npos ? .63f : .43f;
                }
                scene.time=0; cloth.Reset();
                for(int frame=0;frame<120;++frame) {
                    scene.Update(app,1.0f/60);
                    if(frame%6==0) {
                        const auto result=probes.Measure(object,cloth);
                        const auto intersections=probes.CountIntersections(object,bodyProbes,cloth);
                        crossings[variant]+=intersections.legs; outsideCounts[variant]+=intersections.outsideColliders;
                        envelopeCounts[variant]+=intersections.outsideSampleEnvelope;
                        nearCounts[variant]+=intersections.nearSegments; offCounts[variant]+=intersections.offSegments; shortsCounts[variant]+=intersections.shortsOnly;
                        check(result.probes>0,"No displayed skirt mesh probes were measured");
                        inside[variant]+=result.inside; maximum[variant]=std::max(maximum[variant],result.maximum);
                        comparison<<variants[variant]<<','<<frame<<','<<result.probes<<','<<result.inside<<','<<result.maximum<<','<<cloth.groups[0].solver.sampleContacts<<','<<intersections.legs<<','<<intersections.shortsOnly<<','<<intersections.outsideColliders<<','<<intersections.outsideSampleEnvelope<<','<<intersections.nearSegments<<','<<intersections.offSegments<<'\n';
                    }
                    if(frame==55) {
                        const std::array<float,3> views{0,1.5707963f,.78f};
                        const std::array<std::wstring,3> names{L"front",L"side",L"oblique"};
                        for(size_t view=0;view<views.size();++view) {
                            scene.cameraYaw=views[view]; scene.Update(app,0);
                            const std::wstring tag(variants[variant].begin(),variants[variant].end());
                            capture(L"generated/cloth-tests/anby-"+tag+L"-"+names[view]+L".png");
                        }
                        if(variant>=2) {
                            cloth.showCollisionSamples=true; scene.cameraYaw=.78f; scene.Update(app,0);
                            capture(L"generated/cloth-tests/anby-"+std::wstring(variants[variant].begin(),variants[variant].end())+L"-samples.png",true);
                            cloth.showCollisionSamples=false;
                        }
                    }
                }
                log<<variants[variant]<<" meshProbePenetrations="<<inside[variant]<<" maxMeshProbePenetration="<<maximum[variant]<<" actualLegMeshTriangleCrossings="<<crossings[variant]
                    <<" shortsOnly="<<shortsCounts[variant]<<" outsideLegCapsules="<<outsideCounts[variant]<<" outsideSampleEnvelope="<<envelopeCounts[variant]<<" nearSegments="<<nearCounts[variant]<<" offSegments="<<offCounts[variant]<<std::endl;
            }
            check(crossings[2]<crossings[0],"Vertical samples and leg padding did not reduce actual rendered-skirt/leg triangle intersections");
            cloth.groups[0].solver.settings=originalSettings;
            for(size_t i=0;i<cloth.colliders.size();++i) cloth.colliders[i].modelRadius=originalRadii[i];
            scene.gaitAmplitude=1; cloth.Reset();
        }
        if(model==1) {
            // ema's front opening is between chains 0 and 1, while 11 -> 0 is fabric.
            const auto edgeCount=[](const ClothComponent::Group& group,size_t a,size_t b) {
                size_t count=0;
                for(const auto& segment:group.solver.collisionSegments) if(segment.horizontal) {
                    const auto contains=[](const auto& chain,size_t p) { return std::find(chain.particles.begin(),chain.particles.end(),p)!=chain.particles.end(); };
                    if((contains(group.chains[a],segment.a)&&contains(group.chains[b],segment.b)) ||
                       (contains(group.chains[b],segment.a)&&contains(group.chains[a],segment.b))) ++count;
                }
                return count;
            };
            check(edgeCount(cloth.groups[0],0,1)==0 && edgeCount(cloth.groups[0],11,0)>0,"ema opening was sewn shut or its back seam was removed");
            std::ifstream profile("resources/physics/ema.json"); nlohmann::json config; profile>>config;
            config["groups"][0].erase("openSeams");
            const std::string profilePath="generated/cloth-tests/seam-profile.json";
            const auto writeProfile=[&]() { std::ofstream file(profilePath); file<<config.dump(2); };
            writeProfile(); ClothComponent compatible;
            check(compatible.Load(profilePath,object.GetModel()->GetSkeleton()) && edgeCount(compatible.groups[0],0,1)>0,"Missing openSeams changed legacy closed-ring topology");
            config["groups"][0]["openSeams"]={{0,3}}; writeProfile();
            check(!compatible.Load(profilePath,object.GetModel()->GetSkeleton()) && compatible.groups.empty(),"Non-neighboring open seam was accepted");
            ClothSkirtProbes skirt(*object.GetModel()),drawers(*object.GetModel(),true,true);
            check(skirt.TriangleCount()>0 && drawers.TriangleCount()>0,"ema skirt/drawer material probes are empty");
            const auto settings=cloth.groups[0].solver.settings;
            const auto constraints=cloth.groups[0].solver.constraints;
            const auto segments=cloth.groups[0].solver.collisionSegments;
            scene.motion=0; scene.moving=false; scene.yaw=0; scene.cameraYaw=0; scene.time=0;
            cloth.enabled=false; scene.Update(app,0);
            const float bindWidth=skirt.Width(object);
            const float bindMiddleWidth=skirt.Width(object,9.5f,11.5f);
            capture(L"generated/cloth-tests/ema-bind-front.png");
            cloth.enabled=true; cloth.Reset();
            float idleWidth=0,idleMiddleWidth=0;
            for(int frame=0;frame<300;++frame) {
                scene.Update(app,1.0f/60);
                if(frame%12==0) { idleWidth=std::max(idleWidth,skirt.Width(object)); idleMiddleWidth=std::max(idleMiddleWidth,skirt.Width(object,9.5f,11.5f)); }
            }
            log<<"ema silhouette bindWidth="<<bindWidth<<" maxIdleWidth="<<idleWidth<<" ratio="<<idleWidth/bindWidth
                <<" bindMiddleWidth="<<bindMiddleWidth<<" maxIdleMiddleWidth="<<idleMiddleWidth<<" middleRatio="<<idleMiddleWidth/bindMiddleWidth<<std::endl;
            check(idleWidth<=bindWidth*1.05f && idleMiddleWidth<=bindMiddleWidth*1.05f,"ema idle coat silhouette widened excessively");
            capture(L"generated/cloth-tests/ema-idle-front.png");
            scene.cameraYaw=3.14159265f; scene.Update(app,0); capture(L"generated/cloth-tests/ema-idle-back.png");
            scene.moving=true;
            std::vector<float> radii; for(const auto& binding:cloth.colliders) radii.push_back(binding.modelRadius);
            std::ofstream comparisons("generated/cloth-tests/ema-drawers-comparison.csv");
            comparisons<<"variant,frame,skirt_triangles_crossing_drawers,outside_leg_capsules,outside_sample_envelope,near_segments,off_segments\n";
            std::array<size_t,4> intersections{},outside{},envelope{},off{};
            std::array<float,4> walkWidths{},middleWidths{};
            const float savedYaw=scene.yaw;
            scene.motion=1; scene.gaitAmplitude=1.35f;
            for(int variant=0;variant<4;++variant) {
                // Include the reported screenshot's character direction/stride.
                scene.yaw=variant==3 ? -103.0f*3.14159265f/180 : 0;
                scene.gaitAmplitude=variant==3 ? 1.0f : 1.35f;
                const char* tag=variant==0 ? "before" : variant==1 ? "wide" : variant==2 ? "fitted" : "screen-angle";
                cloth.groups[0].solver.settings=settings;
                cloth.groups[0].solver.constraints=constraints;
                cloth.groups[0].solver.collisionSegments=segments;
                if(variant<=1) {
                    const auto& a=cloth.groups[0].chains[0]; const auto& b=cloth.groups[0].chains[1];
                    for(size_t row=1;row<std::min(a.particles.size(),b.particles.size());++row) {
                        cloth.groups[0].solver.constraints.push_back({a.particles[row],b.particles[row],0,.65f});
                        cloth.groups[0].solver.collisionSegments.push_back({a.particles[row],b.particles[row],true});
                    }
                }
                for(size_t i=0;i<cloth.colliders.size();++i) cloth.colliders[i].modelRadius=radii[i];
                for(size_t i=6;i<cloth.colliders.size();++i) cloth.colliders[i].collider.enabled=variant!=0;
                if(variant==1) {
                    cloth.groups[0].solver.settings.collisionSampleRadius=.035f;
                    for(size_t i=7;i<cloth.colliders.size();++i) cloth.colliders[i].collider.enabled=false;
                    for(auto& binding:cloth.colliders) if(binding.name.find("UpperLeg")!=std::string::npos) binding.modelRadius=1.1f;
                }
                if(variant==0) {
                    cloth.groups[0].solver.settings.collisionSamplesPerSegment=0;
                    cloth.groups[0].solver.settings.enableHorizontalCollisionSamples=false;
                    for(auto& binding:cloth.colliders) {
                        if(binding.name.find("UpperLeg")!=std::string::npos) binding.modelRadius=.63f;
                        if(binding.name.find("LowerLeg")!=std::string::npos) binding.modelRadius=.43f;
                    }
                }
                scene.time=0; cloth.Reset();
                for(int frame=0;frame<120;++frame) {
                    scene.Update(app,1.0f/60);
                    if(frame%12==0) {
                        const auto crossing=skirt.CountIntersections(object,drawers,cloth);
                        walkWidths[variant]=std::max(walkWidths[variant],skirt.Width(object));
                        middleWidths[variant]=std::max(middleWidths[variant],skirt.Width(object,9.5f,11.5f));
                        intersections[variant]+=crossing.legs; outside[variant]+=crossing.outsideColliders;
                        envelope[variant]+=crossing.outsideSampleEnvelope; off[variant]+=crossing.offSegments;
                        comparisons<<tag<<','<<frame<<','<<crossing.legs<<','<<crossing.outsideColliders<<','
                            <<crossing.outsideSampleEnvelope<<','<<crossing.nearSegments<<','<<crossing.offSegments<<std::endl;
                    }
                    if(frame==55) {
                        const std::array<float,3> angles{0,1.5707963f,.78f};
                        const std::array<std::wstring,3> names{L"front",L"side",L"oblique"};
                        for(size_t view=0;view<angles.size();++view) {
                            scene.cameraYaw=angles[view]; scene.Update(app,0);
                            const std::wstring wideTag=variant==0 ? L"before" : variant==1 ? L"wide" : variant==2 ? L"fitted" : L"screen-angle";
                            capture(L"generated/cloth-tests/ema-"+wideTag+L"-"+names[view]+L".png");
                        }
                    }
                }
            }
            for(size_t variant=0;variant<intersections.size();++variant)
                log<<"ema drawers variant="<<variant<<" intersections="<<intersections[variant]<<" outsideCapsules="<<outside[variant]<<" outsideSampleEnvelope="<<envelope[variant]<<" offSegments="<<off[variant]
                    <<" maxWalkWidth="<<walkWidths[variant]<<" maxWalkMiddleWidth="<<middleWidths[variant]<<std::endl;
            check(intersections[0]>0 && intersections[2]<intersections[0],"ema drawer/skirt mesh intersections did not decrease");
            check(intersections[2]==0 && intersections[3]==0,"ema fitted coat still intersected drawers in tested Walk poses");
            check(middleWidths[2]<middleWidths[1]*.95f && middleWidths[2]<=bindMiddleWidth*1.10f,"ema Walk coat retained the oversized hip silhouette");
            cloth.groups[0].solver.settings=settings;
            cloth.groups[0].solver.constraints=constraints;
            cloth.groups[0].solver.collisionSegments=segments;
            for(size_t i=0;i<cloth.colliders.size();++i) cloth.colliders[i].modelRadius=radii[i];
            for(auto& binding:cloth.colliders) binding.collider.enabled=true;
            scene.gaitAmplitude=1; scene.yaw=savedYaw; cloth.Reset();
        }
        scene.cameraYaw=1.2f; scene.motion=1; cloth.Reset();
        for(int i=0;i<55;++i) scene.Update(app,1.0f/60);
        capture(L"generated/cloth-tests/model"+std::to_wstring(model)+L"-walk-side-on.png");
        cloth.showParticles=cloth.showConstraints=cloth.showCollisionSamples=true;
        capture(L"generated/cloth-tests/model"+std::to_wstring(model)+L"-debug-workspace.png",true);
        cloth.showParticles=cloth.showConstraints=cloth.showCollisionSamples=false;
        // Compare enabled/disabled at exactly the same animated pose and camera.
        cloth.enabled=false; scene.Update(app,0);
        for(const auto& group:cloth.groups) for(const auto& p:group.solver.particles)
            check(length(p.position-p.target)<.0001f,"Disabled physics failed to restore pose");
        capture(L"generated/cloth-tests/model"+std::to_wstring(model)+L"-physics-off.png");
        scene.cameraYaw=0;
        cloth.enabled=true; scene.Update(app,1.0f/60);
        for(size_t i=0;i<original.size();++i)
            check(original[i].skeletonSpaceMatrix.m[3][1]==object.GetModel()->GetSkeleton().joints[i].skeletonSpaceMatrix.m[3][1],"Shared Model skeleton modified");
    }
    RunClothMotionContactTests(app,scene,capture);
    std::ofstream("generated/cloth-tests/result.txt")<<"PASS: both PMX models, profiles/compatibility, Idle/Walk/Run FK, continuous Walk/Run selection, body/forehead mesh contacts, fixed roots, colliders, skinning/render captures, disable/re-enable, shared model isolation\n";
}
