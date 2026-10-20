#pragma once
#include <Windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif






#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"
#include <cstdint>
#include <functional>
#include"DirectXTex.h"
#include "d3dx12.h"

class WinApp
{
public:
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	static const int32_t kClientWidth = 1280; //クライアント領域の幅
	static const int32_t kClientHeight = 720; //クライアント領域の高さ


public:

	//初期化
	void Initialize();
	//更新
	void Update();

	HWND GetHwnd() const { return hwnd; }
    void SetCloseGuard(std::function<bool()> guard) { closeGuard_=std::move(guard); }

	//getter
	HINSTANCE GetHInstance() const { return wc.hInstance; }

	// メッセージ処理
	bool ProcessMessage();

	//終了
	void Finalize();

private:
    inline static std::function<bool()> closeGuard_;
	HWND hwnd = nullptr; //ウィンドウハンドル
	//ウィンドウクラスの設定
	WNDCLASS wc{};


};

