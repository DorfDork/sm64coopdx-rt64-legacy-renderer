//
// RT64
//

#ifndef RT64_MINIMAL

#include "rt64_inspector.h"

#include "rt64_device.h"
#include "rt64_scene.h"
#include "rt64_view.h"

#include "im3d/im3d.h"
#include "im3d/im3d_math.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx12.h"
#include "imgui/imgui_impl_win32.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>

std::string dateAsFilename() {
    std::time_t time = std::time(nullptr);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%F_%T");
    std::string s = ss.str();
    std::replace(s.begin(), s.end(), ':', '-');
    return s;
}

RT64::Inspector::Inspector(Device* device) {
    this->device = device;
    prevCursorX = prevCursorY = 0;
    cameraControl = false;
    cameraPanX = 0.0f;
    cameraPanY = 0.0f;
    dumpFrameCount = 0;
    sceneDesc = nullptr;
    material = nullptr;
    hasMaterialDefaults = false;
    geoLayoutMaterial = nullptr;
    geoLayoutLight = nullptr;
    geoLayoutLightEnabled = nullptr;
    geoLayoutOrigins = nullptr;
    geoLayoutOriginCount = 0;
    lights = nullptr;
    lightCount = 0;
    maxLightCount = 0;

    // Im3D
    Im3d::AppData &appData = Im3d::GetAppData();

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();


    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    D3D12_CHECK(device->getD3D12Device()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&d3dSrvDescHeap)));

    ImGui_ImplWin32_Init(device->getHwnd());
    ImGui_ImplDX12_Init(device->getD3D12Device(), 2, DXGI_FORMAT_R8G8B8A8_UNORM, d3dSrvDescHeap, d3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(), d3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

    device->addInspector(this);
}

RT64::Inspector::~Inspector() {
    device->removeInspector(this);
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    d3dSrvDescHeap->Release();
}

void RT64::Inspector::render(View *activeView, int cursorX, int cursorY) {
    setupWithView(activeView, cursorX, cursorY);
    
    // Start the frame.
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    Im3d::NewFrame();

    renderViewParams(activeView);
    renderSceneInspector();
    renderMaterialInspector();
    renderGeoLayoutInspector();
    renderLightInspector();
    renderCameraControl(activeView, cursorX, cursorY);
    renderPrint();

    Im3d::EndFrame();

    // If dumping frames is active, save the current state of the RTV into a file.
    if (!dumpPath.empty()) {
        const int LeadingZeroes = 8;
        std::ostringstream oss;
        oss << dumpPath << "/" << std::setw(LeadingZeroes) << std::setfill('0') << dumpFrameCount++ << ".bmp";
        device->dumpRenderTarget(oss.str());
    }

    activeView->renderInspector(this);

    // Send the commands to D3D12.
    device->getD3D12CommandList()->SetDescriptorHeaps(1, &d3dSrvDescHeap);
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), device->getD3D12CommandList());
}

void RT64::Inspector::resize() {
    ImGui_ImplDX12_InvalidateDeviceObjects();
    ImGui_ImplDX12_CreateDeviceObjects();
}

void RT64::Inspector::renderViewParams(View *view) {
    assert(view != nullptr);

    ImGui::Begin("View Params Inspector");
    int diSamples = view->getDISamples();
    int giSamples = view->getGISamples();
    int maxLights = view->getMaxLights();
    int maxReflections = view->getMaxReflections();
    float motionBlurStrength = view->getMotionBlurStrength();
    int motionBlurSamples = view->getMotionBlurSamples();
    int visualizationMode = view->getVisualizationMode();
    int resScale = lround(view->getResolutionScale() * 100.0f);
    int upscaleMode = (int)(view->getUpscaleMode());
    bool denoiser = view->getDenoiserEnabled();

    ImGui::DragInt("DI samples", &diSamples, 0.1f, 0, 32);
    ImGui::DragInt("GI samples", &giSamples, 0.1f, 0, 32);
    ImGui::DragInt("Max lights", &maxLights, 0.1f, 0, 16);
    ImGui::DragInt("Max reflections", &maxReflections, 0.1f, 0, 32);
    ImGui::DragFloat("Motion blur strength", &motionBlurStrength, 0.1f, 0.0f, 10.0f);
    ImGui::DragInt("Motion blur samples", &motionBlurSamples, 0.1f, 0, 256);
    ImGui::Combo("Visualization Mode", &visualizationMode, "Final\0Shading position\0Shading normal\0Shading specular\0"
        "Color\0Instance ID\0Direct light raw\0Direct light filtered\0Indirect light raw\0Indirect light filtered\0"
        "Reflection\0Refraction\0Transparent\0Motion vectors\0Reactive mask\0Lock mask\0Depth\0");

    ImGui::Combo("Upscale Mode", &upscaleMode, "Bilinear\0AMD FSR 2\0NVIDIA DLSS\0Intel XeSS\0");

    const RT64::UpscaleMode eUpscaleMode = static_cast<RT64::UpscaleMode>(upscaleMode);
    if (view->getUpscalerInitialized(eUpscaleMode)) {
        int upscalerQualityMode = (int)(view->getUpscalerQualityMode());
        float upscalerSharpness = view->getUpscalerSharpness();
        bool upscalerResolutionOverride = view->getUpscalerResolutionOverride();
        bool upscalerReactiveMask = view->getUpscalerReactiveMask();
        bool upscalerLockMask = view->getUpscalerLockMask();
        if (eUpscaleMode != RT64::UpscaleMode::Bilinear) {
            ImGui::Combo("Quality", &upscalerQualityMode, "Ultra Performance\0Performance\0Balanced\0Quality\0Ultra Quality\0Native\0Auto\0");

            if (eUpscaleMode != RT64::UpscaleMode::XeSS) {
                ImGui::DragFloat("Sharpness", &upscalerSharpness, 0.01f, 0.0f, 1.0f);
            }

            ImGui::Checkbox("Resolution Override", &upscalerResolutionOverride);
            if (upscalerResolutionOverride) {
                ImGui::SameLine();
                ImGui::DragInt("Resolution %", &resScale, 1, 1, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
            }

            if (eUpscaleMode == RT64::UpscaleMode::FSR) {
                ImGui::Checkbox("Reactive Mask", &upscalerReactiveMask);
            }

            ImGui::Checkbox("Lock Mask", &upscalerLockMask);

            view->setUpscalerQualityMode((Upscaler::QualityMode)(upscalerQualityMode));
            view->setUpscalerSharpness(upscalerSharpness);
            view->setUpscalerResolutionOverride(upscalerResolutionOverride);
            view->setUpscalerReactiveMask(upscalerReactiveMask);
            view->setUpscalerLockMask(upscalerLockMask);
        }
        else {
            ImGui::DragInt("Resolution %", &resScale, 1, 1, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
        }

        view->setUpscaleMode(eUpscaleMode);
    }

    ImGui::Checkbox("Denoiser", &denoiser);

    // Dumping toggle.
    bool isDumping = !dumpPath.empty();
    if (ImGui::Button(isDumping ? "Stop dump" : "Dump frames")) {
        if (isDumping) {
            dumpPath = std::string();
        }
        else {
            dumpPath = "dump/" + dateAsFilename();
            std::filesystem::create_directories(dumpPath);
            dumpFrameCount = 0;
        }
    }

    // Update viewport parameters.
    view->setDISamples(diSamples);
    view->setGISamples(giSamples);
    view->setMaxLights(maxLights);
    view->setMaxReflections(maxReflections);
    view->setMotionBlurStrength(motionBlurStrength);
    view->setMotionBlurSamples(motionBlurSamples);
    view->setVisualizationMode(visualizationMode);
    view->setResolutionScale(resScale / 100.0f);
    view->setDenoiserEnabled(denoiser);

    ImGui::End();
}

static void snapColorChannels(RT64_VECTOR3 *v) {
    v->x = (float)(std::min(std::max((int)(lroundf(v->x)), 0), 255));
    v->y = (float)(std::min(std::max((int)(lroundf(v->y)), 0), 255));
    v->z = (float)(std::min(std::max((int)(lroundf(v->z)), 0), 255));
}

static float sPickerColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
static float sPickerRefColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

static bool renderHexColor(const char *id, RT64_VECTOR3 *v, const char *label = nullptr) {
    ImGui::PushID(id);

    bool changed = false;
    RT64_VECTOR3 shown = { v->x / 255.0f, v->y / 255.0f, v->z / 255.0f };

    if (ImGui::ColorButton("##swatch", ImVec4(shown.x, shown.y, shown.z, 1.0f), ImGuiColorEditFlags_NoAlpha)) {
        sPickerColor[0] = shown.x;
        sPickerColor[1] = shown.y;
        sPickerColor[2] = shown.z;
        memcpy(sPickerRefColor, sPickerColor, sizeof(sPickerRefColor));
        ImGui::OpenPopup("picker");
    }

    if (ImGui::BeginPopup("picker")) {
        const float pickerWidth = ImGui::GetFrameHeight() * 12.0f;
        const ImGuiColorEditFlags sharedFlags = ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB;
        bool pickerChanged = false;

        ImGui::SetNextItemWidth(pickerWidth);
        pickerChanged |= ImGui::ColorPicker4("##picker", sPickerColor,
            sharedFlags | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoLabel |
            ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoInputs, sPickerRefColor);

        const ImGuiColorEditFlags rowFlags = sharedFlags | ImGuiColorEditFlags_NoLabel |
            ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoOptions |
            ImGuiColorEditFlags_NoSmallPreview;

        ImGui::SetNextItemWidth(pickerWidth);
        pickerChanged |= ImGui::ColorEdit3("##channels255", sPickerColor,
            rowFlags | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Uint8);

        ImGui::SetNextItemWidth(pickerWidth);
        pickerChanged |= ImGui::ColorEdit3("##channels01", sPickerColor,
            rowFlags | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Float);

        ImGui::SetNextItemWidth(pickerWidth);
        pickerChanged |= ImGui::ColorEdit3("##channelsHSV", sPickerColor,
            rowFlags | ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float);

        ImGui::SetNextItemWidth(pickerWidth);
        pickerChanged |= ImGui::ColorEdit3("##channelsHex", sPickerColor,
            rowFlags | ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_Float);

        if (pickerChanged) {
            *v = { sPickerColor[0] * 255.0f, sPickerColor[1] * 255.0f, sPickerColor[2] * 255.0f };
            snapColorChannels(v);
            changed = true;
        }

        ImGui::EndPopup();
    }

    auto toByte = [](float c) {
        return std::min(std::max((int)(lroundf(c)), 0), 255);
    };

    char hex[16];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", toByte(v->x), toByte(v->y), toByte(v->z));

    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.5f);
    bool committed = ImGui::InputText("##hex", hex, sizeof(hex),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_EnterReturnsTrue);

    committed = committed || ImGui::IsItemDeactivatedAfterEdit();
    if (committed) {
        const char *p = hex;
        while ((*p == '#') || (*p == ' ')) { p++; }

        unsigned int r = 0, g = 0, b = 0;
        if (sscanf(p, "%02x%02x%02x", &r, &g, &b) == 3) {
            *v = { (float)(r), (float)(g), (float)(b) };
            changed = true;
        }
    }

    if (label != nullptr) {
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
    }

    ImGui::PopID();
    return changed;
}

void RT64::Inspector::renderSceneInspector() {
    if (sceneDesc != nullptr) {
        ImGui::Begin("Scene Inspector");
        renderHexColor("ambientBaseColor", &sceneDesc->ambientBaseColor, "Ambient Base Color");
        renderHexColor("ambientNoGIColor", &sceneDesc->ambientNoGIColor, "Ambient No GI Color");
        renderHexColor("eyeLightDiffuseColor", &sceneDesc->eyeLightDiffuseColor, "Eye Light Diffuse Color");
        renderHexColor("eyeLightSpecularColor", &sceneDesc->eyeLightSpecularColor, "Eye Light Specular Color");

        ImGui::DragFloat3("Sky Diffuse Multiplier", &sceneDesc->skyDiffuseMultiplier.x, 0.01f, 0.0f, 5.0f);
        ImGui::DragFloat3("Sky HSL Modifier", &sceneDesc->skyHSLModifier.x, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat("Sky Yaw Offset", &sceneDesc->skyYawOffset, 1.0f, 0.0f, 360.0f, "%.1f deg");
        ImGui::DragFloat("GI Diffuse Strength", &sceneDesc->giDiffuseStrength, 0.01f, 0.0f, 100.0f);
        ImGui::DragFloat("GI Sky Strength", &sceneDesc->giSkyStrength, 0.01f, 0.0f, 100.0f);
        ImGui::End();
    }
}

void RT64::Inspector::renderMaterialAttributes(RT64_MATERIAL *mat, MapNames *panelMapNames) {
    auto pushCommon = [](const char* name, int mask, int* attributes, bool *justEnabled = nullptr) {
        bool checkboxValue = *attributes & mask;
        ImGui::PushID(name);

        const bool toggled = ImGui::Checkbox("", &checkboxValue);
        if (justEnabled != nullptr) {
            *justEnabled = toggled && checkboxValue;
        }
        if (checkboxValue) {
            *attributes |= mask;
        }
        else {
            *attributes &= ~(mask);
        }

        ImGui::SameLine();

        if (checkboxValue) {
            ImGui::Text(name);
        }
        else {
            ImGui::TextDisabled(name);
        }

        return checkboxValue;
    };

    auto seedFromDefaults = [this, mat](void *field, size_t size) {
        if (!hasMaterialDefaults) {
            return;
        }

        const size_t offset = (size_t)((const char *)(field) - (const char *)(mat));
        memcpy(field, (const char *)(&materialDefaults) + offset, size);
    };

    auto pushFloat = [pushCommon, seedFromDefaults](const char* name, int mask, float *v, int *attributes, float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f) {
        bool justEnabled = false;
        if (pushCommon(name, mask, attributes, &justEnabled)) {
            if (justEnabled) { seedFromDefaults(v, sizeof(*v)); }
            ImGui::SameLine();
            ImGui::DragFloat("V", v, v_speed, v_min, v_max);
        }

        ImGui::PopID();
    };

    auto pushWhole = [pushCommon, seedFromDefaults](const char *name, int mask, float *v, int *attributes, float v_min, float v_max) {
        bool justEnabled = false;
        if (pushCommon(name, mask, attributes, &justEnabled)) {
            if (justEnabled) { seedFromDefaults(v, sizeof(*v)); }
            ImGui::SameLine();

            int shown = (int)(lroundf(*v));
            if (ImGui::DragInt("V", &shown, 1.0f, (int)(v_min), (int)(v_max), "%d", ImGuiSliderFlags_AlwaysClamp)) {
                *v = (float)(shown);
            }
        }

        ImGui::PopID();
    };

    // A colour rather than three numbers
    auto pushColor3 = [pushCommon, seedFromDefaults](const char *name, int mask, RT64_VECTOR3 *v, int *attributes) {
        bool justEnabled = false;
        if (pushCommon(name, mask, attributes, &justEnabled)) {
            if (justEnabled) { seedFromDefaults(v, sizeof(*v)); }
            ImGui::SameLine();
            renderHexColor("V", v);
        }

        ImGui::PopID();
    };

    auto pushColorMix = [pushCommon, seedFromDefaults](const char *name, int mask, RT64_VECTOR4 *v, int *attributes) {
        bool justEnabled = false;
        if (pushCommon(name, mask, attributes, &justEnabled)) {
            if (justEnabled) { seedFromDefaults(v, sizeof(*v)); }
            ImGui::SameLine();
            RT64_VECTOR3 color = { v->x, v->y, v->z };
            if (renderHexColor("V", &color)) {
                v->x = color.x;
                v->y = color.y;
                v->z = color.z;
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
            ImGui::DragFloat("Mix", &v->w, 0.01f, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        }

        ImGui::PopID();
    };

    auto pushInt = [pushCommon, seedFromDefaults](const char *name, int mask, int *v, int *attributes) {
        bool justEnabled = false;
        if (pushCommon(name, mask, attributes, &justEnabled)) {
            if (justEnabled) { seedFromDefaults(v, sizeof(*v)); }
            ImGui::SameLine();
            ImGui::InputInt("V", v);
        }

        ImGui::PopID();
    };

    int effectiveShadingModel = RT64_SHADING_MODEL_BLINN;
    {
        ImGui::PushID("Shading model");
        if (mat->enabledAttributes & RT64_ATTRIBUTE_SHADING_MODEL) {
            effectiveShadingModel = (int)(mat->shadingModel);
        }

        if (ImGui::Combo("", &effectiveShadingModel, "Lambert\0Phong\0Blinn\0")) {
            mat->enabledAttributes |= RT64_ATTRIBUTE_SHADING_MODEL;
            mat->shadingModel = (unsigned int)(effectiveShadingModel);
        }

        ImGui::SameLine();
        ImGui::Text("Shading model");
        ImGui::PopID();
    }

    const bool hasHighlight = (effectiveShadingModel != RT64_SHADING_MODEL_LAMBERT);

    pushFloat("Diffuse", RT64_ATTRIBUTE_DIFFUSE_INTENSITY, &mat->diffuseIntensity, &mat->enabledAttributes, 0.01f, 0.0f, 1.0f);

    if (hasHighlight) {
        pushFloat("Specular factor", RT64_ATTRIBUTE_SPECULAR_FACTOR, &mat->specularFactor, &mat->enabledAttributes, 0.01f, 0.0f, 1.0f);
        pushColor3("Specular color", RT64_ATTRIBUTE_SPECULAR_COLOR, &mat->specularColor, &mat->enabledAttributes);
        pushFloat("Specular intensity", RT64_ATTRIBUTE_SPECULAR_INTENSITY, &mat->specularIntensity, &mat->enabledAttributes, 0.1f, 0.0f, 100.0f);
        {
            ImGui::PushID("Specular tint");
            bool specularTint = (mat->enabledAttributes & RT64_ATTRIBUTE_SPECULAR_TINT) ? (mat->specularTint != 0) : true;
            const bool toggled = ImGui::Checkbox("", &specularTint);
            ImGui::SameLine();
            ImGui::Text("Specular surface tint");
            if (toggled) {
                mat->enabledAttributes |= RT64_ATTRIBUTE_SPECULAR_TINT;
                mat->specularTint = specularTint ? 1u : 0u;
            }

            ImGui::PopID();
        }
    }

    if (effectiveShadingModel == RT64_SHADING_MODEL_PHONG) {
        pushFloat("Specular shinyness", RT64_ATTRIBUTE_SPECULAR_SHINYNESS, &mat->specularShinyness, &mat->enabledAttributes, 0.1f, 0.0f, 100.0f);
    }

    if (effectiveShadingModel == RT64_SHADING_MODEL_BLINN) {
        pushFloat("Specular eccentricity", RT64_ATTRIBUTE_SPECULAR_ECCENTRICITY, &mat->specularEccentricity, &mat->enabledAttributes, 0.01f, 0.0f, 1.0f);
    }

    pushFloat("Reflection factor", RT64_ATTRIBUTE_REFLECTION_FACTOR, &mat->reflectionFactor, &mat->enabledAttributes, 0.01f, 0.0f, 1.0f);
    pushColor3("Reflection color", RT64_ATTRIBUTE_REFLECTION_COLOR, &mat->reflectionColor, &mat->enabledAttributes);
    pushFloat("Reflection fresnel factor", RT64_ATTRIBUTE_REFLECTION_FRESNEL_FACTOR, &mat->reflectionFresnelFactor, &mat->enabledAttributes, 0.01f, 0.0f, 1.0f);
    pushFloat("Reflection shine factor", RT64_ATTRIBUTE_REFLECTION_SHINE_FACTOR, &mat->reflectionShineFactor, &mat->enabledAttributes, 0.01f, 0.0f, 1.0f);
    pushFloat("Refraction factor", RT64_ATTRIBUTE_REFRACTION_FACTOR, &mat->refractionFactor, &mat->enabledAttributes, 0.01f, 0.0f, 1.0f);
    pushWhole("Ignore normal factor", RT64_ATTRIBUTE_IGNORE_NORMAL_FACTOR, &mat->ignoreNormalFactor, &mat->enabledAttributes, 0.0f, 1.0f);
    pushFloat("UV detail scale", RT64_ATTRIBUTE_UV_DETAIL_SCALE, &mat->uvDetailScale, &mat->enabledAttributes, 0.01f, -50.0f, 50.0f);
    pushFloat("Solid alpha multiplier", RT64_ATTRIBUTE_SOLID_ALPHA_MULTIPLIER, &mat->solidAlphaMultiplier, &mat->enabledAttributes, 0.01f, 0.0f, 1.0f);
    pushFloat("Shadow alpha multiplier", RT64_ATTRIBUTE_SHADOW_ALPHA_MULTIPLIER, &mat->shadowAlphaMultiplier, &mat->enabledAttributes, 0.01f, 0.0f, 1.0f);
    pushWhole("Depth bias", RT64_ATTRIBUTE_DEPTH_BIAS, &mat->depthBias, &mat->enabledAttributes, -1000.0f, 1000.0f);
    pushWhole("Shadow ray bias", RT64_ATTRIBUTE_SHADOW_RAY_BIAS, &mat->shadowRayBias, &mat->enabledAttributes, 0.0f, 1000.0f);
    {
        ImGui::PushID("Shadow center");
        bool shadowCenter = (mat->enabledAttributes & RT64_ATTRIBUTE_SHADOW_CENTER) ? (mat->shadowCenter != 0) : false;
        const bool toggled = ImGui::Checkbox("", &shadowCenter);
        ImGui::SameLine();
        ImGui::Text("Shadow center");
        if (toggled) {
            mat->enabledAttributes |= RT64_ATTRIBUTE_SHADOW_CENTER;
            mat->shadowCenter = shadowCenter ? 1u : 0u;
        }

        ImGui::PopID();
    }

    {
        ImGui::PushID("Shadow enable");
        bool shadowEnabled = (mat->enabledAttributes & RT64_ATTRIBUTE_SHADOW_ENABLED) ? (mat->shadowEnabled != 0) : true;
        const bool toggled = ImGui::Checkbox("", &shadowEnabled);
        ImGui::SameLine();
        ImGui::Text("Shadow enable");
        if (toggled) {
            mat->enabledAttributes |= RT64_ATTRIBUTE_SHADOW_ENABLED;
            mat->shadowEnabled = shadowEnabled ? 1u : 0u;
        }

        ImGui::PopID();
    }

    pushColor3("Self light color", RT64_ATTRIBUTE_SELF_LIGHT_COLOR, &mat->selfLightColor, &mat->enabledAttributes);
    pushFloat("Self light intensity", RT64_ATTRIBUTE_SELF_LIGHT_INTENSITY, &mat->selfLightIntensity, &mat->enabledAttributes, 0.1f, 0.0f, 100.0f);
    pushColorMix("Diffuse color mix", RT64_ATTRIBUTE_DIFFUSE_COLOR_MIX, &mat->diffuseColorMix, &mat->enabledAttributes);
    pushInt("Light group mask bits", RT64_ATTRIBUTE_LIGHT_GROUP_MASK_BITS, (int *)(&mat->lightGroupMaskBits), &mat->enabledAttributes);

    if (panelMapNames != nullptr) {
        ImGui::Separator();
        ImGui::TextDisabled("Relative to the mod's texture folder");
        if (ImGui::InputText("Bump map", panelMapNames->bump, sizeof(panelMapNames->bump), ImGuiInputTextFlags_EnterReturnsTrue)) {
            panelMapNames->changed = true;
        }

        // Kept next to the map it belongs to rather than up with the rest of the attributes, since
        // it does nothing at all without one.
        pushFloat("Bump strength", RT64_ATTRIBUTE_BUMP_STRENGTH, &mat->bumpStrength, &mat->enabledAttributes, 0.01f, 0.0f, 10.0f);

        if (ImGui::InputText("Normal map", panelMapNames->normal, sizeof(panelMapNames->normal), ImGuiInputTextFlags_EnterReturnsTrue)) {
            panelMapNames->changed = true;
        }

        pushFloat("Normal strength", RT64_ATTRIBUTE_NORMAL_STRENGTH, &mat->normalStrength, &mat->enabledAttributes, 0.01f, 0.0f, 10.0f);

        if (ImGui::InputText("Specular map", panelMapNames->specular, sizeof(panelMapNames->specular), ImGuiInputTextFlags_EnterReturnsTrue)) {
            panelMapNames->changed = true;
        }
    }
}

void RT64::Inspector::renderMaterialInspector() {
    if (material != nullptr) {
        ImGui::Begin("Material Inspector");
        ImGui::Text("Name: %s", materialName.c_str());
        renderMaterialAttributes(material, &mapNames[RT64_INSPECTOR_PANEL_MATERIAL]);
        ImGui::End();
    }
}

void RT64::Inspector::renderGeoLayoutInspector() {
    if (geoLayoutMaterial != nullptr) {
        ImGui::Begin("Geo Layout Inspector");
        ImGui::Text("Geo layout: %s", geoLayoutName.c_str());

        if (ImGui::CollapsingHeader("Material")) {
            renderMaterialAttributes(geoLayoutMaterial, &mapNames[RT64_INSPECTOR_PANEL_GEO_LAYOUT]);
        }

        if ((geoLayoutLight != nullptr) && (geoLayoutLightEnabled != nullptr)) {
            if (ImGui::CollapsingHeader("Light")) {
                ImGui::Checkbox("Enabled", geoLayoutLightEnabled);
                if (*geoLayoutLightEnabled) {
                    ImGui::TextDisabled("Position is relative to the actor");
                    renderLightAttributes(geoLayoutLight, geoLayoutOrigins, geoLayoutOriginCount);
                }
            }
        }

        ImGui::End();
    }
}

static void computePointLightBasis(float pitchDegrees, float yawDegrees, float rollDegrees, Im3d::Vec3 *outForward, Im3d::Vec3 *outRight, Im3d::Vec3 *outUp) {
    const float degToRad = Im3d::Pi / 180.0f;
    const float pitch = pitchDegrees * degToRad;
    const float yaw = yawDegrees * degToRad;
    const float roll = rollDegrees * degToRad;
    const float sinYaw = sinf(yaw), cosYaw = cosf(yaw);
    const Im3d::Vec3 forwardYawed(sinYaw, 0.0f, cosYaw);
    const Im3d::Vec3 rightYawed(cosYaw, 0.0f, -sinYaw);
    const Im3d::Vec3 worldUp(0.0f, 1.0f, 0.0f);

    const float sinPitch = sinf(pitch), cosPitch = cosf(pitch);
    const Im3d::Vec3 forwardPitched = (forwardYawed * cosPitch) + (worldUp * sinPitch);
    const Im3d::Vec3 upPitched = (forwardYawed * -sinPitch) + (worldUp * cosPitch);

    const float sinRoll = sinf(roll), cosRoll = cosf(roll);
    *outForward = forwardPitched;
    *outRight = (rightYawed * cosRoll) + (upPitched * sinRoll);
    *outUp = (rightYawed * -sinRoll) + (upPitched * cosRoll);
}

static void computePointLightAimAngles(Im3d::Vec3 forward, float *outPitchDegrees, float *outYawDegrees) {
    const float length = sqrtf((forward.x * forward.x) + (forward.y * forward.y) + (forward.z * forward.z));
    if (length <= 0.0f) { return; }

    forward = forward / length;
    const float radToDeg = 180.0f / Im3d::Pi;
    *outPitchDegrees = asinf(std::min(std::max(forward.y, -1.0f), 1.0f)) * radToDeg;
    *outYawDegrees = atan2f(forward.x, forward.z) * radToDeg;
}

void RT64::Inspector::renderLightAttributes(RT64_LIGHT *light, const RT64_VECTOR3 *origins, int originCount) {
    const int SphereDetail = 64;

    if (origins != nullptr) {
        ImGui::DragFloat3("Position (offset)", &light->position.x);
    }
    else {
        ImGui::DragFloat3("Position", &light->position.x);
    }

    int lightType = (int)(light->lightType);
    ImGui::Combo("Light Type", &lightType, "Area\0Point\0");
    light->lightType = (unsigned int)(lightType);

    renderHexColor("DiffuseColor", &light->diffuseColor, "Diffuse color");
    ImGui::DragFloat("Attenuation radius", &light->attenuationRadius);
    ImGui::DragFloat("Point radius", &light->pointRadius);
    renderHexColor("SpecularColor", &light->specularColor, "Specular color");

    // Scales both colors above past white - see RT64_LIGHT in rt64.h.
    ImGui::DragFloat("Intensity", &light->intensity, 0.1f, 0.0f, 100.0f);

    ImGui::DragFloat("Shadow offset", &light->shadowOffset);
    ImGui::DragFloat("Attenuation exponent", &light->attenuationExponent);
    ImGui::DragFloat("Flicker intensity", &light->flickerIntensity);
    ImGui::InputInt("Group bits", (int *)(&light->groupBits));

    const bool isPointLight = (light->lightType == RT64_LIGHT_TYPE_POINT);
    if (isPointLight) {
        ImGui::Separator();
        ImGui::TextDisabled("Point light settings");

        auto wrapDegrees = [](float degrees) {
            return degrees - (floorf((degrees + 180.0f) / 360.0f) * 360.0f);
        };

        ImGui::DragFloat("Pitch", &light->pitch, 1.0f, 0.0f, 0.0f, "%.1f deg");
        light->pitch = wrapDegrees(light->pitch);

        ImGui::DragFloat("Yaw", &light->yaw, 1.0f, 0.0f, 0.0f, "%.1f deg");
        light->yaw = wrapDegrees(light->yaw);

        ImGui::DragFloat("Roll", &light->roll, 1.0f, 0.0f, 0.0f, "%.1f deg");
        light->roll = wrapDegrees(light->roll);

        int lightShape = (int)(light->lightShape);
        ImGui::Combo("Shape", &lightShape, "Circle\0Square\0");
        light->lightShape = (unsigned int)(lightShape);

        ImGui::DragFloat("Scale X", &light->scaleX);
        ImGui::DragFloat("Scale Y", &light->scaleY);

        bool apertureEnabled = (light->apertureEnabled != 0);
        ImGui::Checkbox("Flat opening", &apertureEnabled);
        light->apertureEnabled = apertureEnabled ? 1 : 0;

        if (apertureEnabled) {
            ImGui::DragFloat("Opening pitch", &light->aperturePitch, 1.0f, 0.0f, 0.0f, "%.1f deg");
            light->aperturePitch = wrapDegrees(light->aperturePitch);

            ImGui::DragFloat("Opening yaw", &light->apertureYaw, 1.0f, 0.0f, 0.0f, "%.1f deg");
            light->apertureYaw = wrapDegrees(light->apertureYaw);
        }

        bool volumetricEnabled = (light->volumetricEnabled != 0);
        if (ImGui::Checkbox("Volumetric", &volumetricEnabled)) {
            if (volumetricEnabled && (light->volumetricIntensity <= 0.0f)) {
                light->volumetricIntensity = 1.0f;
            }
        }

        light->volumetricEnabled = volumetricEnabled ? 1 : 0;

        if (volumetricEnabled) {
            ImGui::DragFloat("Volumetric intensity", &light->volumetricIntensity, 0.01f, 0.0f, 10.0f);
        }
    }

    const RT64_VECTOR3 worldOrigin = { 0.0f, 0.0f, 0.0f };
    const int drawCount = (origins != nullptr) ? originCount : 1;
    for (int i = 0; i < drawCount; i++) {
        const RT64_VECTOR3 &origin = (origins != nullptr) ? origins[i] : worldOrigin;
        float worldPosition[3] = {
            origin.x + light->position.x,
            origin.y + light->position.y,
            origin.z + light->position.z
        };

        Im3d::PushId(i);
        Im3d::GizmoTranslation("GizmoPosition", worldPosition);
        Im3d::PopId();
        light->position.x = worldPosition[0] - origin.x;
        light->position.y = worldPosition[1] - origin.y;
        light->position.z = worldPosition[2] - origin.z;

        const Im3d::Vec3 worldPos(worldPosition[0], worldPosition[1], worldPosition[2]);

        if (isPointLight) {
            Im3d::Vec3 lightForward, lightRight, lightUp;
            computePointLightBasis(light->pitch, light->yaw, light->roll, &lightForward, &lightRight, &lightUp);

            const float aimHandleDistance = std::max(light->attenuationRadius * 0.25f, 100.0f);
            Im3d::Vec3 aimPoint = worldPos + (lightForward * aimHandleDistance);
            float aimPosition[3] = { aimPoint.x, aimPoint.y, aimPoint.z };

            Im3d::PushId(i + drawCount);
            const bool aimDragged = Im3d::GizmoTranslation("GizmoAim", aimPosition);
            Im3d::PopId();

            if (aimDragged) {
                Im3d::Vec3 draggedForward = Im3d::Vec3(aimPosition[0], aimPosition[1], aimPosition[2]) - worldPos;
                computePointLightAimAngles(draggedForward, &light->pitch, &light->yaw);
                computePointLightBasis(light->pitch, light->yaw, light->roll, &lightForward, &lightRight, &lightUp);
            }

            Im3d::Vec3 planeNormal = lightForward, planeRight = lightRight, planeUp = lightUp;
            if (light->apertureEnabled) {
                computePointLightBasis(light->aperturePitch, light->apertureYaw, light->roll, &planeNormal, &planeRight, &planeUp);
            }

            const Im3d::Vec3 beamEnd = worldPos + (lightForward * light->attenuationRadius);
            const Im3d::Color beamColor(light->diffuseColor.x / 255.0f, light->diffuseColor.y / 255.0f, light->diffuseColor.z / 255.0f);
            Im3d::SetColor(beamColor);
            Im3d::DrawLine(worldPos, beamEnd, 2.0f, beamColor);

            const int ShapeDetail = 32;
            auto drawOpeningOutline = [&](const Im3d::Vec3 &center) {
                if (light->lightShape == RT64_LIGHT_SHAPE_SQUARE) {
                    Im3d::Vec3 corners[4] = {
                        center + (planeRight * light->scaleX) + (planeUp * light->scaleY),
                        center - (planeRight * light->scaleX) + (planeUp * light->scaleY),
                        center - (planeRight * light->scaleX) - (planeUp * light->scaleY),
                        center + (planeRight * light->scaleX) - (planeUp * light->scaleY),
                    };

                    Im3d::BeginLineLoop();
                    for (const Im3d::Vec3 &corner : corners) { Im3d::Vertex(corner); }
                    Im3d::End();
                }
                else {
                    Im3d::BeginLineLoop();
                    for (int p = 0; p < ShapeDetail; p++) {
                        const float angle = (Im3d::TwoPi * p) / ShapeDetail;
                        Im3d::Vertex(center + (planeRight * (cosf(angle) * light->scaleX)) + (planeUp * (sinf(angle) * light->scaleY)));
                    }
                    Im3d::End();
                }
            };

            drawOpeningOutline(worldPos);
            drawOpeningOutline(beamEnd);
        }
        else {
            Im3d::SetColor(light->diffuseColor.x / 255.0f, light->diffuseColor.y / 255.0f, light->diffuseColor.z / 255.0f);
            Im3d::DrawSphere(worldPos, light->attenuationRadius, SphereDetail);
            Im3d::SetColor(light->diffuseColor.x / 510.0f, light->diffuseColor.y / 510.0f, light->diffuseColor.z / 510.0f);
            Im3d::DrawSphere(worldPos, light->pointRadius, SphereDetail);
            Im3d::SetColor(Im3d::Color_Black);
            Im3d::DrawSphere(worldPos, light->shadowOffset, SphereDetail);
        }
    }
}

void RT64::Inspector::renderLightInspector() {
    if (lights != nullptr) {
        ImGui::Begin("Light Inspector");
        ImGui::InputInt("Light count", lightCount);
        *lightCount = std::min(std::max(*lightCount, 1), maxLightCount);

        for (int i = 0; i < *lightCount; i++) {
            ImGui::PushID(i);
            Im3d::PushId(i);

            char header[32];
            snprintf(header, sizeof(header), "Light %d (%s)", i, (lights[i].lightType == RT64_LIGHT_TYPE_POINT) ? "Point" : "Area");
            if (ImGui::CollapsingHeader(header)) {
                renderLightAttributes(&lights[i], nullptr, 0);

                if ((*lightCount) < maxLightCount) {
                    if (ImGui::Button("Duplicate")) {
                        lights[*lightCount] = lights[i];
                        *lightCount = *lightCount + 1;
                    }
                }
            }

            Im3d::PopId();
            ImGui::PopID();
        }
        ImGui::End();
    }
}

void RT64::Inspector::renderCameraControl(View *view, int cursorX, int cursorY) {
    assert(view != nullptr);

    if (ImGui::Begin("Camera controls")) {
        ImGui::Checkbox("Enable", &cameraControl);
        view->setPerspectiveControlActive(cameraControl);
        if (cameraControl) {
            ImGui::DragFloat("Camera pan x", &cameraPanX, 0.1f, -100.0f, 100.0f);
            ImGui::DragFloat("Camera pan y", &cameraPanY, 0.1f, -100.0f, 100.0f);

            if ((cameraPanX != 0.0f) || (cameraPanY != 0.0f)) {
                view->movePerspective({ cameraPanX, cameraPanY, 0.0f });
            }
            else if (!ImGui::GetIO().WantCaptureMouse) {
                float cameraSpeed = (view->getFarDistance() - view->getNearDistance()) / 5.0f;
                bool leftAlt = GetAsyncKeyState(VK_LMENU) & 0x8000;
                bool leftCtrl = GetAsyncKeyState(VK_LCONTROL) & 0x8000;
                float localX = (cursorX - prevCursorX) / (float)(view->getWidth());
                float localY = (cursorY - prevCursorY) / (float)(view->getHeight());
                localX += cameraPanX;
                localY += cameraPanY;

                if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) {
                    if (leftCtrl) {
                        view->movePerspective({ 0.0f, 0.0f, (localX + localY) * cameraSpeed });
                    }
                    else if (leftAlt) {
                        float cameraRotationSpeed = 5.0f;
                        view->rotatePerspective(0.0f, -localX * cameraRotationSpeed, -localY * cameraRotationSpeed);
                    }
                    else {
                        view->movePerspective({ -localX * cameraSpeed, localY * cameraSpeed, 0.0f });
                    }
                }
            }

            prevCursorX = cursorX;
            prevCursorY = cursorY;

            ImGui::Text("Middle Button: Pan");
            ImGui::Text("Ctrl + Middle Button: Zoom");
            ImGui::Text("Alt + Middle Button: Rotate");
        }
        else {
            cameraPanX = 0.0f;
            cameraPanY = 0.0f;
        }

        ImGui::End();
    }
}

void RT64::Inspector::renderPrint() {
    if (!printMessages.empty()) {
        ImGui::Begin("Print");
        for (size_t i = 0; i < printMessages.size(); i++) {
            ImGui::Text("%s", printMessages[i].c_str());
        }
        ImGui::End();
    }
}

void RT64::Inspector::setupWithView(View *view, int cursorX, int cursorY) {
    assert(view != nullptr);
    Im3d::AppData &appData = Im3d::GetAppData();
    RT64_VECTOR3 viewPos = view->getViewPosition();
    RT64_VECTOR3 viewDir = view->getViewDirection();
    RT64_VECTOR3 rayDir = view->getRayDirectionAt(cursorX, cursorY);
    appData.m_deltaTime = 1.0f / 30.0f;
    appData.m_viewportSize = Im3d::Vec2((float)(view->getWidth()), (float)(view->getHeight()));
    appData.m_viewOrigin = Im3d::Vec3(viewPos.x, viewPos.y, viewPos.z);
    appData.m_viewDirection = Im3d::Vec3(viewDir.x, viewDir.y, viewDir.z);
    appData.m_worldUp = Im3d::Vec3(0.0f, 1.0f, 0.0f);
    appData.m_projOrtho = false;
    appData.m_projScaleY = tanf(view->getFOVRadians() * 0.5f) * 2.0f;
    appData.m_snapTranslation = 0.0f;
    appData.m_snapRotation = 0.0f;
    appData.m_snapScale = 0.0f;
    appData.m_cursorRayOrigin = Im3d::Vec3(viewPos.x, viewPos.y, viewPos.z);
    appData.m_cursorRayDirection = Im3d::Vec3(rayDir.x, rayDir.y, rayDir.z);
    appData.m_keyDown[Im3d::Mouse_Left] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

void RT64::Inspector::setSceneDescription(RT64_SCENE_DESC* sceneDesc) {
    this->sceneDesc = sceneDesc;
}

void RT64::Inspector::setMaterialDefaults(const RT64_MATERIAL *defaults) {
    if (defaults == nullptr) {
        hasMaterialDefaults = false;
        return;
    }

    materialDefaults = *defaults;
    hasMaterialDefaults = true;
}

void RT64::Inspector::setMaterial(RT64_MATERIAL* material, const std::string &materialName) {
    this->material = material;
    this->materialName = materialName;
}

void RT64::Inspector::setMapNames(int panel, const char *bumpMapName, const char *normalMapName, const char *specularMapName) {
    if ((panel < 0) || (panel >= RT64_INSPECTOR_PANEL_COUNT)) {
        return;
    }

    MapNames &names = mapNames[panel];
    if (names.changed) {
        return;
    }

    if (ImGui::GetIO().WantTextInput) {
        return;
    }

    snprintf(names.bump, sizeof(names.bump), "%s", (bumpMapName != nullptr) ? bumpMapName : "");
    snprintf(names.normal, sizeof(names.normal), "%s", (normalMapName != nullptr) ? normalMapName : "");
    snprintf(names.specular, sizeof(names.specular), "%s", (specularMapName != nullptr) ? specularMapName : "");
}

bool RT64::Inspector::getMapNames(int panel, char *outBumpMapName, char *outNormalMapName, char *outSpecularMapName, int bufferSize) {
    if ((panel < 0) || (panel >= RT64_INSPECTOR_PANEL_COUNT) || (bufferSize <= 0)) {
        return false;
    }

    MapNames &names = mapNames[panel];
    if (!names.changed) {
        return false;
    }

    names.changed = false;
    if (outBumpMapName != nullptr) { snprintf(outBumpMapName, bufferSize, "%s", names.bump); }
    if (outNormalMapName != nullptr) { snprintf(outNormalMapName, bufferSize, "%s", names.normal); }
    if (outSpecularMapName != nullptr) { snprintf(outSpecularMapName, bufferSize, "%s", names.specular); }
    return true;
}

void RT64::Inspector::setGeoLayout(RT64_MATERIAL* material, RT64_LIGHT* light, bool* lightEnabled, const RT64_VECTOR3* origins, int originCount, const std::string &geoLayoutName) {
    this->geoLayoutMaterial = material;
    this->geoLayoutLight = light;
    this->geoLayoutLightEnabled = lightEnabled;
    this->geoLayoutOrigins = (originCount > 0) ? origins : nullptr;
    this->geoLayoutOriginCount = originCount;
    this->geoLayoutName = geoLayoutName;
}

bool RT64::Inspector::wantsMouse() const {
    if ((Im3d::GetHotId() != Im3d::Id_Invalid) || (Im3d::GetActiveId() != Im3d::Id_Invalid)) {
        return true;
    }

    return ImGui::GetIO().WantCaptureMouse;
}

void RT64::Inspector::setLights(RT64_LIGHT* lights, int *lightCount, int maxLightCount) {
    this->lights = lights;
    this->lightCount = lightCount;
    this->maxLightCount = maxLightCount;
}

void RT64::Inspector::printClear() {
    printMessages.clear();
}

void RT64::Inspector::printMessage(const std::string& message) {
    printMessages.push_back(message);
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool RT64::Inspector::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    return ImGui_ImplWin32_WndProcHandler(device->getHwnd(), msg, wParam, lParam);
}

// Public

DLLEXPORT RT64_INSPECTOR* RT64_CreateInspector(RT64_DEVICE* devicePtr) {
    assert(devicePtr != nullptr);
    RT64::Device* device = (RT64::Device*)(devicePtr);
    RT64::Inspector* inspector = new RT64::Inspector(device);
    return (RT64_INSPECTOR*)(inspector);
}

DLLEXPORT bool RT64_HandleMessageInspector(RT64_INSPECTOR* inspectorPtr, UINT msg, WPARAM wParam, LPARAM lParam) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    return inspector->handleMessage(msg, wParam, lParam);
}

DLLEXPORT void RT64_SetSceneInspector(RT64_INSPECTOR* inspectorPtr, RT64_SCENE_DESC* sceneDesc) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    inspector->setSceneDescription(sceneDesc);
}

DLLEXPORT void RT64_SetMaterialInspector(RT64_INSPECTOR* inspectorPtr, RT64_MATERIAL* material, const char* materialName) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    inspector->setMaterial(material, std::string(materialName));
}

DLLEXPORT void RT64_SetGeoLayoutInspector(RT64_INSPECTOR* inspectorPtr, RT64_MATERIAL* material, RT64_LIGHT* light, bool* lightEnabled, const RT64_VECTOR3* origins, int originCount, const char* geoLayoutName) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    inspector->setGeoLayout(material, light, lightEnabled, origins, originCount, (geoLayoutName != nullptr) ? std::string(geoLayoutName) : std::string());
}

DLLEXPORT void RT64_SetInspectorMaterialDefaults(RT64_INSPECTOR* inspectorPtr, const RT64_MATERIAL* defaults) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    inspector->setMaterialDefaults(defaults);
}

DLLEXPORT void RT64_SetInspectorMapNames(RT64_INSPECTOR* inspectorPtr, int panel, const char* bumpMapName, const char* normalMapName, const char* specularMapName) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    inspector->setMapNames(panel, bumpMapName, normalMapName, specularMapName);
}

DLLEXPORT bool RT64_GetInspectorMapNames(RT64_INSPECTOR* inspectorPtr, int panel, char* outBumpMapName, char* outNormalMapName, char* outSpecularMapName, int bufferSize) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    return inspector->getMapNames(panel, outBumpMapName, outNormalMapName, outSpecularMapName, bufferSize);
}

DLLEXPORT bool RT64_InspectorWantsMouse(RT64_INSPECTOR* inspectorPtr) {
    if (inspectorPtr == nullptr) {
        return false;
    }

    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    return inspector->wantsMouse();
}

DLLEXPORT void RT64_SetLightsInspector(RT64_INSPECTOR* inspectorPtr, RT64_LIGHT* lights, int* lightCount, int maxLightCount) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    inspector->setLights(lights, lightCount, maxLightCount);
}

DLLEXPORT void RT64_PrintClearInspector(RT64_INSPECTOR* inspectorPtr) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    inspector->printClear();
}

DLLEXPORT void RT64_PrintMessageInspector(RT64_INSPECTOR* inspectorPtr, const char* message) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    std::string messageStr(message);
    inspector->printMessage(messageStr);
}

DLLEXPORT void RT64_DestroyInspector(RT64_INSPECTOR* inspectorPtr) {
    delete (RT64::Inspector*)(inspectorPtr);
}

#endif