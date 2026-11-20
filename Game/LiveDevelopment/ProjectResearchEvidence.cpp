#include "ProjectResearch.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace ProjectResearch {
namespace {
std::vector<std::string> Tokens(std::string_view source) {
    std::vector<std::string> tokens;
    const auto word = [](unsigned char c) { return std::isalnum(c) || c == '_'; };
    while (!source.empty()) {
        size_t length = 1;
        const auto ch = source.front();
        if (std::isspace(static_cast<unsigned char>(ch))) {
            source.remove_prefix(1);
            continue;
        }
        if (source.starts_with("//") || source.starts_with("/*")) {
            const auto end = source.starts_with("//") ? source.find('\n') : source.find("*/", 2);
            source.remove_prefix(end == std::string_view::npos ? source.size() : end + (source.starts_with("/*") ? 2 : 0));
            continue;
        }
        if (ch == '"' || ch == '\'') {
            while (length < source.size()) {
                const char next = source[length++];
                if (next == ch) break;
                if (next == '\\' && length < source.size()) ++length;
            }
        } else if (word(static_cast<unsigned char>(ch))) {
            while (length < source.size() && word(static_cast<unsigned char>(source[length]))) ++length;
        } else if (source.size() >= 2) {
            const auto pair = source.substr(0, 2);
            if (pair == "&&" || pair == "||" || pair == "<=" || pair == ">=" || pair == "==" ||
                pair == "!=" || pair == "->" || pair == "::") length = 2;
        }
        tokens.emplace_back(source.substr(0, length));
        source.remove_prefix(length);
    }
    return tokens;
}

struct Check {
    const char* path;
    const char* marker;
    int lines;
    std::initializer_list<const char*> patterns;
};

bool Matches(const Evidence& evidence, const Check& check) {
    std::istringstream rows(evidence.excerpt);
    std::string source;
    for (std::string line; std::getline(rows, line);) {
        const auto prefix = line.find(" | ");
        if (prefix == std::string::npos || line.ends_with(" …")) return false;
        source += line.substr(prefix + 3) + '\n';
    }
    const auto actual = Tokens(source);
    return std::ranges::all_of(check.patterns, [&](const char* pattern) {
        const auto expected = Tokens(pattern);
        return std::search(actual.begin(), actual.end(), expected.begin(), expected.end()) != actual.end();
    });
}

constexpr auto kAi = "Game/enemy/EnemyAI.h";
constexpr auto kEnemy = "Game/enemy/Enemy.cpp";
}

std::string EvidenceTopic(std::string_view question) {
    const auto has = [&](std::string_view text) { return question.find(text) != std::string_view::npos; };
    if ((has("壁") || has("遮蔽")) && (has("発見") || has("視認") || has("認識") || has("考慮"))) return "sight";
    if (has("部位") && (has("死亡") || has("破壊"))) return "death";
    if (has("ダメージ") && (has("武器") || has("HP") || has("ApplyDamage"))) return "damage";
    if (has("EnemyAI") && has("更新") && (has("呼ばれ") || has("呼び出"))) return "update";
    if ((has("Spawn Group") || has("spawnGroups")) && has("出現")) return "spawn";
    if (has("Lua") && has("反映") && has("実行中")) return "lua";
    return {};
}

bool InvestigateEvidence(const SourceReader& source, const Limits& limits, std::string_view topic, Turn& turn, const Report& report) {
    if (topic.empty()) return false;
    turn.topic = topic;
    const auto check = [&](const Check& rule) -> std::optional<size_t> {
        if (turn.progress.step >= limits.steps) { turn.limitReached = true; return {}; }
        ++turn.progress.step;
        turn.progress.path = rule.path;
        turn.progress.activity = "コードの条件と呼び出しを確認中…";
        report(turn.progress);
        auto evidence = source.FindRange(rule.path, rule.marker, std::min(rule.lines, limits.lines));
        const bool matched = evidence && Matches(*evidence, rule);
        turn.progress.operations.push_back(std::string("read_file ") + rule.path + (matched ? " → 条件一致" : " → 未確認"));
        if (!evidence) return {};
        turn.references.push_back(std::move(*evidence));
        return matched ? std::optional<size_t>(turn.references.size() - 1) : std::nullopt;
    };
    const auto claim = [&](const char* verified, const char* unknown, std::initializer_list<std::optional<size_t>> refs) {
        Claim line;
        line.verified = std::ranges::all_of(refs, [](const auto& ref) { return ref.has_value(); });
        line.text = line.verified ? verified : unknown;
        for (const auto& ref : refs) if (ref) line.references.push_back(*ref);
        turn.claims.push_back(std::move(line));
    };
    if (topic == "sight") {
        const auto sight = check({kAi, "visible=false; occluded=true;", 16, {
            "visible=false; occluded=true; const Vector3 samples[]={target+Vector3{0,playerEyeHeight,0},target+Vector3{0,playerEyeHeight*.6f,0}}; sightTarget=samples[0]; for(const auto&sample:samples){ StageHit hit; const bool blocked=world&&world->Raycast(eye,sample-eye,StageLength(sample-eye),hit,0,true); if(!blocked){occluded=false;sightTarget=sample;} if(!blocked&&inFov&&StageLength(sample-eye)<=senses.viewDistance){visible=true;sightTarget=sample;break;} }",
            "detection=visible?std::min(senses.detectionTime,detection+dt):0; const bool recognized=visible&&(hasLastSeen||detection>=senses.detectionTime); if(recognized&&!pursuitOrdered)OrderPursuit(target); const bool tracking=recognized||pursuitOrdered;"}});
        const auto world = check({"Game/StageWorld.h", "bool Raycast(const Vector3& origin", 20, {
            "for(const auto&c:colliders){if(c.playerOnly)continue;if(sight&&!c.blocksSight)continue;",
            "if(CastBox(c,origin,unit,range,padding,candidate)){found=true;hit=candidate;range=hit.distance;} } return found;"}});
        const auto caller = check({kEnemy, "attackDamage=ai_.Update(", 10, {"ai_.Update(position_,rotation_,playerPosition,dt,dead,world,", "return attackDamage;"}});
        claim("初回発見では、視界を遮る壁で両方の視認サンプルが遮られると認識できません。Enemy::Update から渡されたワールドの Raycast が、視界を遮るコライダーとの交差を判定します。",
            "ワールドの衝突判定から初回発見までの接続を確認できませんでした。", {sight, world, caller});
        claim("遮蔽なし・視野内・視認距離内を満たすサンプルがなければ視認は失敗します。視認できても、初回で検知時間に達していなければ認識は成立しません。既に出ている追跡指示は tracking を継続させます。",
            "視認・認識・追跡を分ける条件を確認できませんでした。", {sight});
    } else if (topic == "death") {
        const auto death = check({kAi, "inline bool EnemyPartsDead(", 7, {
            "inline bool EnemyPartsDead(const EnemyParts&parts){ if(parts.deathProcessed)return true; for(const auto&group:parts.hpGroups)if(group.deathOnZero&&group.hp<=0)return true; for(const auto&part:parts)if(part.deathOnZero&&part.usesLocalHp&&part.hp<=0)return true; return false; }"}});
        const auto apply = check({kEnemy, "float Enemy::ApplyDamage(size_t", 8, {
            "if(IsDead())return 0; const float lost=DamageEnemyPart(parts_,index,damage); if(lost<=0)return lost; if(EnemyPartsDead(parts_))Die(shotDirection);"}});
        claim("死亡条件を満たすHPグループか部位が1つでもあれば、その時点で死亡判定が成立します。グループは deathOnZero が有効でHPが0以下、部位はそれに加えて usesLocalHp が有効であることが条件です。死亡処理済みの場合も成立します。",
            "部位とHPグループの死亡条件を確認できませんでした。", {death});
        claim("敵が生存中にダメージでHPを失い、EnemyPartsDead が true になると ApplyDamage が Die を呼びます。",
            "死亡判定から Die までの呼び出しを確認できませんでした。", {death, apply});
    } else if (topic == "damage") {
        const auto json = check({"resources/Data/weapons.json", "\"initialWeapon\"", 22, {"\"weapons\":[{", "\"damage\":"}});
        const auto scene = check({"Game/scene/Main/GameScene.cpp", "nextWeapons.Load(\"resources/Data/weapons.json\"", 7, {
            "nextWeapons.Load(\"resources/Data/weapons.json\",levelPath,weaponSeedOverride_,stageDraft)", "weapons_=std::move(nextWeapons);"}});
        const auto definition = check({"Game/WeaponSystem.cpp", "bool WeaponSystem::LoadDocuments(", 20, {
            "for(const auto&item:source.at(\"weapons\")){WeaponDefinition definition;", "definition.damage=Number(item.at(\"damage\"),.001f,100000);"}});
        const auto bullet = check({"Game/Bullet.cpp", "size_t spawned = 0;", 15, {
            "for(int pellet=0;pellet<weapon.pelletCount;++pellet){Bullet bullet;", "bullet.damage=weapon.damage; spawned+=Spawn(bullet)?1:0;"}});
        const auto impact = check({"Game/BulletManager.cpp", "void BulletManager::Spawn(", 25, {
            "simulation_.SpawnShot(weapon,cameraWorld,adsBlend,random,MakeTrace(world,enemies,raycastTarget));",
            "if(hit.wall)return;auto&enemy=*enemies[hit.enemyIndex];auto result=enemy.ApplyBulletDamage(hit.partIndex,bullet.damage,bullet.direction);"}});
        const auto wrapper = check({kEnemy, "EnemyBulletHitResult Enemy::ApplyBulletDamage(size_t", 12, {
            "if(IsDead()||part>=parts_.size()||parts_[part].Destroyed())return{};", "result.damage=ApplyDamage(part,damage,direction);"}});
        const auto apply = check({kEnemy, "float Enemy::ApplyDamage(size_t", 8, {
            "const float lost=DamageEnemyPart(parts_,index,damage); if(lost<=0)return lost;"}});
        const auto hp = check({"Game/enemy/EnemyParts.h", "inline float ReduceEnemyHp(", 19, {
            "hp=std::max(0.0f,before-damage);", "if(part.usesLocalHp)localLost=ReduceEnemyHp(part.hp,damage);",
            "if(part.sharedGroup<parts.hpGroups.size()){auto&group=parts.hpGroups[part.sharedGroup];sharedLost=ReduceEnemyHp(group.hp,damage*part.sharedDamageRate);}", "return std::max(localLost,sharedLost);"}});
        claim("武器定義JSONの damage が WeaponDefinition.damage に読み込まれ、弾を生成するときに各ペレットへコピーされます。",
            "武器定義の damage から各ペレットまでの値の経路を確認できませんでした。", {json, scene, definition, bullet});
        claim("弾が敵に当たると BulletManager が部位と弾のダメージを ApplyBulletDamage に渡します。ApplyDamage を経由して DamageEnemyPart が部位HPを減らし、共有HPには sharedDamageRate を掛けた値を適用します。HPの下限は0です。",
            "弾の命中から部位HP・共有HPの減算までの全経路を確認できませんでした。", {impact, wrapper, apply, hp});
    } else if (topic == "update") {
        const auto definition = check({kAi, "float Update(Vector3& position", 10, {"float Update(Vector3&position,Vector3&rotation,const Vector3&target,float dt,bool dead,"}});
        const auto caller = check({kEnemy, "attackDamage=ai_.Update(", 10, {"ai_.Update(position_,rotation_,playerPosition,dt,dead,world,", "return attackDamage;"}});
        claim("EnemyAI::Update は EnemyAI.h に定義され、Enemy::Update から位置・回転・プレイヤー位置・ワールドを渡して呼ばれます。",
            "EnemyAI::Update の定義と呼び出し元を確認できませんでした。", {definition, caller});
    } else if (topic == "spawn") {
        constexpr auto spawn = "Game/enemy/EnemySpawnSystem.h";
        const auto config = check({"Game/enemy/EnemySpawnSystem.cpp", "if (data.contains(\"spawnGroups\"))", 24, {
            "if(mode==\"Simultaneous\")group.mode=SpawnMode::Simultaneous;", "group.startTime=item.value(\"time\",0.0); group.interval=item.value(\"interval\",.5);",
            "SpawnGroupEnemy member{entry.at(\"enemy\").get<std::string>(),entry.at(\"spawnPoint\").get<std::string>()};"}});
        const auto group = check({spawn, "for (auto& group : groups_) {", 13, {
            "while(group.spawned<group.enemies.size()){const double due=group.startTime+(group.mode==SpawnMode::Sequential?group.interval*group.spawned:0); if(elapsedTime_+1e-7<due)break; const auto&entry=group.enemies[group.spawned]; spawnResolved(*FindPoint(entry.spawnPointId),group.id,entry.enemyId); ++group.spawned;}"}});
        const auto points = check({spawn, "// Stage Start, trigger groups and external events", 20, {
            "if(!point.active)continue;const double interval=point.mode==SpawnMode::Sequential?point.spawnInterval:0; while(point.spawned<point.spawnCount&&point.nextSpawn<=elapsedTime_+1e-7){if(living.size()>=static_cast<size_t>(point.maxAlive)){point.nextSpawn=elapsedTime_;break;}"}});
        const auto level = check({"resources/levels/stage03/stage03.json", "\"spawnGroups\"", 2, {"\"spawnGroups\":[],"}});
        claim("spawnGroups のメンバーごとに敵と出現地点を指定します。Simultaneous は全員の期限が共通の time で、Sequential は interval を出現済み人数に掛けて期限をずらします。",
            "グループ設定から実際の同時・逐次スケジュールまでを確認できませんでした。", {config, group});
        claim("通常の出現地点では、地点ごとの active・spawnCount・nextSpawn・maxAlive で出現を制御します。Stage 03 の設定では spawnGroups が空です。",
            "通常地点の制御条件または Stage 03 の設定を確認できませんでした。", {points, level});
    } else if (topic == "lua") {
        constexpr auto live = "Game/scene/Debug/EnemyAITestLiveDevelopment.cpp";
        const auto request = check({live, "} else if (liveDiagnostics_.HasPending()", 16, {
            "auto lua=std::exchange(luaRequest_,std::nullopt);", "if(lua)changes.push_back({{\"resource\",\"enemy.lua\"}", "PrepareChanges(operation,replay,prepared);"}});
        const auto validate = check({live, "if (after.at(\"apiVersion\") != 1)", 18, {
            "auto policy=std::make_unique<LiveLua>(source);", "RunLuaPolicy(*policy,PolicyFields(id,type,100,100,\"Idle\",10,1),budget);",
            "prepared.lua=compile(after.at(\"source\").get_ref<const std::string&>(),\"probe\",\"Normal\");"}});
        const auto apply = check({live, "const bool applied = liveDiagnostics_.ApplyPending", 5, {
            "liveDiagnostics_.ApplyPending(MakeDiagnosticsInput(),std::move(details),std::move(changes),prepare,kind); if(applied)CommitChanges(std::move(prepared));"}});
        const auto install = check({live, "void EnemyAITestScene::CommitChanges(", 17, {
            "if(prepared.replaceLua){lua_=std::move(prepared.lua);luaOverrides_.swap(prepared.overrides);liveScript_.swap(prepared.script);"}});
        const auto evaluate = check({live, "void EnemyAITestScene::EvaluateLuaPolicies()", 42, {
            "speeds[i]=RunLuaPolicy(*lua_,fields,budget);", "luaSpeeds_.swap(speeds);luaSpeedIds_.swap(ids);"}});
        const auto use = check({"Game/scene/Debug/EnemyAITestScene.cpp", "if (!pauseAll_ && dt > 0) EvaluateLuaPolicies();", 24, {
            "if(dt>0&&i<luaSpeeds_.size())speedMultiplier=luaSpeeds_[i];",
            "e->Update(dt,player,&world_,player_.Settings().cameraHeight,std::numeric_limits<int>::max(),speedMultiplier);"}});
        claim("敵AIテストの更新時にLua要求を取り出し、候補VMを作って試行します。ApplyPending が成功したときに CommitChanges がポリシーを差し替えます。",
            "Lua要求の検証から差し替えまでを確認できませんでした。", {request, validate, apply, install});
        claim("AI停止中を除き、EvaluateLuaPolicies が返した速度倍率が次の敵更新に渡されます。",
            "Luaの試行結果から敵更新までを確認できませんでした。", {evaluate, use});
    } else return false;
    for (const auto& ref : turn.references) {
        if (!source.StillCurrent(ref)) throw std::runtime_error("根拠ファイルが更新されました。調査をやり直してください。");
    }
    for (const auto& item : turn.claims) {
        if (!turn.answer.empty()) turn.answer += '\n';
        if (!item.verified) turn.answer += "未確認　";
        turn.answer += item.text;
    }
    return true;
}
}
