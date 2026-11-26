#include <editor_common.h>
#include "vehicle_editor.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <imgui.h>

#include <asset_pipeline/vehicle_file.h>
#include <physics/tire_model.h>

namespace wiwa
{
	namespace
	{
		const ImU32 AXIS_COLOR = IM_COL32(120, 120, 120, 255);
		const ImU32 TORQUE_COLOR = IM_COL32(255, 190, 80, 255);
		const ImU32 POWER_COLOR = IM_COL32(120, 200, 255, 255);
		const ImU32 FRONT_COLOR = IM_COL32(255, 140, 120, 255);
		const ImU32 REAR_COLOR = IM_COL32(140, 230, 140, 255);

		float torque_at(const VehicleSettingsData& data, float rpm)
		{
			const std::vector<TorquePoint>& curve = data.TorqueCurve;
			if (curve.empty())
			{
				return 0.0f;
			}
			if (rpm <= curve.front().Rpm)
			{
				return curve.front().Torque;
			}
			for (size_t i = 1; i < curve.size(); i++)
			{
				if (rpm <= curve[i].Rpm)
				{
					const float t = (rpm - curve[i - 1].Rpm) / (curve[i].Rpm - curve[i - 1].Rpm);
					return curve[i - 1].Torque + t * (curve[i].Torque - curve[i - 1].Torque);
				}
			}
			return curve.back().Torque;
		}

		void frame(ImDrawList* draw, ImVec2 origin, ImVec2 size, const char* title)
		{
			draw->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), AXIS_COLOR);
			draw->AddText(ImVec2(origin.x + 6.0f, origin.y + 4.0f), IM_COL32(220, 220, 220, 255), title);
		}
	}

	bool VehicleEditor::load(const std::filesystem::path& file, std::string& error)
	{
		std::ifstream stream(file, std::ios::binary);
		if (!stream)
		{
			error = "cannot read the file";
			return false;
		}
		std::stringstream text;
		text << stream.rdbuf();
		VehicleSettingsData data;
		if (!vehicle_from_text(text.str(), data, error))
		{
			return false;
		}
		m_File = file;
		m_Model = std::make_unique<VehicleEditModel>(std::move(data));
		return true;
	}

	bool VehicleEditor::save(std::string& error)
	{
		if (!m_Model)
		{
			error = "nothing to save";
			return false;
		}
		std::ofstream stream(m_File, std::ios::binary | std::ios::trunc);
		if (!stream)
		{
			error = "cannot write the file";
			return false;
		}
		stream << vehicle_to_json(m_Model->data()).dump(2);
		if (!stream)
		{
			error = "cannot write the file";
			return false;
		}
		m_Model->mark_saved();
		return true;
	}

	void VehicleEditor::draw_engine_plot(ImDrawList* draw, ImVec2 origin, ImVec2 size)
	{
		const VehicleSettingsData& data = m_Model->data();
		frame(draw, origin, size, "Torque (orange, Nm) and power (blue, kW) over rpm");
		float maxTorque = 1.0f;
		float maxPower = 1.0f;
		constexpr int kSamples = 64;
		for (int i = 0; i <= kSamples; i++)
		{
			const float rpm = data.MinRpm + (data.MaxRpm - data.MinRpm) * static_cast<float>(i) / kSamples;
			const float torque = torque_at(data, rpm);
			maxTorque = std::max(maxTorque, torque);
			maxPower = std::max(maxPower, torque * rpm * 0.10472f / 1000.0f);
		}
		ImVec2 lastTorque{};
		ImVec2 lastPower{};
		for (int i = 0; i <= kSamples; i++)
		{
			const float t = static_cast<float>(i) / kSamples;
			const float rpm = data.MinRpm + (data.MaxRpm - data.MinRpm) * t;
			const float torque = torque_at(data, rpm);
			const float power = torque * rpm * 0.10472f / 1000.0f;
			const ImVec2 pt(origin.x + t * size.x, origin.y + size.y - torque / maxTorque * (size.y - 24.0f));
			const ImVec2 pp(origin.x + t * size.x, origin.y + size.y - power / maxPower * (size.y - 24.0f));
			if (i > 0)
			{
				draw->AddLine(lastTorque, pt, TORQUE_COLOR, 2.0f);
				draw->AddLine(lastPower, pp, POWER_COLOR, 2.0f);
			}
			lastTorque = pt;
			lastPower = pp;
		}
		char label[96];
		std::snprintf(label, sizeof(label), "%.0f-%.0f rpm, peak %.0f Nm, %.0f kW", data.MinRpm, data.MaxRpm, maxTorque, maxPower);
		draw->AddText(ImVec2(origin.x + 6.0f, origin.y + size.y - 18.0f), IM_COL32(200, 200, 200, 255), label);
	}

	void VehicleEditor::draw_tire_plot(ImDrawList* draw, ImVec2 origin, ImVec2 size, bool lateral)
	{
		const VehicleSettingsData& data = m_Model->data();
		frame(draw, origin, size, lateral ? "Lateral force over slip angle 0-15 deg (front red, rear green; 0.5x, 1x, 1.5x load)" : "Longitudinal force over slip ratio 0-0.5");
		const float maxLoad = 1.5f * std::max(data.FrontTire.NominalLoad, data.RearTire.NominalLoad);
		const float top = maxLoad * std::max({ data.FrontTire.Lateral.D, data.FrontTire.Longitudinal.D, data.RearTire.Lateral.D, data.RearTire.Longitudinal.D }) * 1.6f;
		constexpr int kSamples = 48;
		for (const auto& [tire, color] : { std::pair{ &data.FrontTire, FRONT_COLOR }, std::pair{ &data.RearTire, REAR_COLOR } })
		{
			for (const float scale : { 0.5f, 1.0f, 1.5f })
			{
				const float load = tire->NominalLoad * scale;
				ImVec2 last{};
				for (int i = 0; i <= kSamples; i++)
				{
					const float t = static_cast<float>(i) / kSamples;
					const TireForces f = lateral ? tire_forces(*tire, load, 0.0f, glm::radians(15.0f) * t, 1.0f) : tire_forces(*tire, load, 0.5f * t, 0.0f, 1.0f);
					const float value = std::abs(lateral ? f.Lateral : f.Longitudinal);
					const ImVec2 p(origin.x + t * size.x, origin.y + size.y - std::min(value / std::max(top, 1.0f), 1.0f) * (size.y - 24.0f));
					if (i > 0)
					{
						draw->AddLine(last, p, color, scale == 1.0f ? 2.0f : 1.0f);
					}
					last = p;
				}
			}
		}
	}

	void VehicleEditor::draw_preview(ImVec2 size)
	{
		if (!m_Model)
		{
			return;
		}
		m_Frames++;
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float h = std::max((size.y - 16.0f) / 3.0f, 40.0f);
		const float w = std::max(size.x, 40.0f);
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw_engine_plot(draw, origin, ImVec2(w, h));
		draw_tire_plot(draw, ImVec2(origin.x, origin.y + h + 8.0f), ImVec2(w, h), true);
		draw_tire_plot(draw, ImVec2(origin.x, origin.y + 2.0f * (h + 8.0f)), ImVec2(w, h), false);
		ImGui::Dummy(ImVec2(w, 3.0f * h + 16.0f));
	}

	void VehicleEditor::after_widget(VehicleSettingsData next)
	{
		// only a widget that changed something edits: an unchanged one must not end the gesture another widget is in
		if (!(next == m_Model->data()))
		{
			m_Model->edit(std::move(next), ImGui::IsItemActive());
		}
		if (ImGui::IsItemDeactivated())
		{
			m_Model->end_gesture();
		}
	}

	bool VehicleEditor::float_field(const char* label, float VehicleSettingsData::* field, float speed, float minimum, float maximum, const char* format)
	{
		VehicleSettingsData next = m_Model->data();
		const bool changed = ImGui::DragFloat(label, &(next.*field), speed, minimum, maximum, format);
		after_widget(std::move(next));
		return changed;
	}

	bool VehicleEditor::tire_fields(const char* id, TireModel VehicleSettingsData::* tire)
	{
		bool changed = false;
		ImGui::PushID(id);
		for (const auto& [name, curve] : { std::pair{ "Longitudinal", &TireModel::Longitudinal }, std::pair{ "Lateral", &TireModel::Lateral } })
		{
			VehicleSettingsData next = m_Model->data();
			TireCurve& c = (next.*tire).*curve;
			float bcde[4] = { c.B, c.C, c.D, c.E };
			changed |= ImGui::DragFloat4(name, bcde, 0.01f, -10.0f, 100.0f, "%.3f");
			c = TireCurve{ bcde[0], bcde[1], bcde[2], bcde[3] };
			after_widget(std::move(next));
		}
		for (const auto& [name, member, speed] : { std::tuple{ "Nominal load (N)", &TireModel::NominalLoad, 10.0f }, std::tuple{ "Load sensitivity", &TireModel::LoadSensitivity, 0.005f },
			std::tuple{ "Pneumatic trail (m)", &TireModel::PneumaticTrail, 0.001f } })
		{
			VehicleSettingsData next = m_Model->data();
			changed |= ImGui::DragFloat(name, &((next.*tire).*member), speed, 0.0f, 100000.0f, "%.3f");
			after_widget(std::move(next));
		}
		ImGui::PopID();
		return changed;
	}

	void VehicleEditor::draw_tools()
	{
		if (!m_Model)
		{
			return;
		}
		ImGui::TextDisabled("B C D E are the magic formula's stiffness, shape, peak and curvature");
		if (!m_Model->error().empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Not applied: %s", m_Model->error().c_str());
		}

		if (ImGui::CollapsingHeader("Engine", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const std::vector<TorquePoint> curve = m_Model->data().TorqueCurve;
			for (size_t i = 0; i < curve.size(); i++)
			{
				ImGui::PushID(static_cast<int>(i));
				VehicleSettingsData next = m_Model->data();
				float point[2] = { curve[i].Rpm, curve[i].Torque };
				ImGui::DragFloat2("rpm, Nm", point, 5.0f, 0.0f, 30000.0f, "%.0f");
				next.TorqueCurve[i] = TorquePoint{ point[0], point[1] };
				after_widget(std::move(next));
				ImGui::SameLine();
				if (ImGui::SmallButton("x"))
				{
					m_Model->remove_torque_point(i);
					ImGui::PopID();
					break;
				}
				ImGui::PopID();
			}
			if (ImGui::SmallButton("Add torque point"))
			{
				m_Model->add_torque_point();
			}
			float_field("Min rpm", &VehicleSettingsData::MinRpm, 10.0f, 0.0f, 30000.0f, "%.0f");
			float_field("Max rpm", &VehicleSettingsData::MaxRpm, 10.0f, 0.0f, 30000.0f, "%.0f");
			float_field("Inertia (kg m2)", &VehicleSettingsData::EngineInertia, 0.01f, 0.01f, 10.0f);
			float_field("Damping", &VehicleSettingsData::EngineDamping, 0.01f, 0.0f, 10.0f);
		}
		if (ImGui::CollapsingHeader("Transmission"))
		{
			VehicleSettingsData automatic = m_Model->data();
			ImGui::Checkbox("Automatic", &automatic.AutomaticGearbox);
			after_widget(std::move(automatic));
			const std::vector<float> gears = m_Model->data().Gears;
			for (size_t i = 0; i < gears.size(); i++)
			{
				ImGui::PushID(static_cast<int>(i));
				VehicleSettingsData next = m_Model->data();
				const std::string label = "Gear " + std::to_string(i + 1);
				ImGui::DragFloat(label.c_str(), &next.Gears[i], 0.01f, 0.05f, 10.0f, "%.2f");
				after_widget(std::move(next));
				ImGui::SameLine();
				if (ImGui::SmallButton("x"))
				{
					m_Model->remove_gear(i);
					ImGui::PopID();
					break;
				}
				ImGui::PopID();
			}
			if (ImGui::SmallButton("Add gear"))
			{
				m_Model->add_gear();
			}
			if (!m_Model->data().ReverseGears.empty())
			{
				VehicleSettingsData next = m_Model->data();
				ImGui::DragFloat("Reverse", &next.ReverseGears[0], 0.01f, 0.05f, 10.0f, "%.2f");
				after_widget(std::move(next));
			}
			float_field("Shift up rpm", &VehicleSettingsData::ShiftUpRpm, 10.0f, 0.0f, 30000.0f, "%.0f");
			float_field("Shift down rpm", &VehicleSettingsData::ShiftDownRpm, 10.0f, 0.0f, 30000.0f, "%.0f");
			float_field("Shift time (s)", &VehicleSettingsData::ShiftTime, 0.01f, 0.0f, 3.0f);
			float_field("Clutch strength", &VehicleSettingsData::ClutchStrength, 0.1f, 0.0f, 100.0f);
		}
		if (ImGui::CollapsingHeader("Differentials"))
		{
			const size_t count = m_Model->data().Differentials.size();
			for (size_t i = 0; i < count; i++)
			{
				ImGui::PushID(static_cast<int>(i));
				VehicleSettingsData next = m_Model->data();
				VehicleDifferentialData& diff = next.Differentials[i];
				ImGui::SeparatorText(("Driven axle " + std::to_string(diff.Axle)).c_str());
				ImGui::DragInt("Axle", &diff.Axle, 0.1f, 0, 7);
				after_widget(next);
				next = m_Model->data();
				ImGui::DragFloat("Ratio", &next.Differentials[i].Ratio, 0.01f, 0.1f, 20.0f, "%.2f");
				after_widget(next);
				next = m_Model->data();
				ImGui::DragFloat("Limited slip", &next.Differentials[i].LimitedSlipRatio, 0.01f, 1.0f, 100.0f, "%.2f");
				after_widget(next);
				next = m_Model->data();
				ImGui::DragFloat("Torque share", &next.Differentials[i].EngineTorqueRatio, 0.01f, 0.0f, 1.0f, "%.2f");
				after_widget(std::move(next));
				ImGui::PopID();
			}
		}
		if (ImGui::CollapsingHeader("Anti-roll"))
		{
			const size_t count = m_Model->data().AntiRollStiffness.size();
			for (size_t i = 0; i < count; i++)
			{
				VehicleSettingsData next = m_Model->data();
				const std::string label = "Axle " + std::to_string(i) + " (N/m)";
				ImGui::DragFloat(label.c_str(), &next.AntiRollStiffness[i], 10.0f, 0.0f, 200000.0f, "%.0f");
				after_widget(std::move(next));
			}
		}
		if (ImGui::CollapsingHeader("Front tire (axle 0)"))
		{
			tire_fields("front", &VehicleSettingsData::FrontTire);
		}
		if (ImGui::CollapsingHeader("Rear tire (other axles)"))
		{
			tire_fields("rear", &VehicleSettingsData::RearTire);
		}
		if (ImGui::CollapsingHeader("Aero"))
		{
			float_field("Drag area (Cd A, m2)", &VehicleSettingsData::DragArea, 0.01f, 0.0f, 10.0f);
			float_field("Downforce area (Cl A, m2)", &VehicleSettingsData::DownforceArea, 0.01f, 0.0f, 20.0f);
			float_field("Downforce front share", &VehicleSettingsData::DownforceFront, 0.01f, 0.0f, 1.0f);
		}
		if (ImGui::CollapsingHeader("Brakes"))
		{
			float_field("Brake torque (Nm)", &VehicleSettingsData::MaxBrakeTorque, 10.0f, 0.0f, 20000.0f, "%.0f");
			float_field("Handbrake torque (Nm)", &VehicleSettingsData::HandbrakeTorque, 10.0f, 0.0f, 20000.0f, "%.0f");
			float_field("Front bias", &VehicleSettingsData::BrakeBiasFront, 0.01f, 0.0f, 1.0f);
		}
		if (ImGui::CollapsingHeader("Assists"))
		{
			float_field("Traction control", &VehicleSettingsData::TractionControl, 0.01f, 0.0f, 1.0f);
			float_field("ABS", &VehicleSettingsData::Abs, 0.01f, 0.0f, 1.0f);
			float_field("Stability", &VehicleSettingsData::Stability, 0.01f, 0.0f, 1.0f);
			float_field("Steering help", &VehicleSettingsData::SteeringHelp, 0.01f, 0.0f, 1.0f);
		}
		if (ImGui::CollapsingHeader("Steering"))
		{
			float_field("Steer speed (/s)", &VehicleSettingsData::SteerSpeed, 0.05f, 0.05f, 20.0f);
			const size_t count = m_Model->data().SteerLimit.size();
			for (size_t i = 0; i < count; i++)
			{
				ImGui::PushID(static_cast<int>(i));
				VehicleSettingsData next = m_Model->data();
				float point[2] = { next.SteerLimit[i].Speed, next.SteerLimit[i].Fraction };
				ImGui::DragFloat2("m/s, lock", point, 0.05f, 0.0f, 200.0f, "%.2f");
				next.SteerLimit[i] = SteerLimitPoint{ point[0], point[1] };
				after_widget(std::move(next));
				ImGui::PopID();
			}
		}
	}
}
