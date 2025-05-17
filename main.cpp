#include "DxLib.h"
#include "ecsTest.h"
#include <iostream>
#include <windows.h>

void viewConsole()
{
	AllocConsole();  // コンソールを生成
	FILE* stream;
	freopen_s(&stream, "CONOUT$", "w", stdout);  // 標準出力をコンソールにリダイレクト
}

// プログラムは WinMain から始まります
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	ChangeWindowMode(TRUE), DxLib_Init(), SetDrawScreen(DX_SCREEN_BACK); //ウィンドウモード変更と初期化と裏画面設定

	viewConsole();

	int x = 0, y = 0;
	int Green = GetColor(0, 255, 0);      // 緑の色コードを取得

	bool isNotFinish = false;
	
	// while(裏画面を表画面に反映, メッセージ処理, 画面クリア)
	while (!isNotFinish && ScreenFlip() == 0 && ProcessMessage() == 0 && ClearDrawScreen() == 0)
	{
		test();
		//Transform2D trans(150,100);
		DrawFormatString(0,0, Green, "座標[%d,%d]", 0,0); // 文字を描画する
	}

	DxLib_End();				// ＤＸライブラリ使用の終了処理

	return 0;				// ソフトの終了 
}