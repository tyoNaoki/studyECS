#pragma once
#include <windows.h>
#include <DxLib.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
#include <iostream>

namespace ECS {
    class World;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ImGuiにプロシージャの情報を流す
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);

class EditorApp
{
public:
    void Run();
private:
    bool Initialize();
	void ShutDown();

    void DrawDockSpace();
    void DrawSceneView();
    void DrawHierarchy();
    void DrawInspector();

	ECS::World* world;
    ID3D11Device* g_pd3dDevice = nullptr;
    ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
};