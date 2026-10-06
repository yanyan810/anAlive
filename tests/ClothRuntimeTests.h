#pragma once
#include "scene/Main/ClothShowroomScene.h"
#include "DirectXTex.h"
#include "ImGuiManagaer.h"
#include <fstream>
#include <stdexcept>

inline void RunClothRuntimeTests(GameApp& app) {
    const auto check=[](bool ok,const char* message) { if(!ok) throw std::runtime_error(message); };
    const auto length=[](Vector3 v) { return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z); };
    std::filesystem::create_directories("generated/cloth-tests");
    app.Scenes().Change(app,"ClothShowroom");
    auto& scene=*dynamic_cast<ClothShowroomScene*>(app.Scenes().Current());
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
    for(int model=0;model<2;++model) {
        scene.SelectModel(model);
        auto& cloth=scene.Cloth(); auto& object=scene.Character();
        check(cloth.error.empty() && cloth.groups.size()>=2,"Cloth profile/bone binding failed");
        check(cloth.colliders.size()==6,"Body collider bindings missing");
        const auto original=object.GetModel()->GetSkeleton().joints;
        size_t contacts=0; float displacement=0, maxPenetration=0, maxStretch=0;
        for(int motion=0;motion<3;++motion) {
            scene.motion=motion; scene.time=0; cloth.Reset();
            for(int i=0;i<240;++i) {
                scene.Update(app,1.0f/60);
                for(const auto& group:cloth.groups) {
                    contacts+=group.solver.contacts;
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
        log<<"model="<<model<<" contacts="<<contacts<<" maxDisplacement="<<displacement<<" maxPenetration="<<maxPenetration<<" maxStructuralError="<<maxStretch<<'\n';
        check(contacts>0 && displacement>.02f,"Character cloth did not move/collide");
        check(maxPenetration<.004f,"Cloth particle remained inside body collider");
        check(maxStretch<.01f,"Cloth structural constraints stretched excessively");
        scene.cameraYaw=1.2f; scene.motion=1; cloth.Reset();
        for(int i=0;i<55;++i) scene.Update(app,1.0f/60);
        capture(L"generated/cloth-tests/model"+std::to_wstring(model)+L"-walk-side-on.png");
        cloth.showParticles=cloth.showConstraints=true;
        capture(L"generated/cloth-tests/model"+std::to_wstring(model)+L"-debug-workspace.png",true);
        cloth.showParticles=cloth.showConstraints=false;
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
    std::ofstream("generated/cloth-tests/result.txt")<<"PASS: both PMX models, explicit profiles, Idle/Walk/Run FK, fixed roots, bone-following colliders, collisions, skinning/render captures, disable/re-enable, shared model isolation\n";
}
