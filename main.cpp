#include "DxLib.h"
#include "ecsTest.h"
#include <iostream>
#include <windows.h>

void viewConsole()
{
	AllocConsole();  // �R���\�[���𐶐�
	FILE* stream;
	freopen_s(&stream, "CONOUT$", "w", stdout);  // �W���o�͂��R���\�[���Ƀ��_�C���N�g
}

// �v���O������ WinMain ����n�܂�܂�
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	ChangeWindowMode(TRUE), DxLib_Init(), SetDrawScreen(DX_SCREEN_BACK); //�E�B���h�E���[�h�ύX�Ə������Ɨ���ʐݒ�

	viewConsole();

	int x = 0, y = 0;
	int Green = GetColor(0, 255, 0);      // �΂̐F�R�[�h���擾

	bool isNotFinish = false;
	
	// while(����ʂ�\��ʂɔ��f, ���b�Z�[�W����, ��ʃN���A)
	while (!isNotFinish && ScreenFlip() == 0 && ProcessMessage() == 0 && ClearDrawScreen() == 0)
	{
		test();
		//Transform2D trans(150,100);
		DrawFormatString(0,0, Green, "���W[%d,%d]", 0,0); // ������`�悷��
	}

	DxLib_End();				// �c�w���C�u�����g�p�̏I������

	return 0;				// �\�t�g�̏I�� 
}