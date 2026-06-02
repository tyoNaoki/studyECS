#include "Editor/EditorApp.h"
#include "Engine/ECS/World.h"
#include <DxLib.h>

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

	float positionArray[3] = { 0.0f, 0.0f, 0.0f };

	int sceneTextGraph = MakeScreen(1280, 720, TRUE);

	// while(裏画面を表画面に反映, メッセージ処理, 画面クリア)
	while (ProcessMessage() == 0&& CheckHitKey(KEY_INPUT_RETURN) == 0)
	{
		
		//DrawBox(0, 0, 100, 100, GetColor(255, 0, 0), TRUE);
		
		//SetDrawScreen(sceneTextGraph);
		ClearDrawScreen();
		// world->Render(); // ← ECS の RenderSystem を呼ぶ

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		DrawDockSpace();
		DrawSceneView(sceneTextGraph);
		DrawHierarchy();	
		DrawInspector();

		//ImGui::Begin("Demo");
		//ImGui::Text("Hello ImGui!");
		//ImGui::End();

		//ImGui::EndFrame();

		/*if (ImGui::Begin("window")) {
			ImGui::Text("hoge");
		}

		ImGui::End();

		if (ImGui::Begin("Transform")) {
			ImGui::DragFloat3("Position", positionArray, 0.01f);
		}

		ImGui::End();*/

		 //ウィンドウモード変更と初期化と裏画面設定

		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		ScreenFlip();
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

	//sceneRT = MakeScreen(1280, 720, TRUE);

	SetDrawScreen(DX_SCREEN_BACK); //ウィンドウモード変更と初期化と裏画面設定

	world = new ECS::World();
	world->initialize();

	return true;
}

void EditorApp::ShutDown()
{
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	delete world;

	DxLib_End();				// ＤＸライブラリ使用の終了処理
}

void EditorApp::DrawDockSpace()
{
	ImGuiWindowFlags window_flags =
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus;

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);

	ImGui::Begin("DockSpace", nullptr, window_flags);

	ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
	ImGui::DockSpace(dockspace_id);

	static bool first_time = true;

	if (first_time)
	{
		first_time = false;

		// 既存レイアウトをクリア
		ImGui::DockBuilderRemoveNode(dockspace_id);
		ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

		// 分割
		ImGuiID dock_main_id = dockspace_id;
		ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);
		ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.25f, nullptr, &dock_main_id);

		// ウインドウをドッキング
		ImGui::DockBuilderDockWindow("SceneView", dock_main_id);
		ImGui::DockBuilderDockWindow("Hierarchy", dock_right);
		ImGui::DockBuilderDockWindow("Inspector", dock_bottom);

		ImGui::DockBuilderFinish(dockspace_id);
	}

	ImGui::End();
}

void EditorApp::DrawSceneView(int sceneGraph)
{
	ImGui::Begin("SceneView");

	ImVec2 size = ImGui::GetContentRegionAvail();

	ImTextureID srv = (ImTextureID)GetGraphID3D11Texture2D(sceneGraph);

	RenderTarget* rt;                       // ImGuiに表示したい画像用RT
	ID3D* srvHeap = ImGUIImage::GetImGUIDescriptorHeap();

	ImTextureID id = ImGUIImage::GetImage(srvHeap, rt);

	if (srv) {
		ImGui::Image((void*)srv, size);
	}else {
		ImGui::Text("SRV is null!");
	}
		
	ImGui::End();
}

void EditorApp::DrawHierarchy()
{
	ImGui::Begin("Hierarchy");
	// world->ListEntities();
	ImGui::End();
}

void EditorApp::DrawInspector()
{
	ImGui::Begin("Inspector");
	// world->DrawComponents(selectedEntity);
	ImGui::End();
}

ID3D11ShaderResourceView* EditorApp::GetUseDirect3D11ShaderResourceView()
{
	ID3D11Texture2D* sceneTexture = nullptr;

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = 1280;
	desc.Height = 720;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	g_pd3dDevice->CreateTexture2D(&desc, nullptr, &sceneTexture);

	// RenderTargetView
	ID3D11RenderTargetView* sceneRTV = nullptr;
	g_pd3dDevice->CreateRenderTargetView(sceneTexture, nullptr, &sceneRTV);

	// ShaderResourceView
	ID3D11ShaderResourceView* sceneSRV = nullptr;
	g_pd3dDevice->CreateShaderResourceView(sceneTexture, nullptr, &sceneSRV);

	return sceneSRV;
}

