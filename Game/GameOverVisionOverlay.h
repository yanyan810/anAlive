#pragma once
#include "GameOverSequence.h"
#include "Sprite.h"
#include <array>

// Scene-local consciousness/eyelids. No shared SceneManager fade state or images.
// Normalized coordinates follow any target viewport, including debug resizing.
class GameOverVisionOverlay {
public:
    static constexpr size_t Segments=32;
    void Initialize(SpriteCommon* common,DirectXCommon* dx) {
        const auto init=[&](Sprite& sprite) {
            sprite.Initialize(common,dx,"resources/white1x1.png");
            const auto& white=TextureManager::GetInstance()->GetMetaData("resources/white1x1.png");
            sprite.SetScale({1.0f/static_cast<float>(white.width),1.0f/static_cast<float>(white.height),1});
            sprite.SetColor({0,0,0,1});
        };
        init(shade_); for (auto& lid:lids_) init(lid);
    }
    static float HalfGap(float x,float opening,float curvature) {
        const float p=std::clamp(opening,0.0f,1.0f), edge=2*x-1;
        return std::clamp(.5f*p-std::clamp(curvature,0.0f,2.0f)*p*(1-p)*edge*edge,0.0f,.5f);
    }
    void Draw(const GameOverSequence::VisionState& vision,float curvature) {
        const auto view=Matrix4x4::MakeIdentity4x4();
        const auto projection=Matrix4x4::MakeOrthographicMatrix(0,0,1,1,0,100);
        if (vision.shade>0) {
            shade_.SetColor({0,0,0,std::clamp(vision.shade,0.0f,1.0f)});
            shade_.Update(view,projection); shade_.Draw();
        }
        if (vision.opening>=1) return;
        for (size_t i=0;i<Segments;++i) {
            const float x0=static_cast<float>(i)/Segments, x1=static_cast<float>(i+1)/Segments;
            const float a=HalfGap(x0,vision.opening,curvature), b=HalfGap(x1,vision.opening,curvature);
            auto& top=lids_[i*2]; auto& bottom=lids_[i*2+1];
            top.SetQuad({{{x0,.5f-a},{x0,0},{x1,.5f-b},{x1,0}}});
            bottom.SetQuad({{{x0,1},{x0,.5f+a},{x1,1},{x1,.5f+b}}});
            top.Update(view,projection); top.Draw();
            bottom.Update(view,projection); bottom.Draw();
        }
    }
private:
    Sprite shade_;
    std::array<Sprite,Segments*2> lids_;
};
