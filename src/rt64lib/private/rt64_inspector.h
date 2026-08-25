//
// RT64
//

#pragma once

#include "rt64_common.h"

namespace RT64 {
	class Device;
	class Scene;
	class View;

	class Inspector {
	public:
		static const int MaxMapNameLength = 256;

	private:
		Device* device;
		ID3D12DescriptorHeap* d3dSrvDescHeap;
		RT64_SCENE_DESC *sceneDesc;
		RT64_MATERIAL *material;
		RT64_MATERIAL materialDefaults;
		bool hasMaterialDefaults;
		std::string materialName;
		RT64_MATERIAL *geoLayoutMaterial;
		RT64_LIGHT *geoLayoutLight;
		bool *geoLayoutLightEnabled;
		const RT64_VECTOR3 *geoLayoutOrigins;
		int geoLayoutOriginCount;
		std::string geoLayoutName;
		struct MapNames {
			char bump[MaxMapNameLength] = {};
			char normal[MaxMapNameLength] = {};
			char specular[MaxMapNameLength] = {};
			bool changed = false;
		};
		MapNames mapNames[RT64_INSPECTOR_PANEL_COUNT];
		RT64_LIGHT* lights;
		int *lightCount;
		int maxLightCount;
		bool cameraControl;
		float cameraPanX;
		float cameraPanY;
		int prevCursorX, prevCursorY;
		std::string dumpPath;
		int dumpFrameCount;
		std::vector<std::string> printMessages;

		void setupWithView(View *view, int cursorX, int cursorY);
		void renderViewParams(View *view);
		void renderSceneInspector();
		void renderMaterialAttributes(RT64_MATERIAL *mat, MapNames *panelMapNames);
		void renderLightAttributes(RT64_LIGHT *light, const RT64_VECTOR3 *origins, int originCount);
		void renderMaterialInspector();
		void renderGeoLayoutInspector();
		void renderLightInspector();
		void renderPrint();
		void renderCameraControl(View *view, int cursorX, int cursorY);
	public:
		Inspector(Device* device);
		~Inspector();
		void render(View *activeView, int cursorX, int cursorY);
		void resize();
		void setSceneDescription(RT64_SCENE_DESC *sceneDesc);
		void setMaterial(RT64_MATERIAL *material, const std::string& materialName);
		void setMaterialDefaults(const RT64_MATERIAL *defaults);
		void setGeoLayout(RT64_MATERIAL *material, RT64_LIGHT *light, bool *lightEnabled, const RT64_VECTOR3 *origins, int originCount, const std::string& geoLayoutName);
		void setMapNames(int panel, const char *bumpMapName, const char *normalMapName, const char *specularMapName);
		bool getMapNames(int panel, char *outBumpMapName, char *outNormalMapName, char *outSpecularMapName, int bufferSize);
		bool wantsMouse() const;
		void setLights(RT64_LIGHT *lights, int *lightCount, int maxLightCount);
		void printClear();
		void printMessage(const std::string& message);
		bool handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
	};
};