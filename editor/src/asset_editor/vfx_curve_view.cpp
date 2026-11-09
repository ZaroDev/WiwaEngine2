#include <editor_common.h>
#include "vfx_curve_view.h"

#include <algorithm>
#include <cmath>

#include <ImCurveEdit.h>

namespace wiwa
{
	void normalize_particle_keys(std::vector<ParticleCurveKey>& keys)
	{
		for (ParticleCurveKey& key : keys)
		{
			key.Time = std::isfinite(key.Time) ? std::clamp(key.Time, 0.0f, 1.0f) : 0.0f;
			key.Value = std::isfinite(key.Value) ? key.Value : 0.0f;
		}
		std::stable_sort(keys.begin(), keys.end(), [](const ParticleCurveKey& a, const ParticleCurveKey& b) { return a.Time < b.Time; });
		// the file format wants times that rise: two keys at one time are moved a hair apart
		for (size_t i = 1; i < keys.size(); i++)
		{
			if (keys[i].Time <= keys[i - 1].Time)
			{
				keys[i].Time = std::min(keys[i - 1].Time + 0.001f, 1.0f);
			}
		}
	}

	struct VfxCurveView::Impl : public ImCurveEdit::Delegate
	{
		std::vector<ImVec2> Points{};
		ImVec2 Min{ 0.0f, 0.0f };
		ImVec2 Max{ 1.0f, 1.0f };
		bool Changed = false;
		bool Holding = false;

		void sort_points()
		{
			std::stable_sort(Points.begin(), Points.end(), [](const ImVec2& a, const ImVec2& b) { return a.x < b.x; });
		}

		size_t GetCurveCount() override { return 1; }
		ImCurveEdit::CurveType GetCurveType(size_t) const override { return ImCurveEdit::CurveLinear; }
		ImVec2& GetMin() override { return Min; }
		ImVec2& GetMax() override { return Max; }
		size_t GetPointCount(size_t) override { return Points.size(); }
		uint32_t GetCurveColor(size_t) override { return IM_COL32(120, 200, 255, 255); }
		ImVec2* GetPoints(size_t) override { return Points.data(); }

		int EditPoint(size_t, int point, ImVec2 value) override
		{
			if (point < 0 || static_cast<size_t>(point) >= Points.size())
			{
				return point;
			}
			value.x = std::isfinite(value.x) ? std::clamp(value.x, 0.0f, 1.0f) : 0.0f;
			value.y = std::isfinite(value.y) ? value.y : 0.0f;
			Points[static_cast<size_t>(point)] = value;
			// the point keeps its identity through the sort: find it again by the value just set
			const ImVec2 moved = value;
			sort_points();
			Changed = true;
			for (size_t i = 0; i < Points.size(); i++)
			{
				if (Points[i].x == moved.x && Points[i].y == moved.y)
				{
					return static_cast<int>(i);
				}
			}
			return point;
		}

		void AddPoint(size_t, ImVec2 value) override
		{
			if (Points.size() >= 64)
			{
				return;
			}
			value.x = std::isfinite(value.x) ? std::clamp(value.x, 0.0f, 1.0f) : 0.0f;
			value.y = std::isfinite(value.y) ? value.y : 0.0f;
			Points.push_back(value);
			sort_points();
			Changed = true;
		}

		void BeginEdit(int) override { Holding = true; }
		void EndEdit() override { Holding = false; }
	};

	VfxCurveView::VfxCurveView() : m_Impl(std::make_unique<Impl>()) {}
	VfxCurveView::~VfxCurveView() = default;

	bool VfxCurveView::draw(ParticleValue& value, ImVec2 size, bool& dragging)
	{
		Impl& impl = *m_Impl;
		dragging = false;
		if (value.Kind != ParticleValue::Mode::Curve)
		{
			return false;
		}

		// while a key is held the graph owns the points; otherwise they follow the document (undo, the key list)
		if (!impl.Holding)
		{
			impl.Points.clear();
			for (const ParticleCurveKey& key : value.Keys)
			{
				impl.Points.push_back(ImVec2(key.Time, key.Value));
			}
		}
		if (m_Fit)
		{
			float low = 0.0f;
			float high = 1.0f;
			for (const ImVec2& point : impl.Points)
			{
				low = std::min(low, point.y);
				high = std::max(high, point.y);
			}
			const float pad = std::max((high - low) * 0.1f, 0.05f);
			impl.Min = ImVec2(0.0f, low - (low < 0.0f ? pad : 0.0f));
			impl.Max = ImVec2(1.0f, high + pad);
			m_Fit = false;
		}

		impl.Changed = false;
		ImCurveEdit::Edit(impl, size, static_cast<unsigned int>(ImGui::GetID("##particlecurve")));
		dragging = impl.Holding;
		if (!impl.Changed)
		{
			return false;
		}
		value.Keys.clear();
		for (const ImVec2& point : impl.Points)
		{
			value.Keys.push_back(ParticleCurveKey{ point.x, point.y });
		}
		normalize_particle_keys(value.Keys);
		value.bake();
		return true;
	}
}
