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

//VECTOR camPos = VGet(0.0f, 10.0f, -30.0f);  // カメラ位置
//float camYaw = 0.0f;   // 左右回転
//float camPitch = 0.0f; // 上下回転
//
//void UpdateCamera()
//{
//	// マウスで視点回転
//	int mx, my;
//	GetMousePoint(&mx, &my);
//	camYaw += (mx - 640) * 0.002f;
//	camPitch += (my - 360) * 0.002f;
//	SetMousePoint(640, 360);
//
//	// 前方向ベクトル
//	VECTOR forward = VGet(
//		cosf(camPitch) * sinf(camYaw),
//		sinf(camPitch),
//		cosf(camPitch) * cosf(camYaw)
//	);
//
//	// 右方向ベクトル
//	VECTOR right = VCross(forward, VGet(0, 1, 0));
//
//	// WASD移動
//	float speed = 0.5f;
//	if (CheckHitKey(KEY_INPUT_W)) camPos = VAdd(camPos, VScale(forward, speed));
//	if (CheckHitKey(KEY_INPUT_S)) camPos = VSub(camPos, VScale(forward, speed));
//	if (CheckHitKey(KEY_INPUT_A)) camPos = VSub(camPos, VScale(right, speed));
//	if (CheckHitKey(KEY_INPUT_D)) camPos = VAdd(camPos, VScale(right, speed));
//
//	// カメラ更新
//	VECTOR target = VAdd(camPos, forward);
//	SetCameraPositionAndTarget_UpVecY(camPos, target);
//}

// プログラムは WinMain から始まります
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	ChangeWindowMode(TRUE);
	//SetGraphMode(1280, 720, 32);
	SetGraphMode(2560, 1440, 32);

	DxLib_Init();

	SetDrawScreen(DX_SCREEN_BACK); //ウィンドウモード変更と初期化と裏画面設定
	
	//viewConsole();

	bool isNotFinish = false;
	
	
	// while(裏画面を表画面に反映, メッセージ処理, 画面クリア)
	while (!isNotFinish && ScreenFlip() == 0 && ProcessMessage() == 0 && ClearDrawScreen() == 0 && CheckHitKey(KEY_INPUT_RETURN) == 0)
	{
		cube_demo();
	}

	DxLib_End();				// ＤＸライブラリ使用の終了処理

	return 0;				// ソフトの終了 
}