#pragma once
#include "UIFont.h"

class GameOverOverlay {
public:
    void Reset() { selected_=0; armed_=false; }
    void Initialize(SpriteCommon* common,DirectXCommon* dx) {
        shade_.Initialize(common,dx,"resources/white1x1.png");
        const auto& white=TextureManager::GetInstance()->GetMetaData("resources/white1x1.png");
        shade_.SetScale({WinApp::kClientWidth/static_cast<float>(white.width),WinApp::kClientHeight/static_cast<float>(white.height),1});
        font_.Initialize(common,dx,160); Reset();
    }
    int Hit(const Vector2& p) const {
        if (std::abs(p.x-WinApp::kClientWidth*.5f)>155) return -1;
        for (int i=0;i<2;++i) if (std::abs(p.y-(WinApp::kClientHeight*.6f+i*56+10))<=23) return i;
        return -1;
    }
    void Select(int index) { if (index>=0 && index<2) selected_=index; }
    void Toggle() { selected_=1-selected_; }
    int Selected() const { return selected_; }
    bool Armed() const { return armed_; }
    void Arm() { armed_=true; }
    void Draw(const Matrix4x4& view,const Matrix4x4& projection,bool ready,float elapsed) {
        if (!ready) return;
        const float alpha=std::clamp(elapsed,0.0f,1.0f);
        shade_.SetColor({.015f,.02f,.035f,.48f*alpha*alpha*(3-2*alpha)});
        shade_.Update(view,projection); shade_.Draw();
        font_.Reset();
        const float w=WinApp::kClientWidth,h=WinApp::kClientHeight;
        Line("GAME OVER",w*.5f,h*.32f,42,{1,1,1,alpha});
        Line("RETRY",w*.5f,h*.6f,24,selected_==0 ? Vector4{1,.8f,.4f,alpha} : Vector4{.65f,.68f,.72f,alpha});
        Line("TITLE",w*.5f,h*.6f+56,24,selected_==1 ? Vector4{1,.8f,.4f,alpha} : Vector4{.65f,.68f,.72f,alpha});
        Line("UP / DOWN + ENTER   OR CLICK",w*.5f,h*.85f,12,{.8f,.8f,.85f,alpha});
        font_.Draw(view,projection);
    }
private:
    void Line(const std::string& text,float x,float y,float size,Vector4 color) {
        font_.DrawText(text,x-text.size()*size*.5f,y,size,color);
    }
    Sprite shade_;
    UIFont font_;
    int selected_=0;
    bool armed_=false;
};
