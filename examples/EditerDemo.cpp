#pragma once

// プログラムは WinMain から始まります
void EditoreDemo(){
	
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