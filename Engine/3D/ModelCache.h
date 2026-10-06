#pragma once
#include "Model.h"
#include "../Utility/AssetCache.h"
#include <cstring>
#include <set>
#include <type_traits>
#include <nlohmann/json.hpp>

namespace ModelCache {
constexpr uint64_t version=5; // Bump when import rules or cached structures change.
constexpr size_t maxBytes=512*1024*1024;
struct Writer {
    std::vector<uint8_t> bytes;
    template<class T> void Pod(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* p=reinterpret_cast<const uint8_t*>(&value); bytes.insert(bytes.end(),p,p+sizeof(T));
    }
    void String(const std::string& value) { Pod(static_cast<uint64_t>(value.size())); bytes.insert(bytes.end(),value.begin(),value.end()); }
    template<class T> void Vector(const std::vector<T>& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        Pod(static_cast<uint64_t>(value.size()));
        if(!value.empty()) { const auto* p=reinterpret_cast<const uint8_t*>(value.data()); bytes.insert(bytes.end(),p,p+value.size()*sizeof(T)); }
    }
    void Node(const Model::Node& n) {
        Pod(n.transform); Pod(n.localMatrix); String(n.name); Vector(n.meshIndices);
        Pod(static_cast<uint64_t>(n.children.size())); for(const auto& c:n.children) Node(c);
    }
    void Data(const Model::ModelData& d) {
        Pod(static_cast<uint8_t>(d.hasSkinning)); String(d.defaultAnimationName); Vector(d.indices); Vector(d.instances); Node(d.rootNode);
        Pod(static_cast<uint64_t>(d.materials.size()));
        for(const auto& m:d.materials) { String(m.textureFilePath); Pod(m.baseColor); }
        Pod(static_cast<uint64_t>(d.meshes.size()));
        for(const auto& m:d.meshes) {
            Vector(m.vertices); Pod(m.materialIndex); Pod(m.startVertex); Pod(m.vertexCount); Pod(m.startIndex); Pod(m.indexCount);
            Pod(static_cast<uint8_t>(m.skinned)); String(m.name);
        }
        Pod(static_cast<uint64_t>(d.skinClusterData.size()));
        for(const auto& [name,j]:d.skinClusterData) { String(name); Pod(j.inverseBindPoseMatrix); Vector(j.vertexWeights); }
        Pod(static_cast<uint64_t>(d.animations.size()));
        std::vector<std::string> animations; for(const auto& pair:d.animations) animations.push_back(pair.first);
        std::sort(animations.begin(),animations.end());
        for(const auto& name:animations) {
            const auto& a=d.animations.at(name);
            String(name); Pod(a.duration); Pod(static_cast<uint64_t>(a.nodeAnimations.size()));
            std::vector<std::string> nodes; for(const auto& pair:a.nodeAnimations) nodes.push_back(pair.first);
            std::sort(nodes.begin(),nodes.end());
            for(const auto& node:nodes) { const auto& n=a.nodeAnimations.at(node); String(node); Vector(n.translate.keyframes); Vector(n.rotate.keyframes); Vector(n.scale.keyframes); }
        }
        Pod(static_cast<uint64_t>(d.embeddedTextures.size()));
        for(const auto& t:d.embeddedTextures) { String(t.key); Vector(t.bytes); }
    }
};
struct Reader {
    std::span<const uint8_t> bytes; size_t cursor=0;
    void Need(size_t size) const { if(size>bytes.size()-cursor) throw std::runtime_error("Truncated model cache"); }
    template<class T> T Pod() {
        static_assert(std::is_trivially_copyable_v<T>); Need(sizeof(T)); T value;
        std::memcpy(&value,bytes.data()+cursor,sizeof(T)); cursor+=sizeof(T); return value;
    }
    uint64_t Count() { const auto n=Pod<uint64_t>(); if(n>100000 || n>bytes.size()-cursor) throw std::runtime_error("Invalid cache count"); return n; }
    std::string String() {
        const auto n=Pod<uint64_t>(); Need(static_cast<size_t>(n));
        std::string value(reinterpret_cast<const char*>(bytes.data()+cursor),static_cast<size_t>(n)); cursor+=static_cast<size_t>(n); return value;
    }
    template<class T> std::vector<T> Vector() {
        static_assert(std::is_trivially_copyable_v<T>); const auto n=Pod<uint64_t>();
        if(n>(bytes.size()-cursor)/sizeof(T)) throw std::runtime_error("Invalid cache array");
        std::vector<T> value(static_cast<size_t>(n)); const auto size=value.size()*sizeof(T);
        if(size) std::memcpy(value.data(),bytes.data()+cursor,size); cursor+=size; return value;
    }
    Model::Node Node(int depth=0) {
        if(depth>256) throw std::runtime_error("Invalid cache node depth");
        Model::Node n; n.transform=Pod<QuaternionTransform>(); n.localMatrix=Pod<Matrix4x4>(); n.name=String(); n.meshIndices=Vector<uint32_t>();
        const auto count=Count(); for(uint64_t i=0;i<count;++i) n.children.push_back(Node(depth+1)); return n;
    }
    Model::ModelData Data() {
        Model::ModelData d; d.hasSkinning=Pod<uint8_t>()!=0; d.defaultAnimationName=String(); d.indices=Vector<uint32_t>(); d.instances=Vector<Model::MeshInstance>(); d.rootNode=Node();
        auto count=Count(); for(uint64_t i=0;i<count;++i) { Model::MaterialData m; m.textureFilePath=String(); m.baseColor=Pod<Vector4>(); d.materials.push_back(std::move(m)); }
        count=Count(); uint64_t vertices=0;
        for(uint64_t i=0;i<count;++i) {
            Model::MeshData m; m.vertices=Vector<Model::VertexData>(); m.materialIndex=Pod<uint32_t>(); m.startVertex=Pod<uint32_t>(); m.vertexCount=Pod<uint32_t>();
            m.startIndex=Pod<uint32_t>(); m.indexCount=Pod<uint32_t>(); m.skinned=Pod<uint8_t>()!=0; m.name=String();
            if(m.startVertex!=vertices || m.vertexCount!=m.vertices.size() || m.materialIndex>=d.materials.size() ||
                static_cast<uint64_t>(m.startIndex)+m.indexCount>d.indices.size()) throw std::runtime_error("Invalid cache mesh");
            for(size_t j=m.startIndex;j<static_cast<size_t>(m.startIndex)+m.indexCount;++j)
                if(d.indices[j]>=m.vertices.size()) throw std::runtime_error("Invalid cache index");
            vertices+=m.vertices.size(); d.meshes.push_back(std::move(m));
        }
        count=Count(); for(uint64_t i=0;i<count;++i) {
            const auto name=String(); Model::JointWeightData j; j.inverseBindPoseMatrix=Pod<Matrix4x4>(); j.vertexWeights=Vector<Model::VertexWeightData>();
            for(const auto& w:j.vertexWeights) if(w.vertexIndex>=vertices) throw std::runtime_error("Invalid cache weight");
            d.skinClusterData.emplace(name,std::move(j));
        }
        count=Count(); for(uint64_t i=0;i<count;++i) {
            const auto name=String(); Animation a; a.duration=Pod<float>(); const auto channels=Count();
            for(uint64_t j=0;j<channels;++j) {
                const auto node=String(); NodeAnimation n;
                n.translate.keyframes=Vector<KeyframeVector3>(); n.rotate.keyframes=Vector<KeyframeQuaternion>(); n.scale.keyframes=Vector<KeyframeVector3>();
                a.nodeAnimations.emplace(node,std::move(n));
            }
            d.animations.emplace(name,std::move(a));
        }
        count=Count(); for(uint64_t i=0;i<count;++i) { Model::ModelData::EmbeddedTexture t; t.key=String(); t.bytes=Vector<uint8_t>(); d.embeddedTextures.push_back(std::move(t)); }
        if(cursor!=bytes.size() || d.meshes.empty() || (!d.defaultAnimationName.empty() && !d.animations.contains(d.defaultAnimationName))) throw std::runtime_error("Invalid cache payload");
        return d;
    }
};
inline std::string Signature(const std::string& path) {
    using namespace AssetLoading;
    const auto p=Path(Canonical(path)); std::set<std::string> dependencies{Canonical(path)};
    auto ext=p.extension().wstring(); std::transform(ext.begin(),ext.end(),ext.begin(),::towlower);
    if(ext==L".gltf") {
        // JSON buffers may be located outside the model directory.
        std::ifstream file(p); nlohmann::json gltf; file>>gltf;
        for(const auto& b:gltf.value("buffers",nlohmann::json::array())) {
            const auto uri=b.value("uri",std::string{});
            if(!uri.empty() && !uri.starts_with("data:")) dependencies.insert(StringUtility::ConvertString((p.parent_path()/Path(uri)).wstring()));
        }
    }
    if(ext==L".obj") {
        std::ifstream file(p); std::string line;
        while(std::getline(file,line)) {
            std::istringstream stream(line); std::string command; stream>>command;
            if(command=="mtllib") {
                std::string name; std::getline(stream>>std::ws,name);
                const auto entire=p.parent_path()/Path(name);
                if(std::filesystem::is_regular_file(entire)) dependencies.insert(StringUtility::ConvertString(entire.wstring()));
                else { std::istringstream names(name); while(names>>name) dependencies.insert(StringUtility::ConvertString((p.parent_path()/Path(name)).wstring())); }
            }
        }
    }
    if(p.filename()==L"anbi.glb") dependencies.insert(StringUtility::ConvertString((p.parent_path()/L"安比.pmx").wstring()));
    std::string signature="model-v"+std::to_string(version)+":vertex="+std::to_string(sizeof(Model::VertexData));
    for(const auto& dependency:dependencies) signature+='\n'+FileStamp(dependency);
    return signature;
}
inline bool Load(const std::string& path,Model::ModelData& out) {
    if(!AssetLoading::CacheEnabled()) return false;
    AssetLoading::Timer timer("model.cache-read",path);
    try {
        const auto target=AssetLoading::CachePath("models",AssetLoading::Canonical(path),".ymodel");
        std::ifstream file(target,std::ios::binary|std::ios::ate); if(!file) return false;
        const auto size=file.tellg(); if(size<32 || size>static_cast<std::streamoff>(maxBytes)) return false;
        file.seekg(0); uint64_t header[4]{}; file.read(reinterpret_cast<char*>(header),sizeof(header));
        const auto signature=Signature(path);
        if(header[0]!=0x59414e4d4f44454cull || header[1]!=version || header[2]!=AssetLoading::Hash({reinterpret_cast<const uint8_t*>(signature.data()),signature.size()})) return false;
        std::vector<uint8_t> bytes(static_cast<size_t>(size)-sizeof(header)); file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        if(!file || AssetLoading::Hash(bytes)!=header[3]) return false;
        Reader reader{bytes}; out=reader.Data(); return true;
    } catch(const std::exception&) { return false; }
}
inline void Save(const std::string& path,const Model::ModelData& data) {
    if(!AssetLoading::CacheEnabled() || data.meshes.empty()) return;
    AssetLoading::Timer timer("model.cache-write",path);
    try {
        Writer writer; writer.Data(data); if(writer.bytes.size()>maxBytes-sizeof(uint64_t)*4) return;
        const auto signature=Signature(path);
        const uint64_t header[]{0x59414e4d4f44454cull,version,AssetLoading::Hash({reinterpret_cast<const uint8_t*>(signature.data()),signature.size()}),AssetLoading::Hash(writer.bytes)};
        const auto target=AssetLoading::CachePath("models",AssetLoading::Canonical(path),".ymodel");
        std::filesystem::create_directories(target.parent_path()); const auto temp=AssetLoading::Temporary(target);
        { std::ofstream file(temp,std::ios::binary); file.write(reinterpret_cast<const char*>(header),sizeof(header));
          file.write(reinterpret_cast<const char*>(writer.bytes.data()),static_cast<std::streamsize>(writer.bytes.size())); if(!file) return; }
        AssetLoading::Publish(temp,target);
    } catch(const std::exception&) { /* A read-only cache directory must still allow normal loading. */ }
}
}
