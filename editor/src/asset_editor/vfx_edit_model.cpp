#include <editor_common.h>
#include "vfx_edit_model.h"

#include <algorithm>

namespace wiwa
{
	size_t particle_effect_bytes(const ParticleEffectData& effect)
	{
		return effect.Emitters.size() * sizeof(ParticleEmitterData);
	}

	VfxEditModel::VfxEditModel(ParticleEffectData data) : m_Data(std::move(data))
	{
		for (ParticleEmitterData& emitter : m_Data.Emitters)
		{
			emitter.bake();
		}
	}

	bool VfxEditModel::edit(ParticleEffectData next, bool continuing)
	{
		for (ParticleEmitterData& emitter : next.Emitters)
		{
			emitter.bake();
		}
		if (next == m_Data)
		{
			if (!continuing)
			{
				m_Gesture = false;
			}
			return false;
		}
		if (!m_Gesture)
		{
			m_History.push(m_Data, particle_effect_bytes(m_Data));
		}
		m_Gesture = continuing;
		m_Data = std::move(next);
		m_Revision++;
		fix_switches();
		return true;
	}

	void VfxEditModel::fix_switches()
	{
		if (m_Data.Emitters.empty())
		{
			m_Selected = 0;
			return;
		}
		m_Selected = std::min(m_Selected, m_Data.Emitters.size() - 1);
	}

	std::string VfxEditModel::unique_name(const std::string& base, std::optional<size_t> except) const
	{
		const auto taken = [&](const std::string& name)
		{
			for (size_t i = 0; i < m_Data.Emitters.size(); i++)
			{
				if (i != except && m_Data.Emitters[i].Name == name)
				{
					return true;
				}
			}
			return false;
		};
		if (!taken(base))
		{
			return base;
		}
		// "Spark 2" -> "Spark 3": the number at the end counts up
		std::string stem = base;
		const size_t space = base.find_last_of(' ');
		if (space != std::string::npos && space + 1 < base.size() && std::all_of(base.begin() + static_cast<std::ptrdiff_t>(space) + 1, base.end(), [](char c) { return c >= '0' && c <= '9'; }))
		{
			stem = base.substr(0, space);
		}
		for (int n = 2; n < 1000; n++)
		{
			const std::string candidate = stem + " " + std::to_string(n);
			if (!taken(candidate))
			{
				return candidate;
			}
		}
		return base;
	}

	bool VfxEditModel::add_emitter()
	{
		if (m_Data.Emitters.size() >= PARTICLE_MAX_EMITTERS)
		{
			return false;
		}
		ParticleEffectData next = m_Data;
		ParticleEmitterData emitter;
		emitter.Name = unique_name("Emitter");
		next.Emitters.push_back(std::move(emitter));
		if (!edit(std::move(next)))
		{
			return false;
		}
		m_Selected = m_Data.Emitters.size() - 1;
		return true;
	}

	bool VfxEditModel::remove_emitter(size_t index)
	{
		if (index >= m_Data.Emitters.size() || m_Data.Emitters.size() <= 1)
		{
			return false; // an effect keeps at least one emitter (an empty effect cannot be played)
		}
		ParticleEffectData next = m_Data;
		next.Emitters.erase(next.Emitters.begin() + static_cast<std::ptrdiff_t>(index));
		for (size_t i = index; i + 1 < PARTICLE_MAX_EMITTERS; i++)
		{
			m_Muted[i] = m_Muted[i + 1];
			m_Solo[i] = m_Solo[i + 1];
		}
		m_Muted[PARTICLE_MAX_EMITTERS - 1] = false;
		m_Solo[PARTICLE_MAX_EMITTERS - 1] = false;
		const size_t keep = m_Selected > index ? m_Selected - 1 : m_Selected;
		const bool changed = edit(std::move(next));
		m_Selected = std::min(keep, m_Data.Emitters.size() - 1);
		return changed;
	}

	bool VfxEditModel::duplicate_emitter(size_t index)
	{
		if (index >= m_Data.Emitters.size() || m_Data.Emitters.size() >= PARTICLE_MAX_EMITTERS)
		{
			return false;
		}
		ParticleEffectData next = m_Data;
		ParticleEmitterData copy = next.Emitters[index];
		copy.Name = unique_name(copy.Name);
		next.Emitters.insert(next.Emitters.begin() + static_cast<std::ptrdiff_t>(index) + 1, std::move(copy));
		for (size_t i = PARTICLE_MAX_EMITTERS - 1; i > index + 1; i--)
		{
			m_Muted[i] = m_Muted[i - 1];
			m_Solo[i] = m_Solo[i - 1];
		}
		m_Muted[index + 1] = false;
		m_Solo[index + 1] = false;
		if (!edit(std::move(next)))
		{
			return false;
		}
		m_Selected = index + 1;
		return true;
	}

	bool VfxEditModel::move_emitter(size_t index, int direction)
	{
		const long target = static_cast<long>(index) + direction;
		if (index >= m_Data.Emitters.size() || target < 0 || target >= static_cast<long>(m_Data.Emitters.size()) || direction == 0)
		{
			return false;
		}
		ParticleEffectData next = m_Data;
		std::swap(next.Emitters[index], next.Emitters[static_cast<size_t>(target)]);
		std::swap(m_Muted[index], m_Muted[static_cast<size_t>(target)]);
		std::swap(m_Solo[index], m_Solo[static_cast<size_t>(target)]);
		if (!edit(std::move(next)))
		{
			return false;
		}
		if (m_Selected == index)
		{
			m_Selected = static_cast<size_t>(target);
		}
		else if (m_Selected == static_cast<size_t>(target))
		{
			m_Selected = index;
		}
		return true;
	}

	bool VfxEditModel::rename_emitter(size_t index, const std::string& name)
	{
		if (index >= m_Data.Emitters.size() || name.empty())
		{
			return false;
		}
		ParticleEffectData next = m_Data;
		next.Emitters[index].Name = unique_name(name, index);
		return edit(std::move(next));
	}

	void VfxEditModel::select(size_t index)
	{
		if (!m_Data.Emitters.empty())
		{
			m_Selected = std::min(index, m_Data.Emitters.size() - 1);
		}
	}

	void VfxEditModel::set_muted(size_t index, bool muted)
	{
		if (index < m_Muted.size() && m_Muted[index] != muted)
		{
			m_Muted[index] = muted;
			m_Revision++;
		}
	}

	void VfxEditModel::set_solo(size_t index, bool solo)
	{
		if (index < m_Solo.size() && m_Solo[index] != solo)
		{
			m_Solo[index] = solo;
			m_Revision++;
		}
	}

	bool VfxEditModel::any_solo() const
	{
		return std::any_of(m_Solo.begin(), m_Solo.end(), [](bool solo) { return solo; });
	}

	bool VfxEditModel::audible(size_t index) const
	{
		if (index >= m_Data.Emitters.size() || muted(index))
		{
			return false;
		}
		return !any_solo() || soloed(index);
	}

	ParticleEffectData VfxEditModel::preview_data() const
	{
		ParticleEffectData data = m_Data;
		for (size_t i = 0; i < data.Emitters.size(); i++)
		{
			if (!audible(i))
			{
				data.Emitters[i].Rate = ParticleValue::constant(0.0f);
				data.Emitters[i].Bursts.clear();
			}
		}
		return data;
	}

	void VfxEditModel::undo()
	{
		if (!m_History.can_undo())
		{
			return;
		}
		m_Data = m_History.undo(m_Data, particle_effect_bytes(m_Data));
		m_Gesture = false;
		m_Revision++;
		fix_switches();
	}

	void VfxEditModel::redo()
	{
		if (!m_History.can_redo())
		{
			return;
		}
		m_Data = m_History.redo(m_Data, particle_effect_bytes(m_Data));
		m_Gesture = false;
		m_Revision++;
		fix_switches();
	}
}
