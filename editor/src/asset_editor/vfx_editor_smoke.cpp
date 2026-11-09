#include <editor_common.h>
#include "vfx_editor_smoke.h"

#include <fstream>

#include <asset_pipeline/asset_database.h>
#include <assets/particle_effect.h>
#include <renderer/view/view_types.h>

#include "asset_editor_panel.h"
#include "thumbnail_cache.h"
#include "vfx_editor.h"

namespace wiwa
{
	namespace
	{
		constexpr u32 FRAME_WRITE = 5;
		constexpr u32 FRAME_LIT = 60;
		constexpr u32 FRAME_EDIT = 70;
		constexpr u32 FRAME_RESTART = 80; // after the silenced effect reached the render thread: a particle born before it would light thousands of pixels
		constexpr u32 FRAME_SILENT = 105;
		constexpr u32 FRAME_THUMBNAILS = 90;   // from here the Asset Browser's thumbnail cache is asked for the two effects
		constexpr u32 FRAME_END = 125;
		constexpr u32 THUMBNAIL_PATIENCE = 2500; // frames to wait for them: a thumbnail plays the effect in real time

		ParticleEffectData smoke_effect(bool quiet = false)
		{
			ParticleEffectData effect;
			effect.Name = "VfxEditorSmoke";
			ParticleEmitterData glow;
			glow.Name = "Glow";
			glow.Blend = ParticleBlend::Additive;
			glow.Rate = ParticleValue::constant(150.0f);
			glow.Shape = ParticleShape::Sphere;
			glow.ShapeSize = glm::vec3(0.3f);
			glow.Lifetime = ParticleValue::constant(1.0f);
			glow.StartSpeed = ParticleValue::constant(0.3f);
			glow.StartSize = ParticleValue::constant(0.25f);
			glow.StartColor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
			glow.Intensity = 4.0f;
			glow.SoftDistance = 0.0f;
			if (quiet)
			{
				glow.Name = "Quiet";
				glow.Rate = ParticleValue::constant(0.0f);
			}
			glow.bake();
			effect.Emitters.push_back(std::move(glow));
			return effect;
		}

		VfxEditor* vfx_editor_of(AssetEditorPanel& panel)
		{
			IAssetEditor* editor = panel.editor();
			return editor != nullptr && std::string(editor->kind()) == "Particle effect" ? static_cast<VfxEditor*>(editor) : nullptr;
		}
	}

	VfxEditorSmoke::~VfxEditorSmoke()
	{
		std::error_code removed;
		if (!m_Dir.empty())
		{
			std::filesystem::remove_all(m_Dir, removed);
		}
	}

	// Pixels where green stands out of a grey background: only the particles of the smoke effect are green.
	void VfxEditorSmoke::read_back_lit(u32& into, bool& done)
	{
		VfxEditor* editor = vfx_editor_of(m_Panel);
		if (editor == nullptr || !editor->preview().view().valid())
		{
			into = 0;
			done = true;
			return;
		}
		done = false;
		u32* target = &into;
		bool* flag = &done;
		editor->preview().view().read_back([target, flag](ImageData image)
			{
				u32 lit = 0;
				u64 sum[3] = { 0, 0, 0 };
				for (size_t p = 0; p + 3 < image.Rgba.size(); p += 4)
				{
					const int r = image.Rgba[p];
					const int g = image.Rgba[p + 1];
					const int b = image.Rgba[p + 2];
					sum[0] += static_cast<u64>(r);
					sum[1] += static_cast<u64>(g);
					sum[2] += static_cast<u64>(b);
					if (g > r + 40 && g > b + 40)
					{
						lit++;
					}
				}
				const u64 pixels = std::max<u64>(static_cast<u64>(image.Rgba.size() / 4), 1);
				WI_INFO("Editor", "VFX smoke readback {0}x{1}: {2} green pixels, mean color {3} {4} {5}", image.Width, image.Height, lit, sum[0] / pixels, sum[1] / pixels, sum[2] / pixels);
								*target = lit;
				*flag = true;
			});
	}

	std::string VfxEditorSmoke::check_model()
	{
		VfxEditor* editor = vfx_editor_of(m_Panel);
		if (editor == nullptr || editor->model() == nullptr)
		{
			return "the Asset Editor did not open the effect";
		}
		VfxEditModel& model = *editor->model();
		if (model.data().Emitters.size() != 1 || model.dirty() || model.can_undo())
		{
			return "a freshly opened effect should be one emitter, clean, with nothing to undo";
		}

		// add, undo and redo; undoing back to the saved state is clean again
		if (!model.add_emitter() || model.data().Emitters.size() != 2 || !model.dirty() || model.selected() != 1)
		{
			return "add_emitter";
		}
		if (model.data().Emitters[1].Name != "Emitter")
		{
			return "the new emitter has the base name";
		}
		model.undo();
		if (model.data().Emitters.size() != 1 || model.dirty() || !model.can_redo())
		{
			return "undo of an added emitter";
		}
		model.redo();
		if (model.data().Emitters.size() != 2 || !model.dirty())
		{
			return "redo of an added emitter";
		}

		// duplicate gets a new unique name; the limit is enforced
		if (!model.duplicate_emitter(0) || model.data().Emitters.size() != 3 || model.data().Emitters[1].Name == model.data().Emitters[0].Name)
		{
			return "duplicate_emitter";
		}
		while (model.data().Emitters.size() < PARTICLE_MAX_EMITTERS)
		{
			model.add_emitter();
		}
		if (model.add_emitter() || model.duplicate_emitter(0))
		{
			return "the emitter limit is not enforced";
		}
		for (size_t i = 0; i < model.data().Emitters.size(); i++)
		{
			for (size_t j = i + 1; j < model.data().Emitters.size(); j++)
			{
				if (model.data().Emitters[i].Name == model.data().Emitters[j].Name)
				{
					return "two emitters share a name";
				}
			}
		}
		while (model.data().Emitters.size() > 2)
		{
			if (!model.remove_emitter(model.data().Emitters.size() - 1))
			{
				return "remove_emitter";
			}
		}
		if (model.remove_emitter(9))
		{
			return "remove_emitter out of range";
		}
		const std::string firstName = model.data().Emitters[0].Name;
		if (!model.move_emitter(1, -1) || model.data().Emitters[1].Name != firstName || model.move_emitter(0, -1))
		{
			return "move_emitter";
		}
		model.move_emitter(0, 1); // and back

		// solo and mute: the preview plays what is audible
		model.set_muted(0, true);
		if (model.audible(0) || !model.audible(1))
		{
			return "mute";
		}
		model.set_muted(0, false);
		model.set_solo(1, true);
		if (model.audible(0) || !model.audible(1))
		{
			return "solo";
		}
		if (model.preview_data().Emitters[0].Rate.A != 0.0f || model.preview_data().Emitters[1].Rate == ParticleValue::constant(0.0f))
		{
			return "the preview data silences the emitters that are not audible";
		}
		model.set_solo(1, false);

		// one drag is one undo step
		const size_t before = model.data().Emitters.size();
		model.undo();
		while (model.can_undo())
		{
			model.undo();
		}
		if (model.data().Emitters.size() != 1 || model.dirty())
		{
			return "undo all the way back (emitters " + std::to_string(before) + " -> " + std::to_string(model.data().Emitters.size()) + ")";
		}
		const float original = model.data().Emitters[0].StartSize.A;
		for (int step = 1; step <= 5; step++)
		{
			ParticleEffectData next = model.data();
			next.Emitters[0].StartSize.A = original + 0.01f * static_cast<float>(step);
			model.edit(std::move(next), true); // the slider is still held
		}
		model.end_gesture();
		if (!model.dirty())
		{
			return "a drag makes the effect dirty";
		}
		model.undo();
		if (model.data().Emitters[0].StartSize.A != original || model.dirty() || model.can_undo())
		{
			return "a drag of five frames is one undo step";
		}
		model.redo();

		// an effect that cannot play cannot be saved, and the file stays as it was
		{
			ParticleEffectData next = model.data();
			next.Emitters[0].Lifetime = ParticleValue::constant(0.0f);
			model.edit(std::move(next));
			std::string error;
			if (editor->save(error) || error.empty())
			{
				return "a zero lifetime was saved";
			}
			model.undo();
		}

		// a valid edit is saved and read back
		if (!model.add_emitter())
		{
			return "add before save";
		}
		m_Panel.save(); // through the panel: it then knows the change on disk is its own (it would reload the editor otherwise)
		if (model.dirty() || !m_Panel.last_error().empty())
		{
			return "save: " + m_Panel.last_error();
		}
		std::ifstream in(m_File, std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		std::string reason;
		const std::optional<ParticleEffectData> read = read_particle_file(text, reason);
		if (!read || read->Emitters.size() != 2 || !(*read == model.data()))
		{
			return "the saved file reads back as another effect: " + reason;
		}
		return {};
	}

	bool VfxEditorSmoke::update(u32 frame, std::string& failure)
	{
		failure.clear();
		if (frame == FRAME_WRITE)
		{
			m_Dir = m_Database.get_assets_root() / "zz_vfx_editor_smoke";
			std::error_code error;
			std::filesystem::create_directories(m_Dir, error);
			m_File = m_Dir / "glow.wfx";
			{
				std::ofstream out(m_File, std::ios::binary | std::ios::trunc);
				out << write_particle_file(smoke_effect());
			}
			m_Quiet = m_Dir / "quiet.wfx";
			{
				std::ofstream out(m_Quiet, std::ios::binary | std::ios::trunc);
				out << write_particle_file(smoke_effect(true));
			}
			if (!m_Database.ensure_imported(m_File).has_value() || !m_Database.ensure_imported(m_Quiet).has_value())
			{
				failure = "the effects did not import";
				return true;
			}
			m_Panel.open_file(m_File);
			return false;
		}
		if (frame < FRAME_WRITE)
		{
			return false;
		}

		VfxEditor* editor = vfx_editor_of(m_Panel);
		if (editor != nullptr && editor->preview().is_open())
		{
			m_MaxAlive = std::max(m_MaxAlive, editor->preview().stats().Alive);
		}

		if (frame == FRAME_LIT)
		{
			if (editor == nullptr || !editor->preview().is_open())
			{
				failure = "the editor or its preview is not open at frame " + std::to_string(frame) + " (editor " + (editor != nullptr ? "open" : "missing") + ", kind '" + m_Panel.open_kind() +
					"', frames drawn " + std::to_string(m_Panel.frames_with_editor()) + ", error \"" + m_Panel.last_error() + "\")";
				return true;
			}
			read_back_lit(m_Lit, m_LitDone);
		}
		if (frame == FRAME_EDIT)
		{
			m_ModelFailure = check_model();
			m_ModelChecked = true;
			if (editor != nullptr && editor->model() != nullptr)
			{
				// silence every emitter (the restart comes later): the preview must then show nothing
				for (size_t i = 0; i < editor->model()->data().Emitters.size(); i++)
				{
					editor->model()->set_muted(i, true);
				}
			}
		}
		if (frame == FRAME_RESTART && editor != nullptr)
		{
			editor->preview().restart();
		}
		if (frame == FRAME_SILENT)
		{
			read_back_lit(m_LitSilent, m_LitSilentDone);
		}
		// the thumbnails of the Asset Browser: the glow effect gets a picture, the quiet one draws nothing and gets none
		if (m_Thumbnails != nullptr && frame >= FRAME_THUMBNAILS)
		{
			m_GlowTexture = m_Thumbnails->request(m_File);
			m_QuietTexture = m_Thumbnails->request(m_Quiet);
			if (m_GlowTexture != 0 && m_ThumbDoneFrame == 0)
			{
				m_ThumbDoneFrame = frame;
			}
		}
		const bool thumbsPending = m_Thumbnails != nullptr && m_GlowTexture == 0 && frame < THUMBNAIL_PATIENCE;
		const bool quietPending = m_Thumbnails != nullptr && m_ThumbDoneFrame != 0 && frame < m_ThumbDoneFrame + 400; // the quiet one is tried after the glow one
		if (frame < FRAME_END || thumbsPending || quietPending)
		{
			return false;
		}

		if (!m_ModelFailure.empty())
		{
			failure = "edit model: " + m_ModelFailure;
		}
		else if (!m_LitDone || !m_LitSilentDone)
		{
			failure = "a readback of the preview never arrived";
		}
		else if (m_MaxAlive == 0)
		{
			VfxEditor* shown = vfx_editor_of(m_Panel);
			failure = "the preview never counted a particle (lit " + std::to_string(m_Lit) + ", silent " + std::to_string(m_LitSilent) + ", play time " +
				(shown != nullptr ? std::to_string(shown->preview().play_time()) + (shown->preview().playing() ? ", playing" : ", not playing") + ", allocated " +
					std::to_string(shown->preview().stats().Allocated) : std::string("no editor")) + ")";
		}
		else if (m_Lit < 200)
		{
			failure = "the playing preview shows too few green pixels (" + std::to_string(m_Lit) + ")";
		}
		else if (m_LitSilent * 10 > m_Lit)
		{
			failure = "the silenced preview still shows green pixels (" + std::to_string(m_LitSilent) + " of " + std::to_string(m_Lit) + ")";
		}
		else if (m_Thumbnails != nullptr && m_GlowTexture == 0)
		{
			failure = "the effect got no thumbnail";
		}
		else if (m_Thumbnails != nullptr && m_QuietTexture != 0)
		{
			failure = "an effect that spawns nothing got a thumbnail (the control)";
		}
		else if (m_Panel.frames_with_editor() < 100 || !m_Panel.last_error().empty())
		{
			failure = "the panel drew " + std::to_string(m_Panel.frames_with_editor()) + " frames, error \"" + m_Panel.last_error() + "\"";
		}
		return true;
	}
}
