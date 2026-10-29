#pragma once
#include "GameApp.h"
#include "IScene.h"
#include "scene/Flow/TitleScene.h"
#include "scene/Debug/EnemyAITestScene.h"
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace ScenePolymorphismTests {
struct Calls { int enters=0, exits=0, updates=0, draws=0, destroys=0; };

class GameplayProbe final : public IScene {
public:
    explicit GameplayProbe(Calls& calls) : calls_(calls) {}
    ~GameplayProbe() override { ++calls_.destroys; }
    void OnEnter(GameApp&) override { ++calls_.enters; }
    void OnExit(GameApp&) override { ++calls_.exits; }
    void Update(GameApp&,float) override { ++calls_.updates; }
    void Draw(GameApp&) override { ++calls_.draws; }
private:
    Calls& calls_;
};

class WorkspaceProbe final : public IScene {
public:
    explicit WorkspaceProbe(Calls& calls) : calls_(calls) {}
    ~WorkspaceProbe() override { ++calls_.destroys; }
    void OnEnter(GameApp&) override { ++calls_.enters; }
    void Update(GameApp&,float) override { calls_.updates+=10; }
    void Draw(GameApp&) override { calls_.draws+=10; }
    bool UsesSceneViewport() const override { return true; }
private:
    Calls& calls_;
};

inline void Run(GameApp& app) {
    static_assert(std::is_abstract_v<IScene> && std::has_virtual_destructor_v<IScene>);
    const auto check=[](bool ok,const char* message) { if (!ok) throw std::runtime_error(message); };
    // The same base-reference caller observes two real scene policies without
    // initializing their GPU resources or relying on their registration names.
    const auto usesViewport=[](const IScene& scene) { return scene.UsesSceneViewport(); };
    std::unique_ptr<IScene> title=std::make_unique<TitleScene>();
    std::unique_ptr<IScene> ai=std::make_unique<EnemyAITestScene>();
    check(!usesViewport(*title) && usesViewport(*ai),"Real scene policy did not dispatch through IScene");

    Calls gameplay, workspace;
    {
        SceneManager manager;
        check(!manager.UsesSceneViewport(),"Empty SceneManager needs no viewport");
        manager.Register("RenamedGameplay",[&] { return std::make_unique<GameplayProbe>(gameplay); });
        manager.Register("RenamedTool",[&] { return std::make_unique<WorkspaceProbe>(workspace); });
        manager.Change(app,"RenamedGameplay");
        check(!manager.UsesSceneViewport(),"Default scene policy changed");
        manager.UpdateScene(app,.25f); manager.Draw(app);
        check(gameplay.enters==1 && gameplay.updates==1 && gameplay.draws==1,
            "Gameplay methods did not dispatch through SceneManager's base pointer");
        manager.Change(app,"RenamedTool");
        check(manager.UsesSceneViewport(),"Viewport policy depended on a scene name");
        manager.UpdateScene(app,.25f); manager.Draw(app);
        check(gameplay.exits==1 && workspace.enters==1 && workspace.updates==10 && workspace.draws==10,
            "Different derived scene methods did not dispatch");
    }
    check(gameplay.destroys==1 && workspace.destroys==1,
        "Deleting scenes through unique_ptr<IScene> skipped a derived destructor");
}
} // namespace ScenePolymorphismTests
