#version 450

// Binding numbers match the NVRHI slots (zero binding offsets, unique slots).
layout(binding = 0) uniform texture2D uTexture;
layout(binding = 1) uniform sampler uSampler;

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main()
{
	outColor = vColor * texture(sampler2D(uTexture, uSampler), vUv);
}
