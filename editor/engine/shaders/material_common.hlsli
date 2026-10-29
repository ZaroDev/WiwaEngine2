#ifndef MATERIAL_COMMON_HLSLI
#define MATERIAL_COMMON_HLSLI

#include "scene_common.hlsli"

// A material's textures: descriptor set 1 on Vulkan, registers t10..t13 and s14 on D3D. Mirrors MATERIAL_BINDING_* in gpu_material.h.
VK_BINDING(10, 1) Texture2D uBaseColor : register(t10);
VK_BINDING(11, 1) Texture2D uNormal : register(t11);
VK_BINDING(12, 1) Texture2D uOrm : register(t12);
VK_BINDING(13, 1) Texture2D uEmissive : register(t13);
VK_BINDING(14, 1) SamplerState uMaterialSampler : register(s14);

#endif // MATERIAL_COMMON_HLSLI
