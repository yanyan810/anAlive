#include "EnemySpawnSystem.h"
#include <nlohmann/json.hpp>
#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>

using nlohmann::json;
static json Configuration() {
    return json::parse(R"({
      "spawnPoints": [
        {"id":"A","position":[1,0,0],"rotation":[0,1,0]},
        {"id":"B","position":[2,0,0]},
        {"id":"C","position":[3,0,0]}],
      "spawnTriggers":[{"id":"T","position":[0,0,0],"size":[2,2,2],
        "spawnPointIds":["A","B","C"],"spawnCount":8,"maxAlive":4,
        "spawnInterval":0.5,"initialDelay":0,"selection":"RoundRobin","oneShot":true}]
    })");
}
struct Harness {
    EnemySpawnSystem system;
    EnemyDefinitions definitions;
    Harness() { assert(definitions.Load("../../resources/Data/enemies.json")); }
    std::set<uint64_t> alive;
    std::vector<std::string> points;
    uint64_t next = 0;
    size_t frame = 0;
    struct Event { size_t frame; std::string enemy, source; Vector3 position, rotation; };
    std::vector<Event> events;
    void Load(const json& config) {
        { std::ofstream file("spawn-test.json"); file << config; }
        assert(system.Load("spawn-test.json", definitions));
    }
    void Tick(float dt, Vector3 position = {}) {
        ++frame;
        system.Update(dt, position, [&](const EnemySpawnPoint& point, const std::string& trigger, const std::string& enemyId) {
            assert(!trigger.empty());
            points.push_back(point.id);
            events.push_back({frame, enemyId.empty() ? system.SelectEnemyId(point) : enemyId, trigger, point.position, point.rotation});
            alive.insert(next);
            return next++;
        }, [&](uint64_t id) { return alive.count(id) != 0; });
    }
};
static json GroupConfiguration(const char* mode = "Sequential") {
    auto data = Configuration();
    data.erase("spawnTriggers"); // schedule-only maps do not need player-entry triggers
    data["spawnGroups"] = json::array({{{"time", 3.0}, {"mode", mode}, {"interval", .5},
        {"enemies", json::array({{{"enemy", "normal"}, {"spawnPoint", "A"}},
            {{"enemy", "fast"}, {"spawnPoint", "B"}}, {{"enemy", "tank"}, {"spawnPoint", "C"}}})}}});
    return data;
}
int main() {
    {
        Harness h; h.Load(Configuration());
        h.Tick(10, {5,0,0}); assert(h.next == 0);
        h.Tick(10); assert(h.next == 1); // activate now, not ten seconds ago
        h.Tick(.25f); assert(h.next == 1);
        h.Tick(.25f); assert(h.next == 2);
        h.Tick(1); assert(h.next == 4);
        h.Tick(100); assert(h.next == 4);
        h.alive.erase(0); h.Tick(0); assert(h.next == 5 && h.alive.size() == 4);
        h.alive.erase(1); h.Tick(.25f); assert(h.next == 5);
        h.Tick(.25f); assert(h.next == 6);
        h.alive.clear(); h.Tick(1); assert(h.next == 8);
        assert(!h.system.Triggers()[0].active);
        assert((h.points == std::vector<std::string>{"A","B","C","A","B","C","A","B"}));
        h.Tick(0, {5,0,0}); h.Tick(10); assert(h.next == 8);
    }
    {
        auto config = Configuration(); config["spawnTriggers"][0]["initialDelay"] = 2;
        Harness h; h.Load(config); h.Tick(1); assert(h.next == 0);
        h.Tick(1.5f); assert(h.next == 0); h.Tick(.5f); assert(h.next == 1);
        h.Tick(-10); assert(h.next == 1);
        h.Tick(.5f, {100,0,0}); assert(h.next == 2); // leaving does not cancel
    }
    {
        auto config = Configuration(); auto& t = config["spawnTriggers"][0];
        t["spawnCount"] = 2; t["maxAlive"] = 3; t["spawnInterval"] = 0; t["oneShot"] = false;
        Harness h; h.Load(config); h.Tick(0); assert(h.next == 2);
        h.Tick(100); assert(h.next == 2); // no repeated spawning while remaining inside
        h.Tick(0, {5,0,0}); h.Tick(0); assert(h.next == 3); // previous living enemies count
        h.Tick(100); assert(h.next == 3);
        h.alive.erase(0); h.Tick(0); assert(h.next == 4);
    }
    {
        auto config = Configuration(); auto& t = config["spawnTriggers"][0];
        t["selection"] = "Random"; t["spawnInterval"] = 0; t["maxAlive"] = 8;
        Harness h; h.Load(config); h.Tick(0); assert(h.next == 8);
        for (const auto& id : h.points) assert(id == "A" || id == "B" || id == "C");
    }
    {
        auto config = Configuration(); config["spawnTriggers"].push_back(config["spawnTriggers"][0]);
        config["spawnTriggers"][1]["id"] = "U";
        Harness h; h.Load(config); h.Tick(0); h.Tick(10); assert(h.next == 8);
        assert(h.system.Triggers()[0].livingEnemies.size() == 4);
        assert(h.system.Triggers()[1].livingEnemies.size() == 4);
    }
    {
        Harness h; h.Load(Configuration());
        const auto reject = [&](json config) {
            { std::ofstream file("spawn-test.json"); file << config; }
            assert(!h.system.Load("spawn-test.json", h.definitions));
            assert(!h.system.Error().empty());
            assert(h.system.Points().size() == 3); // previous valid map retained
        };
        auto c = Configuration(); c["spawnTriggers"][0]["spawnPointIds"] = json::array(); reject(c);
        c = Configuration(); c["spawnTriggers"][0]["spawnPointIds"] = {"missing"}; reject(c);
        c = Configuration(); c["spawnTriggers"][0]["maxAlive"] = 0; reject(c);
        c = Configuration(); c["spawnTriggers"][0]["spawnCount"] = 1.5; reject(c);
        c = Configuration(); c["spawnTriggers"][0]["spawnInterval"] = -1; reject(c);
        c = Configuration(); c["spawnTriggers"][0]["initialDelay"] = -1; reject(c);
        c = Configuration(); c["spawnTriggers"][0]["selection"] = "typo"; reject(c);
        c = Configuration(); c["spawnTriggers"][0]["size"] = {1,0,1}; reject(c);
        c = Configuration(); c["spawnPoints"][1]["id"] = "A"; reject(c);
        c = Configuration(); c["spawnTriggers"].push_back(c["spawnTriggers"][0]); reject(c);
        assert(!h.system.Load("nonexistent-spawn-file.json"));
    }
    {
        Harness h;
        EnemyDefinitions definitions; assert(definitions.Load("../../resources/Data/enemies.json"));
        assert(h.system.Load("../../resources/levels/fps_spawns.json", definitions));
        assert(h.system.Points().size() == 9 && h.system.Triggers().size() == 2);
        h.Tick(0, {3,0,-6}); assert(h.next == 0);
        h.Tick(0, {3,0,2}); h.Tick(1, {3,0,4}); assert(h.next == 3);
        h.Tick(1, {3,0,4}); assert(h.next == 5);
        const std::vector<std::string> expected{"normal","ranged","fast","tank","bomber"};
        for (size_t i=0;i<expected.size();++i)
            assert(h.system.SelectEnemyId(*h.system.FindPoint(h.points[i]))==expected[i]);
        h.Tick(20, {3,0,4}); assert(h.next == 5);
        h.alive.clear(); h.Tick(0, {3,0,4}); h.Tick(.5f, {3,0,4}); assert(h.next == 5);
        h.Tick(0, {3,0,24}); assert(h.next == 5);
        h.Tick(2, {3,0,26}); assert(h.next == 6);
        h.Tick(1.5f, {3,0,26}); assert(h.next == 9);
        h.Tick(20, {3,0,26}); assert(h.next == 9);
        h.alive.clear(); h.Tick(0, {3,0,26}); h.Tick(1.5f, {3,0,26}); assert(h.next == 13);
        assert(h.system.Triggers()[0].spawned == 5 && h.system.Triggers()[1].spawned == 8);
        assert(!h.system.Triggers()[0].active && !h.system.Triggers()[1].active);
    }
    {
        Harness h; h.Load(GroupConfiguration());
        h.Tick(2.75f, {100,0,0}); assert(h.next == 0);
        h.Tick(.25f, {100,0,0}); assert(h.next == 1);
        h.Tick(.25f); assert(h.next == 1); h.Tick(.25f); assert(h.next == 2);
        h.Tick(.5f); assert(h.next == 3);
        assert(h.system.Groups()[0].mode == SpawnMode::Sequential);
        assert((h.points == std::vector<std::string>{"A","B","C"}));
        assert(h.events[0].enemy == "normal" && h.events[1].enemy == "fast" && h.events[2].enemy == "tank");
        assert(h.events[0].position.x == 1 && h.events[1].position.x == 2 && h.events[2].position.x == 3);
        assert(h.events[0].rotation.y == 1 && h.events[1].rotation.y == 0);
        h.Tick(100); assert(h.next == 3); // schedule fires once, irrespective of death or player entry
        h.alive.clear(); h.system.Reset();
        assert(h.system.ElapsedTime() == 0 && h.system.Groups()[0].spawned == 0);
        h.Tick(2.5f); assert(h.next == 3); h.Tick(.5f); assert(h.next == 4);
        h.Tick(1); assert(h.next == 6); // low frame rate catches up all due members
        h.Load(GroupConfiguration()); assert(h.system.ElapsedTime() == 0);
        h.Tick(3); assert(h.next == 7); // loading/restarting resets schedules
    }
    {
        auto data = GroupConfiguration("Simultaneous"); data["spawnGroups"][0]["interval"] = 100;
        Harness h; h.Load(data); h.Tick(2.5f); assert(h.next == 0);
        h.Tick(-10); h.Tick(std::numeric_limits<float>::quiet_NaN()); h.Tick(std::numeric_limits<float>::infinity());
        assert(h.system.ElapsedTime() == 2.5 && h.next == 0);
        h.Tick(.5f); assert(h.next == 3);
        for (const auto& event : h.events) assert(event.frame == h.frame && event.source == "SpawnGroup_1");
        h.Tick(100); assert(h.next == 3);
        h.system.Reset(); h.Tick(3); assert(h.next == 6);
        for (size_t i=3; i<6; ++i) assert(h.events[i].frame == h.frame);
    }
    {
        auto data = GroupConfiguration(); data["spawnGroups"][0].erase("mode");
        data["spawnGroups"][0]["time"] = 0;
        Harness h; h.Load(data); h.Tick(0); assert(h.next == 1);
        h.Tick(.5f); assert(h.next == 2); h.Tick(.5f); assert(h.next == 3);
        data["spawnGroups"][0]["interval"] = 0;
        h.Load(data); h.Tick(0); assert(h.next == 6);
    }
    {
        auto data = GroupConfiguration("Simultaneous");
        data["spawnTriggers"] = Configuration()["spawnTriggers"];
        auto other = GroupConfiguration()["spawnGroups"][0]; other["id"] = "SequentialGroup";
        data["spawnGroups"].push_back(other);
        Harness h; h.Load(data); h.Tick(0); assert(h.next == 1);
        h.Tick(3); assert(h.next == 8); // both group modes and original maxAlive behavior coexist
        assert(h.system.Triggers()[0].spawned == 4 && h.system.Groups()[0].spawned == 3 && h.system.Groups()[1].spawned == 1);
        h.system.Reset(); assert(!h.system.Triggers()[0].activated && h.system.Triggers()[0].livingEnemies.empty());
        h.alive.clear(); h.Tick(0); assert(h.next == 9);
    }
    {
        // Existing two-argument callbacks also see the explicit member definition.
        Harness h; h.Load(GroupConfiguration("Simultaneous"));
        std::vector<std::string> selected;
        h.system.Update(3, {}, [&](const EnemySpawnPoint& point, const std::string&) {
            selected.push_back(h.system.SelectEnemyId(point)); return uint64_t{0};
        }, [](uint64_t) { return true; });
        assert((selected == std::vector<std::string>{"normal", "fast", "tank"}));
    }
    {
        Harness h; h.Load(GroupConfiguration()); h.Tick(3);
        const auto reject = [&](const json& bad) {
            { std::ofstream file("spawn-test.json"); file << bad; }
            assert(!h.system.Load("spawn-test.json", h.definitions));
            assert(!h.system.Error().empty() && h.system.ElapsedTime() == 3 && h.system.Groups()[0].spawned == 1);
        };
        auto bad = GroupConfiguration(); bad["spawnGroups"] = json::object(); reject(bad);
        bad = GroupConfiguration(); bad["spawnGroups"][0]["mode"] = "typo"; reject(bad);
        bad = GroupConfiguration(); bad["spawnGroups"][0]["time"] = -1; reject(bad);
        bad = GroupConfiguration(); bad["spawnGroups"][0]["interval"] = -1; reject(bad);
        bad = GroupConfiguration(); bad["spawnGroups"][0]["time"] = "3.0"; reject(bad);
        bad = GroupConfiguration(); bad["spawnGroups"][0]["time"] = nullptr; reject(bad);
        bad = GroupConfiguration(); bad["spawnGroups"][0]["enemies"] = json::array(); reject(bad);
        bad = GroupConfiguration(); bad["spawnGroups"][0]["enemies"] = json::object(); reject(bad);
        bad = GroupConfiguration(); bad["spawnGroups"][0]["enemies"][0]["enemy"] = "missing"; reject(bad);
        bad = GroupConfiguration(); bad["spawnGroups"][0]["enemies"][0]["spawnPoint"] = "missing"; reject(bad);
        bad = GroupConfiguration(); bad["spawnGroups"][0]["enemies"][0].erase("spawnPoint"); reject(bad);
        bad = GroupConfiguration(); bad["spawnGroups"][0]["id"] = "A"; reject(bad);
        bad = GroupConfiguration(); bad["spawnGroups"][0]["id"] = ""; reject(bad);
        bad = GroupConfiguration(); bad["spawnGroups"][0]["id"] = "G"; bad["spawnGroups"].push_back(bad["spawnGroups"][0]); reject(bad);
        bad = GroupConfiguration(); bad["spawnTriggers"] = Configuration()["spawnTriggers"]; bad["spawnGroups"][0]["id"] = "T"; reject(bad);
        h.Tick(.5f); assert(h.next == 2); // failed edits retain the running valid stage
    }
    // If Blender's integration fixture exists, verify its exported schedule directly.
    if (std::ifstream("../spawn-group-tests/project/resources/levels/stage01/stage01.json").good()) {
        EnemyDefinitions definitions; assert(definitions.Load("../../resources/Data/enemies.json"));
        Harness h; assert(h.system.Load("../spawn-group-tests/project/resources/levels/stage01/stage01.json", definitions));
        h.Tick(2.5f, {1000,0,0}); assert(h.next == 0);
        h.Tick(.5f, {1000,0,0}); assert(h.next == 3);
        assert(h.events[0].frame == h.events[2].frame && h.events[0].enemy == "normal" && h.events[2].enemy == "tank");
        h.Tick(1, {1000,0,0}); assert(h.next == 4);
        h.Tick(.5f, {1000,0,0}); assert(h.next == 5);
        h.Tick(.5f, {1000,0,0}); assert(h.next == 6);
    }
    std::cout << "Enemy spawn tests passed: legacy timing/capacity/map, Sequential, same-frame Simultaneous, positions/rotations/definitions, reset/reload, mixed groups/triggers, validation, Blender fixture.\n";
}
