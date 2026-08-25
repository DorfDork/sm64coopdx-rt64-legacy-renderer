//
// RT64
//

#ifndef RT64_MINIMAL

#include "../public/rt64.h"

#include <map>
#include <random>
#include <set>

#include "rt64_scene.h"

#include "rt64_device.h"
#include "rt64_instance.h"
#include "rt64_view.h"

// Private

RT64::Scene::Scene(Device *device) {
	assert(device != nullptr);
	this->device = device;

	description.eyeLightDiffuseColor = { 0.08f, 0.08f, 0.08f };
	description.eyeLightSpecularColor = { 0.04f, 0.04f, 0.04f };
	description.skyDiffuseMultiplier = { 1.0f, 1.0f, 1.0f };
	description.skyHSLModifier = { 0.0f, 0.0f, 0.0f };
	description.skyYawOffset = 0.0f;
	description.giDiffuseStrength = 0.7f;
	description.giSkyStrength = 0.35f;
	lightsBufferSize = 0;
	lightsCount = 0;
	volumetricLights = false;

	device->addScene(this);
}

RT64::Scene::~Scene() {
	device->removeScene(this);

	lightsBuffer.Release();

	auto viewsCopy = views;
	for (View *view : viewsCopy) {
		delete view;
	}

	auto instancesCopy = instances;
	for (Instance *instance : instancesCopy) {
		delete instance;
	}
}

void RT64::Scene::update() {
	RT64_LOG_PRINTF("Started scene update");

	for (View *view : views) {
		view->update();
	}

	RT64_LOG_PRINTF("Finished scene update");
}

void RT64::Scene::render(float deltaTimeMs) {
	RT64_LOG_PRINTF("Started scene render");

	for (View *view : views) {
		view->render(deltaTimeMs);
	}

	RT64_LOG_PRINTF("Finished scene render");
}

void RT64::Scene::resize() {
	for (View *view : views) {
		view->resize();
	}
}

void RT64::Scene::setDescription(RT64_SCENE_DESC v) {
	description = v;
}

RT64_SCENE_DESC RT64::Scene::getDescription() const {
	return description;
}

void RT64::Scene::addInstance(Instance *instance) {
	assert(instance != nullptr);
	instances.push_back(instance);
}

void RT64::Scene::removeInstance(Instance *instance) {
	assert(instance != nullptr);

	auto it = std::find(instances.begin(), instances.end(), instance);
	if (it != instances.end()) {
		instances.erase(it);
	}
}

void RT64::Scene::addView(View *view) {
	views.push_back(view);
}

void RT64::Scene::removeView(View *view) {
	assert(view != nullptr);

	auto it = std::find(views.begin(), views.end(), view);
	if (it != views.end()) {
		views.erase(it);
	}
}

const std::vector<RT64::View *> &RT64::Scene::getViews() const {
	return views;
}

void RT64::Scene::setLights(RT64_LIGHT *lightArray, int lightCount) {
	static std::default_random_engine randomEngine;
	static std::uniform_real_distribution<float> randomDistribution(0.0f, 1.0f);

	assert(lightCount > 0);

	const bool lightsChanged = (lastLights.size() != (size_t)(lightCount)) ||
		((lightCount > 0) && (lightArray != nullptr) && (memcmp(lastLights.data(), lightArray, sizeof(RT64_LIGHT) * lightCount) != 0));

	if (lightsChanged) {
		if (lightArray != nullptr) {
			lastLights.assign(lightArray, lightArray + lightCount);
		}
		else {
			lastLights.clear();
		}

		volumetricLights = false;
		for (const RT64_LIGHT &light : lastLights) {
			if ((light.lightType == RT64_LIGHT_TYPE_POINT) && (light.volumetricEnabled != 0)) {
				volumetricLights = true;
				break;
			}
		}

		for (View *view : views) {
			view->skipReprojection();
		}
	}

	size_t newSize = ROUND_UP(sizeof(RT64_LIGHT) * lightCount, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	if (newSize != lightsBufferSize) {
		lightsBuffer.Release();
		lightsBuffer = getDevice()->allocateBuffer(D3D12_HEAP_TYPE_UPLOAD, newSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
		lightsBufferSize = newSize;
	}

	uint8_t *pData;
	size_t i = 0;
	D3D12_CHECK(lightsBuffer.Get()->Map(0, nullptr, (void **)&pData));
	if (lightArray != nullptr) {
		const float degToRad = 3.14159265358979323846f / 180.0f;
		while (i < lightCount) {
			RT64_LIGHT light = lightArray[i];
			const float colorScale = light.intensity / 255.0f;
			const float flickerIntensity = light.flickerIntensity;
			const float flickerMult = (flickerIntensity > 0.0f)
				? (1.0f + ((randomDistribution(randomEngine) * 2.0f - 1.0f) * flickerIntensity))
				: 1.0f;

			light.diffuseColor.x *= colorScale * flickerMult;
			light.diffuseColor.y *= colorScale * flickerMult;
			light.diffuseColor.z *= colorScale * flickerMult;
			light.specularColor.x *= colorScale;
			light.specularColor.y *= colorScale;
			light.specularColor.z *= colorScale;
			light.pitch *= degToRad;
			light.yaw *= degToRad;
			light.roll *= degToRad;
			light.aperturePitch *= degToRad;
			light.apertureYaw *= degToRad;

			memcpy(pData, &light, sizeof(RT64_LIGHT));
			pData += sizeof(RT64_LIGHT);
			i++;
		}
	}

	lightsBuffer.Get()->Unmap(0, nullptr);
	lightsCount = lightCount;
}

ID3D12Resource *RT64::Scene::getLightsBuffer() const {
	return lightsBuffer.Get();
}

int RT64::Scene::getLightsCount() const {
	return lightsCount;
}

bool RT64::Scene::getVolumetricLights() const {
	return volumetricLights;
}

const std::vector<RT64::Instance *> &RT64::Scene::getInstances() const {
	return instances;
}

RT64::Device *RT64::Scene::getDevice() const {
	return device;
}

// Public

DLLEXPORT RT64_SCENE *RT64_CreateScene(RT64_DEVICE *devicePtr) {
	RT64::Device *device = (RT64::Device *)(devicePtr);
	return (RT64_SCENE *)(new RT64::Scene(device));
}

DLLEXPORT void RT64_SetSceneDescription(RT64_SCENE *scenePtr, RT64_SCENE_DESC sceneDesc) {
	RT64::Scene *scene = (RT64::Scene *)(scenePtr);
	scene->setDescription(sceneDesc);
}

DLLEXPORT void RT64_SetSceneLights(RT64_SCENE *scenePtr, RT64_LIGHT *lightArray, int lightCount) {
	RT64::Scene *scene = (RT64::Scene *)(scenePtr);
	scene->setLights(lightArray, lightCount);
}

DLLEXPORT void RT64_DestroyScene(RT64_SCENE *scenePtr) {
	delete (RT64::Scene *)(scenePtr);
}

#endif