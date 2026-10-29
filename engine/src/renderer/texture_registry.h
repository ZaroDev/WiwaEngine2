#pragma once

#include <unordered_map>

#include <nvrhi/nvrhi.h>

#include <math/math.h>

namespace wiwa
{
	// The textures the UI can draw, by id. The ImGui draw data carries an id as the ImTextureID of each command; the render thread
	// turns it into a texture. 0 is ImGui's "no texture", so the first id is 1. Render thread only.
	constexpr u32 FONT_TEXTURE_ID = 1;     // the font atlas
	constexpr u32 VIEWPORT_TEXTURE_ID = 2; // the scene's viewport target
	constexpr u32 FIRST_FREE_TEXTURE_ID = 16;

	class TextureRegistry
	{
	public:
		// A new id for the texture.
		u32 add(nvrhi::ITexture* texture)
		{
			const u32 id = m_NextId++;
			m_Textures[id] = texture;
			return id;
		}

		// Puts the texture under a given id (one of the fixed ids above, or one add() returned); null clears it.
		void set(u32 id, nvrhi::ITexture* texture)
		{
			if (texture == nullptr)
			{
				m_Textures.erase(id);
				return;
			}
			m_Textures[id] = texture;
		}

		nvrhi::ITexture* get(u32 id) const
		{
			const auto found = m_Textures.find(id);
			return found == m_Textures.end() ? nullptr : found->second.Get();
		}

		void remove(u32 id) { m_Textures.erase(id); }

		bool contains(const nvrhi::ITexture* texture) const
		{
			for (const auto& [id, held] : m_Textures)
			{
				if (held.Get() == texture)
				{
					return true;
				}
			}
			return false;
		}

	private:
		std::unordered_map<u32, nvrhi::TextureHandle> m_Textures{};
		u32 m_NextId = FIRST_FREE_TEXTURE_ID;
	};
}
