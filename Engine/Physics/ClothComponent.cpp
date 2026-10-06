#include "ClothComponent.h"
#include "AnimationEvaluate.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cmath>
#include <stdexcept>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
Vector3 Point(const Vector3& p,const Matrix4x4& m) {
    return {p.x*m.m[0][0]+p.y*m.m[1][0]+p.z*m.m[2][0]+m.m[3][0],
            p.x*m.m[0][1]+p.y*m.m[1][1]+p.z*m.m[2][1]+m.m[3][1],
            p.x*m.m[0][2]+p.y*m.m[1][2]+p.z*m.m[2][2]+m.m[3][2]};
}
float Length(const Vector3& v) { return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z); }
Vector3 ReadVec(const nlohmann::json& j) { return {j.at(0).get<float>(),j.at(1).get<float>(),j.at(2).get<float>()}; }
Matrix4x4 Swing(Vector3 a,Vector3 b) {
    const float la=Length(a),lb=Length(b);
    if (la<1e-6f || lb<1e-6f) return Matrix4x4::MakeIdentity4x4();
    a*=1/la; b*=1/lb;
    const float dot=std::clamp(a.x*b.x+a.y*b.y+a.z*b.z,-1.0f,1.0f);
    auto axis=Matrix4x4::Cross(a,b);
    if (dot<-.9999f) {
        axis=Matrix4x4::Cross(a,std::abs(a.x)<.8f ? Vector3{1,0,0} : Vector3{0,1,0});
        axis*=1/Length(axis);
        return MakeAffineMatrix({1,1,1},Quaternion{axis.x,axis.y,axis.z,0},{0,0,0});
    }
    const float norm=std::sqrt(2*(1+dot));
    return MakeAffineMatrix({1,1,1},Quaternion{axis.x/norm,axis.y/norm,axis.z/norm,norm*.5f},{0,0,0});
}
}

bool ClothComponent::Load(const std::string& path,const Model::Skeleton& bind) {
    groups.clear(); colliders.clear(); error.clear();
    try {
        std::ifstream file(path); nlohmann::json config; file>>config;
        const auto joint=[&](const std::string& name) {
            const auto it=bind.jointMap.find(name);
            if (it==bind.jointMap.end()) throw std::runtime_error("Missing bone: "+name);
            return it->second;
        };
        std::vector<bool> used(bind.joints.size());
        for (const auto& definition:config.at("groups")) {
            Group group; group.name=definition.at("name");
            auto& solver=group.solver;
            solver.settings.gravity=ReadVec(definition.at("gravity"));
            solver.settings.damping=definition.at("damping");
            solver.settings.dampingRelativeToAnimation=definition.value("dampingRelativeToAnimation",false);
            solver.settings.maxSwingAngleDegrees=definition.value("maxSwingAngleDegrees",180.0f);
            if(!std::isfinite(solver.settings.maxSwingAngleDegrees) || solver.settings.maxSwingAngleDegrees<0 || solver.settings.maxSwingAngleDegrees>180)
                throw std::runtime_error("Invalid swing angle: "+group.name);
            solver.settings.stiffness=definition.at("stiffness");
            solver.settings.iterations=definition.value("iterations",12);
            solver.settings.collisionSamplesPerSegment=definition.value("collisionSamplesPerSegment",0);
            solver.settings.collisionSampleRadius=definition.value("collisionSampleRadius",.012f);
            solver.settings.enableHorizontalCollisionSamples=definition.value("enableHorizontalCollisionSamples",false);
            if (solver.settings.collisionSamplesPerSegment<0 || solver.settings.collisionSamplesPerSegment>8 ||
                !std::isfinite(solver.settings.collisionSampleRadius) || solver.settings.collisionSampleRadius<0)
                throw std::runtime_error("Invalid collision sample settings: "+group.name);
            for (const auto& names:definition.at("chains")) {
                Chain chain;
                for (const auto& name:names) {
                    const int index=joint(name.get<std::string>());
                    if (used[index]) throw std::runtime_error("Bone assigned twice: "+bind.joints[index].name);
                    used[index]=true; chain.joints.push_back(index);
                    PhysicsParticle p; p.target=Point({},bind.joints[index].skeletonSpaceMatrix);
                    p.inverseMass=chain.particles.empty() ? 0.0f : 1.0f;
                    chain.particles.push_back(solver.particles.size()); solver.particles.push_back(p);
                }
                if (chain.joints.size()<2) throw std::runtime_error("A chain requires at least two bones");
                const auto last=solver.particles[chain.particles.back()].target;
                const auto prev=solver.particles[chain.particles[chain.particles.size()-2]].target;
                // Virtual tip gives the last weighted bone a simulated orientation too.
                const auto tip=last+(last-prev)*.65f;
                chain.tipLocal=Point(tip,Matrix4x4::Inverse(bind.joints[chain.joints.back()].skeletonSpaceMatrix));
                PhysicsParticle p; p.target=tip;
                chain.particles.push_back(solver.particles.size()); solver.particles.push_back(p);
                for (size_t i=1;i<chain.particles.size();++i) {
                    const size_t a=chain.particles[i-1],b=chain.particles[i];
                    solver.constraints.push_back({a,b,Length(solver.particles[a].target-solver.particles[b].target),1});
                    solver.collisionSegments.push_back({a,b,false});
                    if (i>1) {
                        const size_t c=chain.particles[i-2];
                        solver.constraints.push_back({c,b,Length(solver.particles[c].target-solver.particles[b].target),.25f});
                    }
                }
                group.chains.push_back(std::move(chain));
            }
            if (definition.value("closedRing",false)) {
                // A coat can be ring-shaped in bone layout while its displayed
                // mesh has an opening. Do not sew that opening shut with springs.
                std::vector<bool> openEdges(group.chains.size(),false);
                for(const auto& seam:definition.value("openSeams",nlohmann::json::array())) {
                    if(!seam.is_array() || seam.size()!=2 || !seam[0].is_number_integer() || !seam[1].is_number_integer())
                        throw std::runtime_error("Invalid open seam: "+group.name);
                    const int a=seam[0].get<int>(),b=seam[1].get<int>();
                    const int count=static_cast<int>(group.chains.size());
                    if(a<0 || b<0 || a>=count || b>=count || a==b || ((a+1)%count!=b && (b+1)%count!=a))
                        throw std::runtime_error("Open seam must join neighboring chains: "+group.name);
                    openEdges[(a+1)%count==b ? a : b]=true;
                }
                for (size_t c=0;c<group.chains.size();++c) {
                    if(openEdges[c]) continue;
                    const auto& a=group.chains[c]; const auto& b=group.chains[(c+1)%group.chains.size()];
                    for (size_t row=1;row<std::min(a.particles.size(),b.particles.size());++row) {
                        const size_t ia=a.particles[row],ib=b.particles[row];
                        solver.constraints.push_back({ia,ib,Length(solver.particles[ia].target-solver.particles[ib].target),.65f});
                        solver.collisionSegments.push_back({ia,ib,true});
                    }
                }
            }
            solver.RebuildCollisionSamples();
            groups.push_back(std::move(group));
        }
        for (const auto& definition:config.at("colliders")) {
            ColliderBinding binding;
            binding.name=definition.at("name"); binding.start=joint(definition.at("start"));
            binding.end=joint(definition.value("end",definition.at("start").get<std::string>()));
            binding.modelRadius=definition.at("radius");
            binding.collider.shape=definition.value("shape","Capsule")=="Sphere" ? PhysicsCollider::Shape::Sphere : PhysicsCollider::Shape::Capsule;
            if (definition.contains("startOffset")) binding.startOffset=ReadVec(definition["startOffset"]);
            if (definition.contains("endOffset")) binding.endOffset=ReadVec(definition["endOffset"]);
            colliders.push_back(binding);
        }
        Reset(); return true;
    } catch (const std::exception& e) { error=e.what(); groups.clear(); colliders.clear(); return false; }
}

void ClothComponent::Reset() { resetPending_=true; }

void ClothComponent::Update(Model::Skeleton& pose,const Matrix4x4& world,float dt) {
    if (groups.empty()) return;
    const auto inverseWorld=Matrix4x4::Inverse(world);
    const float worldScale=Length({world.m[0][0],world.m[0][1],world.m[0][2]});
    std::vector<Matrix4x4> animated; animated.reserve(pose.joints.size());
    for (const auto& j:pose.joints) animated.push_back(Matrix4x4::Multiply(j.skeletonSpaceMatrix,world));
    std::vector<PhysicsCollider> shapes;
    for (auto& binding:colliders) {
        auto& c=binding.collider;
        c.previousA=c.a; c.previousB=c.b;
        c.a=Point(binding.startOffset,animated[binding.start]);
        c.b=Point(binding.endOffset,animated[binding.end]);
        c.radius=binding.modelRadius*worldScale;
        if (resetPending_) { c.previousA=c.a; c.previousB=c.b; }
        shapes.push_back(c);
    }
    std::vector<Matrix4x4> desired(pose.joints.size());
    std::vector<bool> overridden(pose.joints.size());
    for (auto& group:groups) {
        auto& solver=group.solver;
        for (const auto& chain:group.chains) {
            for (size_t i=0;i<chain.joints.size();++i) {
                auto& p=solver.particles[chain.particles[i]];
                p.target=Point({},animated[chain.joints[i]]); p.radius=.012f;
            }
            solver.particles[chain.particles.back()].target=Point(chain.tipLocal,animated[chain.joints.back()]);
        }
        // Recompute rest distances for the current model scale, without accumulating deformation.
        for (auto& c:solver.constraints) c.restLength=Length(solver.particles[c.a].target-solver.particles[c.b].target);
        const bool localEnabled=solver.settings.enabled;
        if (!enabled) solver.settings.enabled=false;
        if (resetPending_) solver.Reset();
        solver.Update(dt,shapes);
        solver.settings.enabled=localEnabled;
        if (!enabled || !localEnabled) continue;
        for (const auto& chain:group.chains) for (size_t i=0;i<chain.joints.size();++i) {
            const auto& p=solver.particles[chain.particles[i]];
            const auto& next=solver.particles[chain.particles[i+1]];
            const int joint=chain.joints[i];
            auto base=animated[joint]; base.m[3][0]=base.m[3][1]=base.m[3][2]=0;
            auto result=Matrix4x4::Multiply(base,Swing(next.target-p.target,next.position-p.position));
            result.m[3][0]=p.position.x; result.m[3][1]=p.position.y; result.m[3][2]=p.position.z;
            desired[joint]=Matrix4x4::Multiply(result,inverseWorld); overridden[joint]=true;
        }
    }
    // Parent-first skeleton order. Unselected descendants retain their animated local transforms.
    for (size_t i=0;i<pose.joints.size();++i) {
        auto& j=pose.joints[i];
        if (overridden[i]) {
            j.skeletonSpaceMatrix=desired[i];
            j.localMatrix=j.parent ? Matrix4x4::Multiply(desired[i],Matrix4x4::Inverse(pose.joints[*j.parent].skeletonSpaceMatrix)) : desired[i];
        } else j.skeletonSpaceMatrix=j.parent ? Matrix4x4::Multiply(j.localMatrix,pose.joints[*j.parent].skeletonSpaceMatrix) : j.localMatrix;
    }
    resetPending_=false;
}

void ClothComponent::DrawImGui(const Matrix4x4& vp,const Vector2& lo,const Vector2& hi) {
#ifdef USE_IMGUI
    ImGui::Checkbox("Physics enabled",&enabled); ImGui::SameLine();
    if (ImGui::Button("Reset cloth")) Reset();
    ImGui::Checkbox("Collider display",&showColliders); ImGui::SameLine();
    ImGui::Checkbox("Particle display",&showParticles);
    ImGui::Checkbox("Constraint display",&showConstraints);
    ImGui::Checkbox("Collision Sample display",&showCollisionSamples);
    if (showCollisionSamples) ImGui::TextWrapped("Samples: purple = no contact, pink = pushed this update. Hollow = horizontal. Positions are interpolated from the corrected endpoints.");
    if (!error.empty()) ImGui::TextWrapped("Profile error: %s",error.c_str());
    for (auto& group:groups) {
        ImGui::PushID(group.name.c_str());
        if (ImGui::TreeNode(group.name.c_str())) {
            auto& s=group.solver.settings;
            ImGui::Checkbox("Enabled",&s.enabled);
            ImGui::DragFloat3("Gravity",&s.gravity.x,.1f,-30,30);
            ImGui::SliderFloat("Damping",&s.damping,0,1);
            ImGui::Checkbox("Damp relative to animated pose",&s.dampingRelativeToAnimation);
            ImGui::SliderFloat("Max swing angle",&s.maxSwingAngleDegrees,0,180,"%.0f deg");
            ImGui::SliderInt("Constraint iterations",&s.iterations,1,32);
            ImGui::SliderFloat("Stiffness / return",&s.stiffness,0,1);
            ImGui::SliderInt("Samples per segment",&s.collisionSamplesPerSegment,0,8);
            ImGui::DragFloat("Sample radius (world)",&s.collisionSampleRadius,.001f,0,.05f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
            ImGui::Checkbox("Horizontal collision samples",&s.enableHorizontalCollisionSamples);
            ImGui::Text("%zu particles / %zu constraints / %zu contact projections",group.solver.particles.size(),group.solver.constraints.size(),group.solver.contacts);
            ImGui::Text("%zu collision samples / %zu sample projections",group.solver.collisionSamples.size(),group.solver.sampleContacts);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (ImGui::TreeNode("Body colliders")) {
        for (auto& b:colliders) {
            ImGui::PushID(b.name.c_str()); ImGui::Checkbox(b.name.c_str(),&b.collider.enabled);
            ImGui::SliderFloat("Radius (model units)",&b.modelRadius,.05f,2.0f); ImGui::PopID();
        }
        ImGui::TreePop();
    }
    if (hi.x<=lo.x || hi.y<=lo.y) return;
    auto* draw=ImGui::GetForegroundDrawList();
    draw->PushClipRect({lo.x,lo.y},{hi.x,hi.y},true);
    const auto project=[&](Vector3 p,ImVec2& out) {
        const float w=p.x*vp.m[0][3]+p.y*vp.m[1][3]+p.z*vp.m[2][3]+vp.m[3][3];
        const auto clip=Point(p,vp);
        if (w<=.001f || clip.z<0) return false;
        out={lo.x+(clip.x/w*.5f+.5f)*(hi.x-lo.x),lo.y+(-clip.y/w*.5f+.5f)*(hi.y-lo.y)}; return true;
    };
    const auto line=[&](Vector3 a,Vector3 b,ImU32 color) { ImVec2 pa,pb; if(project(a,pa)&&project(b,pb)) draw->AddLine(pa,pb,color,1.5f); };
    const auto sphere=[&](Vector3 center,float radius,ImU32 color) {
        for(int plane=0;plane<3;++plane) for(int i=0;i<24;++i) {
            const auto offset=[&](int index) { const float a=static_cast<float>(index)*6.2831853f/24; const float x=std::cos(a)*radius,y=std::sin(a)*radius;
                return plane==0 ? Vector3{x,y,0} : plane==1 ? Vector3{x,0,y} : Vector3{0,x,y}; };
            line(center+offset(i),center+offset(i+1),color);
        }
    };
    for(const auto& group:groups) {
        if(showConstraints) for(const auto& c:group.solver.constraints) line(group.solver.particles[c.a].position,group.solver.particles[c.b].position,IM_COL32(70,220,255,150));
        if(showParticles) for(const auto& p:group.solver.particles) { ImVec2 screen; if(project(p.position,screen)) draw->AddCircleFilled(screen,p.inverseMass==0 ? 4.0f : 2.5f,p.inverseMass==0 ? IM_COL32(255,210,20,255) : IM_COL32(100,255,130,255)); }
        if(showCollisionSamples) for(const auto& sample:group.solver.collisionSamples) {
            ImVec2 screen;
            if(project(sample.position,screen)) {
                const ImU32 color=sample.contacted ? IM_COL32(255,65,160,255) : IM_COL32(190,150,255,230);
                if(sample.horizontal) draw->AddCircle(screen,3,color,8,1.5f);
                else draw->AddCircleFilled(screen,3,color,8);
            }
        }
    }
    if(showColliders) for(const auto& binding:colliders) {
        const auto& c=binding.collider; if(!c.enabled) continue;
        const ImU32 color=IM_COL32(255,125,40,210);
        sphere(c.a,c.radius,color);
        if(c.shape==PhysicsCollider::Shape::Capsule && Length(c.b-c.a)>1e-6f) {
            sphere(c.b,c.radius,color);
            auto axis=Matrix4x4::Normalize(c.b-c.a);
            auto u=Matrix4x4::Normalize(Matrix4x4::Cross(axis,std::abs(axis.y)<.9f ? Vector3{0,1,0} : Vector3{1,0,0}));
            auto v=Matrix4x4::Cross(axis,u);
            for(int i=0;i<8;++i) { const float a=static_cast<float>(i)*6.2831853f/8; const auto offset=(u*std::cos(a)+v*std::sin(a))*c.radius; line(c.a+offset,c.b+offset,color); }
        }
        ImVec2 screen; if(project(c.a,screen)) draw->AddText(screen,color,binding.name.c_str());
    }
    draw->PopClipRect();
#else
    (void)vp; (void)lo; (void)hi;
#endif
}
