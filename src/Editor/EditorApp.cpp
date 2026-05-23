#include "Editor/EditorApp.h"

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wp, lp))
		return true;

	return DefWindowProc(hWnd, msg, wp, lp);
}

void EditorApp::Run()
{
	if(!Initialize()) {
		return;
	}

	bool isNotFinish = false;
	float positionArray[3] = { 0.0f, 0.0f, 0.0f };

	// while(裏画面を表画面に反映, メッセージ処理, 画面クリア)
	while (!isNotFinish && ScreenFlip() == 0 && ProcessMessage() == 0 && ClearDrawScreen() == 0 && CheckHitKey(KEY_INPUT_RETURN) == 0)
	{
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);

		ImGui::Begin("DockSpace", nullptr,
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoNavFocus);

		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);

		ImGui::End();

		//ImGui::Begin("Demo");
		//ImGui::Text("Hello ImGui!");
		//ImGui::End();

		//ImGui::EndFrame();

		if (ImGui::Begin("window")) {
			ImGui::Text("hoge");
		}

		ImGui::End();

		if (ImGui::Begin("Transform")) {
			ImGui::DragFloat3("Position", positionArray, 0.01f);
		}

		ImGui::End();

		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	}

	ShutDown();
}

bool EditorApp::Initialize()
{
	ChangeWindowMode(TRUE);
	SetGraphMode(1280, 720, 32);
	SetUseDirect3DVersion(DX_DIRECT3D_11);
	//SetGraphMode(2560, 1440, 32);

	if (DxLib_Init() == -1) {
		return false;
	}

	// ImGui の初期化
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	g_pd3dDevice = (ID3D11Device*)(GetUseDirect3D11Device());
	g_pd3dDeviceContext = (ID3D11DeviceContext*)GetUseDirect3D11DeviceContext();
	ImGui_ImplWin32_Init(GetMainWindowHandle());
	ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
	SetHookWinProc(WndProc);
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // キーボードナビゲーションを有効にする
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	SetDrawScreen(DX_SCREEN_BACK); //ウィンドウモード変更と初期化と裏画面設定
	return true;
}

void EditorApp::ShutDown()
{
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	DxLib_End();				// ＤＸライブラリ使用の終了処理
}

void EditorApp::DrawDockSpace()
{
}

void EditorApp::DrawSceneView()
{
}

void EditorApp::DrawHierarchy()
{
}

void EditorApp::DrawInspector()
{
}
