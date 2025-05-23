#pragma once

#pragma once
#include<vector>
#include<DxLib.h>
#include <string>
#include "Component.h"

#define DEBUG_HELPER Singleton<DebugHelper>::get_instance()

struct debugMessage
{
	debugMessage() :message(""), color(0), drawTime(1.0f) { ; }
	debugMessage(std::string text, float time, unsigned int c) :message(text), drawTime(time), color(c) { ; }
	std::string message;
	float drawTime;
	unsigned int color;
	float currentDrawTime = 0.0f;
};

struct debugDrawPoint
{
	debugDrawPoint() :drawP(Transform2D()), drawTime(0.0f), color(0) { ; }
	debugDrawPoint(Transform2D p, float time, unsigned int c) :drawP(p), drawTime(time), color(c) { ; }
	debugDrawPoint(Transform2D p, unsigned int c) :drawP(p), drawTime(0.0), color(c) { ; }
	debugDrawPoint(Transform2D p) :drawP(p), drawTime(0.0), color(GetColor(255, 0, 0)) { ; }

	Transform2D drawP;
	float drawTime = 3.0f;
	unsigned int color;
	float currentDrawTime = 0.0f;
};

class DebugHelper {

	std::vector<debugMessage> List; //描画する項目
	std::vector<debugDrawPoint> drawPointList;
	int FontHandle = -1; //描画するフォント
	bool Initialize = false;

	int testVolume = 100;
public:
	void Add(std::string text) { //描画する項目を追加
		List.emplace_back(text, 0.0f, 0xffffff);
	}

	void Add(std::string text, float time) { //描画する項目を追加
		List.emplace_back(text, time, 0xffffff);
	}

	void Add(std::string text, float time, unsigned int color) { //描画する項目を追加
		List.emplace_back(text, time, color);
	}

	void Add() { //空行を追加
		List.emplace_back();
	}

	void Add(float time) { //何秒間空行を追加
		List.emplace_back("", time, 0);
	}

	void Add(Transform2D p, float time, unsigned int c) {
		drawPointList.emplace_back(p, time, c);
	}

	void Add(Transform2D p, unsigned int c) {
		drawPointList.emplace_back(p, c);

	}

	void Add(Transform2D p) {
		drawPointList.emplace_back(p);
	}

	void Update(const float deltaTime) {

		//メッセージ処理
		int i = 0;
		for (auto& t : List) {
			if (t.message == "") { //空行処理
				i++;
				continue;
			}
			int x, y, l;
			SetDrawBlendMode(DX_BLENDMODE_ALPHA, 127); //背景の矩形を半透明にする
			GetDrawStringSizeToHandle(&x, &y, &l, t.message.c_str(), t.message.length(), FontHandle); //各行の大きさを取得
			DrawBox(0, i * y, x, i * y + y, 0x000000, TRUE); //背景の矩形を描画
			SetDrawBlendMode(DX_BLENDMODE_NOBLEND, 0); //半透明から通常に戻す
			DrawStringToHandle(0, i * y, t.message.c_str(), t.color, FontHandle); //文字列をフォントを使って描画
		}

		//ポイント描画
		for (auto& p : drawPointList) {
			Transform2D draw = p.drawP;

			int x = static_cast<int>(draw.x);
			int y = static_cast<int>(draw.y);
			DrawCircle(x, y, 10, p.color);

			p.currentDrawTime += deltaTime;
		}

		//描画秒数越えを処理
		for (auto itr = List.begin(); itr != List.end();) {
			if (itr->currentDrawTime > itr->drawTime) {
				itr = List.erase(itr);
			}
			else {
				itr++;
			}
		}

		//描画秒数越えを処理
		for (auto itr = drawPointList.begin(); itr != drawPointList.end();) {
			if (itr->currentDrawTime > itr->drawTime) {
				itr = drawPointList.erase(itr);
			}
			else {
				itr++;
			}
		}
	}

private:
	DebugHelper() { FontHandle = CreateFontToHandle("ＭＳゴシック", 16, 2); };
	~DebugHelper() {
		DeleteFontToHandle(FontHandle);
	};
};