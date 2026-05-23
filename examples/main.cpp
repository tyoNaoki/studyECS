#include <windows.h>
#include <DxLib.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
#include "Editor/EditorApp.h"

#include "parallelCubeDEMO.h"
#include <iostream>

enum class WinMainMode {
	Editor,
	CubeDemo
};

static constexpr WinMainMode mode = WinMainMode::Editor;   // ← ここを変えるだけで切り替え

void RunDemo();
void viewConsole();

// プログラムは WinMain から始まります
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	switch (mode) {
	case WinMainMode::Editor:
	{
		EditorApp editor;
		editor.Run();
		break;
	}
	case WinMainMode::CubeDemo:   RunDemo(); break;

	default: RunDemo(); break;
	}

	return 0;				// ソフトの終了 
}

void RunDemo() {
	ChangeWindowMode(TRUE);
	SetGraphMode(1280, 720, 32);
	SetUseDirect3DVersion(DX_DIRECT3D_11);
	//SetGraphMode(2560, 1440, 32);

	if (DxLib_Init() == -1) {
		return;
	}

	SetDrawScreen(DX_SCREEN_BACK); //ウィンドウモード変更と初期化と裏画面設定

	//viewConsole();

	bool isNotFinish = false;

	// while(裏画面を表画面に反映, メッセージ処理, 画面クリア)
	while (!isNotFinish && ScreenFlip() == 0 && ProcessMessage() == 0 && ClearDrawScreen() == 0 && CheckHitKey(KEY_INPUT_RETURN) == 0)
	{
		cube_demo();
	}

	DxLib_End();				// ＤＸライブラリ使用の終了処理

	return;
}

void viewConsole()
{
	AllocConsole();  // コンソールを生成
	FILE* stream;
	freopen_s(&stream, "CONOUT$", "w", stdout);  // 標準出力をコンソールにリダイレクト
	freopen_s(&stream, "CONOUT$", "w", stderr);  // 標準エラー出力もリダイレクト
}







