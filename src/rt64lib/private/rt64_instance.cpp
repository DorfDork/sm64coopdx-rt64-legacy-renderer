//
// RT64
//

#ifndef RT64_MINIMAL

#include <cstring>

#include "../public/rt64.h"
#include "rt64_instance.h"
#include "rt64_scene.h"

// Private

const RT64_MATERIAL DefaultMaterial;

RT64::Instance::Instance(Scene *scene) {
	assert(scene != nullptr);

	this->scene = scene;
	mesh = nullptr;
	diffuseTexture = nullptr;
	normalTexture = nullptr;
	specularTexture = nullptr;
	diffuse2Texture = nullptr;
	bumpTexture = nullptr;
	transform = XMMatrixIdentity();
	previousTransform = XMMatrixIdentity();
	normalMatrix = XMMatrixIdentity();
	memset(lastTransformRaw, 0, sizeof(lastTransformRaw));
	lastTransformRawValid = false;
	material = DefaultMaterial;
	shader = nullptr;
	scissorRect = { 0, 0, 0, 0 };
	viewportRect = { 0, 0, 0, 0 };
	flags = 0;

	scene->addInstance(this);
}

RT64::Instance::~Instance() {
	scene->removeInstance(this);
}

void RT64::Instance::setMesh(Mesh* mesh) {
	this->mesh = mesh;
}

RT64::Mesh* RT64::Instance::getMesh() const {
	return mesh;
}

void RT64::Instance::setMaterial(const RT64_MATERIAL &material) {
	this->material = material;
}

const RT64_MATERIAL &RT64::Instance::getMaterial() const {
	return material;
}

void RT64::Instance::setShader(Shader *shader) {
	this->shader = shader;
}

RT64::Shader *RT64::Instance::getShader() const {
	return shader;
}

void RT64::Instance::setDiffuseTexture(Texture *texture) {
	this->diffuseTexture = texture;
}

RT64::Texture *RT64::Instance::getDiffuseTexture() const {
	return diffuseTexture;
}

void RT64::Instance::setNormalTexture(Texture* texture) {
	this->normalTexture = texture;
}

RT64::Texture* RT64::Instance::getNormalTexture() const {
	return normalTexture;
}

void RT64::Instance::setSpecularTexture(Texture* texture) {
	this->specularTexture = texture;
}

RT64::Texture* RT64::Instance::getSpecularTexture() const {
	return specularTexture;
}

void RT64::Instance::setDiffuse2Texture(Texture* texture) {
	this->diffuse2Texture = texture;
}

RT64::Texture* RT64::Instance::getDiffuse2Texture() const {
	return diffuse2Texture;
}

void RT64::Instance::setBumpTexture(Texture* texture) {
	this->bumpTexture = texture;
}

RT64::Texture* RT64::Instance::getBumpTexture() const {
	return bumpTexture;
}

inline XMMATRIX matrixFromFloats(const float m[4][4]) {
	return XMMATRIX(
		m[0][0], m[0][1], m[0][2], m[0][3],
		m[1][0], m[1][1], m[1][2], m[1][3],
		m[2][0], m[2][1], m[2][2], m[2][3],
		m[3][0], m[3][1], m[3][2], m[3][3]
	);
}

void RT64::Instance::setTransform(const float m[4][4]) {
	if (lastTransformRawValid && (memcmp(lastTransformRaw, m, sizeof(lastTransformRaw)) == 0)) {
		return;
	}

	memcpy(lastTransformRaw, m, sizeof(lastTransformRaw));
	lastTransformRawValid = true;
	transform = matrixFromFloats(m);
	XMMATRIX upper3x3 = transform;
	upper3x3.r[0].m128_f32[3] = 0.f;
	upper3x3.r[1].m128_f32[3] = 0.f;
	upper3x3.r[2].m128_f32[3] = 0.f;
	upper3x3.r[3].m128_f32[0] = 0.f;
	upper3x3.r[3].m128_f32[1] = 0.f;
	upper3x3.r[3].m128_f32[2] = 0.f;
	upper3x3.r[3].m128_f32[3] = 1.f;

	XMVECTOR det;
	normalMatrix = XMMatrixTranspose(XMMatrixInverse(&det, upper3x3));
}

XMMATRIX RT64::Instance::getTransform() const {
	return transform;
}

XMMATRIX RT64::Instance::getNormalMatrix() const {
	return normalMatrix;
}

void RT64::Instance::setPreviousTransform(const float m[4][4]) {
	previousTransform = matrixFromFloats(m);
}

XMMATRIX RT64::Instance::getPreviousTransform() const {
	return previousTransform;
}

void RT64::Instance::setScissorRect(const RT64_RECT &rect) {
	scissorRect = rect;
}

RT64_RECT RT64::Instance::getScissorRect() const {
	return scissorRect;
}

bool RT64::Instance::hasScissorRect() const {
	return (scissorRect.w > 0) && (scissorRect.h > 0);
}

void RT64::Instance::setViewportRect(const RT64_RECT &rect) {
	viewportRect = rect;
}

RT64_RECT RT64::Instance::getViewportRect() const {
	return viewportRect;
}

bool RT64::Instance::hasViewportRect() const {
	return (viewportRect.w > 0) && (viewportRect.h > 0);
}

void RT64::Instance::setFlags(int v) {
	flags = v;
}

unsigned int RT64::Instance::getFlags() const {
	return flags;
}

void RT64::Instance::setUniformBlocks(const RT64_SHADER_UNIFORM_BLOCK *blocks, unsigned int blockCount) {
	uniformBlocks.clear();
	uniformBlockData.clear();

	if ((blocks == nullptr) || (blockCount == 0)) {
		return;
	}

	for (unsigned int i = 0; i < blockCount; i++) {
		const RT64_SHADER_UNIFORM_BLOCK &block = blocks[i];
		if ((block.data == nullptr) || (block.size == 0) || (block.shaderRegister >= RT64_MAX_SHADER_UNIFORM_BLOCKS)) {
			continue;
		}

		UniformBlock stored;
		stored.shaderRegister = block.shaderRegister;
		stored.size = block.size;
		stored.dataOffset = (unsigned int)(uniformBlockData.size());
		uniformBlocks.push_back(stored);

		const unsigned char *bytes = (const unsigned char *)(block.data);
		uniformBlockData.insert(uniformBlockData.end(), bytes, bytes + block.size);
	}
}

const std::vector<RT64::Instance::UniformBlock> &RT64::Instance::getUniformBlocks() const {
	return uniformBlocks;
}

const unsigned char *RT64::Instance::getUniformBlockData() const {
	return uniformBlockData.data();
}

// Public

DLLEXPORT RT64_INSTANCE *RT64_CreateInstance(RT64_SCENE *scenePtr) {
	RT64::Scene *scene = (RT64::Scene *)(scenePtr);
	RT64::Instance *instance = new RT64::Instance(scene);
	return (RT64_INSTANCE *)(instance);
}

DLLEXPORT void RT64_SetInstanceDescription(RT64_INSTANCE *instancePtr, const RT64_INSTANCE_DESC *instanceDesc) {
	assert(instancePtr != nullptr);
	assert(instanceDesc != nullptr);
	assert(instanceDesc->mesh != nullptr);
	assert(instanceDesc->diffuseTexture != nullptr);
	assert(instanceDesc->shader != nullptr);

	RT64::Instance *instance = (RT64::Instance *)(instancePtr);
	instance->setMesh((RT64::Mesh *)(instanceDesc->mesh));
	instance->setTransform(instanceDesc->transform.m);
	instance->setPreviousTransform(instanceDesc->previousTransform.m);
	instance->setMaterial(instanceDesc->material);
	instance->setShader((RT64::Shader *)(instanceDesc->shader));
	instance->setDiffuseTexture((RT64::Texture *)(instanceDesc->diffuseTexture));
	instance->setNormalTexture((RT64::Texture *)(instanceDesc->normalTexture));
	instance->setSpecularTexture((RT64::Texture *)(instanceDesc->specularTexture));
	instance->setDiffuse2Texture((RT64::Texture *)(instanceDesc->diffuse2Texture));
	instance->setBumpTexture((RT64::Texture *)(instanceDesc->bumpTexture));
	instance->setFlags(instanceDesc->flags);
	instance->setScissorRect(instanceDesc->scissorRect);
	instance->setViewportRect(instanceDesc->viewportRect);
	instance->setUniformBlocks(instanceDesc->shaderUniformBlocks, instanceDesc->shaderUniformBlockCount);
}

DLLEXPORT void RT64_DestroyInstance(RT64_INSTANCE *instancePtr) {
	delete (RT64::Instance *)(instancePtr);
}

#endif