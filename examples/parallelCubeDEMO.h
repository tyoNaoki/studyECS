#pragma once
#include "DxLib.h"

#include "Engine\JobSystem\JobManager.h"
#include "Engine\ECS\World.h"
#include "Engine\ECS\Component\BasicComponent.h"
#include "Engine\Asset\AssetManager.h"

namespace ECS::System {
	struct Initialization {};
	struct Update {};
	struct LateUpdate {};
	struct PreRender {};
	struct Render {};
}

//システムの前方宣言
class StartSystem;
class RotationSystem;
class UpdateTransformSystem;
class DrawSkyBallSystem;
class DrawCubeSystem;

struct SkyBallTag {}; //SkyBallを示すタグ
struct StartSpawnCubeEvent {};
struct StartedTag {}; // Start 済みを示す

static bool initialized = false;
int64_t prevTime;

static ECS::World demoWorld;

//Cubeの数のパラメータ
constexpr static int countX = 20;
constexpr static int countZ = 20;
constexpr static int totalCubes = countX * countZ;
constexpr static int batchSize = 32;
VECTOR camPosOffset = VGet(0, 1200, -600);

// ---- FPS 計測 ----
static float fpsTimer = 0.0f;
static int fpsCount = 0;
static int fps = 0;

void InitializeDemoWorld()
{
	prevTime = GetNowHiPerformanceCount();

	// JobManager の初期化
	auto& jm = ECS::JobSystem::JobManager::Instance();
	jm.initialize(100, 7);
	
	// World の生成,初期化
	demoWorld = ECS::World();
	demoWorld.initialize();

	//startイベントのスポーン
	demoWorld.spawn<StartSpawnCubeEvent>();

	// 100個の Cube の生成
	//// カメラの初期化
	demoWorld.registerSystem<StartSystem, ECS::System::Initialization>();

	//Cubeの並列回転
	demoWorld.registerSystem<RotationSystem, ECS::System::Update>();

	//Transformの更新
	demoWorld.registerSystem<UpdateTransformSystem, ECS::System::LateUpdate>();

	//SkyBallの描画
	demoWorld.registerSystem<DrawSkyBallSystem, ECS::System::PreRender>();
	
	//Cubeの描画
	demoWorld.registerSystem<DrawCubeSystem, ECS::System::Render>();

	
	//ジョブマネージャー起動
	jm.start();
	
}

int cube_demo()
{
	if (!initialized)
	{
		InitializeDemoWorld();
		initialized = true;
	}

	int64_t now = GetNowHiPerformanceCount();
	float deltaTime = (now - prevTime) / 1000000.0f; // 秒

	fpsCount++;
	fpsTimer += deltaTime;

	if (fpsTimer >= 1.0f)
	{
		fps = fpsCount;
		fpsCount = 0;
		fpsTimer = 0.0f;
	}

	demoWorld.update(deltaTime);
	demoWorld.render();
	demoWorld.cleanup();

	//FPS表示
	DrawFormatString(0, 0, GetColor(255, 255, 255), "FPS: %d", fps);
	DrawFormatString(0, 80, GetColor(255, 255, 255), "Cube Num: %d", totalCubes);
	DrawFormatString(0, 160, GetColor(255, 255, 255), "ENTER:ExitDemo");

	prevTime = now;

	return 0;
}

MATRIX MGetRotXYZ(VECTOR rot)
{
	MATRIX rx = MGetRotX(rot.x);
	MATRIX ry = MGetRotY(rot.y);
	MATRIX rz = MGetRotZ(rot.z);

	// Unity と同じ回転順序（Z → X → Y）
	return MMult(MMult(rz, rx), ry);
}

struct cubeParallelJob : ECS::JobSystem::IParallelJob<cubeParallelJob>
{
	std::vector<ECS::Component::TransformCompoent*> transforms;

	inline void Execute(size_t index) {
		//回転
		transforms[index]->rotation.y += 0.02f;
	}
};

struct updateTransformParallelJob : ECS::JobSystem::IParallelJob<updateTransformParallelJob>
{
	std::vector<ECS::Component::TransformCompoent*> transforms;

	inline void Execute(size_t index) {
		MATRIX S = MGetScale(transforms[index]->scale);
		MATRIX R = MGetRotXYZ(transforms[index]->rotation);
		MATRIX T = MGetTranslate(transforms[index]->position);

		transforms[index]->worldMatrix = MMult(MMult(S, R), T);
	}
};


class StartSystem : public ECS::System::SystemBase {
public:
	void onUpdate(ECS::World& world) override {

		//startイベントを持ち、startTagを持たないEntityを検索
		auto view = world.View<StartSpawnCubeEvent>(ECS::exclude_t<StartedTag>{});

		for (auto [entityID, cubeEvent] : view.each()) {
			SetFontSize(80);
			SetFontThickness(2);

			float width = 60.0f + 30;
			float offsetX = (countX - 1) / 2.0f * width;
			float offsetZ = (countZ - 1) / 2.0f * width;

			//カメラの初期設定
			int screenW, screenH;
			GetScreenState(&screenW, &screenH, NULL);
			VECTOR center = VGet(screenW / 2.0f, screenH / 2.0f, 0.0f);
			VECTOR camPos = VAdd(center, camPosOffset);

			//カメラの初期位置と角度
			SetCameraPositionAndTarget_UpVecY(camPos, center);

			//CubeMeshのロード
			auto& assets = ECS::AssetManagement::AssetManager::Instance();
			auto cubeMesh = assets.LoadCubeMesh();

			//キューブの生成
			for (int i = 0; i < totalCubes; i++) {
				auto entity = world.spawn<ECS::Component::CubeMeshRendererComponent, ECS::Component::TransformCompoent>(cubeMesh);

				int ix = i % countX;
				int iz = i / countX;

				auto transform = world.getComponent<ECS::Component::TransformCompoent>(entity);
					
				//初期位置を設定
				transform->position = VGet(
					center.x + (ix * width - offsetX),
					center.y,
					center.z + (iz * width - offsetZ)
				);
			}

			//SkyBallの読み込みと初期設定
			
			auto entity = world.spawn<ECS::Component::MeshCompoennt, SkyBallTag>(assets.LoadModel("assets/skyBall.mv1"));
			auto skymodelComp = world.getComponent<ECS::Component::MeshCompoennt>(entity);

			MV1SetScale(skymodelComp->model, VGet(2.0f, 2.0f, 2.0f));  // 2倍に拡大
			MV1SetPosition(skymodelComp->model, GetCameraPosition());

			//startedTagをつける
			world.emplace<StartedTag>(entityID);
		}
	}
};

class UpdateTransformSystem : public ECS::System::SystemBase {
	std::shared_ptr<updateTransformParallelJob> job;

public:
	void onCreate(ECS::World& world) override {
		job = updateTransformParallelJob::create();
	}

	void onUpdate(ECS::World& world) override {
		auto view = world.View<ECS::Component::TransformCompoent>();
		job->transforms.clear();
		job->transforms.reserve(view.size());

		for (auto [entityID, transform] : view.each()) {
			job->transforms.push_back(&transform);
		}
		
		auto handle = job->schedule(job->transforms.size(), batchSize, 7);
		handle.Complete();
	}
};

class RotationSystem : public ECS::System::SystemBase {
	std::shared_ptr<cubeParallelJob> job;

public:
	void onCreate(ECS::World& world)override {
		job = cubeParallelJob::create();
	}

	void onUpdate(ECS::World& world)override {
		auto view = world.View<ECS::Component::CubeMeshRendererComponent, ECS::Component::TransformCompoent>();
		job->transforms.clear();
		job->transforms.reserve(view.size());

		for (auto [entityID, cube, transform] : view.each()) {
			job->transforms.push_back(&transform);
		}

		auto handle = job->schedule(job->transforms.size(), batchSize, 7);
		handle.Complete();
	}
};

class DrawSkyBallSystem : public ECS::System::SystemBase {
public:
	void onUpdate(ECS::World& world) override {
		SetUseLighting(FALSE);
		SetUseZBuffer3D(TRUE);
		auto view = world.View<ECS::Component::MeshCompoennt, SkyBallTag>();

		for (auto [entityID, mesh, tag] : view.each()) {
			MV1DrawModel(mesh.model);
		}

		SetUseLighting(TRUE);
		SetUseZBuffer3D(FALSE);
	}
};

class DrawCubeSystem : public ECS::System::SystemBase {
public:
	void onUpdate(ECS::World& world) override {
		auto view = world.View<ECS::Component::CubeMeshRendererComponent, ECS::Component::TransformCompoent>();

		for (auto [entity, cube, transform] : view.each()) {
			VECTOR world[8];

			for (int i = 0; i < 8; i++)
			{
				world[i] = VTransform(cube.mesh->vertices[i].pos, transform.worldMatrix);
			}

			DrawTriangle3D(world[0], world[1], world[2], cube.mesh->vertices[0].color, TRUE);
			DrawTriangle3D(world[0], world[2], world[3], cube.mesh->vertices[0].color, TRUE);

			// 背面
			DrawTriangle3D(world[5], world[4], world[7], cube.mesh->vertices[5].color, TRUE);
			DrawTriangle3D(world[5], world[7], world[6], cube.mesh->vertices[5].color, TRUE);
			// 左面
			DrawTriangle3D(world[4], world[0], world[3], cube.mesh->vertices[4].color, TRUE);
			DrawTriangle3D(world[4], world[3], world[7], cube.mesh->vertices[4].color, TRUE);
			// 右面
			DrawTriangle3D(world[1], world[5], world[6], cube.mesh->vertices[1].color, TRUE);
			DrawTriangle3D(world[1], world[6], world[2], cube.mesh->vertices[1].color, TRUE);
			// 上面
			DrawTriangle3D(world[3], world[2], world[6], cube.mesh->vertices[3].color, TRUE);
			DrawTriangle3D(world[3], world[6], world[7], cube.mesh->vertices[3].color, TRUE);
			// 底面
			DrawTriangle3D(world[4], world[5], world[1], cube.mesh->vertices[4].color, TRUE);
			DrawTriangle3D(world[4], world[1], world[0], cube.mesh->vertices[4].color, TRUE);
		}
	}
};