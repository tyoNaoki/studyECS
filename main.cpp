#include "DxLib.h"
#include "Debug.h"
#include "Scene.h"
#include "Component.h"
#include "World.h"
#include "SceneView.h"
#include <tuple>

struct Position
{
	Position(float _x,float _y):x(_x),y(_y){};
	Position() = default;
	float x = 0;
	float y = 0;
};

struct  Velocity
{
	Velocity(float _x, float _y) :x(_x), y(_y) {};
	Velocity() = default;
	float x = 0;
	float y = 0;
};




// プログラムは WinMain から始まります
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	ChangeWindowMode(TRUE), DxLib_Init(), SetDrawScreen(DX_SCREEN_BACK); //ウィンドウモード変更と初期化と裏画面設定

	int x = 0, y = 0;
	int Green = GetColor(0, 255, 0);      // 緑の色コードを取得
	Scene scene;
	Position position = Position(0.0f,5.0f);
	auto entity = ECS::world().spawn("",position,Velocity(5.0f,0.1f));
	auto entity2 = ECS::world().spawnEmpty();

	// while(裏画面を表画面に反映, メッセージ処理, 画面クリア)
	while (ScreenFlip() == 0 && ProcessMessage() == 0 && ClearDrawScreen() == 0)
	{
		//auto entity2 = ECS::sWorld.spawnEmpty();
		ECS::world().emplace<Velocity>(entity2);
		//auto comp = ECS::world().getComponent<Position>(entity);
		
		//auto view2 = view.Exclude<Position>();
		//auto view = ECS::world().View<Velocity>(exclude_t<Position>{});

		/*
		for (size_t i = 0; i < packed.size(); i++) {
			Position a;
			std::tie(a) = packed[i].components;
		}
		*/
		auto view = ECS::world().View<Position,Velocity>();
		//auto view2 = view->Exclude<Position>();

		for(auto& x: *view)
		{
			auto& entityID = x.entity;
			auto& vel = view->get<Velocity>(x.components);
			auto& posi = view->get<Position>(x.components);
			//bool hasComp = ECS::world().has<Velocity>(view->get<EntityID>(x));
		}

		/*
		for (EntityID x : *view2)
		{
			bool hasComp = ECS::world().has<Velocity>(x);
			
		}
		*/

		for (auto [entityID,position,velocity] : view->each()) {
			auto id = entityID;
			auto posi = position;
			auto vel = velocity;
		}

		//auto entities = scene.getWorld().findEntitiesWithComponents<Velocity>();
		//auto comp = scene.getWorld().getComponent<Position>(entity);
		//scene.getWorld().removeComponent<Position>(entity);

		//comp->x+= 10.0f;
		//comp = scene.getWorld().getComponent<Position>(entity);

		//scene.getWorld().despawn(entity);
		//scene.getWorld().despawn(entity);

		//Transform2D trans(150,100);
		DrawFormatString(0,0, Green, "座標[%d,%d]", 0,0); // 文字を描画する
	}

	DxLib_End();				// ＤＸライブラリ使用の終了処理

	return 0;				// ソフトの終了 
}