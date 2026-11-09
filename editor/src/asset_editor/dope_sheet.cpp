#include <editor_common.h>
#include "dope_sheet.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "clip_math.h"

namespace wiwa
{
	namespace
	{
		constexpr float ROW_HEIGHT = 20.0f;
		constexpr float RULER_HEIGHT = 24.0f;
		constexpr float LABEL_WIDTH = 170.0f;
		constexpr float KEY_RADIUS = 5.0f;
		constexpr float PICK_RADIUS = 7.0f;

		const ImU32 DS_BACKGROUND = IM_COL32(28, 28, 33, 255);
		const ImU32 DS_ROW_ALT = IM_COL32(34, 34, 40, 255);
		const ImU32 DS_ROW_SELECTED = IM_COL32(52, 60, 84, 255);
		const ImU32 DS_GRID = IM_COL32(60, 60, 70, 255);
		const ImU32 DS_TEXT = IM_COL32(205, 205, 215, 255);
		const ImU32 DS_DIM = IM_COL32(130, 130, 145, 255);
		const ImU32 DS_PLAYHEAD = IM_COL32(110, 190, 255, 255);
		const ImU32 DS_KEY_LINEAR = IM_COL32(240, 200, 90, 255);
		const ImU32 DS_KEY_STEP = IM_COL32(230, 120, 90, 255);
		const ImU32 DS_KEY_CUBIC = IM_COL32(120, 210, 150, 255);
		const ImU32 DS_KEY_SELECTED = IM_COL32(255, 255, 255, 255);
		const ImU32 DS_EVENT = IM_COL32(255, 160, 60, 255);

		struct Row
		{
			enum class Kind { Events, Joint, Channel } Type = Kind::Joint;
			std::string Joint{};
			std::vector<size_t> Channels{}; // a joint row: all its channels; a channel row: that one
			bool Open = false;
		};

		const char* path_name(AnimationPath path)
		{
			switch (path)
			{
			case AnimationPath::Translation: return "Translation";
			case AnimationPath::Rotation: return "Rotation";
			case AnimationPath::Scale: return "Scale";
			default: return "Weights";
			}
		}

		ImU32 key_color(const AnimationChannel& channel)
		{
			switch (channel.Interpolation)
			{
			case AnimationInterpolation::Step: return DS_KEY_STEP;
			case AnimationInterpolation::CubicSpline: return DS_KEY_CUBIC;
			default: return DS_KEY_LINEAR;
			}
		}

		void diamond(ImDrawList* list, ImVec2 at, float radius, ImU32 fill, ImU32 outline)
		{
			list->AddQuadFilled(ImVec2(at.x, at.y - radius), ImVec2(at.x + radius, at.y), ImVec2(at.x, at.y + radius), ImVec2(at.x - radius, at.y), fill);
			list->AddQuad(ImVec2(at.x, at.y - radius), ImVec2(at.x + radius, at.y), ImVec2(at.x, at.y + radius), ImVec2(at.x - radius, at.y), outline, 1.0f);
		}

		bool is_selected(const KeySelection& selection, KeyRef key)
		{
			return std::binary_search(selection.begin(), selection.end(), key);
		}

		// A round time step for the ruler that is at least `minPixels` wide.
		float nice_step(float pixelsPerSecond, float minPixels)
		{
			static const float steps[] = { 0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 30.0f, 60.0f };
			for (const float step : steps)
			{
				if (step * pixelsPerSecond >= minPixels)
				{
					return step;
				}
			}
			return 60.0f;
		}
	}

	float DopeSheet::snapped(float time, const DopeSheetOptions& options) const
	{
		if (!options.Snap || options.SnapFps <= 0.0f)
		{
			return time;
		}
		return std::round(time * options.SnapFps) / options.SnapFps;
	}

	bool DopeSheet::draw(ClipEditModel& model, float& playhead, KeySelection& selection, const DopeSheetOptions& options, ImVec2 size)
	{
		bool playheadMoved = false;
		const AnimationClipData& clip = model.data();
		const float duration = std::max(clip.Duration, 0.001f);

		// the rows: the event lane, then a row per joint (in the order the clip names them), with a row per channel under an open joint
		std::vector<Row> rows;
		rows.push_back(Row{ Row::Kind::Events, {}, {}, false });
		{
			std::vector<std::string> order;
			std::map<std::string, std::vector<size_t>> perJoint;
			for (size_t c = 0; c < clip.Channels.size(); c++)
			{
				if (clip.Channels[c].Path == AnimationPath::Weights)
				{
					continue;
				}
				if (!perJoint.contains(clip.Channels[c].Joint))
				{
					order.push_back(clip.Channels[c].Joint);
				}
				perJoint[clip.Channels[c].Joint].push_back(c);
			}
			for (const std::string& joint : order)
			{
				const bool open = m_Opened.contains(joint);
				rows.push_back(Row{ Row::Kind::Joint, joint, perJoint[joint], open });
				if (open)
				{
					for (const size_t c : perJoint[joint])
					{
						rows.push_back(Row{ Row::Kind::Channel, joint, { c }, false });
					}
				}
			}
		}

		ImDrawList* list = ImGui::GetWindowDrawList();
		const ImGuiIO& io = ImGui::GetIO();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		size.x = std::max(size.x, LABEL_WIDTH + 80.0f);
		size.y = std::max(size.y, RULER_HEIGHT + ROW_HEIGHT * 3.0f);
		const float areaWidth = size.x - LABEL_WIDTH;
		m_TimeOrigin = origin.x + LABEL_WIDTH;
		if (m_Fit)
		{
			m_Start = 0.0f;
			m_PixelsPerSecond = std::max(areaWidth - 20.0f, 50.0f) / duration;
			m_Fit = false;
		}

		// ---- the ruler (above the scrolling rows)
		const ImVec2 rulerMin(m_TimeOrigin, origin.y);
		const ImVec2 rulerMax(origin.x + size.x, origin.y + RULER_HEIGHT);
		ImGui::SetCursorScreenPos(rulerMin);
		ImGui::InvisibleButton("##ruler", ImVec2(areaWidth, RULER_HEIGHT));
		const bool rulerHovered = ImGui::IsItemHovered();
		if (ImGui::IsItemActivated())
		{
			m_ScrubbingRuler = true;
		}
		if (m_ScrubbingRuler)
		{
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				playhead = std::clamp(snapped(x_to_time(io.MousePos.x), options), 0.0f, clip.Duration);
				playheadMoved = true;
			}
			else
			{
				m_ScrubbingRuler = false;
			}
		}
		list->AddRectFilled(rulerMin, rulerMax, DS_BACKGROUND);
		{
			const float step = nice_step(m_PixelsPerSecond, 70.0f);
			const float first = std::floor(m_Start / step) * step;
			const float last = x_to_time(rulerMax.x);
			for (float t = first; t <= last + step; t += step)
			{
				const float x = time_to_x(t);
				if (x < rulerMin.x - 1.0f || x > rulerMax.x + 1.0f)
				{
					continue;
				}
				list->AddLine(ImVec2(x, rulerMax.y - 8.0f), ImVec2(x, rulerMax.y), DS_DIM);
				char label[32];
				std::snprintf(label, sizeof(label), "%.2f", t);
				list->AddText(ImVec2(x + 3.0f, rulerMin.y + 2.0f), DS_DIM, label);
			}
		}

		// ---- the rows (a child window that scrolls; the time axis is the same as the ruler's)
		ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + RULER_HEIGHT));
		ImGui::BeginChild("##dope_rows", ImVec2(size.x, size.y - RULER_HEIGHT), ImGuiChildFlags_None, ImGuiWindowFlags_NoMove);
		ImDrawList* rowList = ImGui::GetWindowDrawList();
		const ImVec2 rowsOrigin = ImGui::GetCursorScreenPos();
		const float rowsHeight = ROW_HEIGHT * static_cast<float>(rows.size());
		ImGui::InvisibleButton("##dope_area", ImVec2(size.x - ImGui::GetStyle().ScrollbarSize, rowsHeight));
		const bool areaHovered = ImGui::IsItemHovered();
		const bool areaActive = ImGui::IsItemActive();
		const ImVec2 mouse = io.MousePos;
		const float scrollY = ImGui::GetScrollY();
		const float visibleHeight = ImGui::GetWindowHeight();
		const size_t firstRow = static_cast<size_t>(std::max(scrollY / ROW_HEIGHT, 0.0f));
		const size_t lastRow = std::min(rows.size(), static_cast<size_t>((scrollY + visibleHeight) / ROW_HEIGHT) + 2);

		// the row under the mouse
		int hoverRow = -1;
		if (mouse.y >= rowsOrigin.y && mouse.y < rowsOrigin.y + rowsHeight)
		{
			hoverRow = static_cast<int>((mouse.y - rowsOrigin.y) / ROW_HEIGHT);
		}

		// the keys of a row at (about) a time: what a click on a diamond picks
		const auto keys_at = [&](const Row& row, float time, float tolerance) -> KeySelection
		{
			KeySelection found;
			for (const size_t c : row.Channels)
			{
				const AnimationChannel& channel = clip.Channels[c];
				for (size_t k = 0; k < channel.Times.size(); k++)
				{
					if (std::abs(channel.Times[k] - time) <= tolerance)
					{
						found.push_back(KeyRef{ c, k });
					}
				}
			}
			return found;
		};

		// ---- mouse
		const bool altHeld = io.KeyAlt;
		const float tolerance = PICK_RADIUS / m_PixelsPerSecond;
		if (areaHovered && io.KeyCtrl && io.MouseWheel != 0.0f)
		{
			// zoom about the mouse
			const float before = x_to_time(mouse.x);
			m_PixelsPerSecond = std::clamp(m_PixelsPerSecond * (1.0f + io.MouseWheel * 0.15f), 5.0f, 20000.0f);
			m_Start = before - (mouse.x - m_TimeOrigin) / m_PixelsPerSecond;
		}
		if ((areaHovered || rulerHovered) && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
		{
			m_PanDrag = true;
		}
		if (m_PanDrag)
		{
			if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
			{
				m_Start -= io.MouseDelta.x / m_PixelsPerSecond;
			}
			else
			{
				m_PanDrag = false;
			}
		}

		if (areaHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_Drag == Drag::None && hoverRow >= 0 && mouse.x >= m_TimeOrigin)
		{
			const Row& row = rows[static_cast<size_t>(hoverRow)];
			const float time = x_to_time(mouse.x);
			KeySelection hit = row.Type == Row::Kind::Events ? KeySelection{} : keys_at(row, time, tolerance);
			if (row.Type == Row::Kind::Events)
			{
				int eventHit = -1;
				for (size_t e = 0; e < clip.Events.size(); e++)
				{
					if (std::abs(clip.Events[e].Time - time) <= tolerance)
					{
						eventHit = static_cast<int>(e);
						break;
					}
				}
				if (eventHit >= 0)
				{
					m_SelectedEvent = eventHit;
					selection.clear();
					m_Drag = Drag::Event;
					m_DragStart = mouse;
					m_GrabTime = clip.Events[static_cast<size_t>(eventHit)].Time;
					m_AppliedShift = 0.0f;
				}
				else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					const std::optional<size_t> added = model.add_event(snapped(std::clamp(time, 0.0f, clip.Duration), options), "event");
					if (added)
					{
						m_SelectedEvent = static_cast<int>(*added);
						m_RenameRequested = true;
					}
				}
			}
			else if (!hit.empty())
			{
				// a key: select it (add with Shift/Ctrl), then drag moves the selection
				const bool add = io.KeyShift || io.KeyCtrl;
				if (!add && !is_selected(selection, hit.front()))
				{
					selection = hit;
				}
				else if (add)
				{
					selection.insert(selection.end(), hit.begin(), hit.end());
					normalize_selection(selection);
				}
				m_SelectedJoint = row.Joint;
				m_Drag = altHeld ? Drag::Scale : Drag::Keys;
				m_DragStart = mouse;
				m_GrabTime = clip.Channels[hit.front().Channel].Times[hit.front().Key];
				m_AppliedShift = 0.0f;
				m_ScaleReference = 1.0f;
				m_SelectedEvent = -1;
			}
			else if (!(row.Type == Row::Kind::Events && (m_Drag == Drag::Event || m_RenameRequested)))
			{
				if (!(io.KeyShift || io.KeyCtrl))
				{
					selection.clear();
				}
				m_SelectedEvent = -1;
				m_Drag = Drag::Box;
				m_DragStart = mouse;
				if (row.Type != Row::Kind::Events)
				{
					m_SelectedJoint = row.Joint;
				}
			}
		}

		if (m_Drag == Drag::Keys || m_Drag == Drag::Scale)
		{
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				if (std::abs(mouse.x - m_DragStart.x) > 2.0f || m_AppliedShift != 0.0f || m_ScaleReference != 1.0f)
				{
					model.begin_gesture();
					KeySelection moved;
					if (m_Drag == Drag::Keys)
					{
						const float target = snapped(m_GrabTime + (mouse.x - m_DragStart.x) / m_PixelsPerSecond, options);
						const float wanted = target - m_GrabTime;
						const float step = wanted - m_AppliedShift;
						if (step != 0.0f && model.move_keys(selection, step, &moved))
						{
							m_AppliedShift = wanted;
							if (!moved.empty())
							{
								selection = moved;
							}
						}
					}
					else
					{
						const float playheadX = time_to_x(playhead);
						const float startDistance = m_DragStart.x - playheadX;
						if (std::abs(startDistance) >= 4.0f)
						{
							const float ratio = std::max((mouse.x - playheadX) / startDistance, 0.05f);
							if (ratio != m_ScaleReference && model.scale_keys(selection, playhead, ratio / m_ScaleReference, &moved))
							{
								m_ScaleReference = ratio;
								if (!moved.empty())
								{
									selection = moved;
								}
							}
						}
					}
				}
			}
			else
			{
				model.end_gesture();
				m_Drag = Drag::None;
			}
		}
		else if (m_Drag == Drag::Event)
		{
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				if (m_SelectedEvent >= 0 && std::abs(mouse.x - m_DragStart.x) > 2.0f)
				{
					model.begin_gesture();
					const float target = std::clamp(snapped(m_GrabTime + (mouse.x - m_DragStart.x) / m_PixelsPerSecond, options), 0.0f, clip.Duration);
					const std::optional<size_t> moved = model.move_event(static_cast<size_t>(m_SelectedEvent), target);
					if (moved)
					{
						m_SelectedEvent = static_cast<int>(*moved);
					}
				}
			}
			else
			{
				model.end_gesture();
				m_Drag = Drag::None;
			}
		}
		else if (m_Drag == Drag::Box)
		{
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				// the keys of the rows the box touches, inside its times
				const float t0 = x_to_time(std::min(mouse.x, m_DragStart.x));
				const float t1 = x_to_time(std::max(mouse.x, m_DragStart.x));
				const float y0 = std::min(mouse.y, m_DragStart.y);
				const float y1 = std::max(mouse.y, m_DragStart.y);
				if (std::abs(mouse.x - m_DragStart.x) > 3.0f || std::abs(mouse.y - m_DragStart.y) > 3.0f)
				{
					for (size_t r = 0; r < rows.size(); r++)
					{
						const float top = rowsOrigin.y + static_cast<float>(r) * ROW_HEIGHT;
						if (rows[r].Type == Row::Kind::Events || top + ROW_HEIGHT < y0 || top > y1)
						{
							continue;
						}
						for (const size_t c : rows[r].Channels)
						{
							const AnimationChannel& channel = clip.Channels[c];
							for (size_t k = 0; k < channel.Times.size(); k++)
							{
								if (channel.Times[k] >= t0 && channel.Times[k] <= t1)
								{
									selection.push_back(KeyRef{ c, k });
								}
							}
						}
					}
					normalize_selection(selection);
				}
				m_Drag = Drag::None;
			}
		}

		// ---- keyboard
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && m_Drag == Drag::None)
		{
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
			{
				m_Clipboard = model.copy_keys(selection);
			}
			else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !m_Clipboard.empty())
			{
				const KeySelection pasted = model.paste_keys(m_Clipboard, playhead, m_SelectedJoint, options.Skeleton);
				if (!pasted.empty())
				{
					selection = pasted;
				}
			}
			else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && m_SelectedEvent >= 0 && selection.empty())
			{
				if (model.remove_event(static_cast<size_t>(m_SelectedEvent)))
				{
					m_SelectedEvent = -1;
				}
			}
			else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !selection.empty())
			{
				if (model.delete_keys(selection))
				{
					selection.clear();
				}
			}
			else if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && m_SelectedEvent >= 0)
			{
				m_RenameRequested = true;
			}
		}

		// ---- drawing
		const float timeLeft = rowsOrigin.x + LABEL_WIDTH;
		const float rightEdge = rowsOrigin.x + size.x;
		rowList->PushClipRect(ImVec2(rowsOrigin.x, rowsOrigin.y), ImVec2(rightEdge, rowsOrigin.y + rowsHeight), true);
		for (size_t r = firstRow; r < lastRow; r++)
		{
			const Row& row = rows[r];
			const float top = rowsOrigin.y + static_cast<float>(r) * ROW_HEIGHT;
			const float mid = top + ROW_HEIGHT * 0.5f;
			const bool rowSelected = row.Type != Row::Kind::Events && row.Joint == m_SelectedJoint;
			rowList->AddRectFilled(ImVec2(rowsOrigin.x, top), ImVec2(rightEdge, top + ROW_HEIGHT), rowSelected ? DS_ROW_SELECTED : (r % 2 == 0 ? DS_BACKGROUND : DS_ROW_ALT));

			// the label (clicking it selects the joint and its keys; the arrow opens it)
			const ImVec2 labelMin(rowsOrigin.x, top);
			const ImVec2 labelMax(rowsOrigin.x + LABEL_WIDTH, top + ROW_HEIGHT);
			std::string label;
			if (row.Type == Row::Kind::Events)
			{
				label = "Events";
			}
			else if (row.Type == Row::Kind::Joint)
			{
				label = std::string(row.Open ? "v " : "> ") + row.Joint;
			}
			else
			{
				label = std::string("    ") + path_name(clip.Channels[row.Channels.front()].Path);
			}
			rowList->AddText(ImVec2(labelMin.x + 6.0f, top + 3.0f), row.Type == Row::Kind::Channel ? DS_DIM : DS_TEXT, label.c_str());
			if (areaHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse.x >= labelMin.x && mouse.x < labelMax.x && mouse.y >= labelMin.y && mouse.y < labelMax.y
				&& m_Drag == Drag::None)
			{
				if (row.Type == Row::Kind::Joint)
				{
					if (mouse.x < labelMin.x + 22.0f)
					{
						if (row.Open) { m_Opened.erase(row.Joint); } else { m_Opened.insert(row.Joint); }
					}
					else
					{
						m_SelectedJoint = row.Joint;
						if (!(io.KeyShift || io.KeyCtrl)) { selection.clear(); }
						for (const size_t c : row.Channels)
						{
							for (size_t k = 0; k < clip.Channels[c].Times.size(); k++)
							{
								selection.push_back(KeyRef{ c, k });
							}
						}
						normalize_selection(selection);
					}
				}
				else if (row.Type == Row::Kind::Channel)
				{
					m_SelectedJoint = row.Joint;
				}
			}

			// the keys
			if (row.Type == Row::Kind::Events)
			{
				for (size_t e = 0; e < clip.Events.size(); e++)
				{
					const float x = time_to_x(clip.Events[e].Time);
					if (x < timeLeft - 8.0f || x > rightEdge + 8.0f)
					{
						continue;
					}
					const bool chosen = static_cast<int>(e) == m_SelectedEvent;
					diamond(rowList, ImVec2(x, mid), KEY_RADIUS + 1.0f, chosen ? DS_KEY_SELECTED : DS_EVENT, IM_COL32(0, 0, 0, 200));
					rowList->AddText(ImVec2(x + 8.0f, top + 3.0f), DS_DIM, clip.Events[e].Name.c_str());
				}
			}
			else
			{
				const float tMin = x_to_time(timeLeft) - 0.05f;
				const float tMax = x_to_time(rightEdge) + 0.05f;
				for (const size_t c : row.Channels)
				{
					const AnimationChannel& channel = clip.Channels[c];
					const size_t begin = static_cast<size_t>(std::lower_bound(channel.Times.begin(), channel.Times.end(), tMin) - channel.Times.begin());
					for (size_t k = begin; k < channel.Times.size() && channel.Times[k] <= tMax; k++)
					{
						const float x = time_to_x(channel.Times[k]);
						if (x < timeLeft - KEY_RADIUS)
						{
							continue;
						}
						const bool chosen = is_selected(selection, KeyRef{ c, k });
						diamond(rowList, ImVec2(x, mid), row.Type == Row::Kind::Joint ? KEY_RADIUS - 1.0f : KEY_RADIUS, chosen ? DS_KEY_SELECTED : key_color(channel), IM_COL32(0, 0, 0, 180));
					}
				}
			}
		}

		// the label column divider and the end of the clip
		rowList->AddLine(ImVec2(timeLeft, rowsOrigin.y), ImVec2(timeLeft, rowsOrigin.y + rowsHeight), DS_GRID);
		const float endX = time_to_x(clip.Duration);
		rowList->AddLine(ImVec2(endX, rowsOrigin.y), ImVec2(endX, rowsOrigin.y + rowsHeight), IM_COL32(150, 90, 90, 255));
		// the playhead
		const float playheadX = time_to_x(playhead);
		rowList->AddLine(ImVec2(playheadX, rowsOrigin.y), ImVec2(playheadX, rowsOrigin.y + rowsHeight), DS_PLAYHEAD, 2.0f);
		// the box
		if (m_Drag == Drag::Box)
		{
			rowList->AddRect(m_DragStart, mouse, IM_COL32(120, 180, 255, 255));
			rowList->AddRectFilled(m_DragStart, mouse, IM_COL32(120, 180, 255, 40));
		}
		rowList->PopClipRect();
		(void)areaActive;

		// rename the selected event (F2, or right after a double click added it)
		if (m_RenameRequested && m_SelectedEvent >= 0 && static_cast<size_t>(m_SelectedEvent) < clip.Events.size())
		{
			std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", clip.Events[static_cast<size_t>(m_SelectedEvent)].Name.c_str());
			ImGui::OpenPopup("Rename event##dope");
			m_RenameFocus = true;
		}
		m_RenameRequested = false;
		if (ImGui::BeginPopup("Rename event##dope"))
		{
			if (m_RenameFocus)
			{
				ImGui::SetKeyboardFocusHere();
				m_RenameFocus = false;
			}
			const bool entered = ImGui::InputText("##eventname", m_RenameBuffer, sizeof(m_RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
			if (entered && m_SelectedEvent >= 0 && model.rename_event(static_cast<size_t>(m_SelectedEvent), m_RenameBuffer))
			{
				ImGui::CloseCurrentPopup();
			}
			if (!model.error().empty())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", model.error().c_str());
			}
			ImGui::EndPopup();
		}
		ImGui::EndChild();

		// the playhead on the ruler
		list->PushClipRect(rulerMin, rulerMax, true);
		list->AddTriangleFilled(ImVec2(playheadX - 6.0f, rulerMin.y), ImVec2(playheadX + 6.0f, rulerMin.y), ImVec2(playheadX, rulerMin.y + 12.0f), DS_PLAYHEAD);
		list->AddLine(ImVec2(playheadX, rulerMin.y + 12.0f), ImVec2(playheadX, rulerMax.y), DS_PLAYHEAD, 2.0f);
		list->PopClipRect();
		return playheadMoved;
	}
}
