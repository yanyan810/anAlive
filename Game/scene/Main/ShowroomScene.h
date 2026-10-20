#pragma once
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
#include "GameScene.h"
// Separate scene instance and level; combat behavior remains shared with GameScene.
class ShowroomScene final : public GameScene {
public:
    ShowroomScene() : GameScene(true) {}
};
#endif
