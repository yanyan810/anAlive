#include "EnemySpawnSystem.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>
#include <stdexcept>

bool EnemySpawnSystem::Load(const std::string& path, const EnemyDefinitions& definitions) {
    try {
        std::ifstream file(path);
        if (!file) throw std::runtime_error("Cannot open " + path);
        nlohmann::json data;
        file >> data;
        const auto vector = [](const nlohmann::json& value) {
            if (!value.is_array() || value.size() != 3) throw std::runtime_error("Expected a 3-component vector");
            Vector3 result{value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
            if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z))
                throw std::runtime_error("Vector must be finite");
            return result;
        };
        const auto integer = [](const nlohmann::json& value) {
            if (!value.is_number_integer()) throw std::runtime_error("Counts must be integers");
            const double number = value.get<double>();
            if (number < 1 || number > 10000) throw std::runtime_error("Counts must be in [1, 10000]");
            return static_cast<int>(number);
        };
        std::vector<EnemySpawnPoint> points;
        std::vector<EnemySpawnTrigger> triggers;
        std::vector<SpawnGroup> groups;
        std::set<std::string> pointIds, triggerIds;
        const auto triggerData = data.value("spawnTriggers", nlohmann::json::array());
        if (!data.at("spawnPoints").is_array() || !triggerData.is_array())
            throw std::runtime_error("spawnPoints and spawnTriggers must be arrays");
        for (const auto& item : data.at("spawnPoints")) {
            EnemySpawnPoint point;
            point.id = item.at("id").get<std::string>();
            if (point.id.empty() || !pointIds.insert(point.id).second) throw std::runtime_error("Empty/duplicate point ID: " + point.id);
            point.position = vector(item.at("position"));
            if (item.contains("rotation")) point.rotation = vector(item.at("rotation"));
            if (item.contains("enemyPool")) {
                const auto& pool=item.at("enemyPool");
                if (!pool.is_array()) throw std::runtime_error("enemyPool must be an array");
                double total=0;
                for (const auto& entry : pool) {
                    EnemyPoolEntry value{entry.at("id").get<std::string>(),entry.at("weight").get<double>()};
                    if (!definitions.Find(value.id)) throw std::runtime_error("Unknown enemy id: " + value.id);
                    if (!std::isfinite(value.weight) || value.weight<0) throw std::runtime_error("Invalid enemy weight");
                    total+=value.weight; point.enemyPool.push_back(value);
                }
                if (!std::isfinite(total) || total<=0) throw std::runtime_error("enemyPool requires positive total weight");
            }
            points.push_back(std::move(point));
        }
        for (const auto& item : triggerData) {
            EnemySpawnTrigger trigger;
            trigger.id = item.at("id").get<std::string>();
            if (trigger.id.empty() || !triggerIds.insert(trigger.id).second) throw std::runtime_error("Empty/duplicate trigger ID: " + trigger.id);
            trigger.position = vector(item.at("position"));
            trigger.size = vector(item.at("size"));
            if (trigger.size.x <= 0 || trigger.size.y <= 0 || trigger.size.z <= 0) throw std::runtime_error("Invalid size: " + trigger.id);
            trigger.spawnPointIds = item.at("spawnPointIds").get<std::vector<std::string>>();
            if (trigger.spawnPointIds.empty()) throw std::runtime_error("No spawn points: " + trigger.id);
            for (const auto& id : trigger.spawnPointIds)
                if (!pointIds.count(id)) throw std::runtime_error("Unknown spawn point: " + id);
            trigger.spawnCount = integer(item.at("spawnCount"));
            trigger.maxAlive = item.contains("maxAlive") ? integer(item.at("maxAlive")) : trigger.spawnCount;
            trigger.spawnInterval = item.value("spawnInterval", 0.0);
            trigger.initialDelay = item.value("initialDelay", 0.0);
            if (!std::isfinite(trigger.spawnInterval) || !std::isfinite(trigger.initialDelay) || trigger.spawnInterval < 0 || trigger.initialDelay < 0)
                throw std::runtime_error("Invalid timing: " + trigger.id);
            const auto selection = item.value("selection", std::string("Random"));
            if (selection == "RoundRobin" || selection == "Sequential") trigger.selection = SpawnPointSelection::RoundRobin;
            else if (selection != "Random") throw std::runtime_error("Unknown selection: " + selection);
            trigger.oneShot = item.value("oneShot", true);
            triggers.push_back(std::move(trigger));
        }
        if (data.contains("spawnGroups")) {
            const auto& groupData = data.at("spawnGroups");
            if (!groupData.is_array()) throw std::runtime_error("spawnGroups must be an array");
            for (const auto& item : groupData) {
                SpawnGroup group;
                group.id = item.value("id", "SpawnGroup_" + std::to_string(groups.size() + 1));
                if (group.id.empty() || pointIds.count(group.id) || !triggerIds.insert(group.id).second)
                    throw std::runtime_error("Empty/duplicate spawn group ID: " + group.id);
                const auto mode = item.value("mode", std::string("Sequential"));
                if (mode == "Simultaneous") group.mode = SpawnMode::Simultaneous;
                else if (mode != "Sequential") throw std::runtime_error("Unknown spawn mode: " + mode);
                group.startTime = item.value("time", 0.0);
                group.interval = item.value("interval", .5);
                if (!std::isfinite(group.startTime) || group.startTime < 0 || !std::isfinite(group.interval) || group.interval < 0)
                    throw std::runtime_error("Invalid group timing: " + group.id);
                const auto& entries = item.at("enemies");
                if (!entries.is_array() || entries.empty() || entries.size() > 10000)
                    throw std::runtime_error("SpawnGroup requires 1 to 10000 enemies: " + group.id);
                for (const auto& entry : entries) {
                    SpawnGroupEnemy member{entry.at("enemy").get<std::string>(), entry.at("spawnPoint").get<std::string>()};
                    if (!definitions.Find(member.enemyId)) throw std::runtime_error("Unknown enemy id: " + member.enemyId);
                    if (!pointIds.count(member.spawnPointId)) throw std::runtime_error("Unknown spawn point: " + member.spawnPointId);
                    group.enemies.push_back(std::move(member));
                }
                if (group.mode == SpawnMode::Sequential && !std::isfinite(group.startTime + group.interval * (group.enemies.size() - 1)))
                    throw std::runtime_error("Group schedule overflow: " + group.id);
                groups.push_back(std::move(group));
            }
        }
        auto nextRandom=random_;
        if (data.contains("enemyRandom")) {
            const auto& config=data.at("enemyRandom");
            if (config.value("useFixedSeed",false)) {
                const auto& seed=config.at("seed");
                if (!seed.is_number_integer() || seed.get<double>()<0 || seed.get<double>()>4294967295.0)
                    throw std::runtime_error("Invalid enemy seed");
                nextRandom.seed(seed.get<uint32_t>());
            }
        }
        random_=nextRandom;
        initialRandom_=nextRandom;
        points_ = std::move(points);
        triggers_ = std::move(triggers);
        groups_ = std::move(groups);
        elapsedTime_ = 0;
        error_.clear();
        return true;
    } catch (const std::exception& exception) {
        error_ = exception.what();
        return false;
    }
}
