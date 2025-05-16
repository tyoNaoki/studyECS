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

	std::vector<debugMessage> List; //�`�悷�鍀��
	std::vector<debugDrawPoint> drawPointList;
	int FontHandle = -1; //�`�悷��t�H���g
	bool Initialize = false;

	int testVolume = 100;
public:
	void Add(std::string text) { //�`�悷�鍀�ڂ�ǉ�
		List.emplace_back(text, 0.0f, 0xffffff);
	}

	void Add(std::string text, float time) { //�`�悷�鍀�ڂ�ǉ�
		List.emplace_back(text, time, 0xffffff);
	}

	void Add(std::string text, float time, unsigned int color) { //�`�悷�鍀�ڂ�ǉ�
		List.emplace_back(text, time, color);
	}

	void Add() { //��s��ǉ�
		List.emplace_back();
	}

	void Add(float time) { //���b�ԋ�s��ǉ�
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

		//���b�Z�[�W����
		int i = 0;
		for (auto& t : List) {
			if (t.message == "") { //��s����
				i++;
				continue;
			}
			int x, y, l;
			SetDrawBlendMode(DX_BLENDMODE_ALPHA, 127); //�w�i�̋�`�𔼓����ɂ���
			GetDrawStringSizeToHandle(&x, &y, &l, t.message.c_str(), t.message.length(), FontHandle); //�e�s�̑傫�����擾
			DrawBox(0, i * y, x, i * y + y, 0x000000, TRUE); //�w�i�̋�`��`��
			SetDrawBlendMode(DX_BLENDMODE_NOBLEND, 0); //����������ʏ�ɖ߂�
			DrawStringToHandle(0, i * y, t.message.c_str(), t.color, FontHandle); //��������t�H���g���g���ĕ`��
		}

		//�|�C���g�`��
		for (auto& p : drawPointList) {
			Transform2D draw = p.drawP;

			int x = static_cast<int>(draw.x);
			int y = static_cast<int>(draw.y);
			DrawCircle(x, y, 10, p.color);

			p.currentDrawTime += deltaTime;
		}

		//�`��b���z��������
		for (auto itr = List.begin(); itr != List.end();) {
			if (itr->currentDrawTime > itr->drawTime) {
				itr = List.erase(itr);
			}
			else {
				itr++;
			}
		}

		//�`��b���z��������
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
	DebugHelper() { FontHandle = CreateFontToHandle("�l�r�S�V�b�N", 16, 2); };
	~DebugHelper() {
		DeleteFontToHandle(FontHandle);
	};
};