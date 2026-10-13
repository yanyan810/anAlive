#pragma once
#include <memory>
#include "SceneManager.h"
#include "Input.h"
#include "SkyboxCommon.h"

class WinApp;
class DirectXCommon;
class SrvManager;
class SpriteCommon;
class Object3dCommon;
class PrimitiveCommon;
class ParticleCommon;
class ImGuiManagaer;
class ApiDebugBot;
class BasicCombatDebugBot;
class GeminiDebugActionProvider;
class SkinningCommon;
class DebugAIManager;
class OpenAIDebugActionProvider;
class RandomDebugBot;
#include "RenderManager.h"

class SceneManager;

class GameApp {
public:
    GameApp();
    ~GameApp();

    int Run();
    void RequestQuit() { quit_ = true; }

    WinApp* Win() const { return win_.get(); }
    DirectXCommon* Dx() const { return dx_.get(); }
    SrvManager* Srv() const { return srv_.get(); }
    SpriteCommon* SpriteCom() const { return spriteCommon_.get(); }
    Object3dCommon* ObjCom() const { return objCommon_.get(); }
    PrimitiveCommon* PrimitiveCom() const { return primitiveCommon_.get(); }
    ParticleCommon* ParticleCom() const { return particleCommon_.get(); }
    ImGuiManagaer* ImGui() const { return imgui_.get(); }
    SkyboxCommon* SkyboxCom() const { return skyboxCommon_.get(); }
    SkinningCommon* SkinCom() { return skinCom_.get(); }

    SceneManager& Scenes() { return *sceneMgr_; }

    void Update(float dt);
    void Update(float dt, float unscaledDt);

    void Draw();

    RenderManager* Render() const { return render_.get(); }

    Input* GetInput() { return input_.get(); }
    const Input* GetInput() const { return input_.get(); }
    DebugAIManager* DebugAI() const { return debugAI_.get(); }

private:
    bool Initialize_();
    void Finalize_();

private:
    bool quit_ = false;

    std::unique_ptr<WinApp> win_;
    std::unique_ptr<DirectXCommon> dx_;
    std::unique_ptr<SrvManager> srv_;

    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::unique_ptr<Object3dCommon> objCommon_;
    std::unique_ptr<PrimitiveCommon> primitiveCommon_;
    std::unique_ptr<ParticleCommon> particleCommon_;
    std::unique_ptr<ImGuiManagaer> imgui_;

    std::unique_ptr<SceneManager> sceneMgr_;
    std::unique_ptr<Input> input_; 
    std::unique_ptr<SkinningCommon> skinCom_;
    std::unique_ptr<SkyboxCommon> skyboxCommon_;
    std::unique_ptr<DebugAIManager> debugAI_;
    std::unique_ptr<ApiDebugBot> debugAIApiBot_;
    std::unique_ptr<GeminiDebugActionProvider> debugAIGeminiProvider_;
    std::unique_ptr<OpenAIDebugActionProvider> debugAIOpenAIProvider_;
    std::unique_ptr<BasicCombatDebugBot> debugAIBasicCombatFallback_;

    std::unique_ptr<RenderManager> render_;

};

