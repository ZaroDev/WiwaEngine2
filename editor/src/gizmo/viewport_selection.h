#pragma once

#include <optional>

#include <renderer/render_pick.h>
#include <scene/entity_guid.h>
#include <scene/scene_editor.h>

namespace wiwa
{
	// The guid of the selected entity (what the renderer outlines), or none.
	std::optional<EntityGuid> selected_guid(SceneEditor& editor);

	// Applies the answer to a click in the Viewport: selects the entity it found, or clears the selection (empty space, an entity that is gone).
	void apply_pick(SceneEditor& editor, const PickResult& result);
}
