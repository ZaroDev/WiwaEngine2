#include <editor_common.h>
#include "gizmo_smoke_test.h"

#include <cmath>
#include <limits>

#include <core/engine.h>
#include <scene/components.h>
#include <scene/scene.h>
#include <scene/scene_components.h>
#include <scene/scene_editor.h>
#include <scene/scene_module.h>

#include "transform_gizmo.h"
#include "viewport_selection.h"

#define WI_GIZMO_LOG(level, ...) WI_LOG(level, "Editor", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		struct Check
		{
			u32 Failures = 0;

			void operator()(bool condition, const std::string& what)
			{
				if (!condition)
				{
					Failures++;
					WI_GIZMO_LOG(Error, "Gizmo smoke test FAILED: {0}", what);
				}
			}
		};

		constexpr glm::vec2 VIEWPORT(512.0f, 512.0f);
		constexpr float TOLERANCE = 2e-3f;

		bool near_to(float a, float b, float tolerance = TOLERANCE) { return std::abs(a - b) <= tolerance; }
		bool near_to(const glm::vec3& a, const glm::vec3& b, float tolerance = TOLERANCE) { return glm::length(a - b) <= tolerance; }
		bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
		bool finite(const glm::quat& q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); }

		struct Camera
		{
			glm::mat4 View{ 1.0f };
			glm::mat4 Projection{ 1.0f };
		};

		Camera make_camera(glm::vec3 eye, glm::vec3 target)
		{
			return Camera{ glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f)), glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f) };
		}

		// Everything a check needs: a scene in memory, a camera, a gizmo and a way to move the mouse.
		class Rig
		{
		public:
			explicit Rig(glm::vec3 eye = glm::vec3(0.0f, 0.0f, 6.0f), glm::vec3 target = glm::vec3(0.0f))
				: Editor(*Engine::get().get_module<AssetManager>(), *Engine::get().get_module<SceneModule>()), Cam(make_camera(eye, target))
			{
				Editor.set_scene(CreateRef<Scene>(), {});
			}

			entt::registry& registry() { return Editor.scene().registry(); }

			entt::entity make(glm::vec3 position, entt::entity parent = entt::null)
			{
				const entt::entity entity = Editor.create_entity(parent);
				Editor.set_field(entity, "Transform", "Position", FieldValue(position));
				Editor.scene().update_world_transforms();
				return entity;
			}

			glm::vec3 position(entt::entity entity) { return registry().get<Transform>(entity).Position; }
			glm::vec3 scale(entt::entity entity) { return registry().get<Transform>(entity).Scale; }
			glm::quat rotation(entt::entity entity) { return registry().get<Transform>(entity).Rotation; }
			glm::vec3 world_position(entt::entity entity) { return glm::vec3(registry().get<WorldTransform>(entity).Matrix[3]); }

			// How long a handle unit is in world units for a gizmo at this point.
			float unit(glm::vec3 at) const
			{
				return gizmo::screen_constant_scale(at, Cam.View, Cam.Projection, TransformGizmo::SIZE_PIXELS, VIEWPORT.y);
			}

			glm::vec2 project(glm::vec3 world) const
			{
				const glm::vec4 clip = Cam.Projection * Cam.View * glm::vec4(world, 1.0f);
				const glm::vec3 ndc = glm::vec3(clip) / clip.w;
				return glm::vec2((ndc.x * 0.5f + 0.5f) * VIEWPORT.x, (1.0f - (ndc.y * 0.5f + 0.5f)) * VIEWPORT.y);
			}

			// One frame with the mouse over the world point. Returns whether the gizmo kept the mouse.
			bool step(glm::vec3 mouseAt, bool pressed, bool down, bool ctrl = false, bool hovered = true)
			{
				GizmoInput input;
				input.MousePixel = project(mouseAt);
				input.ViewportSize = VIEWPORT;
				input.Hovered = hovered;
				input.LeftPressed = pressed;
				input.LeftDown = down;
				input.Ctrl = ctrl;
				input.View = Cam.View;
				input.Projection = Cam.Projection;
				const bool consumed = Gizmo.update(Draw, input, Editor);
				Editor.scene().update_world_transforms();
				Draw.seal(0.016f);
				return consumed;
			}

			bool hover(glm::vec3 mouseAt) { return step(mouseAt, false, false); }
			bool press(glm::vec3 mouseAt, bool ctrl = false) { return step(mouseAt, true, true, ctrl); }
			bool drag(glm::vec3 mouseAt, bool ctrl = false) { return step(mouseAt, false, true, ctrl); }
			bool release(glm::vec3 mouseAt) { return step(mouseAt, false, false); }

			SceneEditor Editor;
			Camera Cam;
			DebugDraw Draw{};
			TransformGizmo Gizmo{};
		};

		void check_selection_helpers(Check& check)
		{
			Rig rig;
			const entt::entity a = rig.make(glm::vec3(0.0f));
			const entt::entity b = rig.make(glm::vec3(1.0f));
			check(!selected_guid(rig.Editor), "nothing selected, no guid");

			rig.Editor.select(a);
			check(selected_guid(rig.Editor) == rig.registry().get<Id>(a).Guid, "the selected entity's guid");

			apply_pick(rig.Editor, PickResult{ 1, rig.registry().get<Id>(b).Guid });
			check(rig.Editor.selected() == b, "a pick that found an entity selects it");
			apply_pick(rig.Editor, PickResult{ 2, std::nullopt });
			check(rig.Editor.selected() == entt::null && !selected_guid(rig.Editor), "a pick that found nothing clears the selection");

			rig.Editor.select(a);
			apply_pick(rig.Editor, PickResult{ 3, EntityGuid{ 0xDEAD, 0xBEEF } });
			check(rig.Editor.selected() == entt::null, "a pick that found an entity that is not in the scene clears the selection");

			rig.Editor.select(a);
			rig.Editor.delete_entity(a);
			check(!selected_guid(rig.Editor), "a deleted selection has no guid");
		}

		void check_translate(Check& check)
		{
			Rig rig;
			const entt::entity entity = rig.make(glm::vec3(0.0f));
			rig.Editor.select(entity);
			rig.Gizmo.mode = gizmo::Mode::Translate;
			const float s = rig.unit(glm::vec3(0.0f));
			const glm::vec3 grab(0.8f * s, 0.0f, 0.0f);

			check(rig.hover(grab) && rig.Gizmo.hovered_handle() == gizmo::Handle::AxisX, "hovering the X arrow keeps the mouse for the gizmo");
			check(!rig.hover(glm::vec3(5.0f * s, 5.0f * s, 0.0f)) && rig.Gizmo.hovered_handle() == gizmo::Handle::None, "empty space is not the gizmo's");
			check(!rig.step(grab, false, false, false, false), "a mouse that is not over the viewport is not the gizmo's");
			check(!rig.press(glm::vec3(5.0f * s, 5.0f * s, 0.0f)) && !rig.Gizmo.dragging(), "a click on empty space does not start a drag, and may pick");

			check(rig.press(grab) && rig.Gizmo.dragging(), "a click on a handle starts a drag and does not pick");
			rig.drag(grab + glm::vec3(2.0f, 0.0f, 0.0f));
			check(near_to(rig.position(entity), glm::vec3(2.0f, 0.0f, 0.0f)), "dragging the X arrow by 2 moves the entity by 2 along X");
			rig.drag(grab + glm::vec3(-1.0f, 0.0f, 0.0f));
			check(near_to(rig.position(entity), glm::vec3(-1.0f, 0.0f, 0.0f)), "the drag follows the mouse back, from the position it began at");
			rig.release(grab);
			check(!rig.Gizmo.dragging() && near_to(rig.position(entity), glm::vec3(-1.0f, 0.0f, 0.0f)), "letting go ends the drag and keeps the position");
		}

		void check_snap_and_planes(Check& check)
		{
			{
				Rig rig;
				const entt::entity entity = rig.make(glm::vec3(0.0f));
				rig.Editor.select(entity);
				const float s = rig.unit(glm::vec3(0.0f));
				const glm::vec3 grab(0.8f * s, 0.0f, 0.0f);
				rig.press(grab, true);
				rig.drag(grab + glm::vec3(1.3f, 0.0f, 0.0f), true);
				check(near_to(rig.position(entity).x, 1.5f), "with Ctrl a drag of 1.3 snaps to a multiple of 0.5 (1.5)");
				rig.drag(grab + glm::vec3(1.2f, 0.0f, 0.0f), true);
				check(near_to(rig.position(entity).x, 1.0f), "and 1.2 snaps to 1.0");
				rig.release(grab);
			}

			{
				Rig rig;
				const entt::entity entity = rig.make(glm::vec3(0.0f));
				rig.Editor.select(entity);
				const float s = rig.unit(glm::vec3(0.0f));
				const glm::vec3 grab(0.45f * s, 0.45f * s, 0.0f);
				check(rig.press(grab) && rig.Gizmo.dragging(), "a click inside the XY square starts a plane drag");
				rig.drag(grab + glm::vec3(1.0f, -0.5f, 0.0f));
				check(near_to(rig.position(entity), glm::vec3(1.0f, -0.5f, 0.0f)), "a plane drag moves along both axes of the plane");
				rig.release(grab);
			}
		}

		void check_hierarchy_and_space(Check& check)
		{
			{
				// A child of a parent that is scaled by 2: a world move of 2 is a local move of 1.
				Rig rig(glm::vec3(10.0f, 0.0f, 8.0f), glm::vec3(10.0f, 0.0f, 0.0f));
				const entt::entity parent = rig.make(glm::vec3(10.0f, 0.0f, 0.0f));
				rig.Editor.set_field(parent, "Transform", "Scale", FieldValue(glm::vec3(2.0f)));
				const entt::entity child = rig.make(glm::vec3(1.0f, 0.0f, 0.0f), parent);
				rig.Editor.scene().update_world_transforms();
				check(near_to(rig.world_position(child), glm::vec3(12.0f, 0.0f, 0.0f)), "the child sits at world x = 12");

				rig.Editor.select(child);
				const glm::vec3 origin = rig.world_position(child);
				const float s = rig.unit(origin);
				const glm::vec3 grab = origin + glm::vec3(0.8f * s, 0.0f, 0.0f);
				rig.press(grab);
				rig.drag(grab + glm::vec3(2.0f, 0.0f, 0.0f));
				rig.release(grab);
				check(near_to(rig.world_position(child), glm::vec3(14.0f, 0.0f, 0.0f)), "dragging the child by 2 in the world moves it by 2 in the world");
				check(near_to(rig.position(child), glm::vec3(2.0f, 0.0f, 0.0f)), "which is 1 in the parent's space: its local x went from 1 to 2");
			}

			{
				// Local space: the entity is turned a quarter about Z, so its X axis points along world Y.
				Rig rig;
				const entt::entity entity = rig.make(glm::vec3(0.0f));
				rig.Editor.set_field(entity, "Transform", "Rotation", FieldValue(glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f))));
				rig.Editor.scene().update_world_transforms();
				rig.Editor.select(entity);
				rig.Gizmo.space = gizmo::Space::Local;
				const float s = rig.unit(glm::vec3(0.0f));
				const glm::vec3 grab(0.0f, 0.8f * s, 0.0f);
				check(rig.hover(grab) && rig.Gizmo.hovered_handle() == gizmo::Handle::AxisX, "in local space the entity's X handle lies along world Y");
				rig.press(grab);
				rig.drag(grab + glm::vec3(0.0f, 1.0f, 0.0f));
				rig.release(grab);
				check(near_to(rig.position(entity), glm::vec3(0.0f, 1.0f, 0.0f)), "dragging it moves the entity along world Y");
			}
		}

		void check_rotate_and_scale(Check& check)
		{
			{
				Rig rig;
				const entt::entity entity = rig.make(glm::vec3(0.0f));
				rig.Editor.select(entity);
				rig.Gizmo.mode = gizmo::Mode::Rotate;
				const float s = rig.unit(glm::vec3(0.0f));
				const glm::vec3 from(s, 0.0f, 0.0f);
				check(rig.press(from) && rig.Gizmo.dragging(), "a click on the Z ring starts a rotation");
				rig.drag(glm::vec3(0.0f, s, 0.0f));
				const glm::quat quarter = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
				check(std::abs(glm::dot(rig.rotation(entity), quarter)) > std::cos(glm::radians(0.5f)), "dragging a quarter of the ring turns the entity 90 degrees about Z");
				rig.release(from);
			}

			{
				Rig rig;
				const entt::entity entity = rig.make(glm::vec3(0.0f));
				rig.Editor.select(entity);
				rig.Gizmo.mode = gizmo::Mode::Rotate;
				const float s = rig.unit(glm::vec3(0.0f));
				const glm::vec3 from(s, 0.0f, 0.0f);
				rig.press(from, true);
				const float angle = glm::radians(50.0f);
				rig.drag(glm::vec3(std::cos(angle) * s, std::sin(angle) * s, 0.0f), true);
				const glm::quat snapped = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f));
				check(std::abs(glm::dot(rig.rotation(entity), snapped)) > std::cos(glm::radians(0.5f)), "with Ctrl a turn of 50 degrees snaps to 45");
				rig.release(from);
			}

			{
				Rig rig;
				const entt::entity entity = rig.make(glm::vec3(0.0f));
				rig.Editor.select(entity);
				rig.Gizmo.mode = gizmo::Mode::Scale;
				const float s = rig.unit(glm::vec3(0.0f));
				check(rig.hover(glm::vec3(0.0f)) && rig.Gizmo.hovered_handle() == gizmo::Handle::Uniform, "scale: the middle is the uniform handle");

				const glm::vec3 grab(0.9f * s, 0.0f, 0.0f);
				check(rig.press(grab) && rig.Gizmo.dragging(), "a click on the X scale handle starts a drag");
				rig.drag(glm::vec3(1.8f * s, 0.0f, 0.0f));
				check(near_to(rig.scale(entity), glm::vec3(2.0f, 1.0f, 1.0f)), "dragging the handle to twice its distance doubles the X scale");
				rig.release(grab);

				rig.Editor.select(entity);
				const glm::vec3 center(0.0f);
				rig.press(center);
				rig.drag(glm::vec3(0.5f * s, 0.5f * s, 0.0f));
				const glm::vec3 uniform = rig.scale(entity);
				check(finite(uniform) && near_to(uniform.x / 2.0f, uniform.y) && near_to(uniform.y, uniform.z), "the uniform handle scales every axis by the same factor");
				rig.release(center);
			}
		}

		void check_bad_input(Check& check)
		{
			{
				// A scale of 0 makes the entity's matrix singular: nothing may turn into NaN.
				Rig rig;
				const entt::entity entity = rig.make(glm::vec3(0.0f));
				rig.Editor.set_field(entity, "Transform", "Scale", FieldValue(glm::vec3(0.0f)));
				rig.Editor.scene().update_world_transforms();
				rig.Editor.select(entity);
				rig.Gizmo.space = gizmo::Space::Local;
				const float s = rig.unit(glm::vec3(0.0f));
				const glm::vec3 grab(0.8f * s, 0.0f, 0.0f);
				rig.press(grab);
				rig.drag(grab + glm::vec3(1.0f, 0.0f, 0.0f));
				rig.release(grab);
				check(finite(rig.position(entity)) && near_to(rig.position(entity), glm::vec3(1.0f, 0.0f, 0.0f)), "a zero-scale entity can still be moved");

				rig.Gizmo.mode = gizmo::Mode::Scale;
				const glm::vec3 scaleGrab(1.0f + 0.9f * s, 0.0f, 0.0f);
				rig.press(scaleGrab);
				rig.drag(scaleGrab + glm::vec3(1.0f, 0.0f, 0.0f));
				rig.release(scaleGrab);
				check(finite(rig.scale(entity)) && finite(rig.rotation(entity)), "scaling a zero-scale entity writes no NaN");

				rig.Gizmo.mode = gizmo::Mode::Rotate;
				const glm::vec3 ringGrab(1.0f + s, 0.0f, 0.0f);
				rig.press(ringGrab);
				rig.drag(glm::vec3(1.0f, s, 0.0f));
				rig.release(ringGrab);
				check(finite(rig.position(entity)) && finite(rig.rotation(entity)), "rotating a zero-scale entity writes no NaN");
			}

			{
				// The selection goes away in the middle of a drag.
				Rig rig;
				const entt::entity entity = rig.make(glm::vec3(0.0f));
				rig.Editor.select(entity);
				const float s = rig.unit(glm::vec3(0.0f));
				const glm::vec3 grab(0.8f * s, 0.0f, 0.0f);
				rig.press(grab);
				rig.Editor.delete_entity(entity);
				check(!rig.drag(grab + glm::vec3(1.0f, 0.0f, 0.0f)) && !rig.Gizmo.dragging(), "deleting the entity in the middle of a drag ends it");
			}

			{
				// A camera that cannot be used: the gizmo does nothing and writes nothing.
				Rig rig;
				const entt::entity entity = rig.make(glm::vec3(1.0f, 2.0f, 3.0f));
				rig.Editor.select(entity);
				GizmoInput input;
				input.MousePixel = glm::vec2(256.0f);
				input.ViewportSize = VIEWPORT;
				input.Hovered = true;
				input.LeftPressed = true;
				input.LeftDown = true;
				input.View = glm::mat4(std::numeric_limits<float>::quiet_NaN());
				input.Projection = rig.Cam.Projection;
				check(!rig.Gizmo.update(rig.Draw, input, rig.Editor) && near_to(rig.position(entity), glm::vec3(1.0f, 2.0f, 3.0f)), "a NaN camera matrix is ignored");

				input.View = rig.Cam.View;
				input.ViewportSize = glm::vec2(0.0f);
				check(!rig.Gizmo.update(rig.Draw, input, rig.Editor) && near_to(rig.position(entity), glm::vec3(1.0f, 2.0f, 3.0f)), "a viewport of size 0 is ignored");

				// Nothing selected: no gizmo.
				rig.Editor.select(entt::null);
				input.ViewportSize = VIEWPORT;
				check(!rig.Gizmo.update(rig.Draw, input, rig.Editor), "with nothing selected the gizmo is not there");
			}

			{
				// The camera behind the gizmo, and inside it.
				Rig rig(glm::vec3(0.0f, 0.0f, -6.0f), glm::vec3(0.0f));
				const entt::entity entity = rig.make(glm::vec3(0.0f));
				rig.Editor.select(entity);
				const float s = rig.unit(glm::vec3(0.0f));
				rig.press(glm::vec3(0.8f * s, 0.0f, 0.0f));
				rig.drag(glm::vec3(0.8f * s + 1.0f, 0.0f, 0.0f));
				rig.release(glm::vec3(0.0f));
				check(finite(rig.position(entity)), "dragging with the camera on the other side writes a finite position");

				Rig inside(glm::vec3(0.05f, 0.05f, 0.05f), glm::vec3(0.0f, 0.0f, -1.0f));
				const entt::entity close = inside.make(glm::vec3(0.0f));
				inside.Editor.select(close);
				inside.press(glm::vec3(0.0f, 0.0f, -1.0f));
				inside.drag(glm::vec3(0.5f, 0.0f, -1.0f));
				inside.release(glm::vec3(0.0f, 0.0f, -1.0f));
				check(finite(inside.position(close)) && finite(inside.scale(close)) && finite(inside.rotation(close)), "a camera inside the gizmo writes no NaN");
			}
		}
	}

	bool run_gizmo_smoke_test(AssetPipelineModule& pipeline)
	{
		register_scene_components();
		Check check;

		check_selection_helpers(check);
		check_translate(check);
		check_snap_and_planes(check);
		check_hierarchy_and_space(check);
		check_rotate_and_scale(check);
		check_bad_input(check);

		if (check.Failures == 0)
		{
			WI_GIZMO_LOG(Info, "Gizmo smoke test passed");
		}
		return check.Failures == 0;
	}
}
