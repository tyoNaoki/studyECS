#pragma once

struct  Transform
{
	Transform() :x(0.0f), y(0.0f),z(0.0f) {};
	Transform(float _x, float _y,float _z) :x(_x), y(_y),z(_z) {};
	float x;
	float y;
	float z;
};

struct Transform2D
{
	Transform2D():x(0.0f),y(0.0f){};
	Transform2D(float _x,float _y) :x(_x), y(_y) {};
	float x;
	float y;
};
