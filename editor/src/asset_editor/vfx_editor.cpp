#include <editor_common.h>
#include "vfx_editor.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include <imgui.h>

#include <assets/asset_manager.h>
#include <core/engine.h>

#include <asset_pipeline/asset_database.h>
#include <asset_pipeline/atomic_write.h>
#include <asset_pipeline/file_utils.h>
#include <panels/asset_field.h>

using wiwa::LogLevel; // for WI_LOG
#define WI_ASSET_LOG(level, ...) WI_LOG(level, "AssetEditor", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		ImVec4 error_color() { return ImVec4(1.0f, 0.45f, 0.4f, 1.0f); }

		std::optional<AssetRef> asset_at(const std::string& path)
		{
			AssetManager* assets = Engine::get().get_module<AssetManager>();
			return assets != nullptr ? assets->find_asset(path) : std::nullopt;
		}

		// A value of an emitter the editor offers a curve for (the order is the numbering of VfxEditor::select_curve).
		struct ValueSlot
		{
			const char* Label;
			ParticleValue ParticleEmitterData::* Member;
			float Speed;
			float Min;
			float Max;
		};

		const ValueSlot VALUE_SLOTS[] = {
			{ "Rate (per second)", &ParticleEmitterData::Rate, 0.5f, 0.0f, 100000.0f },
			{ "Lifetime (s)", &ParticleEmitterData::Lifetime, 0.01f, 0.01f, 600.0f },
			{ "Start speed", &ParticleEmitterData::StartSpeed, 0.05f, -1000.0f, 1000.0f },
			{ "Start size", &ParticleEmitterData::StartSize, 0.01f, 0.0f, 1000.0f },
			{ "Start rotation (deg)", &ParticleEmitterData::StartRotation, 1.0f, -3600.0f, 3600.0f },
			{ "Size over life", &ParticleEmitterData::SizeOverLife, 0.01f, 0.0f, 100.0f },
			{ "Rotation speed (deg/s)", &ParticleEmitterData::RotationSpeed, 1.0f, -3600.0f, 3600.0f },
			{ "Alpha over life", &ParticleEmitterData::AlphaOverLife, 0.01f, 0.0f, 4.0f },
			{ "Frame over life", &ParticleEmitterData::FrameOverLife, 0.005f, 0.0f, 1.0f },
		};
		constexpr int VALUE_SLOT_COUNT = static_cast<int>(sizeof(VALUE_SLOTS) / sizeof(VALUE_SLOTS[0]));

		template<typename E>
		bool enum_combo(const char* label, E& value, std::initializer_list<const char*> names)
		{
			int index = static_cast<int>(value);
			bool changed = false;
			ImGui::SetNextItemWidth(140.0f);
			if (ImGui::BeginCombo(label, index >= 0 && index < static_cast<int>(names.size()) ? *(names.begin() + index) : "?"))
			{
				int i = 0;
				for (const char* name : names)
				{
					if (ImGui::Selectable(name, i == index))
					{
						value = static_cast<E>(i);
						changed = true;
					}
					i++;
				}
				ImGui::EndCombo();
			}
			return changed;
		}

		float first_value(const ParticleValue& value)
		{
			return value.Kind == ParticleValue::Mode::Curve && !value.Keys.empty() ? value.Keys.front().Value : value.A;
		}

		// Changes how a value is given, keeping what it was as close as it can be.
		void switch_mode(ParticleValue& value, ParticleValue::Mode mode)
		{
			if (value.Kind == mode)
			{
				return;
			}
			const float first = first_value(value);
			const float last = value.Kind == ParticleValue::Mode::Curve && !value.Keys.empty() ? value.Keys.back().Value : (value.Kind == ParticleValue::Mode::Range ? value.B : first);
			switch (mode)
			{
			case ParticleValue::Mode::Constant:
				value.A = first;
				break;
			case ParticleValue::Mode::Range:
				value.A = first;
				value.B = last;
				break;
			case ParticleValue::Mode::Curve:
				value.Keys = { ParticleCurveKey{ 0.0f, first }, ParticleCurveKey{ 1.0f, last } };
				break;
			}
			value.Kind = mode;
			value.bake();
		}

		// One module value: how it is given, its numbers, and the button that shows its curve below. Returns whether it changed.
		bool value_row(int slotIndex, ParticleValue& value, int& curveSlot, VfxCurveView& curveView)
		{
			const ValueSlot& slot = VALUE_SLOTS[slotIndex];
			bool changed = false;
			ImGui::PushID(slotIndex);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(slot.Label);
			ImGui::SameLine(190.0f);

			int mode = static_cast<int>(value.Kind);
			ImGui::SetNextItemWidth(84.0f);
			if (ImGui::Combo("##mode", &mode, "Constant\0Range\0Curve\0"))
			{
				switch_mode(value, static_cast<ParticleValue::Mode>(mode));
				if (value.Kind == ParticleValue::Mode::Curve)
				{
					curveSlot = slotIndex;
					curveView.fit();
				}
				changed = true;
			}
			ImGui::SameLine();
			switch (value.Kind)
			{
			case ParticleValue::Mode::Constant:
				ImGui::SetNextItemWidth(90.0f);
				changed |= ImGui::DragFloat("##a", &value.A, slot.Speed, slot.Min, slot.Max, "%.3f");
				break;
			case ParticleValue::Mode::Range:
				ImGui::SetNextItemWidth(70.0f);
				changed |= ImGui::DragFloat("##a", &value.A, slot.Speed, slot.Min, slot.Max, "%.3f");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(70.0f);
				changed |= ImGui::DragFloat("##b", &value.B, slot.Speed, slot.Min, slot.Max, "%.3f");
				break;
			case ParticleValue::Mode::Curve:
				if (ImGui::Button(curveSlot == slotIndex ? "Editing below" : "Edit curve"))
				{
					curveSlot = slotIndex;
					curveView.fit();
				}
				ImGui::SameLine();
				ImGui::TextDisabled("%zu keys", value.Keys.size());
				break;
			}
			ImGui::PopID();
			return changed;
		}

		void normalize_color_keys(std::vector<ParticleColorKey>& keys)
		{
			for (ParticleColorKey& key : keys)
			{
				key.Time = std::isfinite(key.Time) ? std::clamp(key.Time, 0.0f, 1.0f) : 0.0f;
			}
			std::stable_sort(keys.begin(), keys.end(), [](const ParticleColorKey& a, const ParticleColorKey& b) { return a.Time < b.Time; });
			for (size_t i = 1; i < keys.size(); i++)
			{
				if (keys[i].Time <= keys[i - 1].Time)
				{
					keys[i].Time = std::min(keys[i - 1].Time + 0.001f, 1.0f);
				}
			}
		}
	}

	VfxEditor::~VfxEditor() = default;

	// The asset a file is: the catalog knows it, or (a file that was only just made) the database imports it now.
	AssetRef VfxEditor::find_ref(const std::filesystem::path& file)
	{
		if (const std::optional<AssetRef> known = asset_at(file.generic_string()))
		{
			return *known;
		}
		return m_Host.Database.ensure_imported(file).value_or(AssetRef{});
	}

	bool VfxEditor::load(const std::filesystem::path& file, std::string& error)
	{
		const std::optional<std::vector<std::byte>> bytes = read_binary_file(file);
		if (!bytes)
		{
			error = "cannot read " + file.string();
			return false;
		}
		std::optional<ParticleEffectData> effect = read_particle_file(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), error);
		if (!effect)
		{
			return false;
		}
		m_File = file;
		m_Model = std::make_unique<VfxEditModel>(std::move(*effect));
		m_Ref = find_ref(file);
		m_Message = m_Ref.valid() ? std::string{} : "This file is not in the asset catalog yet: the preview needs it to be imported";
		m_ShownRevision = 0;
		return true;
	}

	bool VfxEditor::save(std::string& error)
	{
		error.clear();
		if (!m_Model)
		{
			error = "there is nothing open";
			return false;
		}
		if (!particle_effect_valid(m_Model->data(), error))
		{
			return false; // the file would not import
		}
		const std::string text = write_particle_file(m_Model->data());
		if (!write_file_atomic(m_File, text.data(), text.size(), error))
		{
			return false;
		}
		m_Model->mark_saved();
		return true;
	}

	void VfxEditor::sync_preview()
	{
		if (!m_Model)
		{
			return;
		}
		if (!m_Ref.valid())
		{
			// a file that was only just imported is not in the catalog when it is opened
			m_Ref = find_ref(m_File);
			if (m_Ref.valid())
			{
				m_Message.clear();
			}
		}
		if (!m_Preview.is_open() && m_Ref.valid())
		{
			m_Preview.open(m_Ref);
			m_ShownRevision = 0;
		}
		if (m_Preview.is_open() && m_ShownRevision != m_Model->revision())
		{
			m_Preview.push_effect(m_Model->preview_data());
			m_ShownRevision = m_Model->revision();
		}
	}

	void VfxEditor::draw_preview(ImVec2 size)
	{
		if (!m_Model)
		{
			return;
		}
		sync_preview();
		if (!m_Preview.is_open())
		{
			ImGui::TextColored(error_color(), "%s", m_Message.empty() ? "The preview could not start" : m_Message.c_str());
			return;
		}
		m_Preview.draw(size, m_Model->data(), m_Model->data().Emitters.empty() ? -1 : static_cast<int>(m_Model->selected()));
	}

	void VfxEditor::draw_tools()
	{
		if (!m_Model)
		{
			return;
		}
		VfxEditModel& model = *m_Model;

		std::string problem;
		if (!particle_effect_valid(model.data(), problem))
		{
			ImGui::TextColored(error_color(), "Cannot be saved: %s", problem.c_str());
		}
		if (!m_Message.empty())
		{
			ImGui::TextColored(error_color(), "%s", m_Message.c_str());
		}

		// the emitters
		ImGui::SeparatorText("Emitters");
		if (ImGui::SmallButton("Add"))
		{
			model.add_emitter();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Duplicate"))
		{
			model.duplicate_emitter(model.selected());
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Remove"))
		{
			model.remove_emitter(model.selected());
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Up"))
		{
			model.move_emitter(model.selected(), -1);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Down"))
		{
			model.move_emitter(model.selected(), 1);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%zu of %u", model.data().Emitters.size(), PARTICLE_MAX_EMITTERS);

		for (size_t i = 0; i < model.data().Emitters.size(); i++)
		{
			ImGui::PushID(static_cast<int>(i));
			const bool audible = model.audible(i);
			if (!audible)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			}
			if (ImGui::Selectable(model.data().Emitters[i].Name.c_str(), i == model.selected(), ImGuiSelectableFlags_None, ImVec2(ImGui::GetContentRegionAvail().x - 56.0f, 0.0f)))
			{
				model.select(i);
			}
			if (!audible)
			{
				ImGui::PopStyleColor();
			}
			ImGui::SameLine();
			const bool solo = model.soloed(i);
			if (solo)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.6f, 0.1f, 1.0f));
			}
			if (ImGui::SmallButton("S"))
			{
				model.set_solo(i, !solo);
			}
			if (solo)
			{
				ImGui::PopStyleColor();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Solo: play only the soloed emitters");
			}
			ImGui::SameLine();
			const bool muted = model.muted(i);
			if (muted)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.25f, 0.2f, 1.0f));
			}
			if (ImGui::SmallButton("M"))
			{
				model.set_muted(i, !muted);
			}
			if (muted)
			{
				ImGui::PopStyleColor();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Mute: do not play this emitter in the preview");
			}
			ImGui::PopID();
		}

		if (model.data().Emitters.empty())
		{
			return;
		}

		// the edits of this frame: a copy that the widgets change, put back as one step when it differs
		ParticleEffectData next = model.data();
		ParticleEmitterData& emitter = next.Emitters[model.selected()];
		AssetFieldContext context;
		context.Database = &m_Host.Database;

		char name[128];
		std::snprintf(name, sizeof(name), "%s", next.Name.c_str());
		ImGui::SetNextItemWidth(-60.0f);
		if (ImGui::InputText("Effect name", name, sizeof(name)))
		{
			next.Name = name;
		}
		std::snprintf(name, sizeof(name), "%s", emitter.Name.c_str());
		ImGui::SetNextItemWidth(-60.0f);
		if (ImGui::InputText("Emitter name", name, sizeof(name)) && name[0] != '\0')
		{
			emitter.Name = model.unique_name(name, model.selected());
		}

		if (ImGui::CollapsingHeader("Renderer", ImGuiTreeNodeFlags_DefaultOpen))
		{
			enum_combo("Kind", emitter.Renderer, { "Sprite", "Ribbon", "Mesh" });
			if (emitter.Renderer != ParticleRenderer::Sprite)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Ribbons and mesh particles are in the format but are not drawn yet");
			}
			std::string dropped;
			context.Expected = AssetType::Material;
			AssetFieldEvent event = draw_asset_slot("##fxmaterial", "Material", emitter.Material, context, dropped);
			if (event == AssetFieldEvent::Dropped)
			{
				emitter.Material = asset_at(dropped).value_or(emitter.Material);
			}
			else if (event == AssetFieldEvent::Cleared)
			{
				emitter.Material = {};
			}
			if (emitter.Renderer == ParticleRenderer::Mesh)
			{
				context.Expected = AssetType::Mesh;
				event = draw_asset_slot("##fxmesh", "Mesh", emitter.Mesh, context, dropped);
				if (event == AssetFieldEvent::Dropped)
				{
					emitter.Mesh = asset_at(dropped).value_or(emitter.Mesh);
				}
				else if (event == AssetFieldEvent::Cleared)
				{
					emitter.Mesh = {};
				}
			}
			int sheet[2] = { static_cast<int>(emitter.SheetColumns), static_cast<int>(emitter.SheetRows) };
			ImGui::SetNextItemWidth(140.0f);
			if (ImGui::DragInt2("Sheet (columns, rows)", sheet, 0.2f, 1, 256))
			{
				emitter.SheetColumns = static_cast<u32>(std::max(sheet[0], 1));
				emitter.SheetRows = static_cast<u32>(std::max(sheet[1], 1));
			}
			enum_combo("Align", emitter.Align, { "Camera", "Velocity", "Axis", "World" });
			enum_combo("Blend", emitter.Blend, { "Additive", "Alpha", "Multiply" });
			ImGui::SetNextItemWidth(140.0f);
			ImGui::DragFloat("Soft distance", &emitter.SoftDistance, 0.01f, 0.0f, 10.0f, "%.2f m");
			ImGui::SetNextItemWidth(140.0f);
			ImGui::DragFloat("Intensity", &emitter.Intensity, 0.02f, 0.0f, 64.0f, "%.2f");
			ImGui::BeginDisabled(emitter.Blend != ParticleBlend::Alpha);
			ImGui::Checkbox("Sorted back to front", &emitter.Sorted);
			ImGui::EndDisabled();
			int maxParticles = static_cast<int>(emitter.MaxParticles);
			ImGui::SetNextItemWidth(140.0f);
			if (ImGui::DragInt("Max particles", &maxParticles, 8.0f, 1, static_cast<int>(PARTICLE_LIMIT_MAX_PARTICLES)))
			{
				emitter.MaxParticles = static_cast<u32>(std::clamp(maxParticles, 1, static_cast<int>(PARTICLE_LIMIT_MAX_PARTICLES)));
			}
			if (emitter.Renderer == ParticleRenderer::Ribbon)
			{
				int segments = static_cast<int>(emitter.RibbonSegments);
				ImGui::SetNextItemWidth(140.0f);
				if (ImGui::DragInt("Ribbon segments", &segments, 0.2f, 2, static_cast<int>(PARTICLE_MAX_RIBBON_SEGMENTS)))
				{
					emitter.RibbonSegments = static_cast<u32>(std::clamp(segments, 2, static_cast<int>(PARTICLE_MAX_RIBBON_SEGMENTS)));
				}
				ImGui::SetNextItemWidth(140.0f);
				ImGui::DragFloat("Min segment length", &emitter.MinSegmentLength, 0.005f, 0.0f, 10.0f, "%.3f m");
			}
		}

		if (ImGui::CollapsingHeader("Collision"))
		{
			enum_combo("Mode", emitter.Collision, { "None", "Depth" });
			ImGui::BeginDisabled(emitter.Collision == ParticleCollision::None);
			enum_combo("Response", emitter.Response, { "Bounce", "Stick", "Kill" });
			ImGui::SetNextItemWidth(140.0f);
			ImGui::DragFloat("Restitution", &emitter.Restitution, 0.01f, 0.0f, 1.0f, "%.2f");
			ImGui::EndDisabled();
		}

		bool valueChanged = false;
		if (ImGui::CollapsingHeader("Spawn", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Looping", &emitter.Looping);
			ImGui::SetNextItemWidth(140.0f);
			ImGui::DragFloat("Duration (s)", &emitter.Duration, 0.05f, 0.05f, 3600.0f, "%.2f");
			ImGui::SetNextItemWidth(140.0f);
			ImGui::DragFloat("Start delay (s)", &emitter.StartDelay, 0.05f, 0.0f, 3600.0f, "%.2f");
			valueChanged |= value_row(0, emitter.Rate, m_CurveSlot, m_CurveView);
			ImGui::TextUnformatted("Bursts");
			for (size_t b = 0; b < emitter.Bursts.size(); b++)
			{
				ImGui::PushID(1000 + static_cast<int>(b));
				ImGui::SetNextItemWidth(90.0f);
				ImGui::DragFloat("##time", &emitter.Bursts[b].Time, 0.02f, 0.0f, 3600.0f, "at %.2f s");
				ImGui::SameLine();
				int count = static_cast<int>(emitter.Bursts[b].Count);
				ImGui::SetNextItemWidth(90.0f);
				if (ImGui::DragInt("##count", &count, 0.5f, 1, 100000, "%d particles"))
				{
					emitter.Bursts[b].Count = static_cast<u32>(std::max(count, 1));
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("x"))
				{
					emitter.Bursts.erase(emitter.Bursts.begin() + static_cast<std::ptrdiff_t>(b));
					ImGui::PopID();
					break;
				}
				ImGui::PopID();
			}
			if (ImGui::SmallButton("Add burst"))
			{
				emitter.Bursts.push_back(ParticleBurst{});
			}
			enum_combo("Shape", emitter.Shape, { "Point", "Sphere", "Cone", "Box" });
			if (emitter.Shape == ParticleShape::Sphere || emitter.Shape == ParticleShape::Cone)
			{
				ImGui::SetNextItemWidth(140.0f);
				ImGui::DragFloat("Radius", &emitter.ShapeSize.x, 0.01f, 0.0f, 1000.0f, "%.2f m");
			}
			else if (emitter.Shape == ParticleShape::Box)
			{
				ImGui::SetNextItemWidth(220.0f);
				ImGui::DragFloat3("Extents", &emitter.ShapeSize.x, 0.01f, 0.0f, 1000.0f, "%.2f");
			}
			if (emitter.Shape == ParticleShape::Cone)
			{
				ImGui::SetNextItemWidth(140.0f);
				ImGui::DragFloat("Cone angle", &emitter.ConeAngle, 0.5f, 0.0f, 89.0f, "%.1f deg");
			}
			ImGui::Checkbox("World space", &emitter.WorldSpace);
		}

		if (ImGui::CollapsingHeader("Start values", ImGuiTreeNodeFlags_DefaultOpen))
		{
			valueChanged |= value_row(1, emitter.Lifetime, m_CurveSlot, m_CurveView);
			valueChanged |= value_row(2, emitter.StartSpeed, m_CurveSlot, m_CurveView);
			valueChanged |= value_row(3, emitter.StartSize, m_CurveSlot, m_CurveView);
			valueChanged |= value_row(4, emitter.StartRotation, m_CurveSlot, m_CurveView);
			ImGui::SetNextItemWidth(220.0f);
			ImGui::ColorEdit4("Start color", &emitter.StartColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		}

		if (ImGui::CollapsingHeader("Forces"))
		{
			ImGui::SetNextItemWidth(220.0f);
			ImGui::DragFloat3("Gravity", &emitter.Gravity.x, 0.02f, -100.0f, 100.0f, "%.2f");
			ImGui::SetNextItemWidth(140.0f);
			ImGui::DragFloat("Drag", &emitter.Drag, 0.01f, 0.0f, 100.0f, "%.2f");
			ImGui::SetNextItemWidth(140.0f);
			ImGui::DragFloat("Noise strength", &emitter.NoiseStrength, 0.01f, 0.0f, 100.0f, "%.2f");
			ImGui::SetNextItemWidth(140.0f);
			ImGui::DragFloat("Noise frequency", &emitter.NoiseFrequency, 0.01f, 0.0f, 100.0f, "%.2f");
		}

		if (ImGui::CollapsingHeader("Over life", ImGuiTreeNodeFlags_DefaultOpen))
		{
			valueChanged |= value_row(5, emitter.SizeOverLife, m_CurveSlot, m_CurveView);
			valueChanged |= value_row(6, emitter.RotationSpeed, m_CurveSlot, m_CurveView);
			valueChanged |= value_row(7, emitter.AlphaOverLife, m_CurveSlot, m_CurveView);
			valueChanged |= value_row(8, emitter.FrameOverLife, m_CurveSlot, m_CurveView);
			ImGui::TextDisabled("Color over life: in the pane below");
		}

		(void)valueChanged;
		// a drag or a typed number goes on while its item is active: one undo step for the whole gesture
		model.edit(std::move(next), ImGui::IsAnyItemActive());
	}

	void VfxEditor::draw_bottom(ImVec2 size)
	{
		(void)size;
		if (!m_Model || m_Model->data().Emitters.empty())
		{
			return;
		}
		VfxEditModel& model = *m_Model;
		ParticleEffectData next = model.data();
		ParticleEmitterData& emitter = next.Emitters[model.selected()];
		bool dragging = false;

		const float width = ImGui::GetContentRegionAvail().x;
		ImGui::BeginChild("##fxcurve", ImVec2(width * 0.58f, 0.0f), ImGuiChildFlags_Borders);
		m_CurveSlot = std::clamp(m_CurveSlot, 0, VALUE_SLOT_COUNT - 1);
		ImGui::SetNextItemWidth(220.0f);
		if (ImGui::BeginCombo("Curve of", VALUE_SLOTS[m_CurveSlot].Label))
		{
			for (int i = 0; i < VALUE_SLOT_COUNT; i++)
			{
				if (ImGui::Selectable(VALUE_SLOTS[i].Label, i == m_CurveSlot))
				{
					m_CurveSlot = i;
					m_CurveView.fit();
				}
			}
			ImGui::EndCombo();
		}
		ParticleValue& value = emitter.*(VALUE_SLOTS[m_CurveSlot].Member);
		if (value.Kind != ParticleValue::Mode::Curve)
		{
			ImGui::TextDisabled("This value is %s. Set it to Curve to shape it over the life of a particle.", value.Kind == ParticleValue::Mode::Constant ? "a constant" : "a range");
			if (ImGui::Button("Make it a curve"))
			{
				switch_mode(value, ParticleValue::Mode::Curve);
				m_CurveView.fit();
			}
		}
		else
		{
			const float tableHeight = ImGui::GetFrameHeightWithSpacing() * 2.0f + 4.0f;
			const ImVec2 room = ImGui::GetContentRegionAvail();
			m_CurveView.draw(value, ImVec2(room.x, std::max(room.y - tableHeight - 90.0f, 80.0f)), dragging);

			// the keys as numbers: the exact values, and the way to remove a key
			ImGui::BeginChild("##fxkeys", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
			for (size_t k = 0; k < value.Keys.size(); k++)
			{
				ImGui::PushID(2000 + static_cast<int>(k));
				ImGui::SetNextItemWidth(80.0f);
				bool edited = ImGui::DragFloat("##t", &value.Keys[k].Time, 0.005f, 0.0f, 1.0f, "t %.3f");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(90.0f);
				edited |= ImGui::DragFloat("##v", &value.Keys[k].Value, VALUE_SLOTS[m_CurveSlot].Speed, VALUE_SLOTS[m_CurveSlot].Min, VALUE_SLOTS[m_CurveSlot].Max, "%.3f");
				ImGui::SameLine();
				const bool canRemove = value.Keys.size() > 1;
				ImGui::BeginDisabled(!canRemove);
				if (ImGui::SmallButton("x"))
				{
					value.Keys.erase(value.Keys.begin() + static_cast<std::ptrdiff_t>(k));
					edited = true;
					ImGui::EndDisabled();
					ImGui::PopID();
					normalize_particle_keys(value.Keys);
					value.bake();
					break;
				}
				ImGui::EndDisabled();
				if (edited)
				{
					normalize_particle_keys(value.Keys);
					value.bake();
				}
				ImGui::PopID();
			}
			ImGui::EndChild();
		}
		ImGui::EndChild();

		ImGui::SameLine();
		ImGui::BeginChild("##fxgradient", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
		ImGui::TextUnformatted("Color over life (multiplies the start color)");
		if (emitter.ColorOverLife.empty())
		{
			ImGui::TextDisabled("No keys: the start color stays.");
		}
		else
		{
			// a strip of the baked gradient
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const float stripWidth = std::max(ImGui::GetContentRegionAvail().x - 4.0f, 16.0f);
			ImDrawList* list = ImGui::GetWindowDrawList();
			const int cells = static_cast<int>(PARTICLE_CURVE_SAMPLES);
			for (int c = 0; c < cells; c++)
			{
				const glm::vec4& color = emitter.ColorSamples[static_cast<size_t>(c)];
				const float x0 = origin.x + stripWidth * static_cast<float>(c) / cells;
				const float x1 = origin.x + stripWidth * static_cast<float>(c + 1) / cells;
				list->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + 18.0f), ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, 1.0f)));
			}
			ImGui::Dummy(ImVec2(stripWidth, 20.0f));
		}
		bool gradientChanged = false;
		for (size_t k = 0; k < emitter.ColorOverLife.size(); k++)
		{
			ImGui::PushID(3000 + static_cast<int>(k));
			ImGui::SetNextItemWidth(70.0f);
			gradientChanged |= ImGui::DragFloat("##t", &emitter.ColorOverLife[k].Time, 0.005f, 0.0f, 1.0f, "t %.2f");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(180.0f);
			gradientChanged |= ImGui::ColorEdit4("##c", &emitter.ColorOverLife[k].Color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
			ImGui::SameLine();
			if (ImGui::SmallButton("x"))
			{
				emitter.ColorOverLife.erase(emitter.ColorOverLife.begin() + static_cast<std::ptrdiff_t>(k));
				gradientChanged = true;
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}
		if (ImGui::SmallButton("Add color key") && emitter.ColorOverLife.size() < PARTICLE_CURVE_SAMPLES)
		{
			ParticleColorKey key;
			key.Time = emitter.ColorOverLife.empty() ? 0.0f : std::min(emitter.ColorOverLife.back().Time + 0.25f, 1.0f);
			key.Color = emitter.ColorOverLife.empty() ? glm::vec4(1.0f) : emitter.ColorOverLife.back().Color;
			emitter.ColorOverLife.push_back(key);
			gradientChanged = true;
		}
		if (gradientChanged)
		{
			normalize_color_keys(emitter.ColorOverLife);
		}
		ImGui::EndChild();

		model.edit(std::move(next), dragging || ImGui::IsAnyItemActive());
	}
}
