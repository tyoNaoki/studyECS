#pragma once
#include "DxLib.h"

namespace ECS::Component
{

struct Vertex
{
	VECTOR pos;
	unsigned int color;
};

////////////////Cube専用////////////////////////
//Cubeの頂点データを格納する構造体
struct CubeMesh
{
	Vertex vertices[8];
};

struct CubeMeshRendererComponent
{
	CubeMeshRendererComponent(CubeMesh* m) : mesh(m) {}

	CubeMesh* mesh;          // 共有Mesh
};

/////////Cube専用////////////////////////

struct MeshCompoennt
{
	MeshCompoennt(int m) : model(m) {}
	int model;               // モデルハンドル
};

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

} //namespace ECS::Component