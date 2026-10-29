#ifndef MATERIAL_COMMON_HLSLI
#define MATERIAL_COMMON_HLSLI

#include "scene_common.hlsli"

// A material's textures: descriptor set 1 on Vulkan, registers t8..t11 and s12 on D3D. Mirrors MATERIAL_BINDING_* in gpu_material.h.
VK_BINDING(8, 1) Texture2D uBaseColor : register(t8);
VK_BINDING(9, 1) Texture2D uNormal : register(t9);
VK_BINDING(10, 1) Texture2D uOrm : register(t10);
VK_BINDING(11, 1) Texture2D uEmissive : register(t11);
VK_BINDING(12, 1) SamplerState uMaterialSampler : register(s12);

#endif // MATERIAL_COMMON_HLSLI
