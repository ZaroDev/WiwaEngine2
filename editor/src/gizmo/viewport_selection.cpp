#include <editor_common.h>
#include "viewport_selection.h"

#include <scene/components.h>

namespace wiwa
{
	std::optional<EntityGuid> selected_guid(SceneEditor& editor)
	{
		const entt::registry& registry = editor.scene().registry();
		const entt::entity entity = editor.selected();
		if (!registry.valid(entity) || !registry.all_of<Id>(entity))
		{
			return std::nullopt;
		}
		return registry.get<Id>(entity).Guid;
	}

	void apply_pick(SceneEditor& editor, const PickResult& result)
	{
		editor.select(result.Guid ? editor.scene().find(*result.Guid) : entt::entity{ entt::null });
	}
}
