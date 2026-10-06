#pragma once
#include "GameApp.h"
#include "ModelManager.h"
#include "ModelCache.h"
#include "Object3d.h"
#include <stdexcept>
#include <assimp/Importer.hpp>

inline void RunAssetLoadingTests(GameApp& app) {
    using namespace AssetLoading;
    const auto check=[](bool ok,const char* message) { if(!ok) throw std::runtime_error(message); };
    std::filesystem::create_directories("generated/loading-tests");
    Assimp::Importer formats;
    std::ofstream("generated/loading-tests/formats.txt")<<"FBX="<<formats.IsExtensionSupported("fbx")<<" GLB="<<formats.IsExtensionSupported("glb")<<" GLTF="<<formats.IsExtensionSupported("gltf")<<" OBJ="<<formats.IsExtensionSupported("obj")<<'\n';
    std::ofstream metrics("generated/loading-tests/models.csv");
    metrics<<"model,milliseconds,upload_submissions,debug_objects,vertices,bones,clips,default_clip,model_digest\n";
    std::vector<std::string> paths{"MyGtYUhe6t/安比.pmx","ema/SakurabaEma_ByPOWER.pmx","CGTest/RobotExpressive.glb","gltf/test.gltf"};
    wchar_t single[512]{}; if(GetEnvironmentVariableW(L"YAN_LOAD_TEST_MODEL",single,512)) paths={StringUtility::ConvertString(single)};
    for(const auto& path:paths) {
        Object3d object;
        object.Initialize(app.ObjCom(),app.Dx(),app.Srv(),app.SkinCom());
        const auto submissions=app.Dx()->GetTextureUploadSubmissions();
        const auto start=std::chrono::steady_clock::now();
        object.SetModel(path);
        const auto milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        auto* model=object.GetModel();
        check(model && model->GetVertexCount()>0,"Model failed to load");
        check(object.GetBoneDebugObjectCount()==0 || Option("YAN_EAGER_BONE_DEBUG",false),"Hidden bone debug objects were eagerly created");
        ModelCache::Writer writer; writer.Data(model->GetModelData());
        metrics<<path<<','<<milliseconds<<','<<app.Dx()->GetTextureUploadSubmissions()-submissions<<','<<object.GetBoneDebugObjectCount()<<','
            <<model->GetVertexCount()<<','<<model->GetSkeleton().joints.size()<<','<<model->GetAnimations().size()<<','<<model->GetDefaultAnimationName()<<','<<Hash(writer.bytes)<<std::endl;
        if(!model->GetAnimations().empty()) {
            object.PlayAnimation(); check(object.GetPlayingAnimName()==model->GetDefaultAnimationName(),"Implicit default animation changed");
        }
        object.Update(1.0f/60);
        if(model->HasSkinning()) {
            // The optimized Animator restores mutable pose data while retaining
            // immutable topology. A modifier must never leak into the next pose.
            Animator animator;
            animator.Initialize(model);
            const auto compare=[&](const Model::Skeleton& expected) {
                const auto& actual=animator.GetPoseSkeleton();
                check(actual.joints.size()==expected.joints.size() && actual.jointMap==expected.jointMap,"Animator topology changed");
                for(size_t i=0;i<actual.joints.size();++i) {
                    check(actual.joints[i].name==expected.joints[i].name && actual.joints[i].parent==expected.joints[i].parent,"Animator bone identity changed");
                    for(int row=0;row<4;++row) for(int column=0;column<4;++column)
                        check(std::abs(actual.joints[i].skeletonSpaceMatrix.m[row][column]-expected.joints[i].skeletonSpaceMatrix.m[row][column])<.0001f,"Animator pose failed to reset");
                }
            };
            for(auto& joint:animator.GetPoseSkeleton().joints) { joint.transform.translate={30,40,50}; joint.localMatrix=Matrix4x4{}; }
            animator.Update(0); compare(model->GetSkeleton());
            Animator::ManualJointTransform manual; manual.rotate={.2f,0,0}; manual.translate={.3f,0,0};
            animator.SetManualJointTransform(0,manual); animator.Update(0);
            const auto manualPose=animator.GetPoseSkeleton();
            animator.Update(0); compare(manualPose);
            animator.ResetManualJointTransforms(); animator.Update(0); compare(model->GetSkeleton());
            if(!model->GetAnimations().empty()) {
                animator.PlayAnimation(model->GetDefaultAnimationName(),false); animator.Update(.2f);
                const auto animated=animator.GetPoseSkeleton();
                for(auto& joint:animator.GetPoseSkeleton().joints) joint.transform.translate={100,100,100};
                animator.Update(0); compare(animated);
                animator.StopAnimation(); animator.Update(0); compare(model->GetSkeleton());
            }
            object.SetDebugSelectedBone(0); object.SetDebugDrawBones(true); object.Update(0);
            const auto count=object.GetBoneDebugObjectCount();
            check(count>=model->GetSkeleton().joints.size(),"Lazy bone debug display failed");
            object.Update(0); check(object.GetBoneDebugObjectCount()==count,"Bone debug objects were recreated each update");
            object.SetDebugDrawBones(false);
        }
        Model* shared=ModelManager::GetInstance()->FindModel("resources/"+path);
        check(shared==model,"Normalized model path alias loaded another model");
        for(const auto& t:model->GetModelData().embeddedTextures)
            check(TextureManager::GetInstance()->HasTexture(t.key),"Embedded texture lost on model cache path");
        const auto& mats=model->GetMaterials();
        for(const auto& m:mats) if(!m.textureFilePath.empty() && std::filesystem::is_regular_file(Path(m.textureFilePath))) {
            const auto before=app.Dx()->GetTextureUploadSubmissions();
            const auto canonical=Canonical(m.textureFilePath);
            TextureManager::GetInstance()->LoadTexture(canonical);
            check(TextureManager::GetInstance()->GetSrvIndex(canonical)==TextureManager::GetInstance()->GetSrvIndex(m.textureFilePath) &&
                app.Dx()->GetTextureUploadSubmissions()==before,"Texture alias was uploaded twice");
        }
    }
    // Dependency invalidation and corruption are checked on test fixtures, never user assets.
    if(CacheEnabled()) {
        const std::string dir="generated/loading-tests/fixtures";
        std::filesystem::create_directories(dir);
        const auto obj=dir+"/triangle.obj",mtl=dir+"/triangle.mtl";
        { std::ofstream file(Path(obj)); file<<"mtllib triangle.mtl\no triangle\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl cloth\nf 1 2 3\n"; }
        { std::ofstream file(Path(mtl)); file<<"newmtl cloth\nKd 1 0 0\n"; }
        auto source=Model::ReadSourceData(dir,"triangle.obj");
        Model::ModelData cached;
        check(ModelCache::Load(obj,cached),"Fresh model cache was not reusable");
        ModelCache::Writer a,b; a.Data(source); b.Data(cached);
        check(a.bytes==b.bytes,"Model cache changed geometry/material/node data");
        const auto stamp=std::filesystem::last_write_time(Path(mtl));
        std::filesystem::last_write_time(Path(mtl),stamp+std::chrono::seconds(2));
        check(!ModelCache::Load(obj,cached),"Updated MTL did not invalidate the model cache");
        source=Model::ReadSourceData(dir,"triangle.obj");
        { std::ofstream file(CachePath("models",Canonical(obj),".ymodel"),std::ios::binary|std::ios::trunc); file<<"corrupt"; }
        check(!ModelCache::Load(obj,cached),"Corrupt model cache was accepted");
        check(!Model::ReadSourceData(dir,"triangle.obj").meshes.empty() && ModelCache::Load(obj,cached),"Corrupt model cache did not recover");
        const auto blocked=dir+"/blocked.obj";
        std::filesystem::copy_file(Path(obj),Path(blocked),std::filesystem::copy_options::overwrite_existing);
        const auto blockedTarget=CachePath("models",Canonical(blocked),".ymodel");
        std::filesystem::create_directory(blockedTarget);
        check(!Model::ReadSourceData(dir,"blocked.obj").meshes.empty(),"Unwritable model cache prevented normal import");
        std::filesystem::remove(blockedTarget); // Only the empty fixture directory created above.
        std::filesystem::last_write_time(Path(obj),std::filesystem::last_write_time(Path(obj))+std::chrono::seconds(2));
        check(!ModelCache::Load(obj,cached),"Updated model did not invalidate the cache");
        const auto buffer=dir+"/outside.bin",gltf=dir+"/dependencies.gltf";
        { std::ofstream file(Path(buffer)); file<<"buffer"; }
        { std::ofstream file(Path(gltf)); file<<R"({"buffers":[{"uri":"outside.bin","byteLength":6}]})"; }
        const auto signature=ModelCache::Signature(gltf);
        std::filesystem::last_write_time(Path(buffer),std::filesystem::last_write_time(Path(buffer))+std::chrono::seconds(2));
        check(ModelCache::Signature(gltf)!=signature,"External GLTF buffer change was missed");
        DirectX::ScratchImage image;
        check(SUCCEEDED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,8,8,1,1)),"Texture fixture allocation failed");
        std::memset(image.GetPixels(),127,image.GetPixelsSize());
        DirectX::Blob dds;
        check(SUCCEEDED(DirectX::SaveToDDSMemory(image.GetImages(),image.GetImageCount(),image.GetMetadata(),DirectX::DDS_FLAGS_NONE,dds)),"DDS fixture encoding failed");
        const auto target=CachePath("textures","test-dds", ".ytex");
        SaveBlob(target,{static_cast<const uint8_t*>(dds.GetBufferPointer()),dds.GetBufferSize()});
        std::vector<uint8_t> bytes;
        check(LoadBlob(target,bytes),"DDS cache was not reusable");
        DirectX::ScratchImage read;
        check(SUCCEEDED(DirectX::LoadFromDDSMemory(bytes.data(),bytes.size(),DirectX::DDS_FLAGS_NONE,nullptr,read)) &&
            image.GetMetadata().format==read.GetMetadata().format && image.GetPixelsSize()==read.GetPixelsSize() &&
            std::memcmp(image.GetPixels(),read.GetPixels(),image.GetPixelsSize())==0,"DDS cache changed pixels or color space");
        { std::fstream file(target,std::ios::in|std::ios::out|std::ios::binary); file.seekp(-1,std::ios::end); file.put(0); }
        check(!LoadBlob(target,bytes),"Damaged DDS payload checksum was accepted");
        const std::string texture=dir+"/texture.png";
        check(SUCCEEDED(DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),Path(texture).c_str())),"Texture fixture save failed");
        const auto first=TextureManager::PrepareFile(texture);
        check(first.image && first.image->GetMetadata().mipLevels==4,"Derived DDS omitted the full mip chain");
        const auto derived=CachePath("textures","dds-v1:"+FileStamp(texture),".ytex");
        { std::ofstream file(derived,std::ios::binary|std::ios::trunc); file<<"damaged DDS"; }
        const auto recovered=TextureManager::PrepareFile(texture);
        check(recovered.image && first.image->GetPixelsSize()==recovered.image->GetPixelsSize() &&
            std::memcmp(first.image->GetPixels(),recovered.image->GetPixels(),first.image->GetPixelsSize())==0,"Damaged DDS cache did not recover unchanged mip pixels");
        const auto key=FileStamp(texture);
        std::filesystem::last_write_time(Path(texture),std::filesystem::last_write_time(Path(texture))+std::chrono::seconds(2));
        check(FileStamp(texture)!=key,"Changed source texture reused a stale DDS cache key");
    }
    // Worker imports an embedded-texture GLB; joining/finalizing on the main thread preserves all textures.
    auto* manager=ModelManager::GetInstance();
    check(manager->PreloadModel("gltf/plane.glb"),"CPU preload request failed");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    while(!manager->IsModelPrepared("gltf/plane.glb") && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(manager->IsModelPrepared("gltf/plane.glb"),"CPU model/texture preload timed out");
    size_t beforeFinalize;
    { std::lock_guard lock(profileMutex); beforeFinalize=events.size(); }
    manager->LoadModel("gltf/plane.glb");
    { std::lock_guard lock(profileMutex);
      for(size_t i=beforeFinalize;i<events.size();++i) check(events[i].stage!="texture.decode" && events[i].stage!="texture.decode-embedded" && events[i].stage!="texture.mip-generation","Texture decoding/mip generation leaked into preload GPU finalization"); }

    auto* preloaded=manager->FindModel("gltf/plane.glb");
    check(preloaded && preloaded->GetVertexCount()>0,"CPU preload/GPU finalize failed");
    check(!preloaded->GetModelData().embeddedTextures.empty(),"Embedded texture preload fixture is empty");
    for(const auto& t:preloaded->GetModelData().embeddedTextures)
        check(TextureManager::GetInstance()->HasTexture(t.key),"Preload omitted an embedded texture");
    SaveProfile("generated/loading-tests/stages.csv");
    std::ofstream("generated/loading-tests/result.txt")<<"PASS: PMX/GLB/GLTF/OBJ, lazy bones, path aliases, binary model/DDS caches, invalidation/corruption, CPU preload and main-thread GPU finalize\n";
}
