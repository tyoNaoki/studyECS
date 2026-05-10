#pragma once
#include "DxLib.h"

#include "Engine\Core\JobManager.h"
#include "Engine\ECS\World.h"

namespace ECS::System {
	struct Initialization {};
	struct Update {};
	struct LateUpdate {};
	struct Render {};
}

struct Vertex
{
	VECTOR pos;
	unsigned int color;
};

struct Mesh
{
	Vertex vertices[8];
};

class StartSystem;
class RotationSystem;
class UpdateTransformSystem;
class DrawCubeSystem;
struct StartSpawnCubeEvent {};
struct StartTag {}; // Start 済みを示す

static bool initialized = false;
static ECS::World demoWorld;
static Mesh cubeMesh;
constexpr static int countX = 10;
constexpr static int countZ = 10;
VECTOR camPosOffset = VGet(0, 1200, -600);

void InitializeDemoWorld()
{
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
	
	//Cubeの描画
	demoWorld.registerSystem<DrawCubeSystem, ECS::System::Render>();
}

int cube_demo(float deltaTime)
{
	
	if (!initialized) 
	{
		InitializeDemoWorld();
		initialized = true;
	}

	demoWorld.update(deltaTime);

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

struct TransformCompoent
{
	TransformCompoent() {
		position = VGet(0, 0, 0);
		rotation = VGet(0, 0, 0);
		scale = VGet(1, 1, 1);
	}

	VECTOR position;
	VECTOR rotation;
	VECTOR scale;
	MATRIX worldMatrix;
};

struct CubeMeshComponent
{
	CubeMeshComponent(Mesh* m) : mesh(m) {}

	Mesh* mesh;          // 共有Mesh
};

/*
class CubeComponent
{
public:
	CubeMeshComponent cubeMesh;          // 共有Mesh
	TransformCompoent transform; // 個別Transform

	CubeComponent(Mesh* sharedMesh)
	{
		cubeMesh.mesh = sharedMesh;
	}
		
	CubeComponent(Mesh* sharedMesh, TransformCompoent t)
	{
		cubeMesh.mesh = sharedMesh;
		transform = t;
	}
};
*/

Mesh LoadCubeMesh()
{
	Mesh mesh;

	const Vertex base[8] =
	{
		{ VGet(-30,-30,-30), GetColor(255,0,0) },
		{ VGet(30,-30,-30), GetColor(0,255,0) },
		{ VGet(30, 30,-30), GetColor(0,0,255) },
		{ VGet(-30, 30,-30), GetColor(255,255,0) },
		{ VGet(-30,-30, 30), GetColor(0,255,255) },
		{ VGet(30,-30, 30), GetColor(255,0,255) },
		{ VGet(30, 30, 30), GetColor(255,255,255) },
		{ VGet(-30, 30, 30), GetColor(0,0,0) }
	};

	memcpy(mesh.vertices, base, sizeof(base));

	return mesh;
}

struct cubeParallelJob : ECS::JobSystem::IParallelJob<cubeParallelJob>
{
	std::vector<TransformCompoent*> transforms;

	inline void Execute(size_t index) {
		//回転
		transforms[index]->rotation.y += 0.02f;
	}
};

struct updateTransformParallelJob : ECS::JobSystem::IParallelJob<updateTransformParallelJob>
{
	std::vector<TransformCompoent*> transforms;

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
		auto view = world.View<StartSpawnCubeEvent>(ECS::exclude_t<StartTag>{});

		for (auto [entityID, cubeEvent] : view.each()) {
			float width = 60.0f + 30;
			float offsetX = (countX - 1) / 2.0f * width;
			float offsetZ = (countZ - 1) / 2.0f * width;

			int maxCount = countX * countZ;

			
			//Mesh の生成
			cubeMesh = LoadCubeMesh();

			int screenW, screenH;
			GetScreenState(&screenW, &screenH, NULL);
			VECTOR center = VGet(screenW / 2.0f, screenH / 2.0f, 0.0f);

			VECTOR camPos = VAdd(center, camPosOffset);
			VECTOR camTarget = center;

			SetCameraPositionAndTarget_UpVecY(camPos, camTarget);

			for (int i = 0; i < 100; i++) {
				auto entity = world.spawn<CubeMeshComponent,TransformCompoent>(&cubeMesh);

				int ix = i % countX;
				int iz = i / countX;

				auto transform = world.getComponent<TransformCompoent>(entity);
					
				// Transform の初期位置を設定
				transform->position = VGet(
					center.x + (ix * width - offsetX),
					center.y,
					center.z + (iz * width - offsetZ)
				);
			}

			//startTagをつける
			world.emplace<StartTag>(entityID);
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
		auto view = world.View<TransformCompoent>();
		job->transforms.clear();
		job->transforms.reserve(view.size());

		for (auto [entityID, transform] : view.each()) {
			job->transforms.push_back(&transform);
		}
		
		auto handle = job->schedule(job->transforms.size(), 10, 7);
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
		auto view = world.View<CubeMeshComponent, TransformCompoent>();
		job->transforms.clear();
		job->transforms.reserve(view.size());

		for (auto [entityID, cube, transform] : view.each()) {
			job->transforms.push_back(&transform);
		}

		auto handle = job->schedule(job->transforms.size(), 10, 7);
		handle.Complete();
	}
};

class DrawCubeSystem : public ECS::System::SystemBase {
public:
	void onUpdate(ECS::World& world) override {
		auto view = world.View<CubeMeshComponent, TransformCompoent>();

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