#include "Common.hlsli"

// The game UI: coloured quads, images and text read from a signed distance field. Slots match UiRenderPass: SRV 0, sampler 1, push
// constants 2. The target is the 8-bit viewport image, which holds display (sRGB) values, so colours are written as they are.
struct PushConstantsData
{
	float2 Scale;
	float2 Translate;
	// x: how far the field reaches in screen pixels, y: outline width (0 to 1 of the range), z: 0 for an image or colour, 1 for text,
	// w: screen pixels per texel of the atlas
	float4 Params;
	float4 OutlineColor;
	// x: 1 draws a shadow under the text
	float4 Extra;
};

PUSH_CONSTANTS(PushConstantsData, Constants, b2);

Texture2D uTexture : register(t0);
SamplerState uSampler : register(s1);

struct VertexInput
{
	float2 Position : POSITION;
	float2 Uv : TEXCOORD;
	float4 Color : COLOR;
};

struct VertexOutput
{
	float4 Position : SV_Position;
	float2 Uv : TEXCOORD0;
	float4 Color : COLOR0;
};

VertexOutput VSMain(VertexInput input)
{
	VertexOutput output;
	output.Position = float4(input.Position * Constants.Scale + Constants.Translate, 0.0, 1.0);
	output.Uv = input.Uv;
	output.Color = input.Color;
	return output;
}

float3 LinearToSrgb(float3 c)
{
	c = saturate(c);
	float3 low = c * 12.92;
	float3 high = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
	return lerp(low, high, step(0.0031308, c));
}

// straight alpha, source over destination
float4 Over(float4 source, float4 destination)
{
	float alpha = source.a + destination.a * (1.0 - source.a);
	float3 color = (source.rgb * source.a + destination.rgb * destination.a * (1.0 - source.a)) / max(alpha, 1e-5);
	return float4(color, alpha);
}

float4 PSMain(VertexOutput input) : SV_Target0
{
	if (Constants.Params.z < 0.5)
	{
		// The texture of an image is sRGB: sampling gives linear values, and the target holds sRGB ones. A plain colour uses a white texture.
		float4 texel = uTexture.Sample(uSampler, input.Uv);
		return float4(input.Color.rgb * LinearToSrgb(texel.rgb), input.Color.a * texel.a);
	}

	// Text. The atlas is 128/255 on the outline, higher inside, and 0 or 255 at the range; the edge is as soft as one screen pixel.
	const float edge = 128.0 / 255.0;
	float rangePixels = max(Constants.Params.x, 1e-3);
	float softness = 0.5 * edge / rangePixels;
	float field = uTexture.Sample(uSampler, input.Uv).r;
	softness = max(softness, 0.5 * fwidth(field));

	float4 result = float4(0.0, 0.0, 0.0, 0.0);

	if (Constants.Extra.x > 0.5)
	{
		// the shadow is the glyph moved down and to the right by a tenth of the range, at least a pixel
		float offsetPixels = max(1.0, 0.25 * rangePixels);
		float2 size;
		uTexture.GetDimensions(size.x, size.y);
		float2 offset = float2(offsetPixels, offsetPixels) / max(Constants.Params.w, 1e-3) / size;
		float shadowField = uTexture.Sample(uSampler, input.Uv - offset).r;
		float shadowAlpha = smoothstep(edge - softness, edge + softness, shadowField) * 0.6 * input.Color.a;
		result = float4(0.0, 0.0, 0.0, shadowAlpha);
	}

	if (Constants.Params.y > 0.0)
	{
		float outer = edge * (1.0 - saturate(Constants.Params.y));
		float outlineAlpha = smoothstep(outer - softness, outer + softness, field) * Constants.OutlineColor.a;
		result = Over(float4(Constants.OutlineColor.rgb, outlineAlpha), result);
	}

	float textAlpha = smoothstep(edge - softness, edge + softness, field) * input.Color.a;
	result = Over(float4(input.Color.rgb, textAlpha), result);
	return result;
}
