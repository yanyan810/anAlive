#pragma once
#include "GameApp.h"
#include "DirectXTex.h"
#include "SrvManager.h"
#include "Sprite.h"
#include "GameOverSequence.h"
#include <fstream>
#include <numeric>

// Uses the production presentation PSO, actual GPU pixels and timestamp queries.
inline void RunGameOverPostEffectRuntimeTests(GameApp& app,const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* message) { if (!ok) throw std::runtime_error(message); };
    auto* dx=app.Dx(); auto& render=*app.Render(); auto* device=dx->GetDevice();
    std::ofstream report(directory/"post-gpu.csv");
    report<<"width,height,copy_us,combined_us,delta_us,fullscreen_draws,disabled_death_draws\n";
    std::ofstream mosaicReport(directory/"mosaic-gpu.csv");
    mosaicReport<<"width,height,forced_strength,normal_chroma,normal_colored_fraction,forced_chroma,time_delta,near_time_delta,epoch_delta,source_max_delta\n";
    UINT64 frequency=0; check(SUCCEEDED(dx->GetCommandQueue()->GetTimestampFrequency(&frequency)) && frequency>0,"GPU timestamp frequency unavailable");
    D3D12_QUERY_HEAP_DESC queryDesc{}; queryDesc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP; queryDesc.Count=2;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queries;
    check(SUCCEEDED(device->CreateQueryHeap(&queryDesc,IID_PPV_ARGS(&queries))),"Post GPU query allocation failed");
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    const CD3DX12_HEAP_PROPERTIES readbackHeap(D3D12_HEAP_TYPE_READBACK);
    const auto bufferDesc=CD3DX12_RESOURCE_DESC::Buffer(16);
    check(SUCCEEDED(device->CreateCommittedResource(&readbackHeap,D3D12_HEAP_FLAG_NONE,&bufferDesc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))),"Post GPU query readback allocation failed");
    constexpr auto format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    const GameOverSequence::PostSettings postDefaults;
    std::array<uint8_t,3> mosaicReference{};
    bool hasMosaicReference=false;
    for (const auto& size:std::vector<std::pair<uint32_t,uint32_t>>{{1280,720},{853,479},{1920,1080},{600,900}}) {
        const auto [width,height]=size;
        DirectX::ScratchImage pattern;
        check(SUCCEEDED(pattern.Initialize2D(format,width,height,1,1)),"Post pattern allocation failed");
        const auto* input=pattern.GetImage(0,0,0);
        for (size_t y=0;y<height;++y) for (size_t x=0;x<width;++x) {
            auto* p=input->pixels+y*input->rowPitch+x*4;
            if (x<width/3) { p[0]=200; p[1]=100; p[2]=40; }
            else { const uint8_t v=(x%2)==0 ? 35 : 225; p[0]=v; p[1]=static_cast<uint8_t>(v*.65f); p[2]=static_cast<uint8_t>(v*.4f); }
            p[3]=255;
        }
        auto source=dx->CreateTextureResource(pattern.GetMetadata()); dx->UploadTextureData(source,pattern); dx->FlushTextureUploads();
        const auto srv=app.Srv()->Allocate(); app.Srv()->CreateSRVTexture2D(srv,source.Get(),format,1);
        // Neutral grey exposes a no-op tint even when ordinary stage pixels are
        // already colored. Read back the same production PSO on this source.
        DirectX::ScratchImage grey; check(SUCCEEDED(grey.Initialize2D(format,width,height,1,1)),"Grey mosaic pattern allocation failed");
        const auto* greyPixels=grey.GetImage(0,0,0);
        for (size_t y=0;y<height;++y) for (size_t x=0;x<width;++x) {
            auto* p=greyPixels->pixels+y*greyPixels->rowPitch+x*4; p[0]=p[1]=p[2]=128; p[3]=255;
        }
        auto greySource=dx->CreateTextureResource(grey.GetMetadata()); dx->UploadTextureData(greySource,grey); dx->FlushTextureUploads();
        const auto greySrv=app.Srv()->Allocate(); app.Srv()->CreateSRVTexture2D(greySrv,greySource.Get(),format,1);
        auto target=dx->CreateRenderTextureResource(width,height,format,{0,0,0,1});
        auto heap=dx->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,false);
        const auto handle=heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_RENDER_TARGET_VIEW_DESC rtv{}; rtv.Format=format; rtv.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(target.Get(),&rtv,handle);
        const D3D12_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
        const D3D12_RECT scissor{0,0,static_cast<LONG>(width),static_cast<LONG>(height)};
        const auto begin=[&] {
            app.Srv()->PreDraw(); dx->PreDraw(false);
            auto* command=dx->GetCommandList(); command->OMSetRenderTargets(1,&handle,FALSE,nullptr);
            command->RSSetViewports(1,&viewport); command->RSSetScissorRects(1,&scissor);
        };
        const auto finish=[&] { dx->SetBackBufferRenderTarget(); dx->PostDraw(); dx->WaitForGPU(); };
        Sprite ui; ui.Initialize(app.SpriteCom(),dx,"resources/white1x1.png");
        const auto& white=TextureManager::GetInstance()->GetMetaData("resources/white1x1.png");
        ui.SetPosition({.4f,.4f}); ui.SetScale({.2f/static_cast<float>(white.width),.2f/static_cast<float>(white.height),1});
        ui.SetColor({0,1,0,1}); ui.Update(Matrix4x4::MakeIdentity4x4(),Matrix4x4::MakeOrthographicMatrix(0,0,1,1,0,100));
        DeathPostEffectParameters combined; combined.vignette=.36f; combined.desaturation=.5f;
        combined.blurPixels=1; combined.chromaticPixels=.75f; combined.vignetteColor={.08f,.003f,.008f,1};
        combined.mosaicStrength=postDefaults.mosaicStrength; combined.mosaicColorStrength=postDefaults.mosaicColorStrength;
        std::vector<std::vector<uint8_t>> frames;
        for (int probe=0;probe<19;++probe) {
            DeathPostEffectParameters p;
            if (probe==1) p.desaturation=1;
            if (probe==2) p.vignette=.5f;
            if (probe==3) p.blurPixels=1.25f;
            if (probe==4) p.chromaticPixels=1;
            if (probe==5 || probe==6) p=combined;
            if (probe==7 || probe==8) { p.mosaicStrength=postDefaults.mosaicStrength; p.mosaicColorStrength=postDefaults.mosaicColorStrength; }
            if (probe==8) p.mosaicColorStrength=0;
            if (probe==9) { p.mosaicStrength=.055f; p.mosaicColorStrength=.45f; } // Previous subtle default.
            if (probe==12 || probe==13 || probe==14 || probe==15 || probe>=17) {
                p.mosaicStrength=postDefaults.mosaicStrength; p.mosaicColorStrength=postDefaults.mosaicColorStrength;
                p.mosaicCellSize=postDefaults.mosaicCellSize; p.mosaicChangeRate=postDefaults.mosaicChangeRate;
            }
            if (probe==11 || probe==16) { p.mosaicStrength=1; p.mosaicColorStrength=1; p.mosaicCellSize=.015f; }
            if (probe==13) p.mosaicTime=1;
            if (probe==14) p.mosaicTime=.01f;
            if (probe==15) { p.mosaicTime=10; p.mosaicChangeRate=0; }
            if (probe==17 || probe==18) p.mosaicTime=1/postDefaults.mosaicChangeRate+(probe==17 ? -.002f : .002f);
            render.SetDeathPostEffect(p); begin();
            const auto draws=render.FullscreenDrawCount(), deathDraws=render.DeathPostDrawCount();
            render.DrawFullscreenPass(PostEffectMode::FullScreen,probe>=10 ? greySrv : srv,nullptr,true);
            check(render.FullscreenDrawCount()-draws==1,"Death post added a fullscreen draw");
            check(render.DeathPostDrawCount()-deathDraws==(p.Active() ? 1u : 0u),"Disabled death post did GPU work");
            if (probe==11) check(render.DeathPostEffect().mosaicStrength==1 && render.deathPostData_->mosaicStrength==1 && render.deathPostData_->mosaicColorStrength==1 && render.deathPostData_->mosaicCellSize==.015f,"Forced mosaic did not reach the GPU constant buffer at 1/1/0.015");
            if (probe==6 || probe==16) ui.Draw();
            finish();
            DirectX::ScratchImage image; constexpr auto state=D3D12_RESOURCE_STATE_RENDER_TARGET;
            check(SUCCEEDED(DirectX::CaptureTexture(dx->GetCommandQueue(),target.Get(),false,image,state,state)),"Post effect GPU capture failed");
            const auto* pixels=image.GetImage(0,0,0); std::vector<uint8_t> packed; packed.reserve(width*height*4);
            for (size_t y=0;y<height;++y) packed.insert(packed.end(),pixels->pixels+y*pixels->rowPitch,pixels->pixels+y*pixels->rowPitch+width*4);
            frames.push_back(std::move(packed));
            const auto path=directory/("post-"+std::to_string(width)+"x"+std::to_string(height)+"-"+std::to_string(probe)+".png");
            check(SUCCEEDED(DirectX::SaveToWICFile(*pixels,DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path.c_str())),"Post GPU PNG failed");
        }
        const auto pixel=[&](int probe,uint32_t x,uint32_t y) { return &frames[probe][(static_cast<size_t>(y)*width+x)*4]; };
        const auto* gray=pixel(1,width/5,height/2);
        check(std::abs(gray[0]-gray[1])<=1 && std::abs(gray[1]-gray[2])<=1,"Desaturation shader did not remove color");
        check(pixel(2,1,1)[0]<pixel(0,1,1)[0] && std::abs(pixel(2,width/2,height/2)[0]-pixel(0,width/2,height/2)[0])<=1,"Vignette did not spare the center/darken the edge");
        uint64_t originalVariation=0,blurredVariation=0,chromaDifference=0;
        for (uint32_t x=width*3/4;x<width-3;++x) {
            originalVariation+=std::abs(pixel(0,x,3)[0]-pixel(0,x+1,3)[0]);
            blurredVariation+=std::abs(pixel(3,x,3)[0]-pixel(3,x+1,3)[0]);
            chromaDifference+=std::abs(pixel(4,x,3)[0]-pixel(0,x,3)[0]);
        }
        check(static_cast<double>(blurredVariation)<static_cast<double>(originalVariation)*.8 && chromaDifference>100,"Peripheral blur/chromatic shader had no effect");
        check(std::equal(pixel(4,0,height/2),pixel(4,0,height/2)+4,pixel(0,0,height/2)),"Chromatic tap wrapped to the opposite edge");
        const auto* hud=pixel(6,width/2,height/2);
        check(hud[0]==0 && hud[1]==255 && hud[2]==0 && hud[3]==255,"Post effect blurred/desaturated the UI layer");
        const auto* forcedHud=pixel(16,width/2,height/2);
        check(forcedHud[0]==0 && forcedHud[1]==255 && forcedHud[2]==0 && forcedHud[3]==255,"Forced mosaic colored the UI layer");
        uint64_t mosaicDifference=0,previousMosaicDifference=0;
        int maxMosaicDelta=0;
        for (uint32_t y=height/5;y<height/3;++y) for (uint32_t x=width/10;x<width/4;++x) for (int channel=0;channel<3;++channel) {
            const int delta=std::abs(pixel(7,x,y)[channel]-pixel(0,x,y)[channel]);
            mosaicDifference+=delta; maxMosaicDelta=std::max(maxMosaicDelta,delta);
            previousMosaicDifference+=std::abs(pixel(9,x,y)[channel]-pixel(0,x,y)[channel]);
        }
        check(mosaicDifference>100,"Rainbow mosaic did not affect the source");
        check(mosaicDifference>previousMosaicDifference*1.5,"Stronger mosaic default did not increase the visible GPU effect");
        check(frames[8]==frames[0],"Zero mosaic color strength did not use the exact copy path");
        const float cellPixels=std::min(width,height)*.006f;
        const auto* cellColor=pixel(12,static_cast<uint32_t>(12.5f*cellPixels),static_cast<uint32_t>(12.5f*cellPixels));
        if (hasMosaicReference) for (int channel=0;channel<3;++channel)
            check(std::abs(cellColor[channel]-mosaicReference[channel])<=1,"Mosaic tile scale/hash depends on render resolution");
        else { std::copy(cellColor,cellColor+3,mosaicReference.begin()); hasMosaicReference=true; }
        double chroma=0,forcedChroma=0,timeDifference=0,nearDifference=0,epochDifference=0;
        size_t colored=0,samplePixels=0;
        for (uint32_t y=height/5;y<height*4/5;++y) for (uint32_t x=width/5;x<width*4/5;++x) {
            const auto* normal=pixel(12,x,y); const auto* forced=pixel(11,x,y);
            const int spread=*std::max_element(normal,normal+3)-*std::min_element(normal,normal+3);
            chroma+=spread; if (spread>=8) ++colored;
            forcedChroma+=*std::max_element(forced,forced+3)-*std::min_element(forced,forced+3);
            for (int c=0;c<3;++c) {
                timeDifference+=std::abs(normal[c]-pixel(13,x,y)[c]);
                nearDifference+=std::abs(normal[c]-pixel(14,x,y)[c]);
                epochDifference+=std::abs(pixel(17,x,y)[c]-pixel(18,x,y)[c]);
            }
            ++samplePixels;
        }
        chroma/=samplePixels; forcedChroma/=samplePixels; timeDifference/=samplePixels*3;
        nearDifference/=samplePixels*3; epochDifference/=samplePixels*3;
        const double coloredFraction=static_cast<double>(colored)/samplePixels;
        check(chroma>=6 && chroma<55 && coloredFraction>.4,"Default mosaic is not visibly colored on grey or is overwhelming");
        check(forcedChroma>chroma*2 && forcedChroma>25,"Forced 1.0 mosaic is not clearly visible on grey");
        check(timeDifference>.8 && nearDifference<.5 && timeDifference>nearDifference*5 && epochDifference<.5,"Mosaic pattern did not change slowly/continuously");
        check(frames[12]==frames[15],"Zero mosaic change rate did not freeze the pattern");
        mosaicReport<<width<<','<<height<<",1,"<<chroma<<','<<coloredFraction<<','<<forcedChroma<<','<<timeDifference<<','<<nearDifference<<','<<epochDifference<<','<<maxMosaicDelta<<'\n';
        uint64_t preservedVariation=0;
        for (uint32_t x=width*3/4;x<width-3;++x) preservedVariation+=std::abs(pixel(7,x,3)[0]-pixel(7,x+1,3)[0]);
        check(preservedVariation>originalVariation*.6,"Mosaic hid the underlying scene detail");
        for (const auto& frame:frames) for (size_t i=3;i<frame.size();i+=4) check(frame[i]==255,"Post effect corrupted opaque scene alpha");
        // Interleave the PSOs, alternating their order to reduce clock/thermal
        // drift. Discard warmup pairs. VSync/CPU waits are outside the queries;
        // production has no query overhead.
        std::vector<double> samples[2];
        for (int repeat=0;repeat<10;++repeat) for (int order=0;order<2;++order) {
            const int mode=(repeat+order)%2;
            render.SetDeathPostEffect(mode==1 ? combined : DeathPostEffectParameters{});
                begin(); auto* command=dx->GetCommandList();
                command->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
                constexpr int draws=128;
                for (int i=0;i<draws;++i) render.DrawFullscreenPass(PostEffectMode::FullScreen,srv,nullptr,true);
                command->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
                command->ResolveQueryData(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,readback.Get(),0);
                finish();
                UINT64* ticks=nullptr; const D3D12_RANGE range{0,16};
                check(SUCCEEDED(readback->Map(0,&range,reinterpret_cast<void**>(&ticks))),"GPU timing Map failed");
                const double us=static_cast<double>(ticks[1]-ticks[0])*1000000.0/static_cast<double>(frequency)/draws;
                const D3D12_RANGE noWrite{0,0}; readback->Unmap(0,&noWrite);
                if (repeat>=2) samples[mode].push_back(us);
        }
        double times[2]{};
        for (int mode=0;mode<2;++mode) {
            std::sort(samples[mode].begin(),samples[mode].end());
            times[mode]=(samples[mode][3]+samples[mode][4])*.5;
        }
        report<<width<<','<<height<<','<<times[0]<<','<<times[1]<<','<<times[1]-times[0]<<",1,0\n";
        // Strengths all zero must select the exact same copy pipeline/pixels.
        render.ResetDeathPostEffect(); begin(); render.DrawFullscreenPass(PostEffectMode::FullScreen,srv,nullptr,true); finish();
        DirectX::ScratchImage bypass; constexpr auto rtState=D3D12_RESOURCE_STATE_RENDER_TARGET;
        check(SUCCEEDED(DirectX::CaptureTexture(dx->GetCommandQueue(),target.Get(),false,bypass,rtState,rtState)),"Bypass readback failed");
        const auto* bypassPixels=bypass.GetImage(0,0,0);
        for (size_t y=0;y<height;++y) check(std::equal(frames[0].begin()+y*width*4,frames[0].begin()+(y+1)*width*4,bypassPixels->pixels+y*bypassPixels->rowPitch),"Disabled post effect changed scene pixels");
        app.Srv()->Retire(srv,source);
        app.Srv()->Retire(greySrv,greySource);
    }
    // Exercise the real preview chain too, not just the isolated shader. The
    // common path replaces one copy; existing Gaussian effects can coexist.
    render.SetMode(PostEffectMode::FullScreen); render.ClearObjectPostLayer(); render.ClearParticlePostLayer();
    DeathPostEffectParameters combined; combined.vignette=.36f; combined.desaturation=.5f; combined.blurPixels=1; combined.chromaticPixels=.75f;
    for (bool enabled:{false,true}) {
        render.SetDeathPostEffect(enabled ? combined : DeathPostEffectParameters{});
        dx->PreDraw(false); const auto count=render.FullscreenDrawCount(), deaths=render.DeathPostDrawCount();
        render.RenderPostEffectsForSceneTexture();
        check(render.FullscreenDrawCount()-count==1 && render.DeathPostDrawCount()-deaths==(enabled ? 1u : 0u),"Preview chain did extra work in the common/disabled path");
        dx->PostDraw(); dx->WaitForGPU();
    }
    render.SetMode(PostEffectMode::GaussianBlur); render.SetDeathPostEffect(combined);
    dx->PreDraw(false); const auto count=render.FullscreenDrawCount();
    render.RenderPostEffectsForSceneTexture();
    check(render.FullscreenDrawCount()-count==3,"Death post did not coexist with existing Gaussian X/Y passes");
    dx->PostDraw(); dx->WaitForGPU();
    render.SetMode(PostEffectMode::FullScreen);
    render.ResetDeathPostEffect();
}
