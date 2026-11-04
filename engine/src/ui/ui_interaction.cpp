#include <pch.h>
#include "ui_interaction.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace wiwa
{
	namespace
	{
		bool interactive_kind(WidgetKind kind)
		{
			return kind == WidgetKind::Button || kind == WidgetKind::Slider || kind == WidgetKind::Toggle;
		}

		bool horizontal(UiDirection direction)
		{
			return direction == UiDirection::LeftToRight || direction == UiDirection::RightToLeft;
		}

		float snap(const UiWidget& slider, float value)
		{
			if (slider.Step > 0.0f && std::isfinite(slider.Step))
			{
				value = slider.MinValue + std::round((value - slider.MinValue) / slider.Step) * slider.Step;
			}
			return std::clamp(value, slider.MinValue, slider.MaxValue);
		}

		// Everything one update works on: the panel by index, who can take part, and the events it makes.
		class Frame
		{
		public:
			Frame(UiPanelData& panel, const UiLayoutResult& layout, std::vector<UiEvent>& events) : m_Panel(panel), m_Layout(layout), m_Events(events)
			{
				const size_t count = panel.Widgets.size();
				for (size_t i = 0; i < count; i++)
				{
					m_IndexOf[panel.Widgets[i].Id] = i;
				}

				// enabled means the widget and all its parents are
				m_Enabled.assign(count, 0);
				for (size_t i = 0; i < count; i++)
				{
					bool enabled = true;
					UiId id = panel.Widgets[i].Id;
					for (size_t steps = 0; id != 0 && steps <= count; steps++)
					{
						const auto it = m_IndexOf.find(id);
						if (it == m_IndexOf.end())
						{
							break;
						}
						enabled = enabled && panel.Widgets[it->second].Enabled;
						id = panel.Widgets[it->second].Parent;
					}
					m_Enabled[i] = enabled ? 1 : 0;
				}

				for (const UiDrawItem& item : ui_draw_order(panel, layout, layout.CanvasSize))
				{
					if (usable(item.Index))
					{
						m_Items.push_back(item);
					}
				}
			}

			const std::vector<UiDrawItem>& items() const { return m_Items; }

			std::optional<size_t> find(UiId id) const
			{
				const auto it = m_IndexOf.find(id);
				return it == m_IndexOf.end() ? std::nullopt : std::optional<size_t>(it->second);
			}

			// A widget that can be pointed at or focused: interactive, drawn, and enabled.
			bool usable(size_t i) const
			{
				return interactive_kind(m_Panel.Widgets[i].Kind) && m_Layout.Visible[i] && m_Layout.Opacity[i] > 0.0f && m_Enabled[i];
			}

			bool usable_id(UiId id) const
			{
				const std::optional<size_t> i = find(id);
				return i && usable(*i);
			}

			UiId hit_test(const glm::vec2& pointer) const
			{
				for (auto it = m_Items.rbegin(); it != m_Items.rend(); ++it)
				{
					const UiRect& rect = m_Layout.Rects[it->Index];
					const bool inClip = pointer.x >= it->Clip.x && pointer.y >= it->Clip.y && pointer.x < it->Clip.z && pointer.y < it->Clip.w;
					if (inClip && rect.contains(pointer))
					{
						return m_Panel.Widgets[it->Index].Id;
					}
				}
				return 0;
			}

			void event(UiEventKind kind, UiId widget, float value = 0.0f)
			{
				m_Events.push_back(UiEvent{ kind, widget, value });
			}

			void set_value(size_t i, float value)
			{
				UiWidget& widget = m_Panel.Widgets[i];
				if (value != widget.Value)
				{
					widget.Value = value;
					event(UiEventKind::ValueChanged, widget.Id, value);
				}
			}

			// The slider's value where the pointer is on its track.
			void drag(size_t i, const glm::vec2& pointer)
			{
				const UiWidget& slider = m_Panel.Widgets[i];
				const float range = slider.MaxValue - slider.MinValue;
				const UiRect& rect = m_Layout.Rects[i];
				const glm::vec2 size = rect.size();
				if (!(std::isfinite(range) && range > 0.0f) || !(size.x > 0.0f) || !(size.y > 0.0f) || !std::isfinite(pointer.x) || !std::isfinite(pointer.y))
				{
					return;
				}

				float t = 0.0f;
				switch (slider.Direction)
				{
				case UiDirection::LeftToRight: t = (pointer.x - rect.Min.x) / size.x; break;
				case UiDirection::RightToLeft: t = 1.0f - (pointer.x - rect.Min.x) / size.x; break;
				case UiDirection::BottomToTop: t = 1.0f - (pointer.y - rect.Min.y) / size.y; break;
				case UiDirection::TopToBottom: t = (pointer.y - rect.Min.y) / size.y; break;
				}
				set_value(i, snap(slider, slider.MinValue + std::clamp(t, 0.0f, 1.0f) * range));
			}

			// A click on a button or toggle (a toggle flips first).
			void activate(size_t i)
			{
				const UiWidget& widget = m_Panel.Widgets[i];
				if (widget.Kind == WidgetKind::Toggle)
				{
					set_value(i, widget.Value > 0.5f ? 0.0f : 1.0f);
				}
				if (widget.Kind == WidgetKind::Button || widget.Kind == WidgetKind::Toggle)
				{
					event(UiEventKind::Click, widget.Id);
				}
			}

			const UiPanelData& panel() const { return m_Panel; }
			const UiLayoutResult& layout() const { return m_Layout; }

		private:
			UiPanelData& m_Panel;
			const UiLayoutResult& m_Layout;
			std::vector<UiEvent>& m_Events;
			std::unordered_map<UiId, size_t> m_IndexOf{};
			std::vector<u8> m_Enabled{};
			std::vector<UiDrawItem> m_Items{};
		};
	}

	void UiInteraction::reset()
	{
		m_Hovered = 0;
		m_Focused = 0;
		m_Captured = 0;
	}

	void UiInteraction::update(UiPanelData& panel, const UiLayoutResult& layout, std::vector<UiInteractionState>& states, const UiInputFrame& input, std::vector<UiEvent>& events)
	{
		const size_t count = panel.Widgets.size();
		states.assign(count, UiInteractionState::Normal);
		if (layout.Rects.size() != count || layout.Visible.size() != count || layout.Opacity.size() != count)
		{
			reset();
			return;
		}

		Frame frame(panel, layout, events);

		// what was held on to may be gone, hidden or disabled by now
		if (m_Captured != 0 && !frame.usable_id(m_Captured))
		{
			m_Captured = 0;
		}
		if (m_Focused != 0 && !frame.usable_id(m_Focused))
		{
			m_Focused = 0;
		}

		const auto focus = [&](UiId id)
		{
			if (id != m_Focused)
			{
				m_Focused = id;
				if (id != 0)
				{
					frame.event(UiEventKind::Focus, id);
				}
			}
		};

		// the pointer
		const UiId hit = input.PointerValid ? frame.hit_test(input.Pointer) : 0;
		const UiId hover = m_Captured != 0 ? (hit == m_Captured ? m_Captured : 0) : hit;
		if (hover != m_Hovered)
		{
			if (m_Hovered != 0 && frame.find(m_Hovered))
			{
				frame.event(UiEventKind::HoverExit, m_Hovered);
			}
			m_Hovered = hover;
			if (hover != 0)
			{
				frame.event(UiEventKind::HoverEnter, hover);
			}
		}

		if (input.Pressed[0] && hit != 0 && m_Captured == 0)
		{
			m_Captured = hit;
			focus(hit);
		}

		if (m_Captured != 0 && input.PointerValid && (input.Down[0] || input.Released[0]))
		{
			const size_t i = *frame.find(m_Captured);
			if (panel.Widgets[i].Kind == WidgetKind::Slider)
			{
				frame.drag(i, input.Pointer);
			}
		}

		if (m_Captured != 0)
		{
			if (input.Released[0])
			{
				const size_t i = *frame.find(m_Captured);
				if (hit == m_Captured)
				{
					frame.activate(i);
				}
				m_Captured = 0;
			}
			else if (!input.Down[0] && !input.Pressed[0])
			{
				m_Captured = 0; // the release was missed
			}
		}

		// the keyboard or gamepad
		const std::vector<UiDrawItem>& focusable = frame.items();
		for (const UiNav nav : { UiNav::Up, UiNav::Down, UiNav::Left, UiNav::Right, UiNav::Submit, UiNav::Cancel })
		{
			if ((input.NavPressed & ui_nav_bit(nav)) == 0)
			{
				continue;
			}

			if (nav == UiNav::Cancel)
			{
				m_Focused = 0;
				continue;
			}

			if (m_Focused == 0)
			{
				if (nav != UiNav::Submit && !focusable.empty())
				{
					focus(panel.Widgets[focusable.front().Index].Id);
				}
				continue;
			}

			const size_t current = *frame.find(m_Focused);
			if (nav == UiNav::Submit)
			{
				frame.activate(current);
				continue;
			}

			const glm::vec2 direction = nav == UiNav::Up ? glm::vec2(0, -1) : nav == UiNav::Down ? glm::vec2(0, 1) : nav == UiNav::Left ? glm::vec2(-1, 0) : glm::vec2(1, 0);

			UiWidget& focused = panel.Widgets[current];
			if (focused.Kind == WidgetKind::Slider)
			{
				const bool onAxis = horizontal(focused.Direction) ? (nav == UiNav::Left || nav == UiNav::Right) : (nav == UiNav::Up || nav == UiNav::Down);
				const float range = focused.MaxValue - focused.MinValue;
				if (onAxis && std::isfinite(range) && range > 0.0f)
				{
					const float stepSize = focused.Step > 0.0f && std::isfinite(focused.Step) ? focused.Step : range * 0.1f;
					// Right and Up raise a slider that grows that way; the reversed directions lower it
					float sign = (nav == UiNav::Right || nav == UiNav::Up) ? 1.0f : -1.0f;
					if (focused.Direction == UiDirection::RightToLeft || focused.Direction == UiDirection::TopToBottom)
					{
						sign = -sign;
					}
					frame.set_value(current, snap(focused, focused.Value + sign * stepSize));
					continue;
				}
			}

			// the nearest widget in that direction: straight ahead counts more than sideways
			const glm::vec2 from = (layout.Rects[current].Min + layout.Rects[current].Max) * 0.5f;
			float best = std::numeric_limits<float>::max();
			UiId target = 0;
			for (const UiDrawItem& candidate : focusable)
			{
				if (candidate.Index == current)
				{
					continue;
				}
				const glm::vec2 to = (layout.Rects[candidate.Index].Min + layout.Rects[candidate.Index].Max) * 0.5f - from;
				const float along = glm::dot(to, direction);
				const float across = std::abs(to.x * direction.y - to.y * direction.x);
				if (along > 1e-3f && along + 2.0f * across < best)
				{
					best = along + 2.0f * across;
					target = panel.Widgets[candidate.Index].Id;
				}
			}
			if (target != 0)
			{
				focus(target);
			}
		}

		// what each widget looks like
		for (size_t i = 0; i < count; i++)
		{
			const UiId id = panel.Widgets[i].Id;
			if (!frame.usable(i))
			{
				continue;
			}

			if (id == m_Captured && (panel.Widgets[i].Kind == WidgetKind::Slider || hit == id))
			{
				states[i] = UiInteractionState::Pressed;
			}
			else if (id == m_Hovered && (m_Captured == 0 || m_Captured == id))
			{
				states[i] = UiInteractionState::Hover;
			}
			else if (id == m_Focused)
			{
				states[i] = UiInteractionState::Focused;
			}
		}
	}
}
