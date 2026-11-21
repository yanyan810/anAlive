#include "EnemyModelSource.h"
#include "ModelManager.h"
#include <bit>
#include <cwctype>
#include <format>

bool IsEnemyModelFile(const std::filesystem::path& path) {
    auto ext=path.extension().wstring();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return ext==L".pmx" || ext==L".gltf" || ext==L".glb" || ext==L".obj" || ext==L".fbx" || ext==L".pmd";
}

static std::shared_ptr<const EnemyModelSurface> ReadEnemyModelImpl(const std::string& reference,EnemyModelEditingData* editing) {
    const auto input=EnemyUtf8Path(reference);
    const auto path=std::filesystem::weakly_canonical(input);
    const auto root=std::filesystem::weakly_canonical("resources");
    const auto relative=path.lexically_relative(root);
    if (input.is_absolute() || relative.empty() || *relative.begin()==L".." ||
        !IsEnemyModelFile(path) || !std::filesystem::is_regular_file(path))
        throw std::runtime_error("Choose a supported model file under resources: "+reference);
    Model::SourceMetadata metadata;
    auto data=Model::ReadSourceData(EnemyPathText(path.parent_path()),EnemyPathText(path.filename()),false,editing ? &metadata : nullptr);
    if (editing) {
        editing->materialNames=std::move(metadata.materialNames);
        editing->topologyVertices.reserve(data.indices.size()/3);
        editing->skinWeights=std::move(data.skinClusterData);
        std::erase_if(editing->skinWeights,[](const auto& bone) { return bone.second.vertexWeights.empty(); });
        if (!editing->skinWeights.empty()) editing->skinVertices.reserve(data.indices.size()/3);
    }
    auto source=std::make_shared<EnemyModelSurface>();
    for (const auto& material:data.materials) {
        const auto c=material.baseColor;
        source->materials.push_back({material.textureFilePath,{c.x,c.y,c.z,c.w}});
    }
    if (source->materials.empty()) source->materials.push_back({{}, {1,1,1,1}});
    for (auto& texture:data.embeddedTextures)
        source->embeddedTextures.emplace(std::move(texture.key),std::move(texture.bytes));

    // Hash the imported index/vertex topology as well as the evaluated rest geometry.
    uint64_t signature=14695981039346656037ull;
    const auto hash=[&](uint32_t value) {
        for (int i=0;i<4;++i) { signature^=value&255u; signature*=1099511628211ull; value>>=8; }
    };
    const auto hashFloat=[&](float value) { hash(std::bit_cast<uint32_t>(value==0 ? 0.f : value)); };
    hash(static_cast<uint32_t>(data.meshes.size()));
    for (const auto& mesh:data.meshes) {
        hash(static_cast<uint32_t>(mesh.vertices.size())); hash(mesh.indexCount); hash(mesh.materialIndex);
        for (const auto& v:mesh.vertices) { hashFloat(v.position.x); hashFloat(v.position.y); hashFloat(v.position.z); }
    }
    for (uint32_t index:data.indices) hash(index);
    uint32_t instance=0,topologyBase=0;
    const auto append=[&](uint32_t meshIndex,const Matrix4x4& world,const std::string& nodeName) {
        const auto instanceIndex=instance++;
        const auto& mesh=data.meshes.at(meshIndex);
        if (!mesh.indexCount) return;
        if (mesh.indexCount%3 || static_cast<size_t>(mesh.startIndex)+mesh.indexCount>data.indices.size() ||
            mesh.materialIndex>=source->materials.size())
            throw std::runtime_error("Invalid triangle or material ranges in "+reference);
        if (editing) {
            editing->meshNames.resize(instance);
            editing->meshNames.back()=mesh.name.empty() ? nodeName : mesh.name;
            if (metadata.pmxVertexIndices.empty() && mesh.vertices.size()>UINT32_MAX-topologyBase)
                throw std::runtime_error("Too many vertices for editing: "+reference);
            editing->skinVertexCount=std::max(editing->skinVertexCount,mesh.startVertex+mesh.vertices.size());
        }
        const Vector3 x{world.m[0][0],world.m[0][1],world.m[0][2]};
        const Vector3 y{world.m[1][0],world.m[1][1],world.m[1][2]};
        const Vector3 z{world.m[2][0],world.m[2][1],world.m[2][2]};
        const auto nx=Matrix4x4::Cross(y,z), ny=Matrix4x4::Cross(z,x), nz=Matrix4x4::Cross(x,y);
        const float determinant=x.x*nx.x+x.y*nx.y+x.z*nx.z;
        if (!std::isfinite(determinant) || determinant==0)
            throw std::runtime_error("Model has a singular node transform: "+reference);
        hash(instanceIndex); hash(meshIndex);
        for (const auto& row:world.m) for (float component:row) hashFloat(component);
        const bool mirrored=determinant<0;
        for (uint32_t i=0;i<mesh.indexCount;i+=3) {
            if (source->faces.size()>=1000000) throw std::runtime_error("Enemy Editor supports up to 1,000,000 triangles.");
            EnemyModelSurface::Face face{{instanceIndex,meshIndex,mesh.materialIndex,i/3},{}};
            std::array<uint32_t,3> topology{},skin{};
            for (uint32_t j=0;j<3;++j) {
                const auto index=mesh.startIndex+i+(mirrored ? 2-j : j);
                const auto local=data.indices[index];
                const auto& v=mesh.vertices.at(local);
                if (editing) {
                    topology[j]=metadata.pmxVertexIndices.empty() ? topologyBase+local : metadata.pmxVertexIndices[index];
                    skin[j]=mesh.startVertex+local;
                }
                auto& vertex=face.vertices[j];
                vertex.position=EnemyPartTransformPoint({v.position.x,v.position.y,v.position.z},world);
                const auto p=vertex.position;
                for (float value:{p.x,p.y,p.z,v.texcoord.x,v.texcoord.y})
                    if (!std::isfinite(value)) throw std::runtime_error("Non-finite model coordinates: "+reference);
                auto normal=nx*v.normal.x+ny*v.normal.y+nz*v.normal.z;
                const float length=std::hypot(normal.x,normal.y,normal.z);
                vertex.normal=std::isfinite(length) && length>0 ? normal*((mirrored ? -1.f : 1.f)/length) : Vector3{0,1,0};
                vertex.uv=v.texcoord;
                auto& bounds=source->bounds;
                if (source->faces.empty() && j==0) bounds={p,p};
                bounds.min={std::min(bounds.min.x,p.x),std::min(bounds.min.y,p.y),std::min(bounds.min.z,p.z)};
                bounds.max={std::max(bounds.max.x,p.x),std::max(bounds.max.y,p.y),std::max(bounds.max.z,p.z)};
            }
            source->faces.push_back(face);
            if (editing) {
                editing->topologyVertices.push_back(topology);
                if (!editing->skinWeights.empty()) editing->skinVertices.push_back(skin);
            }
        }
        if (editing && metadata.pmxVertexIndices.empty()) topologyBase+=static_cast<uint32_t>(mesh.vertices.size());
    };
    const auto visit=[&](auto&& self,const Model::Node& node,const Matrix4x4& parent) -> void {
        const auto world=node.localMatrix*parent;
        for (auto mesh:node.meshIndices) append(mesh,world,node.name);
        for (const auto& child:node.children) self(self,child,world);
    };
    visit(visit,data.rootNode,Matrix4x4::MakeIdentity4x4());
    if (!instance) for (uint32_t i=0;i<data.meshes.size();++i) append(i,Matrix4x4::MakeIdentity4x4(),{});
    const auto span=source->bounds.max-source->bounds.min;
    const float extent=std::max({span.x,span.y,span.z});
    if (source->faces.empty() || !std::isfinite(extent) || extent<1e-12f)
        throw std::runtime_error("The importer returned no usable rest-pose triangles: "+reference);
    source->signature=std::format("yanengine-rest-v1-{:016x}",signature);
    return source;
}

std::shared_ptr<const EnemyModelSurface> ReadEnemyModel(const std::string& reference) {
    return ReadEnemyModelImpl(reference,nullptr);
}

std::shared_ptr<const EnemyModelSurface> ReadEnemyModelForEditing(const std::string& reference,EnemyModelEditingData& editing) {
    editing={};
    return ReadEnemyModelImpl(reference,&editing);
}

EnemyModelTextures BuildEnemyModelTextures(const EnemyModelSurface& source) {
    auto* manager=TextureManager::GetInstance();
    std::map<std::string,std::shared_ptr<const TextureManager::OwnedTexture>> loaded;
    EnemyModelTextures textures(source.materials.size());
    std::vector<bool> used(source.materials.size());
    for (const auto& face:source.faces) used[face.id.material]=true;
    for (size_t index=0;index<used.size();++index) {
        if (!used[index]) continue;
        const auto& key=source.materials[index].texture;
        if (key.empty()) continue;
        const auto [entry,inserted]=loaded.try_emplace(key);
        if (inserted) {
            const auto embedded=source.embeddedTextures.find(key);
            const auto prepared=embedded==source.embeddedTextures.end() ? TextureManager::PrepareFile(key) :
                TextureManager::PrepareMemory(key,embedded->second.data(),embedded->second.size());
            entry->second=manager->CreateOwnedTexture(prepared);
        }
        textures[index]=entry->second;
    }
    return textures;
}

std::unique_ptr<Model> BuildEnemyPartModel(const EnemyPartGeometry& geometry,const EnemyModelSurface& source,
    const EnemyModelTextures& textures,std::span<const uint32_t> instances) {
    if (geometry.triangles.empty()) return {};
    std::map<uint32_t,std::vector<size_t>> groups;
    for (size_t i=0;i<geometry.triangles.size();++i)
        groups[instances.empty() ? geometry.materials[i] : instances[i]].push_back(i);
    Model::ModelData data;
    EnemyModelTextures bindings;
    for (const auto& [key,indices]:groups) {
        const auto material=geometry.materials[indices.front()];
        Model::MeshData mesh;
        mesh.materialIndex=static_cast<uint32_t>(data.materials.size());
        mesh.startIndex=static_cast<uint32_t>(data.indices.size());
        const auto& c=source.materials[material].color;
        data.materials.push_back({{}, {c[0],c[1],c[2],c[3]}});
        bindings.push_back(textures[material]);
        mesh.vertices.reserve(indices.size()*3);
        for (const auto index:indices) for (const auto& v:geometry.triangles[index]) {
            data.indices.push_back(static_cast<uint32_t>(mesh.vertices.size()));
            mesh.vertices.push_back({{v.position.x,v.position.y,v.position.z,1},v.uv,v.normal});
        }
        mesh.indexCount=static_cast<uint32_t>(mesh.vertices.size());
        data.rootNode.meshIndices.push_back(static_cast<uint32_t>(data.meshes.size()));
        data.meshes.push_back(std::move(mesh));
    }
    auto model=ModelManager::GetInstance()->CreateOwnedPrimitiveModel(data);
    model->SetOwnedTextures(std::move(bindings));
    return model;
}
