#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor; // ImU32 RGBA8, normalized by the vertex format

layout(push_constant) uniform PushConstants
{
	vec2 Scale;
	vec2 Translate;
} pc;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;

void main()
{
	vUv = inUv;
	vColor = inColor;
	gl_Position = vec4(inPosition * pc.Scale + pc.Translate, 0.0, 1.0);
}
