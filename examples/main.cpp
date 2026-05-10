#include "DxLib.h"
#include "parallelCubeDEMO.h"
#include <iostream>
#include <windows.h>

void viewConsole()
{
	AllocConsole();  // コンソールを生成
	FILE* stream;
	freopen_s(&stream, "CONOUT$", "w", stdout);  // 標準出力をコンソールにリダイレクト
	freopen_s(&stream, "CONOUT$", "w", stderr);  // 標準エラー出力もリダイレクト
}

// プログラムは WinMain から始まります
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	ChangeWindowMode(TRUE);
	SetGraphMode(1280, 720, 32);
	DxLib_Init();

	SetDrawScreen(DX_SCREEN_BACK); //ウィンドウモード変更と初期化と裏画面設定
	
	

	viewConsole();

	int x = 0, y = 0;
	int Green = GetColor(0, 255, 0);      // 緑の色コードを取得

	bool isNotFinish = false;

	

	int64_t prevTime = GetNowHiPerformanceCount();
	
	// while(裏画面を表画面に反映, メッセージ処理, 画面クリア)
	while (!isNotFinish && ScreenFlip() == 0 && ProcessMessage() == 0 && ClearDrawScreen() == 0 && CheckHitKey(KEY_INPUT_RETURN) == 0)
	{
		int64_t now = GetNowHiPerformanceCount();
		float deltaTime = (now - prevTime) / 1000000.0f; // 秒

		cube_demo(deltaTime);
		
		prevTime = now;

		/*for (CubeComponent& cube : cubes)
		{
			cube.transform.rotation.y += 0.02f;
			UpdateTransformSystem(cube);
			DrawCubeSystem(cube);
		}*/
	}

	DxLib_End();				// ＤＸライブラリ使用の終了処理

	return 0;				// ソフトの終了 
}