#include "ModelCache.h"
#include "Model.h"
#include <sstream>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <cassert>

// ================================
// Assimp helpers (materials/meshes)
// ================================
static void BuildMaterials(Model::ModelData& out,
	const aiScene* scene,
	const std::string& directoryPath)
{
	out.materials.clear();
	out.materials.resize(scene->mNumMaterials);

	for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
		const aiMaterial* mtl = scene->mMaterials[i];
		aiString texPath;
		aiColor4D baseColor(1.0f, 1.0f, 1.0f, 1.0f);
		if (aiGetMaterialColor(mtl, AI_MATKEY_BASE_COLOR, &baseColor) != AI_SUCCESS) {
			aiGetMaterialColor(mtl, AI_MATKEY_COLOR_DIFFUSE, &baseColor);
		}
		out.materials[i].baseColor = {
			baseColor.r, baseColor.g, baseColor.b, baseColor.a
		};
		if (mtl->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
			out.materials[i].textureFilePath = directoryPath + "/" + texPath.C_Str();
		}
		else {
			out.materials[i].textureFilePath.clear();
		}
	}
}

#include <windows.h>
#include <string>

static void PrintLoadedAssimpPath_()
{
	const char* names[] = {
		"assimp-vc143-mtd.dll",
		"assimp-vc142-mtd.dll",
		"assimp-vc143-mt.dll",
		"assimp-vc142-mt.dll",
		"assimp.dll"
	};

	for (auto* n : names) {
		HMODULE h = GetModuleHandleA(n);
		if (h) {
			char path[MAX_PATH]{};
			GetModuleFileNameA(h, path, MAX_PATH);
			OutputDebugStringA(("[AssimpDLL] loaded: " + std::string(n) + "\n").c_str());
			OutputDebugStringA(("[AssimpDLL] path  : " + std::string(path) + "\n").c_str());
			return;
		}
	}

	OutputDebugStringA("[AssimpDLL] not loaded (any known name)\n");
}



static Model::MeshData BuildMeshTriList(const aiMesh* mesh)
{
	Model::MeshData md{};
	md.materialIndex = mesh->mMaterialIndex;
	md.name = mesh->mName.C_Str();

	std::vector<Model::VertexData> base(mesh->mNumVertices);
	aiVector3D zero3(0, 0, 0);

	for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
		const aiVector3D* pos = &mesh->mVertices[i];
		const aiVector3D* nrm = mesh->HasNormals() ? &mesh->mNormals[i] : &zero3;
		const aiVector3D* uv = mesh->HasTextureCoords(0) ? &mesh->mTextureCoords[0][i] : &zero3;

		Model::VertexData v{};
		v.position = { pos->x, pos->y, pos->z, 1.0f };

		// ★統一方針：自前で 1 - v 反転（aiProcess_FlipUVs は使わない）
		v.texcoord = { uv->x, 1.0f - uv->y };

		v.normal = { nrm->x, nrm->y, nrm->z };
		base[i] = v;
	}

	md.vertices.reserve(mesh->mNumFaces * 3);

	for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
		const aiFace& face = mesh->mFaces[f];
		if (face.mNumIndices != 3) continue;

		auto v0 = base[face.mIndices[0]];
		auto v1 = base[face.mIndices[1]];
		auto v2 = base[face.mIndices[2]];

		// あなたの現行と同じ並び（左手系っぽくしたい意図）
		md.vertices.push_back(v2);
		md.vertices.push_back(v1);
		md.vertices.push_back(v0);
	}

	return md;
}

// Assimp matrix -> your Matrix4x4
static Matrix4x4 ConvertAssimpMatrix(const aiMatrix4x4& mIn)
{
	// スライドの通り transpose を挟む（あなたの行列配置に合わせやすい）
	aiMatrix4x4 m = mIn;
	m.Transpose();

	Matrix4x4 out = Matrix4x4::MakeIdentity4x4();
	out.m[0][0] = m.a1; out.m[0][1] = m.a2; out.m[0][2] = m.a3; out.m[0][3] = m.a4;
	out.m[1][0] = m.b1; out.m[1][1] = m.b2; out.m[1][2] = m.b3; out.m[1][3] = m.b4;
	out.m[2][0] = m.c1; out.m[2][1] = m.c2; out.m[2][2] = m.c3; out.m[2][3] = m.c4;
	out.m[3][0] = m.d1; out.m[3][1] = m.d2; out.m[3][2] = m.d3; out.m[3][3] = m.d4;
	return out;
}

static Matrix4x4 ConvertAssimpMatrix_LH(const aiMatrix4x4& mIn)
{
	// まず transpose（あなたの流儀）
	aiMatrix4x4 m = mIn;
	m.Transpose();

	Matrix4x4 out = Matrix4x4::MakeIdentity4x4();
	out.m[0][0] = m.a1; out.m[0][1] = m.a2; out.m[0][2] = m.a3; out.m[0][3] = m.a4;
	out.m[1][0] = m.b1; out.m[1][1] = m.b2; out.m[1][2] = m.b3; out.m[1][3] = m.b4;
	out.m[2][0] = m.c1; out.m[2][1] = m.c2; out.m[2][2] = m.c3; out.m[2][3] = m.c4;
	out.m[3][0] = m.d1; out.m[3][1] = m.d2; out.m[3][2] = m.d3; out.m[3][3] = m.d4;

	// ===== RH -> LH（X反転）=====
	// 行列で座標系反転： S = diag(-1,1,1,1)
	// 変換は out = S * out * S （row-vector運用でもこれが一番事故りにくい）
	Matrix4x4 S = Matrix4x4::MakeIdentity4x4();
	S.m[0][0] = -1.0f;

	out = Matrix4x4::Multiply(Matrix4x4::Multiply(S, out), S);
	return out;
}

static std::string AiStringToStdStringRawDump(const aiString& s)
{
	// 1) length を信じてコピー
	std::string out;
	out.assign(s.data, s.data + s.length);

	// 2) dump（先頭32byteまで）
	char buf[1024]{};
	int n = 0;
	n += sprintf_s(buf + n, sizeof(buf) - n, "[AiStr] len=%u firstBytes=", (unsigned)s.length);
	for (unsigned i = 0; i < s.length && i < 32; ++i) {
		n += sprintf_s(buf + n, sizeof(buf) - n, "%02X ", (unsigned char)s.data[i]);
	}
	n += sprintf_s(buf + n, sizeof(buf) - n, "\n");
	OutputDebugStringA(buf);

	// 3) out 側の長さもログ
	OutputDebugStringA(("[AiStr] out.size=" + std::to_string(out.size()) + "\n").c_str());
	return out;
}

static Model::Node ReadNodeRecursiveImpl(const aiNode* node, int depth) {
	Model::Node out{};
	out.name = node->mName.C_Str();

	// (1) Assimp行列 -> SRT に分解
	aiVector3D s, t;
	aiQuaternion r;
	node->mTransformation.Decompose(s, r, t);

	// (2) RH->LH 変換（あなたのエンジンが LH 前提なら）
	out.transform.scale = Vector3{ s.x, s.y, s.z };      // 多くはそのまま
	out.transform.rotate = { r.x,-r.y,-r.z,r.w };
	out.transform.translate = { -t.x,t.y,t.z };

	// (3) transform から localMatrix を再構築
	out.localMatrix = MakeAffineMatrix(
		out.transform.scale,
		out.transform.rotate,
		out.transform.translate
	);


	// mesh indices
	out.meshIndices.reserve(node->mNumMeshes);
	for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
		out.meshIndices.push_back((uint32_t)node->mMeshes[i]);
	}

	// ★階層ログ
	{
		std::string indent(depth * 2, ' ');
		std::string s = indent + "[Node] '" + out.name + "' meshes=" + std::to_string(out.meshIndices.size()) + " [";
		for (size_t k = 0; k < out.meshIndices.size(); ++k) {
			s += std::to_string(out.meshIndices[k]);
			if (k + 1 < out.meshIndices.size()) s += ",";
		}
		s += "]\n";
		OutputDebugStringA(s.c_str());
	}

	// children
	out.children.resize(node->mNumChildren);
	for (unsigned int i = 0; i < node->mNumChildren; ++i) {
		out.children[i] = ReadNodeRecursiveImpl(node->mChildren[i], depth + 1);
	}
	return out;
}

static Model::Node ReadNodeRecursive(const aiNode* node) {
	return ReadNodeRecursiveImpl(node, 0);
}


static void DBGAnimCh(const char* animName, const std::string& nodeName, unsigned rawLen, unsigned rawFirst)
{
	std::string msg =
		std::string("[AnimCh] anim='") + animName +
		"' node='" + nodeName + "' nodeName.size=" + std::to_string(nodeName.size()) +
		" rawLen=" + std::to_string(rawLen) +
		" rawFirst=" + std::to_string(rawFirst) +
		"\n";
	OutputDebugStringA(msg.c_str());
}


//AnumHelper
static Quaternion ConvertAssimpQuat(const aiQuaternion& q) {
	Quaternion out{};
	out.x = q.x;
	out.y = q.y;
	out.z = q.z;
	out.w = q.w;
	return out;
}

static Quaternion ConvertAssimpQuat_LH(const aiQuaternion& q) {
	// ノード側で { x,-y,-z,w } にしてるのと同じ
	Quaternion out{};
	out.x = q.x;
	out.y = -q.y;
	out.z = -q.z;
	out.w = q.w;
	return out;
}


//========================
//キーフレーム読み取り
//========================

static void ReadVectorKeys(AnimationCurveVector3& outCurve, const aiVectorKey* keys, unsigned int count, double tps) {
	outCurve.keyframes.clear();
	outCurve.keyframes.reserve(count);

	const double inv = (tps > 0.0) ? (1.0 / tps) : (1.0 / 25.0); // tps==0対策

	for (unsigned int i = 0; i < count; ++i) {
		KeyframeVector3 k{};
		k.time = static_cast<float>(keys[i].mTime * inv);
		k.value = Vector3{ keys[i].mValue.x, keys[i].mValue.y, keys[i].mValue.z };
		outCurve.keyframes.push_back(k);
	}

	std::sort(outCurve.keyframes.begin(), outCurve.keyframes.end(),
		[](auto& a, auto& b) { return a.time < b.time; });
}

static void ReadTranslateKeys_LH(AnimationCurveVector3& outCurve,
	const aiVectorKey* keys, unsigned int count, double tps)
{
	outCurve.keyframes.clear();
	outCurve.keyframes.reserve(count);

	const double inv = (tps > 0.0) ? (1.0 / tps) : (1.0 / 25.0);

	for (unsigned int i = 0; i < count; ++i) {
		KeyframeVector3 k{};
		k.time = float(keys[i].mTime * inv);

		// ノード側で translate を {-x, y, z} にしてるのと同じ
		k.value = Vector3{ -keys[i].mValue.x, keys[i].mValue.y, keys[i].mValue.z };

		outCurve.keyframes.push_back(k);
	}

	std::sort(outCurve.keyframes.begin(), outCurve.keyframes.end(),
		[](auto& a, auto& b) { return a.time < b.time; });
}


static void ReadQuatKeys(AnimationCurveQuaternion& outCurve, const aiQuatKey* keys, unsigned int count, double tps) {
	outCurve.keyframes.clear();
	outCurve.keyframes.reserve(count);

	const double inv = (tps > 0.0) ? (1.0 / tps) : (1.0 / 25.0);

	for (unsigned int i = 0; i < count; ++i) {
		KeyframeQuaternion k{};
		k.time = static_cast<float>(keys[i].mTime * inv);
		k.value = ConvertAssimpQuat_LH(keys[i].mValue);
		outCurve.keyframes.push_back(k);
	}

	std::sort(outCurve.keyframes.begin(), outCurve.keyframes.end(),
		[](auto& a, auto& b) { return a.time < b.time; });
}

//アニメーション全体を読む
static void ReadAnimationsFromAssimp(Model::ModelData& out, const aiScene* scene) {
	out.animations.clear();

	if (!scene || scene->mNumAnimations == 0) { return; }

	for (unsigned int ai = 0; ai < scene->mNumAnimations; ++ai) {
		const aiAnimation* a = scene->mAnimations[ai];
		if (!a) continue;

		Animation clip{};
		const double tps = (a->mTicksPerSecond != 0.0) ? a->mTicksPerSecond : 25.0;
		clip.duration = static_cast<float>(a->mDuration / tps);

		// アニメ名（空のことがあるのでフォールバック）
		std::string animName = a->mName.C_Str();
		if (animName.empty()) {
			animName = "anim_" + std::to_string(ai);
		}

		// channels = ノードごとのTRSカーブ
		for (unsigned int ci = 0; ci < a->mNumChannels; ++ci) {
			const aiNodeAnim* ch = a->mChannels[ci];
			if (!ch) continue;

			std::string nodeName = AiStringToStdStringRawDump(ch->mNodeName);

			OutputDebugStringA(("[AnimChFixed] nodeName.size=" + std::to_string(nodeName.size()) + "\n").c_str());

			if (nodeName.empty()) continue;

			NodeAnimation na{};
			if (ch->mNumPositionKeys > 0) {
				ReadTranslateKeys_LH(na.translate, ch->mPositionKeys, ch->mNumPositionKeys, tps);
			}
			if (ch->mNumRotationKeys > 0) {
				ReadQuatKeys(na.rotate, ch->mRotationKeys, ch->mNumRotationKeys, tps); // 中でLH化済み
			}
			if (ch->mNumScalingKeys > 0) {
				ReadVectorKeys(na.scale, ch->mScalingKeys, ch->mNumScalingKeys, tps); // scaleはそのまま
			}


			clip.nodeAnimations.emplace(std::move(nodeName), std::move(na));

			OutputDebugStringA(("[AnimCh] anim='" + animName + "' node='" + nodeName + "'\n").c_str());

			char bb[256];
			std::snprintf(bb, sizeof(bb),
				"[AnimChRaw] anim='%s' nodeLen=%u nodeFirst=%d\n",
				animName.c_str(),
				ch->mNodeName.length,
				(ch->mNodeName.length > 0) ? (unsigned char)ch->mNodeName.data[0] : -1
			);
			OutputDebugStringA(bb);

		}

		out.animations.emplace(std::move(animName), std::move(clip));
	}
}

static void BuildMeshInstances(
	const aiNode* node,
	const Matrix4x4& parent,
	std::vector<Model::MeshInstance>& out)
{
	Matrix4x4 local = ConvertAssimpMatrix(node->mTransformation);

	// ★ここはあなたの掛け順規約に合わせる
	// 行ベクトル運用なら: global = parent * local
	// 列ベクトル運用なら: global = local * parent
	Matrix4x4 global = parent * local;

	for (unsigned i = 0; i < node->mNumMeshes; ++i) {
		Model::MeshInstance inst{};
		inst.meshIndex = node->mMeshes[i];
		inst.nodeGlobal = global;
		out.push_back(inst);
	}

	for (unsigned i = 0; i < node->mNumChildren; ++i) {
		BuildMeshInstances(node->mChildren[i], global, out);
	}
}

void Model::DebugValidateAnimationTracks_() const
{
	for (const auto& [animName, clip] : modelData_.animations) {

		for (const auto& [nodeName, na] : clip.nodeAnimations) {

			bool exists =
				skeleton_.jointMap.contains(nodeName) ||
				nodeNameToIndex_.contains(nodeName); // ←あなたの nodeRuntime マップ名に合わせて

			OutputDebugStringA(
				(std::string("[AnimMap] anim='") + animName +
					"' node='" + nodeName +
					"' exists=" + (exists ? "1" : "0") + "\n").c_str()
			);
		}
	}
}


Model::ModelData Model::ReadSourceData(const std::string& directoryPath,const std::string& filename) {
    const std::string path=AssetLoading::Canonical(directoryPath+"/"+filename);
    const std::string sourceDirectory=StringUtility::ConvertString(AssetLoading::Path(path).parent_path().wstring());
    ModelData data;
    if(ModelCache::Load(path,data)) return data;
    {
        AssetLoading::Timer timer("model.import",path);
        auto extension=AssetLoading::Path(filename).extension().wstring();
        std::transform(extension.begin(),extension.end(),extension.begin(),::towlower);
        if(extension==L".pmx") data=LoadPmxFile(path);
        else if(extension==L".fbx" || extension==L".gltf" || extension==L".glb" || extension==L".obj" || extension==L".pmd") {
            data=LoadAssimpFile(path);
            if(filename=="anbi.glb") {
                const auto pmx=LoadPmxFile(sourceDirectory+"/安比.pmx");
                constexpr size_t offset=5;
                for(size_t i=0;i<pmx.materials.size() && offset+i<data.materials.size();++i)
                    data.materials[offset+i]=pmx.materials[i];
                for(auto& mesh:data.meshes) if(mesh.materialIndex<offset) mesh.indexCount=0;
            }
        } else data=LoadObjFile(sourceDirectory,filename);
    }
    if(!data.animations.empty()) data.defaultAnimationName=data.animations.begin()->first;
    ModelCache::Save(path,data);
    return data;
}

void Model::Initialize(ModelCommon* modelCommon,
	const std::string& directoryPath,
	const std::string& filename, std::optional<ModelData> prepared)
{
	OutputDebugStringA("[Model] Initialize start\n");

	modelCommon_ = modelCommon;
	DirectXCommon* dx = modelCommon_->GetDxCommon();

    AssetLoading::Timer total("model.total",directoryPath+"/"+filename);
    DirectXCommon::TextureUploadBatch uploads(dx);
    modelData_=prepared ? std::move(*prepared) : ReadSourceData(directoryPath,filename);
    {
        AssetLoading::Timer timer("model.skeleton",directoryPath+"/"+filename);
        BuildNodeRuntime_();
        skeleton_=CreateSkeleton(modelData_.rootNode);
        UpdateSkeleton(skeleton_);
    }
    AssetLoading::Timer finalize("model.buffers-materials-textures",directoryPath+"/"+filename);

	// ======================
	// AABB / Normal デバッグ（全Mesh走査）
	// ======================
	bool hasAny = false;

	Vector3 minPos{ +FLT_MAX, +FLT_MAX, +FLT_MAX };
	Vector3 maxPos{ -FLT_MAX, -FLT_MAX, -FLT_MAX };

	Vector3 nMin{ +FLT_MAX, +FLT_MAX, +FLT_MAX };
	Vector3 nMax{ -FLT_MAX, -FLT_MAX, -FLT_MAX };

	for (const auto& mesh : modelData_.meshes) {
		for (const auto& v : mesh.vertices) {
			hasAny = true;

			// AABB
			const Vector4& pp = v.position;
			minPos.x = std::min(minPos.x, pp.x);
			minPos.y = std::min(minPos.y, pp.y);
			minPos.z = std::min(minPos.z, pp.z);
			maxPos.x = std::max(maxPos.x, pp.x);
			maxPos.y = std::max(maxPos.y, pp.y);
			maxPos.z = std::max(maxPos.z, pp.z);

			// Normal range
			const Vector3& nn = v.normal;
			nMin.x = std::min(nMin.x, nn.x);
			nMin.y = std::min(nMin.y, nn.y);
			nMin.z = std::min(nMin.z, nn.z);
			nMax.x = std::max(nMax.x, nn.x);
			nMax.y = std::max(nMax.y, nn.y);
			nMax.z = std::max(nMax.z, nn.z);
		}
	}

	if (hasAny) {
		Vector3 size{
			maxPos.x - minPos.x,
			maxPos.y - minPos.y,
			maxPos.z - minPos.z
		};
		Vector3 center{
			(minPos.x + maxPos.x) * 0.5f,
			(minPos.y + maxPos.y) * 0.5f,
			(minPos.z + maxPos.z) * 0.5f
		};

		char buf[256];
		std::snprintf(
			buf, sizeof(buf),
			"[Model AABB] min=(%.3f, %.3f, %.3f), max=(%.3f, %.3f, %.3f), "
			"center=(%.3f, %.3f, %.3f), size=(%.3f, %.3f, %.3f)\n",
			minPos.x, minPos.y, minPos.z,
			maxPos.x, maxPos.y, maxPos.z,
			center.x, center.y, center.z,
			size.x, size.y, size.z
		);
		OutputDebugStringA(buf);

		char bufN[256];
		std::snprintf(bufN, sizeof(bufN),
			"[Normal] min=(%.3f,%.3f,%.3f) max=(%.3f,%.3f,%.3f)\n",
			nMin.x, nMin.y, nMin.z,
			nMax.x, nMax.y, nMax.z);
		OutputDebugStringA(bufN);
	}

	// ======================
	// VB 作成（全meshの頂点を1本に連結）
	// ======================
	size_t totalVtx = 0;
	for (auto& mesh : modelData_.meshes) {
		totalVtx += mesh.vertices.size();
	}

	if (totalVtx == 0) {
		OutputDebugStringA("[Model] ERROR: totalVtx == 0 (import failed). Skip creating buffers.\n");
		// ここで “描画不可モデル” として終了
		return;
	}

	vertexResource_ = dx->CreateBufferResource(sizeof(VertexData) * totalVtx);
	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));

	uint32_t cursor = 0;
	for (auto& mesh : modelData_.meshes) {

		// ★Assimpロード時に startVertex を確定してるなら一致チェックだけにする
		assert(mesh.startVertex == cursor);

		mesh.vertexCount = static_cast<uint32_t>(mesh.vertices.size());

		std::memcpy(vertexData_ + cursor,
			mesh.vertices.data(),
			sizeof(VertexData) * mesh.vertices.size());

		cursor += mesh.vertexCount;
	}


	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * totalVtx);
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	// ======================
// IB 作成（全meshのindexを1本に連結）★追加
// ======================
	const size_t totalIdx = modelData_.indices.size();
	if (totalIdx == 0) {
		OutputDebugStringA("[Model] WARN: totalIdx == 0 (non-indexed). Skip creating IB.\n");
	}
	else {

		indexResource_ = dx->CreateBufferResource(sizeof(uint32_t) * totalIdx);
		indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData_));

		// GPU上のIBは「VB連結後の頂点番号」に直したものを詰める
		uint32_t ibCursor = 0;

		for (auto& mesh : modelData_.meshes) {

			// mesh.startVertex は VB 連結で設定済み（vertexOffset）
			const uint32_t vOffset = mesh.startVertex;

			const uint32_t srcStart = mesh.startIndex;  // LoadAssimpFileで入れた ModelData::indices の範囲
			const uint32_t count = mesh.indexCount;

			// このmeshが使う GPU IB 範囲に付け替える
			mesh.startIndex = ibCursor;

			for (uint32_t k = 0; k < count; ++k) {
				const uint32_t localIndex = modelData_.indices[srcStart + k];
				indexData_[ibCursor + k] = localIndex + vOffset; // ★ここが超重要
			}

			ibCursor += count;
		}

		indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
		indexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * totalIdx);
		indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

		OutputDebugStringA(("[Model] total index count = " + std::to_string(totalIdx) + "\n").c_str());

		for (size_t i = 0; i < modelData_.meshes.size(); ++i) {
			auto& m = modelData_.meshes[i];
			char b[256];
			std::snprintf(b, sizeof(b),
				"[Mesh%zu] vStart=%u vCount=%u iStart=%u iCount=%u mat=%u\n",
				i, m.startVertex, m.vertexCount, m.startIndex, m.indexCount, m.materialIndex);
			OutputDebugStringA(b);
		}


	}


	// ======================
	// Material CB
	// ======================
	materialResource_ = dx->CreateBufferResource(sizeof(Material));
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

	*materialData_ = {};
	materialData_->color = { 1,1,1,1 };
	materialData_->enableLighting = 1;
	materialData_->uvTransform = Matrix4x4::MakeIdentity4x4();
	materialData_->shininess = 64.0f;
	CreatePerMaterialResources_(dx);

	// ======================
	// Texture load（materials 全部）
	// ======================
    AssetLoading::Timer textures("model.textures",directoryPath+"/"+filename);
    for(const auto& t:modelData_.preparedTextures) TextureManager::GetInstance()->RegisterPrepared(t);
    modelData_.preparedTextures.clear();
    for(const auto& t:modelData_.embeddedTextures)
        TextureManager::GetInstance()->LoadTextureFromMemory(t.key,t.bytes.data(),t.bytes.size());
	for (const auto& m : modelData_.materials) {
		if (!m.textureFilePath.empty()) {
			TextureManager::GetInstance()->LoadTexture(m.textureFilePath);
		}
	}

	OutputDebugStringA(("[Model] total vertex count = " +
		std::to_string(totalVtx) + "\n").c_str());

	char buf2[256];
	std::snprintf(buf2, sizeof(buf2),
		"[VertexData Offsets] pos=%zu uv=%zu nrm=%zu sizeof=%zu\n",
		offsetof(Model::VertexData, position),
		offsetof(Model::VertexData, texcoord),
		offsetof(Model::VertexData, normal),
		sizeof(Model::VertexData));
	OutputDebugStringA(buf2);

	// BuildNodeRuntime_();

}


void Model::InitializeFromModelData(ModelCommon* modelCommon, const ModelData& modelData)
{
	modelCommon_ = modelCommon;
	modelData_ = modelData;

	auto* dx = modelCommon_->GetDxCommon();

	// マテリアルが空なら最低1個作る
	if (modelData_.materials.empty()) {
		modelData_.materials.push_back({});
	}

	// meshes が空なら何もしない
	size_t totalVtx = 0;
	for (auto& mesh : modelData_.meshes) {
		totalVtx += mesh.vertices.size();
	}

	if (totalVtx == 0) {
		OutputDebugStringA("[Model] ERROR: totalVtx == 0 in InitializeFromModelData\n");
		return;
	}

	// Material CB
	materialResource_ = dx->CreateBufferResource(sizeof(Material));
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
	*materialData_ = {};
	materialData_->color = { 1,1,1,1 };
	materialData_->enableLighting = 1;
	materialData_->uvTransform = Matrix4x4::MakeIdentity4x4();
	materialData_->shininess = 64.0f;
	CreatePerMaterialResources_(dx);

	// VB
	vertexResource_ = dx->CreateBufferResource(sizeof(VertexData) * totalVtx);
	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));

	uint32_t vertexCursor = 0;
	for (auto& mesh : modelData_.meshes) {
		mesh.startVertex = vertexCursor;
		mesh.vertexCount = static_cast<uint32_t>(mesh.vertices.size());

		std::memcpy(
			vertexData_ + vertexCursor,
			mesh.vertices.data(),
			sizeof(VertexData) * mesh.vertices.size()
		);

		vertexCursor += mesh.vertexCount;
	}

	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * totalVtx);
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	// IB 作成（元のInitializeと完全に同じロジックで実装）
	const size_t totalIdx = modelData_.indices.size();
	if (totalIdx > 0) {
		indexResource_ = dx->CreateBufferResource(sizeof(uint32_t) * totalIdx);
		indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData_));

		uint32_t ibCursor = 0;
		for (auto& mesh : modelData_.meshes) {
			const uint32_t vOffset = mesh.startVertex;
			const uint32_t srcStart = mesh.startIndex;
			const uint32_t count = mesh.indexCount;

			mesh.startIndex = ibCursor;

			for (uint32_t k = 0; k < count; ++k) {
				const uint32_t localIndex = modelData_.indices[srcStart + k];
				indexData_[ibCursor + k] = localIndex + vOffset;
			}

			ibCursor += count;
		}

		indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
		indexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * totalIdx);
		indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
	}

	BuildNodeRuntime_();
}

void Model::CreatePerMaterialResources_(DirectXCommon* dx) {
	perMaterialResources_.clear();
	perMaterialData_.clear();
	perMaterialResources_.reserve(modelData_.materials.size());
	perMaterialData_.reserve(modelData_.materials.size());

	for (const auto& material : modelData_.materials) {
		auto resource = dx->CreateBufferResource(sizeof(Material));
		Material* mapped = nullptr;
		resource->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
		*mapped = *materialData_;
		mapped->color = material.baseColor;
		perMaterialResources_.push_back(std::move(resource));
		perMaterialData_.push_back(mapped);
	}
}

void Model::BindMaterialForMesh_(ID3D12GraphicsCommandList* cmd, const MeshData& mesh) {
    // GPU commands retain addresses, not a snapshot of shared material contents.
    if (instanceMaterialCBVs_ && mesh.materialIndex < instanceMaterialCBVs_->size()) {
        cmd->SetGraphicsRootConstantBufferView(0, (*instanceMaterialCBVs_)[mesh.materialIndex]);
        return;
    }
	if (mesh.materialIndex >= perMaterialResources_.size() ||
		mesh.materialIndex >= perMaterialData_.size()) {
		cmd->SetGraphicsRootConstantBufferView(0, GetActiveMaterialCBV_());
		return;
	}

	const Material* source = materialDataOverride_ ? materialDataOverride_ : materialData_;
	Material* destination = perMaterialData_[mesh.materialIndex];
	if (source && destination) {
		*destination = *source;
		const Vector4& base = modelData_.materials[mesh.materialIndex].baseColor;
		destination->color = {
			source->color.x * base.x,
			source->color.y * base.y,
			source->color.z * base.z,
			source->color.w * base.w
		};
	}
	cmd->SetGraphicsRootConstantBufferView(
		0, perMaterialResources_[mesh.materialIndex]->GetGPUVirtualAddress());
}

void Model::Draw(ID3D12GraphicsCommandList* cmd) {
	if (!vertexResource_ || !materialResource_ || !materialData_) {
		OutputDebugStringA("[Model] Draw skipped: resources not initialized\n");
		return;
	}
	if (!indexResource_) {
		OutputDebugStringA("[Model] Draw skipped: indexResource_ is null (not indexed)\n");
		return;
	}

	cmd->IASetVertexBuffers(0, 1, &vertexBufferView_);
	cmd->IASetIndexBuffer(&indexBufferView_);

	cmd->SetGraphicsRootConstantBufferView(0, GetActiveMaterialCBV_());

	for (const auto& mesh : modelData_.meshes) {
		BindMaterialForMesh_(cmd, mesh);

		std::string texPath;
		if (mesh.materialIndex < modelData_.materials.size()) {
			texPath = modelData_.materials[mesh.materialIndex].textureFilePath;
		}

		D3D12_GPU_DESCRIPTOR_HANDLE handle{};
		bool useWhite = false;

		if (!texPath.empty()) {
			if (!TextureManager::GetInstance()->HasTexture(texPath)) {
				TextureManager::GetInstance()->LoadTexture(texPath);
			}
			handle = TextureManager::GetInstance()->GetSrvHandleGPU(texPath);
		}
		else {
			useWhite = true;
		}

		if (useWhite) {
			handle = TextureManager::GetInstance()->GetSrvHandleGPU(""); // 空→白
		}

		cmd->SetGraphicsRootDescriptorTable(2, handle);

		// ★Indexed
		cmd->DrawIndexedInstanced(
			mesh.indexCount,
			1,
			mesh.startIndex,
			0, // BaseVertexLocation は IB を vOffset 加算済みなので 0 でOK
			0
		);
	}
}




void Model::Draw(ID3D12GraphicsCommandList* cmd, uint32_t instanceCount) {
	if (!vertexResource_ || !materialResource_ || !materialData_) {
		OutputDebugStringA("[Model] Draw skipped: resources not initialized\n");
		return;
	}

	cmd->IASetVertexBuffers(0, 1, &vertexBufferView_);

	// ★IBがある場合だけセット（無い場合は非Indexed）
	const bool useIndexed = (indexResource_ != nullptr);
	if (useIndexed) {
		cmd->IASetIndexBuffer(&indexBufferView_);
	}

	cmd->SetGraphicsRootConstantBufferView(
		0, materialResource_->GetGPUVirtualAddress());

	for (const auto& mesh : modelData_.meshes) {
		BindMaterialForMesh_(cmd, mesh);

		// ---- texture ----
		std::string texPath;
		if (mesh.materialIndex < modelData_.materials.size()) {
			texPath = modelData_.materials[mesh.materialIndex].textureFilePath;
		}

		D3D12_GPU_DESCRIPTOR_HANDLE handle{};
		bool useWhite = false;

		if (!texPath.empty()) {
			if (!TextureManager::GetInstance()->HasTexture(texPath)) {
				TextureManager::GetInstance()->LoadTexture(texPath);
			}
			handle = TextureManager::GetInstance()->GetSrvHandleGPU(texPath);
		}
		else {
			useWhite = true;
		}

		if (useWhite) {
			handle = TextureManager::GetInstance()->GetSrvHandleGPU("");
		}

		cmd->SetGraphicsRootDescriptorTable(2, handle);

		// ---- draw ----
		if (useIndexed && mesh.indexCount > 0) {
			// ★Indexed描画
			// IB詰めで index に vOffset 足してるなら BaseVertexLocation は 0 でOK
			cmd->DrawIndexedInstanced(
				mesh.indexCount,   // IndexCountPerInstance
				instanceCount,     // InstanceCount
				mesh.startIndex,   // StartIndexLocation
				0,                 // BaseVertexLocation
				0                  // StartInstanceLocation
			);
		}
		else {
			// ★非Indexed描画（従来）
			cmd->DrawInstanced(
				mesh.vertexCount,
				instanceCount,
				mesh.startVertex,
				0
			);
		}
	}
}

void Model::Draw(ID3D12GraphicsCommandList* cmd,
	uint32_t instanceCount,
	const D3D12_GPU_DESCRIPTOR_HANDLE* overrideSrv)
{
	if (!vertexResource_ || !materialResource_ || !materialData_) {
		OutputDebugStringA("[Model] Draw skipped: resources not initialized\n");
		return;
	}

	cmd->IASetVertexBuffers(0, 1, &vertexBufferView_);

	const bool useIndexed = (indexResource_ != nullptr);
	if (useIndexed) cmd->IASetIndexBuffer(&indexBufferView_);

	cmd->SetGraphicsRootConstantBufferView(0, GetActiveMaterialCBV_());

	for (const auto& mesh : modelData_.meshes) {
		BindMaterialForMesh_(cmd, mesh);

		D3D12_GPU_DESCRIPTOR_HANDLE handle{};

		if (overrideSrv) {
			// ★動画など外部SRVを強制使用
			handle = *overrideSrv;

		}
		else {
			// ---- いつものテクスチャ ----
			std::string texPath;
			if (mesh.materialIndex < modelData_.materials.size()) {
				texPath = modelData_.materials[mesh.materialIndex].textureFilePath;
			}

			if (!texPath.empty()) {
				if (!TextureManager::GetInstance()->HasTexture(texPath)) {
					TextureManager::GetInstance()->LoadTexture(texPath);
				}
				handle = TextureManager::GetInstance()->GetSrvHandleGPU(texPath);
			}
			else {
				handle = TextureManager::GetInstance()->GetSrvHandleGPU(""); // 白
			}
		}

		// ★Rigid の RootParam(2) = Texture SRV(t0想定に直す)
		cmd->SetGraphicsRootDescriptorTable(2, handle);

		if (useIndexed && mesh.indexCount > 0) {
			cmd->DrawIndexedInstanced(mesh.indexCount, instanceCount, mesh.startIndex, 0, 0);
		}
		else {
			cmd->DrawInstanced(mesh.vertexCount, instanceCount, mesh.startVertex, 0);
		}
	}
}

// Model.cpp
void Model::DrawSkinned(ID3D12GraphicsCommandList* cmd, const SkinCluster& sc)
{
	if (!vertexResource_ || !indexResource_ || !materialResource_ || !materialData_) {
		OutputDebugStringA("[Model] DrawSkinned skipped: resources not initialized\n");
		return;
	}

	// ★VBV2本：slot0=VertexData, slot1=VertexInfluence
	D3D12_VERTEX_BUFFER_VIEW vbvs[2] = {
		vertexBufferView_,
		sc.influenceBufferView
	};
	cmd->IASetVertexBuffers(0, 2, vbvs);
	cmd->IASetIndexBuffer(&indexBufferView_);

	// Material (b0)
	cmd->SetGraphicsRootConstantBufferView(0, GetActiveMaterialCBV_());

	for (const auto& mesh : modelData_.meshes) {

		if (!mesh.skinned) {
			continue; // ★剣などスキン無しはここでは描かない
		}
		BindMaterialForMesh_(cmd, mesh);

		// ---- texture path ----
		std::string texPath;
		if (mesh.materialIndex < modelData_.materials.size()) {
			texPath = modelData_.materials[mesh.materialIndex].textureFilePath;
		}

		D3D12_GPU_DESCRIPTOR_HANDLE handle{};
		if (!texPath.empty()) {
			if (!TextureManager::GetInstance()->HasTexture(texPath)) {
				TextureManager::GetInstance()->LoadTexture(texPath);
			}
			handle = TextureManager::GetInstance()->GetSrvHandleGPU(texPath);
		}
		else {
			// 白テクスチャ（あなたの仕様）
			handle = TextureManager::GetInstance()->GetSrvHandleGPU("");
		}

		// ★SkinningCommon: RootParam(3) が PS Texture SRV(t1)
		cmd->SetGraphicsRootDescriptorTable(3, handle);

		// ★IB は vOffset 足し込み済みなので BaseVertexLocation=0
		cmd->DrawIndexedInstanced(mesh.indexCount, 1, mesh.startIndex, 0, 0);
	}
}

void Model::DrawSkinnedCompute(ID3D12GraphicsCommandList* cmd, const SkinCluster& sc, const D3D12_GPU_DESCRIPTOR_HANDLE* overrideSrv)
{
	if (!indexResource_ || !materialResource_ || !materialData_ || !sc.outputVertexResource) {
		OutputDebugStringA("[Model] DrawSkinnedCompute skipped: resources not initialized\n");
		return;
	}

	// ★ ComputeShaderでスキニング済みのVBV (slot0のみ)
	cmd->IASetVertexBuffers(0, 1, &sc.skinnedVertexBufferView);
	cmd->IASetIndexBuffer(&indexBufferView_);

	// Material (b0)
	cmd->SetGraphicsRootConstantBufferView(0, GetActiveMaterialCBV_());

	for (const auto& mesh : modelData_.meshes) {

		if (!mesh.skinned) {
			continue; // 剣などスキン無しはここでは描かない
		}
		BindMaterialForMesh_(cmd, mesh);

		D3D12_GPU_DESCRIPTOR_HANDLE handle{};
		if (overrideSrv) {
			handle = *overrideSrv;
		}
		else {
			// ---- texture path ----
			std::string texPath;
			if (mesh.materialIndex < modelData_.materials.size()) {
				texPath = modelData_.materials[mesh.materialIndex].textureFilePath;
			}

			if (!texPath.empty()) {
				if (!TextureManager::GetInstance()->HasTexture(texPath)) {
					TextureManager::GetInstance()->LoadTexture(texPath);
				}
				handle = TextureManager::GetInstance()->GetSrvHandleGPU(texPath);
			}
			else {
				handle = TextureManager::GetInstance()->GetSrvHandleGPU("");
			}
		}

		// ★PrimitiveCommon/Object3dCommon: RootParam(2) が PS Texture SRV(t0)
		cmd->SetGraphicsRootDescriptorTable(2, handle);

		// ★IB は vOffset 足し込み済みなので BaseVertexLocation=0
		cmd->DrawIndexedInstanced(mesh.indexCount, 1, mesh.startIndex, 0, 0);
	}
}

void Model::DrawMeshIndexed(ID3D12GraphicsCommandList* cmd, uint32_t meshIndex, uint32_t instanceCount)
{
	const auto& mesh = modelData_.meshes[meshIndex];
	BindMaterialForMesh_(cmd, mesh);

	// ---- texture ----
	std::string texPath;
	if (mesh.materialIndex < modelData_.materials.size()) {
		texPath = modelData_.materials[mesh.materialIndex].textureFilePath;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE handle{};
	if (!texPath.empty()) {
		if (!TextureManager::GetInstance()->HasTexture(texPath)) {
			TextureManager::GetInstance()->LoadTexture(texPath);
		}
		handle = TextureManager::GetInstance()->GetSrvHandleGPU(texPath);
	}
	else {
		handle = TextureManager::GetInstance()->GetSrvHandleGPU(""); // 白
	}

	cmd->SetGraphicsRootDescriptorTable(2, handle);

	// ---- draw ----
	cmd->DrawIndexedInstanced(
		mesh.indexCount,
		instanceCount,
		mesh.startIndex,
		0,
		0
	);
}

void Model::DrawSkinnedOneMesh(ID3D12GraphicsCommandList* cmd, const SkinCluster& sc, uint32_t meshIndex)
{
	const auto& mesh = modelData_.meshes[meshIndex];

	D3D12_VERTEX_BUFFER_VIEW vbvs[2] = {
		vertexBufferView_,
		sc.influenceBufferView
	};
	cmd->IASetVertexBuffers(0, 2, vbvs);
	cmd->IASetIndexBuffer(&indexBufferView_);

	cmd->SetGraphicsRootConstantBufferView(0, GetActiveMaterialCBV_());
	BindMaterialForMesh_(cmd, mesh);

	std::string texPath;
	if (mesh.materialIndex < modelData_.materials.size()) {
		texPath = modelData_.materials[mesh.materialIndex].textureFilePath;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE handle{};
	if (!texPath.empty()) {
		if (!TextureManager::GetInstance()->HasTexture(texPath)) {
			TextureManager::GetInstance()->LoadTexture(texPath);
		}
		handle = TextureManager::GetInstance()->GetSrvHandleGPU(texPath);
	}
	else {
		handle = TextureManager::GetInstance()->GetSrvHandleGPU("");
	}

	cmd->SetGraphicsRootDescriptorTable(3, handle);

	cmd->DrawIndexedInstanced(mesh.indexCount, 1, mesh.startIndex, 0, 0);
}

Model::ModelData Model::LoadObjFile(
	const std::string& directoryPath,
	const std::string& filename)
{
	ModelData out{};
	std::string path = directoryPath + "/" + filename;

	Assimp::Importer importer;

	unsigned int flags = 0;
	flags |= aiProcess_Triangulate;
	flags |= aiProcess_PreTransformVertices; // ノード変換を焼き込み
	flags |= aiProcess_GenSmoothNormals;
	// ★自前で 1-v するので FlipUVs は使わない
	// flags |= aiProcess_FlipUVs;

	const aiScene* scene = importer.ReadFile(path, flags);
	if (!scene || !scene->HasMeshes()) {
        if(AssetLoading::profiling) { std::lock_guard lock(AssetLoading::profileMutex); AssetLoading::events.push_back({"model.import-error",path+":"+importer.GetErrorString(),0}); }
		OutputDebugStringA(importer.GetErrorString());
		OutputDebugStringA("\n");
		assert(false && "Assimp ReadFile failed");
		return out;
	}

	// materials 全取得
	BuildMaterials(out, scene, directoryPath);

	// meshes 全取得
	out.meshes.clear();
	out.meshes.reserve(scene->mNumMeshes);
	for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
		out.meshes.push_back(BuildMeshTriList(scene->mMeshes[mi]));
	}

	return out;
}

static void DebugAssimpSupport_(Assimp::Importer& importer)
{
	const bool gltf = importer.IsExtensionSupported("gltf");
	const bool glb = importer.IsExtensionSupported("glb");
	const bool fbx = importer.IsExtensionSupported("fbx");

	char buf[256];
	std::snprintf(buf, sizeof(buf),
		"[Assimp] IsExtensionSupported: fbx=%d gltf=%d glb=%d\n",
		fbx ? 1 : 0, gltf ? 1 : 0, glb ? 1 : 0);
	OutputDebugStringA(buf);
}


namespace {
class PmxReader {
public:
	explicit PmxReader(const std::string& path) {
		std::ifstream file(
			std::filesystem::path(StringUtility::ConvertString(path)),
			std::ios::binary);
		if (!file) {
			error_ = "file open failed";
			return;
		}
		data_ = std::vector<uint8_t>(
			std::istreambuf_iterator<char>(file),
			std::istreambuf_iterator<char>());
	}

	template<class T>
	T Read() {
		T value{};
		if (!ReadBytes(&value, sizeof(value))) {
			return {};
		}
		return value;
	}

	bool ReadBytes(void* destination, size_t size) {
		if (size > data_.size() - std::min(offset_, data_.size())) {
			error_ = "unexpected end of file";
			offset_ = data_.size();
			return false;
		}
		std::memcpy(destination, data_.data() + offset_, size);
		offset_ += size;
		return true;
	}

	void Skip(size_t size) {
		if (size > data_.size() - std::min(offset_, data_.size())) {
			error_ = "unexpected end of file";
			offset_ = data_.size();
			return;
		}
		offset_ += size;
	}

	uint32_t ReadUnsignedIndex(uint8_t size) {
		switch (size) {
		case 1: return Read<uint8_t>();
		case 2: return Read<uint16_t>();
		case 4: return Read<uint32_t>();
		default:
			error_ = "invalid PMX index size";
			return 0;
		}
	}

	int32_t ReadSignedIndex(uint8_t size) {
		switch (size) {
		case 1: return Read<int8_t>();
		case 2: return Read<int16_t>();
		case 4: return Read<int32_t>();
		default:
			error_ = "invalid PMX index size";
			return -1;
		}
	}

	std::string ReadText(uint8_t encoding) {
		const int32_t byteLength = Read<int32_t>();
		if (byteLength < 0 || static_cast<size_t>(byteLength) > Remaining()) {
			error_ = "invalid PMX text length";
			return {};
		}
		if (byteLength == 0) {
			return {};
		}

		if (encoding == 1) {
			std::string text(static_cast<size_t>(byteLength), '\0');
			ReadBytes(text.data(), text.size());
			return text;
		}

		if ((byteLength & 1) != 0) {
			error_ = "invalid UTF-16 PMX text length";
			return {};
		}
		std::wstring text(static_cast<size_t>(byteLength) / sizeof(wchar_t), L'\0');
		ReadBytes(text.data(), static_cast<size_t>(byteLength));
		return StringUtility::ConvertString(text);
	}

	bool Good() const { return error_.empty(); }
	size_t Remaining() const { return data_.size() - std::min(offset_, data_.size()); }
	const std::string& Error() const { return error_; }

private:
	std::vector<uint8_t> data_;
	size_t offset_ = 0;
	std::string error_;
};

struct PmxMaterialRecord {
	std::string name;
	Vector4 diffuse{ 1.0f, 1.0f, 1.0f, 1.0f };
	int32_t textureIndex = -1;
	uint32_t indexCount = 0;
};

struct PmxVertexSkin {
	std::array<int32_t, 4> bones{ -1, -1, -1, -1 };
	std::array<float, 4> weights{ 0.0f, 0.0f, 0.0f, 0.0f };
};

struct PmxBoneRecord {
	std::string name;
	Vector3 position{};
	int32_t parent = -1;
};
}

Model::ModelData Model::LoadPmxFile(const std::string& fullPath)
{
	ModelData out{};
	PmxReader reader(fullPath);

	char signature[4]{};
	reader.ReadBytes(signature, sizeof(signature));
	const float version = reader.Read<float>();
	const uint8_t headerSize = reader.Read<uint8_t>();
	if (!reader.Good() || std::memcmp(signature, "PMX ", 4) != 0 ||
		version < 2.0f || version >= 3.0f || headerSize < 8) {
		OutputDebugStringA(("[PMX] Invalid header: " + fullPath + "\n").c_str());
		return out;
	}

	std::vector<uint8_t> globals(headerSize);
	reader.ReadBytes(globals.data(), globals.size());
	if (!reader.Good()) {
		OutputDebugStringA("[PMX] Failed to read globals\n");
		return out;
	}
	const uint8_t encoding = globals[0];
	const uint8_t additionalUvCount = globals[1];
	const uint8_t vertexIndexSize = globals[2];
	const uint8_t textureIndexSize = globals[3];
	const uint8_t boneIndexSize = globals[5];

	// Model name/comment fields (Japanese and English).
	for (int i = 0; i < 4; ++i) {
		reader.ReadText(encoding);
	}

	const int32_t vertexCount = reader.Read<int32_t>();
	if (vertexCount <= 0 || vertexCount > 10'000'000) {
		OutputDebugStringA("[PMX] Invalid vertex count\n");
		return out;
	}

	std::vector<VertexData> vertices(static_cast<size_t>(vertexCount));
	std::vector<PmxVertexSkin> vertexSkins(static_cast<size_t>(vertexCount));
	for (int32_t i = 0; i < vertexCount && reader.Good(); ++i) {
		VertexData vertex{};
		vertex.position = {
			reader.Read<float>(), reader.Read<float>(), reader.Read<float>(), 1.0f
		};
		vertex.normal = {
			reader.Read<float>(), reader.Read<float>(), reader.Read<float>()
		};
		vertex.texcoord = { reader.Read<float>(), reader.Read<float>() };
		reader.Skip(static_cast<size_t>(additionalUvCount) * sizeof(float) * 4);

		const uint8_t deform = reader.Read<uint8_t>();
		PmxVertexSkin skin{};
		switch (deform) {
		case 0: // BDEF1
			skin.bones[0] = reader.ReadSignedIndex(boneIndexSize);
			skin.weights[0] = 1.0f;
			break;
		case 1: // BDEF2
			skin.bones[0] = reader.ReadSignedIndex(boneIndexSize);
			skin.bones[1] = reader.ReadSignedIndex(boneIndexSize);
			skin.weights[0] = reader.Read<float>();
			skin.weights[1] = 1.0f - skin.weights[0];
			break;
		case 2: // BDEF4
		case 4: // QDEF
			for (int j = 0; j < 4; ++j) skin.bones[j] = reader.ReadSignedIndex(boneIndexSize);
			for (int j = 0; j < 4; ++j) skin.weights[j] = reader.Read<float>();
			break;
		case 3: // SDEF
			skin.bones[0] = reader.ReadSignedIndex(boneIndexSize);
			skin.bones[1] = reader.ReadSignedIndex(boneIndexSize);
			skin.weights[0] = reader.Read<float>();
			skin.weights[1] = 1.0f - skin.weights[0];
			reader.Skip(sizeof(float) * 9);
			break;
		default:
			OutputDebugStringA("[PMX] Unsupported vertex deform type\n");
			return {};
		}
		reader.Skip(sizeof(float)); // edge scale
		vertices[static_cast<size_t>(i)] = vertex;
		vertexSkins[static_cast<size_t>(i)] = skin;
	}

	const int32_t indexCount = reader.Read<int32_t>();
	if (indexCount <= 0 || indexCount > 100'000'000) {
		OutputDebugStringA("[PMX] Invalid index count\n");
		return {};
	}
	out.indices.resize(static_cast<size_t>(indexCount));
	for (int32_t i = 0; i < indexCount; ++i) {
		out.indices[static_cast<size_t>(i)] = reader.ReadUnsignedIndex(vertexIndexSize);
	}

	const int32_t textureCount = reader.Read<int32_t>();
	if (textureCount < 0 || textureCount > 100'000) return {};
	std::vector<std::string> textures(static_cast<size_t>(textureCount));
	const std::filesystem::path modelDirectory =
		std::filesystem::path(StringUtility::ConvertString(fullPath)).parent_path();
	for (int32_t i = 0; i < textureCount; ++i) {
		std::string relative = reader.ReadText(encoding);
		std::replace(relative.begin(), relative.end(), '\\', '/');
		if (!relative.empty()) {
			const std::filesystem::path resolved =
				(modelDirectory / std::filesystem::path(StringUtility::ConvertString(relative))).lexically_normal();
			textures[static_cast<size_t>(i)] =
				StringUtility::ConvertString(resolved.wstring());
		}
	}

	const int32_t materialCount = reader.Read<int32_t>();
	if (materialCount <= 0 || materialCount > 100'000) return {};
	std::vector<PmxMaterialRecord> materials;
	materials.reserve(static_cast<size_t>(materialCount));
	for (int32_t i = 0; i < materialCount && reader.Good(); ++i) {
		PmxMaterialRecord material{};
		material.name = reader.ReadText(encoding);
		reader.ReadText(encoding); // English name
		material.diffuse = {
			reader.Read<float>(), reader.Read<float>(),
			reader.Read<float>(), reader.Read<float>()
		};
		reader.Skip(sizeof(float) * 3); // specular
		reader.Skip(sizeof(float));     // specular strength
		reader.Skip(sizeof(float) * 3); // ambient
		reader.Skip(sizeof(uint8_t));   // draw flags
		reader.Skip(sizeof(float) * 4); // edge color
		reader.Skip(sizeof(float));     // edge size
		material.textureIndex = reader.ReadSignedIndex(textureIndexSize);
		reader.ReadSignedIndex(textureIndexSize); // sphere texture
		reader.Skip(sizeof(uint8_t));             // sphere mode
		const uint8_t sharedToon = reader.Read<uint8_t>();
		if (sharedToon == 0) {
			reader.ReadSignedIndex(textureIndexSize);
		}
		else {
			reader.Skip(sizeof(uint8_t));
		}
		reader.ReadText(encoding); // memo
		const int32_t materialIndexCount = reader.Read<int32_t>();
		if (materialIndexCount < 0) return {};
		material.indexCount = static_cast<uint32_t>(materialIndexCount);
		materials.push_back(std::move(material));
	}

	const int32_t boneCount = reader.Read<int32_t>();
	if (boneCount <= 0 || boneCount > 1'000'000) {
		OutputDebugStringA("[PMX] Invalid bone count\n");
		return {};
	}
	std::vector<PmxBoneRecord> bones(static_cast<size_t>(boneCount));
	for (int32_t i = 0; i < boneCount && reader.Good(); ++i) {
		PmxBoneRecord bone{};
		bone.name = reader.ReadText(encoding);
		reader.ReadText(encoding); // English name
		bone.position = {
			reader.Read<float>(), reader.Read<float>(), reader.Read<float>()
		};
		bone.parent = reader.ReadSignedIndex(boneIndexSize);
		reader.Read<int32_t>(); // transform layer
		const uint16_t flags = reader.Read<uint16_t>();

		if ((flags & 0x0001u) != 0) reader.ReadSignedIndex(boneIndexSize);
		else reader.Skip(sizeof(float) * 3);
		if ((flags & (0x0100u | 0x0200u)) != 0) {
			reader.ReadSignedIndex(boneIndexSize);
			reader.Skip(sizeof(float));
		}
		if ((flags & 0x0400u) != 0) reader.Skip(sizeof(float) * 3);
		if ((flags & 0x0800u) != 0) reader.Skip(sizeof(float) * 6);
		if ((flags & 0x2000u) != 0) reader.Skip(sizeof(int32_t));
		if ((flags & 0x0020u) != 0) {
			reader.ReadSignedIndex(boneIndexSize); // IK target
			reader.Skip(sizeof(int32_t) + sizeof(float));
			const int32_t linkCount = reader.Read<int32_t>();
			if (linkCount < 0 || linkCount > 1'000'000) return {};
			for (int32_t link = 0; link < linkCount; ++link) {
				reader.ReadSignedIndex(boneIndexSize);
				const uint8_t hasLimit = reader.Read<uint8_t>();
				if (hasLimit != 0) reader.Skip(sizeof(float) * 6);
			}
		}
		if (bone.name.empty()) bone.name = "PMXBone_" + std::to_string(i);
		bones[static_cast<size_t>(i)] = std::move(bone);
	}

	if (!reader.Good()) {
		OutputDebugStringA(("[PMX] Parse failed: " + reader.Error() + "\n").c_str());
		return {};
	}

	out.materials.reserve(materials.size());
	out.meshes.reserve(materials.size());
	const std::vector<uint32_t> sourceIndices = std::move(out.indices);
	out.indices.clear();
	out.indices.reserve(sourceIndices.size());
	uint32_t firstIndex = 0;
	uint32_t globalVertex = 0;
	for (size_t i = 0; i < materials.size(); ++i) {
		const auto& source = materials[i];
		if (firstIndex + source.indexCount > sourceIndices.size()) {
			OutputDebugStringA("[PMX] Material index range is invalid\n");
			return {};
		}

		MaterialData material{};
		material.baseColor = source.diffuse;
		if (source.textureIndex >= 0 &&
			static_cast<size_t>(source.textureIndex) < textures.size()) {
			material.textureFilePath = textures[static_cast<size_t>(source.textureIndex)];
		}
		/*
		// This model's skin material points at the clothing atlas even though
		// the package includes a dedicated skin texture. The atlas samples
		// nearly the same cyan as the title background on the body UVs.
		if (source.name == "肌" && textures.size() > 1 && !textures[1].empty()) {
			material.textureFilePath = textures[1]; // skin.bmp
		}
		*/
		out.materials.push_back(std::move(material));

		MeshData mesh{};
		mesh.name = source.name.empty() ? ("material_" + std::to_string(i)) : source.name;
		mesh.materialIndex = static_cast<uint32_t>(i);
		mesh.startVertex = globalVertex;
		mesh.vertices.reserve(source.indexCount);
		mesh.startIndex = static_cast<uint32_t>(out.indices.size());
		mesh.indexCount = source.indexCount;
		mesh.skinned = false;

		// The renderer treats every material mesh as an independent vertex
		// range and adds mesh.startVertex while constructing the GPU index
		// buffer. Expand PMX's shared vertices here, but keep each mesh's
		// indices local to that expanded range.
		for (uint32_t localIndex = 0; localIndex < source.indexCount; ++localIndex) {
			const uint32_t sourceVertex =
				sourceIndices[static_cast<size_t>(firstIndex + localIndex)];
			if (sourceVertex >= vertices.size()) {
				OutputDebugStringA("[PMX] Vertex index is out of range\n");
				return {};
			}
			mesh.vertices.push_back(vertices[sourceVertex]);
			out.indices.push_back(localIndex);

			const PmxVertexSkin& skin = vertexSkins[sourceVertex];
			for (size_t influence = 0; influence < skin.bones.size(); ++influence) {
				const int32_t boneIndex = skin.bones[influence];
				const float weight = skin.weights[influence];
				if (boneIndex < 0 || boneIndex >= boneCount || weight <= 0.0f) continue;
				JointWeightData& joint =
					out.skinClusterData[bones[static_cast<size_t>(boneIndex)].name];
				joint.vertexWeights.push_back({ weight, globalVertex + localIndex });
			}
		}
		mesh.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
		mesh.skinned = true;
		globalVertex += mesh.vertexCount;
		out.meshes.push_back(std::move(mesh));
		firstIndex += source.indexCount;
	}

	out.rootNode.name = "PMXRoot";
	out.rootNode.localMatrix = Matrix4x4::MakeIdentity4x4();
	out.rootNode.transform.scale = { 1.0f, 1.0f, 1.0f };
	out.rootNode.transform.rotate = { 0.0f, 0.0f, 0.0f, 1.0f };
	out.rootNode.transform.translate = { 0.0f, 0.0f, 0.0f };
	for (uint32_t i = 0; i < out.meshes.size(); ++i) {
		out.rootNode.meshIndices.push_back(i);
		out.instances.push_back({ i, Matrix4x4::MakeIdentity4x4() });
	}

	std::vector<std::vector<int32_t>> boneChildren(static_cast<size_t>(boneCount));
	std::vector<int32_t> rootBones;
	for (int32_t i = 0; i < boneCount; ++i) {
		const int32_t parent = bones[static_cast<size_t>(i)].parent;
		if (parent >= 0 && parent < boneCount && parent != i) {
			boneChildren[static_cast<size_t>(parent)].push_back(i);
		}
		else {
			rootBones.push_back(i);
		}
	}

	std::function<Node(int32_t)> makeBoneNode = [&](int32_t boneIndex) {
		const PmxBoneRecord& bone = bones[static_cast<size_t>(boneIndex)];
		Node node{};
		node.name = bone.name;
		node.transform.scale = { 1.0f, 1.0f, 1.0f };
		node.transform.rotate = { 0.0f, 0.0f, 0.0f, 1.0f };
		Vector3 localPosition = bone.position;
		if (bone.parent >= 0 && bone.parent < boneCount) {
			const Vector3& parentPosition = bones[static_cast<size_t>(bone.parent)].position;
			localPosition = {
				bone.position.x - parentPosition.x,
				bone.position.y - parentPosition.y,
				bone.position.z - parentPosition.z
			};
		}
		node.transform.translate = localPosition;
		node.localMatrix = MakeAffineMatrix(
			node.transform.scale, node.transform.rotate, node.transform.translate);
		for (int32_t child : boneChildren[static_cast<size_t>(boneIndex)]) {
			node.children.push_back(makeBoneNode(child));
		}
		return node;
	};
	for (int32_t rootBone : rootBones) {
		out.rootNode.children.push_back(makeBoneNode(rootBone));
	}

	for (const PmxBoneRecord& bone : bones) {
		JointWeightData& joint = out.skinClusterData[bone.name];
		joint.inverseBindPoseMatrix =
			Matrix4x4::Translation({ -bone.position.x, -bone.position.y, -bone.position.z });
	}
	out.hasSkinning = true;

	OutputDebugStringA(
		("[PMX] Loaded vertices=" + std::to_string(vertices.size()) +
		 " indices=" + std::to_string(out.indices.size()) +
		 " materials=" + std::to_string(out.materials.size()) + "\n").c_str());
	return out;
}

Model::ModelData Model::LoadAssimpFile(const std::string& fullPath)
{

	PrintLoadedAssimpPath_();

	ModelData out{};

	// 1) パス確認
	{
		OutputDebugStringA(("[Assimp] fullPath = " + fullPath + "\n").c_str());

		// ★ここに追加（絶対パス確認）
		OutputDebugStringA(
			("[Assimp] abs = " +
				StringUtility::ConvertString(std::filesystem::absolute(AssetLoading::Path(fullPath)).wstring()) + "\n").c_str()
		);

		if (!std::filesystem::exists(AssetLoading::Path(fullPath))) {
			OutputDebugStringA("[Assimp] FILE NOT FOUND\n");
			return out;
		}
	}


	{
		std::ifstream f(AssetLoading::Path(fullPath), std::ios::binary);
		char magic[4]{};
		if (f.read(magic, 4)) {
			char buf[128];
			std::snprintf(buf, sizeof(buf),
				"[glb header] %02X %02X %02X %02X (%c%c%c%c)\n",
				(unsigned char)magic[0], (unsigned char)magic[1],
				(unsigned char)magic[2], (unsigned char)magic[3],
				magic[0], magic[1], magic[2], magic[3]);
			OutputDebugStringA(buf);
		}
	}


	Assimp::Importer importer;

	{
		bool gltf = importer.IsExtensionSupported("gltf");
		bool glb = importer.IsExtensionSupported("glb");
		const bool fbx = importer.IsExtensionSupported("fbx");

		char buf[256];
		std::snprintf(buf, sizeof(buf),
			"[Assimp] IsExtensionSupported: fbx=%d gltf=%d glb=%d\n",
			fbx ? 1 : 0, gltf ? 1 : 0, glb ? 1 : 0);
		OutputDebugStringA(buf);
	}

	// 2) 拡張子サポート確認（glb/gltf が 0 なら Assimp 側の問題）
	DebugAssimpSupport_(importer);


	unsigned int flags = 0;
	flags |= aiProcess_Triangulate;
	flags |= aiProcess_JoinIdenticalVertices;
	flags |= aiProcess_GenSmoothNormals;
	//flags |= aiProcess_FlipUVs;flags |= aiProcess_FlipUVs; // ← 自前で 1-v するなら付けない

	const aiScene* scene = nullptr;

	//テスト



	try {

		OutputDebugStringA("[Assimp] about to ReadFile...\n");
		OutputDebugStringA(("[Assimp] file=" + fullPath + "\n").c_str());

		scene = importer.ReadFile(fullPath.c_str(), flags);
	}
	catch (const std::exception& e) {
		OutputDebugStringA("[Assimp] Exception in ReadFile\n");
		OutputDebugStringA(e.what());
        if(AssetLoading::profiling) { std::lock_guard lock(AssetLoading::profileMutex); AssetLoading::events.push_back({"model.import-error",fullPath+":"+e.what(),0}); }
		OutputDebugStringA("\n");
		return out; // ★必ず抜ける
	}
	catch (...) {
		OutputDebugStringA("[Assimp] Unknown exception in ReadFile\n");
		std::string err = importer.GetErrorString();
		if (!err.empty()) {
			OutputDebugStringA(("[Assimp] GetErrorString: " + err + "\n").c_str());
		}
		return out; // ★必ず抜ける
	}

	if (!scene || !scene->HasMeshes()) {
        if(AssetLoading::profiling) { std::lock_guard lock(AssetLoading::profileMutex); AssetLoading::events.push_back({"model.import-error",fullPath+":"+importer.GetErrorString(),0}); }
		OutputDebugStringA(("[Assimp] ReadFile failed: " + std::string(importer.GetErrorString()) + "\n").c_str());
		return out;
	}

	{
		char buf[128];
		std::snprintf(buf, sizeof(buf),
			"[Scene] mNumMeshes=%u mNumAnimations=%u\n",
			scene->mNumMeshes, scene->mNumAnimations);
		OutputDebugStringA(buf);
	}

	// --- directory (for texture paths) ---
	auto p=AssetLoading::Path(fullPath);
	std::string dir=StringUtility::ConvertString(p.parent_path().wstring());

	// --- root node ---
	if (scene->mRootNode) {
		out.rootNode = ReadNodeRecursive(scene->mRootNode);
	}
	else {
		out.rootNode.localMatrix = Matrix4x4::MakeIdentity4x4();
		out.rootNode.name = "root";
	}

	// --- animations ---
	ReadAnimationsFromAssimp(out, scene);

	// --- debug: animation names ---
	for (const auto& [name, clip] : out.animations) {
		OutputDebugStringA(("[AnimName] " + name + " dur=" + std::to_string(clip.duration) + "\n").c_str());
	}


	// --- materials ---
	out.materials.clear();
	out.materials.resize(scene->mNumMaterials);

	for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
		const aiMaterial* mtl = scene->mMaterials[i];
		aiString texPath;
		aiColor4D baseColor(1.0f, 1.0f, 1.0f, 1.0f);
		if (aiGetMaterialColor(mtl, AI_MATKEY_BASE_COLOR, &baseColor) != AI_SUCCESS) {
			aiGetMaterialColor(mtl, AI_MATKEY_COLOR_DIFFUSE, &baseColor);
		}
		out.materials[i].baseColor = {
			baseColor.r, baseColor.g, baseColor.b, baseColor.a
		};

		if (mtl->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {

			std::string t = texPath.C_Str();

			// ★ 埋め込みテクスチャ（"*0" みたいなやつ）
			if (!t.empty() && t[0] == '*') {

				const aiTexture* emb = scene->GetEmbeddedTexture(texPath.C_Str());
				if (emb && emb->mHeight == 0 && emb->mWidth > 0) {
					// mHeight==0: 圧縮データ（png/jpg等）が pcData に入ってる
					const uint8_t* bytes = reinterpret_cast<const uint8_t*>(emb->pcData);
					const size_t   size = static_cast<size_t>(emb->mWidth);

					// ★キーは一意なら何でもOK（モデルごとに衝突しないよう dir を混ぜる）
					std::string key = fullPath + "/__emb" + t;   // 例: ".../__emb*0"

					out.embeddedTextures.push_back({key,std::vector<uint8_t>(bytes,bytes+size)});

					out.materials[i].textureFilePath = key; // ★ここ重要：後段は key を使う
				}
				else {
					out.materials[i].textureFilePath.clear(); // 読めないなら白へ
				}

			}
			else {
				// 外部ファイル
				out.materials[i].textureFilePath = dir + "/" + t;
			}

		}
		else {
			out.materials[i].textureFilePath.clear();
		}

	}

	// --- meshes ---
	out.meshes.clear();
	out.meshes.reserve(scene->mNumMeshes);
	out.indices.clear();

	aiVector3D zero3(0, 0, 0);

	uint32_t globalVertexBase = 0; // ★追加：VB連結を想定した頂点オフセット

	for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
		const aiMesh* mesh = scene->mMeshes[mi];

		auto LogMi = [&](const char* tag) {
			char b[512];
			std::snprintf(b, sizeof(b),
				"[mi=%u] %s name='%s' verts=%u faces=%u hasNormals=%d hasUV0=%d prim=0x%X mat=%u bones=%u\n",
				mi, tag, mesh->mName.C_Str(),
				mesh->mNumVertices, mesh->mNumFaces,
				mesh->HasNormals() ? 1 : 0,
				mesh->HasTextureCoords(0) ? 1 : 0,
				(unsigned)mesh->mPrimitiveTypes,
				mesh->mMaterialIndex,
				mesh->mNumBones
			);
			OutputDebugStringA(b);
			};

		LogMi("ENTER");


		{
			char b[256];
			std::snprintf(b, sizeof(b),
				"[MeshLoop] mi=%u/%u name='%s'\n",
				mi, scene->mNumMeshes, mesh->mName.C_Str());
			OutputDebugStringA(b);
		}

		{
			char b[256];
			std::snprintf(b, sizeof(b),
				"[MeshCheck] mi=%u name='%s' hasBones=%d numBones=%u verts=%u faces=%u\n",
				mi, mesh->mName.C_Str(),
				mesh->HasBones() ? 1 : 0,
				mesh->mNumBones,
				mesh->mNumVertices,
				mesh->mNumFaces
			);
			OutputDebugStringA(b);
		}

		if (mesh->HasBones()) {
			for (unsigned int bi = 0; bi < mesh->mNumBones; ++bi) {
				aiBone* bone = mesh->mBones[bi];
				std::string bn = bone->mName.C_Str();
				char bb[512];
				std::snprintf(bb, sizeof(bb),
					"  [Bone] mesh='%s' bi=%u bone='%s' weights=%u\n",
					mesh->mName.C_Str(), bi, bn.c_str(), bone->mNumWeights
				);
				OutputDebugStringA(bb);
			}
		}


		if (mesh->HasBones()) {
			out.hasSkinning = true;
		}

		MeshData md{};
		md.materialIndex = mesh->mMaterialIndex;
		md.name = mesh->mName.C_Str();

		md.skinned = mesh->HasBones();   // ★追加
		if (md.skinned) {
			out.hasSkinning = true;
		}

		md.startVertex = globalVertexBase;
		md.vertexCount = static_cast<uint32_t>(mesh->mNumVertices);

		// ===== vertices: mNumVertices をそのまま =====
		md.vertices.resize(mesh->mNumVertices);
		for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
			const aiVector3D* pos = &mesh->mVertices[i];
			const aiVector3D* nrm = mesh->HasNormals() ? &mesh->mNormals[i] : &zero3;
			const aiVector3D* uv = mesh->HasTextureCoords(0) ? &mesh->mTextureCoords[0][i] : &zero3;

			VertexData v{};

			// ★RH->LH：X反転（ノード側と合わせる）
			v.position = { -pos->x, pos->y, pos->z, 1.0f };

			// ★法線もX反転（重要）
			v.normal = { -nrm->x, nrm->y, nrm->z };

			// UVは今のままでOK（V反転方針は維持）
			v.texcoord = { uv->x, 1.0f - uv->y };

			md.vertices[i] = v;
		}


		// ===== indices: faces から結合して out.indices に入れる =====
		md.startIndex = static_cast<uint32_t>(out.indices.size());
		md.indexCount = 0;

		out.indices.reserve(out.indices.size() + mesh->mNumFaces * 3);

		// ★ indices: faces
		for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
			const aiFace& face = mesh->mFaces[f];
			if (face.mNumIndices != 3) continue;

			const uint32_t i0 = (uint32_t)face.mIndices[0];
			const uint32_t i1 = (uint32_t)face.mIndices[1];
			const uint32_t i2 = (uint32_t)face.mIndices[2];

			// 左手系の並び替えを維持
			out.indices.push_back(i2);
			out.indices.push_back(i1);
			out.indices.push_back(i0);

			md.indexCount += 3;
		}


		for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {

			aiBone* bone = mesh->mBones[boneIndex];
			std::string jointName = bone->mName.C_Str();
			JointWeightData& jointWeightData = out.skinClusterData[jointName];

			// ✅ mOffsetMatrix は「inverse bind pose」そのもの
			jointWeightData.inverseBindPoseMatrix =
				ConvertAssimpMatrix_LH(bone->mOffsetMatrix);

			for (uint32_t weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
				const float w = bone->mWeights[weightIndex].mWeight;
				const uint32_t localV = bone->mWeights[weightIndex].mVertexId;

				const uint32_t globalV = globalVertexBase + localV;
				jointWeightData.vertexWeights.push_back({ w, globalV });
			}
		}


		{
			std::string meshName = mesh->mName.C_Str();
			char b[512];
			std::snprintf(b, sizeof(b),
				"[Mesh] mi=%u name='%s' verts=%u faces=%u hasBones=%d numBones=%u\n",
				mi,
				meshName.c_str(),
				mesh->mNumVertices,
				mesh->mNumFaces,
				mesh->HasBones() ? 1 : 0,
				mesh->mNumBones
			);
			OutputDebugStringA(b);
		}

		out.meshes.push_back(std::move(md));
		globalVertexBase += static_cast<uint32_t>(mesh->mNumVertices);

	}

	out.instances.clear();
	BuildMeshInstances(scene->mRootNode, Matrix4x4::MakeIdentity4x4(), out.instances);

	char buf[128];
	sprintf_s(buf, "meshes=%zu instances=%zu\n", out.meshes.size(), out.instances.size());
	OutputDebugStringA(buf);

	OutputDebugStringA(("[DBG] skinClusterData joints = " + std::to_string(out.skinClusterData.size()) + "\n").c_str());


	return out;
}

const Matrix4x4& Model::GetRootLocalMatrix() const {
	return modelData_.rootNode.localMatrix;
}

bool Model::GetLocalAABB(AABB& out) const {
	bool hasAny = false;
	Vector3 minPos{ +FLT_MAX, +FLT_MAX, +FLT_MAX };
	Vector3 maxPos{ -FLT_MAX, -FLT_MAX, -FLT_MAX };

	for (const auto& mesh : modelData_.meshes) {
		for (const auto& v : mesh.vertices) {
			hasAny = true;
			const Vector4& p = v.position;
			minPos.x = std::min(minPos.x, p.x);
			minPos.y = std::min(minPos.y, p.y);
			minPos.z = std::min(minPos.z, p.z);
			maxPos.x = std::max(maxPos.x, p.x);
			maxPos.y = std::max(maxPos.y, p.y);
			maxPos.z = std::max(maxPos.z, p.z);
		}
	}

	if (!hasAny) {
		return false;
	}

	out.min = minPos;
	out.max = maxPos;
	return true;
}

std::vector<Model::MeshCollisionData> Model::GetMeshesLocalAABBs() const {
	std::vector<Model::MeshCollisionData> aabbs;
	aabbs.reserve(modelData_.meshes.size());

	for (const auto& mesh : modelData_.meshes) {
		if (mesh.vertices.empty()) continue;

		Vector3 minPos{ +FLT_MAX, +FLT_MAX, +FLT_MAX };
		Vector3 maxPos{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (const auto& v : mesh.vertices) {
			const Vector4& p = v.position;
			minPos.x = std::min(minPos.x, p.x);
			minPos.y = std::min(minPos.y, p.y);
			minPos.z = std::min(minPos.z, p.z);
			maxPos.x = std::max(maxPos.x, p.x);
			maxPos.y = std::max(maxPos.y, p.y);
			maxPos.z = std::max(maxPos.z, p.z);
		}
		MeshCollisionData data{};
		data.name = mesh.name;
		data.localAABB.min = minPos;
		data.localAABB.max = maxPos;
		aabbs.push_back(data);
	}
	return aabbs;
}

Model::Skeleton Model::CreateSkeleton(const Model::Node& rootNode) {

	Model::Skeleton skeleton;
	skeleton.root = CreateJoint(rootNode, {}, skeleton.joints);

	//名前とインデックスのマッピング
	for (const Model::Joint joint : skeleton.joints) {

		skeleton.jointMap.emplace(joint.name, joint.index);

	}

	return skeleton;

}

int32_t Model::CreateJoint(const Model::Node& node, const std::optional<int32_t>& parent, std::vector<Model::Joint>& joints) {

	Model::Joint joint;
	joint.name = node.name;
	joint.localMatrix = node.localMatrix;
	joint.skeletonSpaceMatrix = Matrix4x4::MakeIdentity4x4();
	joint.transform = node.transform;
	joint.index = int32_t(joints.size());
	joint.parent = parent;
	const int32_t myIndex = (int32_t)joints.size();
	joints.push_back(joint);
	for (const Model::Node& child : node.children) {

		//子jointを作成し、そのインデックスを取得
		int32_t childIndex = CreateJoint(child, myIndex, joints);
		joints[myIndex].children.push_back(childIndex);


	}

	return joint.index;

}

void Model::UpdateSkeleton(Skeleton& skeleton) {
	for (auto& joint : skeleton.joints) {

		// 1) Transform -> localMatrix
		joint.localMatrix = MakeAffineMatrix(
			joint.transform.scale,
			joint.transform.rotate,
			joint.transform.translate
		);

		// 2) local -> skeletonSpace（親から累積）
		if (joint.parent.has_value()) {
			const auto& parent = skeleton.joints[*joint.parent];

			// 行ベクトル想定：子 * 親
			joint.skeletonSpaceMatrix =
				Matrix4x4::Multiply(joint.localMatrix, parent.skeletonSpaceMatrix);
		}
		else {
			// root
			joint.skeletonSpaceMatrix = joint.localMatrix;
		}
	}
}


void Model::ComputeNodeGlobalMatrices(const Animation* anim, float time,
	std::vector<Matrix4x4>& outGlobals) const
{
	const size_t n = nodePtrs_.size();
	outGlobals.resize(n);

	std::vector<Matrix4x4> locals(n);

	for (size_t i = 0; i < n; ++i) {
		// 元のノードTransform
		Vector3 t = nodePtrs_[i]->transform.translate;
		Quaternion r = nodePtrs_[i]->transform.rotate;
		Vector3 s = nodePtrs_[i]->transform.scale;

		// アニメがあるなら上書き
		if (anim) {
			auto it = anim->nodeAnimations.find(nodePtrs_[i]->name);
			if (it != anim->nodeAnimations.end()) {
				const NodeAnimation& na = it->second;
				if (!na.translate.keyframes.empty()) t = CalculateValue(na.translate.keyframes, time);
				if (!na.rotate.keyframes.empty())    r = CalculateValue(na.rotate.keyframes, time);
				if (!na.scale.keyframes.empty())     s = CalculateValue(na.scale.keyframes, time);
			}
		}

		locals[i] = MakeAffineMatrix(s, r, t);
	}

	// TraverseNode_ は親→子の順で積んでるので、順番に累積でOK
	for (size_t i = 0; i < n; ++i) {
		const int32_t p = parentIndex_[i];
		if (p >= 0) {
			outGlobals[i] = Matrix4x4::Multiply(locals[i], outGlobals[p]); // 子 * 親（あなたの流儀）
		}
		else {
			outGlobals[i] = locals[i];
		}
	}
	auto& anims = GetAnimations();
	OutputDebugStringA(("[Anim] count=" + std::to_string(anims.size()) + "\n").c_str());
	if (!anims.empty()) {
		OutputDebugStringA(("[Anim] tracks=" + std::to_string(anims.begin()->second.nodeAnimations.size()) + "\n").c_str());
	}



}

void Model::DrawOneMesh(ID3D12GraphicsCommandList* cmd, uint32_t meshIndex, uint32_t texRootParam, const D3D12_GPU_DESCRIPTOR_HANDLE* overrideSrv)
{
	if (meshIndex >= modelData_.meshes.size()) return;

	const auto& mesh = modelData_.meshes[meshIndex];
	BindMaterialForMesh_(cmd, mesh);

	D3D12_GPU_DESCRIPTOR_HANDLE handle{};
	if (overrideSrv) {
		handle = *overrideSrv;
	}
	else {
		std::string texPath;
		if (mesh.materialIndex < modelData_.materials.size()) {
			texPath = modelData_.materials[mesh.materialIndex].textureFilePath;
		}

		if (!texPath.empty()) {
			if (!TextureManager::GetInstance()->HasTexture(texPath)) {
				TextureManager::GetInstance()->LoadTexture(texPath);
			}
			handle = TextureManager::GetInstance()->GetSrvHandleGPU(texPath);
		}
		else {
			handle = TextureManager::GetInstance()->GetSrvHandleGPU("");
		}
	}

	cmd->SetGraphicsRootDescriptorTable(texRootParam, handle);

	// ★Indexed 前提（あなたの Model::Draw と同じ）
	cmd->DrawIndexedInstanced(mesh.indexCount, 1, mesh.startIndex, 0, 0);
}

bool Model::IsMeshSkinned(uint32_t meshIndex) const {
	if (meshIndex >= modelData_.meshes.size()) return false;
	return modelData_.meshes[meshIndex].skinned;
}

void Model::BuildNodeRuntime_()
{
	OutputDebugStringA("[DBG] BuildNodeRuntime_ CALLED\n");

	nodePtrs_.clear();
	parentIndex_.clear();
	nodeNameToIndex_.clear();
	nodeInstances_.clear();
	meshOwnerNodeIndex_.clear();
	nodeRuntime_.nodes.clear();

	meshOwnerNodeIndex_.assign(modelData_.meshes.size(), -1);

	TraverseNode_(&modelData_.rootNode, -1);

	char buf[256];
	std::snprintf(buf, sizeof(buf),
		"[NodeRuntime] nodes=%zu instances=%zu\n",
		nodePtrs_.size(), nodeInstances_.size());
	OutputDebugStringA(buf);
}


void Model::TraverseNode_(const Node* n, int32_t parent)
{
	if (!n) { return; }

	const int32_t myIndex = (int32_t)nodePtrs_.size();

	char dbg[256];
	std::snprintf(dbg, sizeof(dbg),
		"[TraverseNode] node='%s' meshes=%zu\n",
		n->name.c_str(),
		n->meshIndices.size());
	OutputDebugStringA(dbg);

	nodePtrs_.push_back(n);
	parentIndex_.push_back(parent);

	// runtime node
	NodeRuntime::FlatNode fn;
	fn.name = n->name;
	fn.transform = n->transform;
	fn.localMatrix = n->localMatrix;
	fn.parent = parent;
	nodeRuntime_.nodes.push_back(fn);

	// name -> index
	if (!fn.name.empty()) {
		nodeNameToIndex_[fn.name] = (uint32_t)myIndex;
	}

	// ★ここが重要：このノードが持つ mesh を NodeInstance として登録
	for (uint32_t meshIdx : n->meshIndices) {
		NodeInstance inst{};
		inst.nodeIndex = (uint32_t)myIndex;
		inst.meshIndex = meshIdx;
		nodeInstances_.push_back(inst);

		if (meshIdx < meshOwnerNodeIndex_.size()) {
			meshOwnerNodeIndex_[meshIdx] = myIndex;
		}
	}

	// children
	for (const auto& c : n->children) {
		TraverseNode_(&c, myIndex);
	}
}

Matrix4x4 Model::GetNodeWorldMatrix(int nodeIndex) const
{
	if (nodeIndex < 0 || nodeIndex >= (int)nodeRuntime_.nodes.size()) {
		return Matrix4x4::MakeIdentity4x4();
	}

	Matrix4x4 world = Matrix4x4::MakeIdentity4x4();

	int idx = nodeIndex;
	while (idx >= 0) {
		const auto& n = nodeRuntime_.nodes[idx];
		world = Matrix4x4::Multiply(world, n.localMatrix);
		idx = n.parent;
	}

	return world;
}

Vector3 Model::GetVertexPosition(uint32_t index) const
{
	if (vertexData_ && index < GetVertexCount()) {
		return { vertexData_[index].position.x, vertexData_[index].position.y, vertexData_[index].position.z };
	}
	return { 0.0f, 0.0f, 0.0f };
}

Vector3 Model::GetSourceVertexPosition(uint32_t index) const
{
	for (const auto& mesh : modelData_.meshes) {
		if (index >= mesh.startVertex && index < mesh.startVertex + mesh.vertices.size()) {
			const Vector4& position = mesh.vertices[index - mesh.startVertex].position;
			return { position.x, position.y, position.z };
		}
	}
	return { 0.0f, 0.0f, 0.0f };
}

void Model::UpdateVertexPosition(uint32_t index, const Vector3& position)
{
	if (vertexData_ && index < GetVertexCount()) {
		vertexData_[index].position = { position.x, position.y, position.z, 1.0f };
	}
}

