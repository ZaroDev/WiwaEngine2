#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>

#include <assets/particle_effect.h>

#include "edit_history.h"

namespace wiwa
{
	// The document of the VFX editor: a ParticleEffectData with snapshot undo, and what only the editor knows (the selected emitter, which are muted or
	// soloed). Every change goes through edit(), so the tables are baked and one drag of a slider is one undo step. No ImGui here (the smoke test drives it).
	class VfxEditModel
	{
	public:
		explicit VfxEditModel(ParticleEffectData data);

		const ParticleEffectData& data() const { return m_Data; }
		// Counts up with every change (also undo and redo); the preview shows the effect again when it moved.
		u64 revision() const { return m_Revision; }

		// Replaces the document with `next` (baked here). A change that is the same as the document does nothing. continuing: the change is part of a gesture
		// that goes on (a slider being dragged), so it joins the undo step the gesture began; the first call with continuing false ends the gesture.
		bool edit(ParticleEffectData next, bool continuing = false);
		void end_gesture() { m_Gesture = false; }

		// Emitters. Each returns false when it cannot be done (the limit of PARTICLE_MAX_EMITTERS, the last emitter, an index out of range).
		bool add_emitter();
		bool remove_emitter(size_t index);
		bool duplicate_emitter(size_t index);
		bool move_emitter(size_t index, int direction); // -1 up, +1 down

		// A name no other emitter has, from a base ("Emitter" -> "Emitter 2").
		std::string unique_name(const std::string& base, std::optional<size_t> except = std::nullopt) const;

		// The selection and the preview switches are not part of the document: no undo, never dirty.
		size_t selected() const { return m_Selected; }
		void select(size_t index);
		bool muted(size_t index) const { return index < m_Muted.size() && m_Muted[index]; }
		bool soloed(size_t index) const { return index < m_Solo.size() && m_Solo[index]; }
		void set_muted(size_t index, bool muted);
		void set_solo(size_t index, bool solo);
		bool any_solo() const;
		// Whether an emitter plays in the preview: not muted, and when some emitter is soloed, that it is one of them.
		bool audible(size_t index) const;
		// The effect the preview plays: the document with the emitters that are not audible turned silent (no rate, no bursts).
		ParticleEffectData preview_data() const;
		// Changes since the last save when the preview switches are ignored (they are never part of it).
		bool dirty() const { return m_History.dirty(); }
		void mark_saved() { m_History.mark_saved(); }

		bool can_undo() const { return m_History.can_undo(); }
		bool can_redo() const { return m_History.can_redo(); }
		void undo();
		void redo();

	private:
		void fix_switches();

		ParticleEffectData m_Data{};
		EditHistory<ParticleEffectData> m_History{};
		u64 m_Revision = 1;
		bool m_Gesture = false;
		size_t m_Selected = 0;
		std::array<bool, PARTICLE_MAX_EMITTERS> m_Muted{};
		std::array<bool, PARTICLE_MAX_EMITTERS> m_Solo{};
	};

	// What a stored size of an effect weighs for the undo budget.
	size_t particle_effect_bytes(const ParticleEffectData& effect);
}
