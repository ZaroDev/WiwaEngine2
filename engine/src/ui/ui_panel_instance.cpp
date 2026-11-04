#include <pch.h>
#include "ui_panel_instance.h"

#include <core/log.h>

namespace wiwa
{
	void UiPanelInstance::poll(const Loader& loader)
	{
		if (!loader || !m_Ref.valid())
		{
			return;
		}
		if (!m_Handle)
		{
			m_Handle = loader(m_Ref);
			if (!m_Handle)
			{
				return;
			}
		}

		switch (m_Handle.state())
		{
		case LoadState::Loading:
			return;

		case LoadState::Failed:
			if (!m_Reported)
			{
				m_Reported = true;
				m_FailureReports++;
				WI_LOG(Error, "UI", "The UI panel {0} could not be loaded, so its canvas draws nothing", m_Ref.Guid.to_string());
			}
			return;

		case LoadState::Ready:
			if (const UiPanelData* asset = m_Handle.get(); asset != nullptr && asset != m_AdoptedFrom)
			{
				adopt(*asset);
				m_AdoptedFrom = asset;
			}
			return;
		}
	}

	void UiPanelInstance::reload()
	{
		m_Handle = Handle<UiPanelData>{};
		m_Reported = false;
	}

	void UiPanelInstance::adopt(const UiPanelData& asset)
	{
		UiPanelData merged = asset;
		if (m_Ready)
		{
			for (UiWidget& widget : merged.Widgets)
			{
				const UiWidget* current = m_Data.find(widget.Id);
				const UiWidget* origin = m_Origin.find(widget.Id);
				if (current == nullptr || origin == nullptr || current->Kind != widget.Kind)
				{
					continue;
				}

				if (current->Text != origin->Text) { widget.Text = current->Text; }
				if (current->Value != origin->Value) { widget.Value = current->Value; }
				if (current->Visible != origin->Visible) { widget.Visible = current->Visible; }
				if (current->Enabled != origin->Enabled) { widget.Enabled = current->Enabled; }
			}
		}

		m_Origin = asset;
		m_Data = std::move(merged);
		m_States.assign(m_Data.Widgets.size(), UiInteractionState::Normal);
		m_Interaction.reset();
		m_Ready = true;
		m_Generation++;
	}

	std::optional<UiId> UiPanelInstance::find(std::string_view path) const
	{
		if (path.empty() || !m_Ready)
		{
			return std::nullopt;
		}

		UiId parent = 0;
		size_t start = 0;
		while (true)
		{
			const size_t end = path.find('/', start);
			const std::string_view part = path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
			if (part.empty())
			{
				return std::nullopt;
			}

			const UiWidget* match = nullptr;
			for (const UiWidget& widget : m_Data.Widgets)
			{
				if (widget.Parent == parent && widget.Name == part)
				{
					match = &widget;
					break;
				}
			}
			if (match == nullptr)
			{
				return std::nullopt;
			}

			parent = match->Id;
			if (end == std::string_view::npos)
			{
				return parent;
			}
			start = end + 1;
		}
	}
}
