#pragma once
#include <windows.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_internal.h"
#include <iostream>

#include <d3d11.h>
#include <dxgi.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

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
    void DrawSceneView(int sceneGraph);
    void DrawHierarchy();
    void DrawInspector();

	ID3D11ShaderResourceView* GetUseDirect3D11ShaderResourceView();

	ECS::World* world;
    ID3D11Device* g_pd3dDevice = nullptr;
    ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;

    int sceneRT = -1; // SceneView 用の RenderTexture
};