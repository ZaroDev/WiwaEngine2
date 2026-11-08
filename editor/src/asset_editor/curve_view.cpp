#include <editor_common.h>
#include "curve_view.h"

#include <algorithm>
#include <cmath>

#include <ImCurveEdit.h>
#include <imgui_internal.h>

#include "clip_math.h"

namespace wiwa
{
	namespace
	{
		struct Curve
		{
			size_t Channel = 0;
			int Component = 0;
			AnimationPath Path = AnimationPath::Translation;
			AnimationInterpolation Interpolation = AnimationInterpolation::Linear;
			ImU32 Color = 0;
			std::vector<ImVec2> Points{};
		};

		ImU32 curve_color(AnimationPath path, int component)
		{
			static const ImU32 translation[3] = { IM_COL32(240, 90, 80, 255), IM_COL32(110, 220, 110, 255), IM_COL32(90, 150, 250, 255) };
			static const ImU32 rotation[3] = { IM_COL32(255, 150, 140, 255), IM_COL32(170, 240, 150, 255), IM_COL32(150, 190, 255, 255) };
			static const ImU32 scale[3] = { IM_COL32(170, 70, 70, 255), IM_COL32(70, 150, 70, 255), IM_COL32(70, 100, 170, 255) };
			return path == AnimationPath::Translation ? translation[component] : (path == AnimationPath::Rotation ? rotation[component] : scale[component]);
		}
	}

	struct CurveView::Impl : public ImCurveEdit::Delegate
	{
		ClipEditModel* Model = nullptr;
		float Fps = 30.0f;
		std::vector<Curve> Curves{};
		ImVec2 Min{ 0.0f, -1.0f };
		ImVec2 Max{ 1.0f, 1.0f };
		ImVec2 Range{};

		void rebuild(const std::string& joint)
		{
			Curves.clear();
			const AnimationClipData& clip = Model->data();
			for (const AnimationPath path : { AnimationPath::Translation, AnimationPath::Rotation, AnimationPath::Scale })
			{
				const std::optional<size_t> found = Model->find_channel(joint, path);
				if (!found)
				{
					continue;
				}
				const AnimationChannel& channel = clip.Channels[*found];
				if (!channel_valid(channel))
				{
					continue;
				}
				const bool rotation = path == AnimationPath::Rotation && channel.Components == 4;
				const std::vector<glm::vec3> euler = rotation ? Model->euler_values(*found) : std::vector<glm::vec3>{};
				for (int component = 0; component < 3; component++)
				{
					Curve curve;
					curve.Channel = *found;
					curve.Component = component;
					curve.Path = path;
					curve.Interpolation = channel.Interpolation;
					curve.Color = curve_color(path, component);
					for (size_t k = 0; k < channel.Times.size(); k++)
					{
						const float value = rotation ? euler[k][component] : key_value(channel, k)[component];
						curve.Points.push_back(ImVec2(channel.Times[k] * Fps, value));
					}
					Curves.push_back(std::move(curve));
				}
			}
		}

		// The value range that holds every point, with some room.
		void fit_values()
		{
			float low = 1e30f;
			float high = -1e30f;
			for (const Curve& curve : Curves)
			{
				for (const ImVec2& point : curve.Points)
				{
					low = std::min(low, point.y);
					high = std::max(high, point.y);
				}
			}
			if (!(low <= high))
			{
				low = -1.0f;
				high = 1.0f;
			}
			const float pad = std::max((high - low) * 0.1f, 0.05f);
			Min.y = low - pad;
			Max.y = high + pad;
		}

		size_t GetCurveCount() override { return Curves.size(); }
		ImCurveEdit::CurveType GetCurveType(size_t index) const override
		{
			switch (Curves[index].Interpolation)
			{
			case AnimationInterpolation::Step: return ImCurveEdit::CurveDiscrete;
			case AnimationInterpolation::CubicSpline: return ImCurveEdit::CurveSmooth;
			default: return ImCurveEdit::CurveLinear;
			}
		}
		ImVec2& GetMin() override { return Min; }
		ImVec2& GetMax() override { return Max; }
		size_t GetPointCount(size_t index) override { return Curves[index].Points.size(); }
		uint32_t GetCurveColor(size_t index) override { return Curves[index].Color; }
		ImVec2* GetPoints(size_t index) override { return Curves[index].Points.data(); }

		int EditPoint(size_t index, int point, ImVec2 value) override
		{
			const Curve curve = Curves[index];
			const AnimationClipData& clip = Model->data();
			size_t key = static_cast<size_t>(point);
			if (curve.Channel >= clip.Channels.size() || key >= clip.Channels[curve.Channel].Times.size())
			{
				return point;
			}
			const float newTime = std::clamp(value.x / Fps, 0.0f, clip.Duration);
			const float oldTime = clip.Channels[curve.Channel].Times[key];
			if (newTime != oldTime)
			{
				KeySelection moved;
				if (Model->move_keys({ KeyRef{ curve.Channel, key } }, newTime - oldTime, &moved) && !moved.empty())
				{
					key = moved.front().Key;
				}
			}

			const AnimationChannel& channel = Model->data().Channels[curve.Channel];
			if (key < channel.Times.size())
			{
				float components[4] = {};
				std::span<const float> span(components, channel.Components);
				if (curve.Path == AnimationPath::Rotation && channel.Components == 4)
				{
					const std::vector<glm::vec3> euler = Model->euler_values(curve.Channel);
					glm::vec3 e = euler[key];
					e[curve.Component] = value.y;
					components[0] = e.x;
					components[1] = e.y;
					components[2] = e.z;
					span = std::span<const float>(components, 3);
				}
				else
				{
					const glm::vec4 current = key_value(channel, key);
					for (u32 c = 0; c < channel.Components && c < 4; c++)
					{
						components[c] = current[static_cast<int>(c)];
					}
					components[curve.Component] = value.y;
				}
				Model->set_key_value(KeyRef{ curve.Channel, key }, span);
			}
			Dirty = true;
			return static_cast<int>(key);
		}

		void AddPoint(size_t index, ImVec2 value) override
		{
			const Curve curve = Curves[index];
			const float time = std::clamp(value.x / Fps, 0.0f, Model->data().Duration);
			if (const std::optional<KeyRef> key = Model->insert_key(curve.Channel, time))
			{
				const AnimationChannel& channel = Model->data().Channels[key->Channel];
				float components[4] = {};
				std::span<const float> span(components, channel.Components);
				if (curve.Path == AnimationPath::Rotation && channel.Components == 4)
				{
					glm::vec3 e = Model->euler_values(key->Channel)[key->Key];
					e[curve.Component] = value.y;
					components[0] = e.x;
					components[1] = e.y;
					components[2] = e.z;
					span = std::span<const float>(components, 3);
				}
				else
				{
					const glm::vec4 current = key_value(channel, key->Key);
					for (u32 c = 0; c < channel.Components && c < 4; c++)
					{
						components[c] = current[static_cast<int>(c)];
					}
					components[curve.Component] = value.y;
				}
				Model->set_key_value(*key, span);
			}
			Dirty = true;
		}

		void BeginEdit(int) override { Model->begin_gesture(); }
		void EndEdit() override { Model->end_gesture(); }

		bool Dirty = false;
	};

	CurveView::CurveView() : m_Impl(std::make_unique<Impl>()) {}
	CurveView::~CurveView() = default;

	void CurveView::draw(ClipEditModel& model, const std::string& joint, KeySelection& selection, float fps, ImVec2 size)
	{
		Impl& impl = *m_Impl;
		impl.Model = &model;
		impl.Fps = std::max(fps, 1.0f);
		if (joint != m_Joint)
		{
			m_Joint = joint;
			m_Fit = true;
		}
		impl.rebuild(joint);
		impl.Min.x = 0.0f;
		impl.Max.x = std::max(model.data().Duration * impl.Fps, 1.0f);
		if (m_Fit)
		{
			impl.fit_values();
			m_Fit = false;
		}
		if (impl.Curves.empty())
		{
			ImGui::TextDisabled(joint.empty() ? "Select a joint in the dope sheet to see its curves" : "This joint has no translation, rotation or scale keys");
			return;
		}

		ImVector<ImCurveEdit::EditPoint> selected;
		ImCurveEdit::Edit(impl, size, static_cast<unsigned int>(ImGui::GetID("##curves")), nullptr, &selected);
		if (selected.Size > 0)
		{
			KeySelection fromCurves;
			for (const ImCurveEdit::EditPoint& point : selected)
			{
				if (point.curveIndex >= 0 && static_cast<size_t>(point.curveIndex) < impl.Curves.size())
				{
					fromCurves.push_back(KeyRef{ impl.Curves[static_cast<size_t>(point.curveIndex)].Channel, static_cast<size_t>(point.pointIndex) });
				}
			}
			normalize_selection(fromCurves);
			if (!(fromCurves == selection) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			{
				selection = fromCurves;
			}
		}
	}
}
