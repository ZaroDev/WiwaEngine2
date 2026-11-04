#pragma once

#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include <assets/handle.h>

#include "ui_interaction.h"
#include "ui_widget.h"

namespace wiwa
{
	// The live state of one UI panel shown by one UICanvas: a copy of the panel asset that scripts and the pointer change, the interaction
	// state of each widget, and the events of the frame. The asset itself is never touched, so two canvases of one panel are independent
	// and a scene clone (play mode) starts from the asset again.
	class UiPanelInstance
	{
	public:
		using Loader = std::function<Handle<UiPanelData>(const AssetRef&)>;

		explicit UiPanelInstance(AssetRef panel) : m_Ref(panel) {}

		const AssetRef& panel_ref() const { return m_Ref; }

		// Asks the loader for the asset and takes it over once it is Ready (a Loading panel is waited for, a Failed one is logged once).
		// When the asset was cooked again (reload()) the new one replaces the data: a widget keeps the Text, Value, Visible and Enabled that
		// were changed since the previous version, and takes the rest from the new file.
		void poll(const Loader& loader);

		// The asset was cooked again: ask for it again at the next poll(). The old data stays in use until the new one is ready.
		void reload();

		bool ready() const { return m_Ready; }
		u32 failure_reports() const { return m_FailureReports; }

		UiPanelData& data() { return m_Data; }
		const UiPanelData& data() const { return m_Data; }
		std::vector<UiInteractionState>& states() { return m_States; }
		UiInteraction& interaction() { return m_Interaction; }
		// What happened to the widgets in the latest update; cleared when the next one starts.
		std::vector<UiEvent>& events() { return m_Events; }

		// The widget at a path of names from a root: "Menu/Start". Nothing if any part is missing, or the path is empty.
		std::optional<UiId> find(std::string_view path) const;

		// Scripts can hide a whole panel without touching the canvas component.
		bool visible() const { return m_Visible; }
		void set_visible(bool visible) { m_Visible = visible; }

		// Counts the times the data was replaced by a newer version of the asset.
		u64 generation() const { return m_Generation; }

	private:
		void adopt(const UiPanelData& asset);

		AssetRef m_Ref{};
		Handle<UiPanelData> m_Handle{};
		const UiPanelData* m_AdoptedFrom = nullptr;
		UiPanelData m_Origin{}; // the asset as it was adopted: what "changed since" is measured against
		UiPanelData m_Data{};
		std::vector<UiInteractionState> m_States{};
		UiInteraction m_Interaction{};
		std::vector<UiEvent> m_Events{};
		bool m_Ready = false;
		bool m_Visible = true;
		bool m_Reported = false;
		u32 m_FailureReports = 0;
		u64 m_Generation = 0;
	};
}
