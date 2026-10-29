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

#include "gizmo_settings.h"
#include "scene_gizmos.h"
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

		size_t line_count(const DebugDrawData& data) { return data.Lines[0].size() + data.Lines[1].size(); }

		entt::entity make_light(Rig& rig, glm::vec3 position, LightKind kind, float range = 10.0f)
		{
			const entt::entity entity = rig.make(position);
			rig.Editor.add_component(entity, "Light");
			rig.Editor.set_field(entity, "Light", "Kind", FieldValue(static_cast<i32>(kind)));
			rig.Editor.set_field(entity, "Light", "Range", FieldValue(range));
			return entity;
		}

		void check_scene_gizmos(Check& check)
		{
			Rig rig;
			const entt::entity point = make_light(rig, glm::vec3(1.0f, 0.0f, 0.0f), LightKind::Point);
			const entt::entity spot = make_light(rig, glm::vec3(2.0f, 0.0f, 0.0f), LightKind::Spot);
			const entt::entity sun = make_light(rig, glm::vec3(3.0f, 0.0f, 0.0f), LightKind::Directional);
			const entt::entity camera = rig.make(glm::vec3(0.0f, 0.0f, 5.0f));
			rig.Editor.add_component(camera, "Camera");
			const entt::entity mesh = rig.make(glm::vec3(0.0f, 2.0f, 0.0f));
			rig.Editor.add_component(mesh, "MeshRenderer");
			rig.Editor.scene().update_world_transforms();

			SceneGizmos gizmos([](const AssetRef&) { return std::optional<std::pair<glm::vec3, glm::vec3>>(std::pair<glm::vec3, glm::vec3>(glm::vec3(-1.0f), glm::vec3(1.0f))); });
			SceneGizmoView view;
			view.View = rig.Cam.View;
			view.Projection = rig.Cam.Projection;
			view.ViewportHeight = VIEWPORT.y;
			Scene& scene = rig.Editor.scene();
			DebugDraw draw;
			GizmoSettings settings;

			// Nothing selected: a marker for every light (3 lines), the sun's arrow (5), the camera's marker (3) and frustum (12).
			gizmos.draw(draw, scene, entt::null, settings, view);
			DebugDrawData data = draw.seal(0.0f);
			check(line_count(data) == 3 * 3 + 5 + 3 + 12, "nothing selected: light markers, the sun's arrow, the camera's marker and frustum (" + std::to_string(line_count(data)) + " lines)");
			check(data.PickIcons.size() == 4, "every light and camera can be picked");
			check(data.Triangles[0].empty() && data.Triangles[1].empty(), "no triangles are drawn");

			// A selected point light also shows its range: a sphere (3 circles of 32 segments).
			gizmos.draw(draw, scene, point, settings, view);
			data = draw.seal(0.0f);
			check(line_count(data) == 3 * 3 + 5 + 3 + 12 + 96, "a selected point light adds its range sphere (" + std::to_string(line_count(data)) + " lines)");

			// A selected spot light shows its cone (a base circle of 32 segments and 8 side lines).
			gizmos.draw(draw, scene, spot, settings, view);
			data = draw.seal(0.0f);
			check(line_count(data) == 3 * 3 + 5 + 3 + 12 + 40, "a selected spot light adds its cone (" + std::to_string(line_count(data)) + " lines)");

			// The light points down -Z (its forward axis): the 8 lines from the apex reach the base, a range of 10 away.
			const glm::vec3 apex(2.0f, 0.0f, 0.0f);
			u32 sides = 0;
			for (const std::vector<DebugLine>& lines : data.Lines)
			{
				for (const DebugLine& line : lines)
				{
					const bool fromApex = near_to(line.A, apex, 1e-4f);
					const glm::vec3& other = fromApex ? line.B : line.A;
					if ((fromApex || near_to(line.B, apex, 1e-4f)) && near_to(other.z, -10.0f, 1e-3f))
					{
						sides++;
					}
				}
			}
			check(sides == 8, "the spot light's cone opens along its forward axis, 10 units deep (" + std::to_string(sides) + " side lines)");

			// A selected sun has nothing more than every sun has.
			gizmos.draw(draw, scene, sun, settings, view);
			check(line_count(draw.seal(0.0f)) == 3 * 3 + 5 + 3 + 12, "a selected sun adds nothing");

			// A selected mesh shows its world bounds (12 lines).
			gizmos.draw(draw, scene, mesh, settings, view);
			data = draw.seal(0.0f);
			check(line_count(data) == 3 * 3 + 5 + 3 + 12 + 12, "a selected mesh adds its bounds (" + std::to_string(line_count(data)) + " lines)");

			// A mesh that has not loaded has no bounds to show.
			SceneGizmos unloaded([](const AssetRef&) { return std::optional<std::pair<glm::vec3, glm::vec3>>(); });
			unloaded.draw(draw, scene, mesh, settings, view);
			check(line_count(draw.seal(0.0f)) == 3 * 3 + 5 + 3 + 12, "a mesh that has not loaded shows no bounds");

			// The categories switch off separately.
			GizmoSettings noLights = settings;
			noLights.Lights = false;
			gizmos.draw(draw, scene, point, noLights, view);
			data = draw.seal(0.0f);
			check(line_count(data) == 3 + 12 && data.PickIcons.size() == 1, "lights off: only the camera is left, with its icon (" + std::to_string(line_count(data)) + " lines)");

			GizmoSettings noCameras = settings;
			noCameras.Cameras = false;
			gizmos.draw(draw, scene, entt::null, noCameras, view);
			data = draw.seal(0.0f);
			check(line_count(data) == 3 * 3 + 5 && data.PickIcons.size() == 3, "cameras off: only the lights are left");

			GizmoSettings noBounds = settings;
			noBounds.Bounds = false;
			gizmos.draw(draw, scene, mesh, noBounds, view);
			check(line_count(draw.seal(0.0f)) == 3 * 3 + 5 + 3 + 12, "bounds off: a selected mesh shows nothing extra");

			// Values that cannot be drawn are dropped, not drawn.
			rig.Editor.set_field(point, "Light", "Range", FieldValue(0.0f));
			rig.Editor.set_field(spot, "Light", "Range", FieldValue(-1.0f));
			gizmos.draw(draw, scene, point, settings, view);
			gizmos.draw(draw, scene, spot, settings, view);
			data = draw.seal(0.0f);
			check(line_count(data) == 2 * (3 * 3 + 5 + 3 + 12), "a light with no range shows no sphere or cone");

			// A view that cannot size the markers falls back to a size, and nothing is NaN.
			SceneGizmoView broken;
			broken.View = glm::mat4(std::numeric_limits<float>::quiet_NaN());
			gizmos.draw(draw, scene, entt::null, settings, broken);
			data = draw.seal(0.0f);
			bool allFinite = true;
			for (const std::vector<DebugLine>& lines : data.Lines)
			{
				for (const DebugLine& line : lines)
				{
					allFinite = allFinite && finite(line.A) && finite(line.B);
				}
			}
			check(allFinite && line_count(data) > 0, "a view that cannot be used still gives finite markers");

			// An entity that was deleted, as the selection.
			const entt::entity gone = rig.make(glm::vec3(0.0f));
			rig.Editor.delete_entity(gone);
			gizmos.draw(draw, scene, gone, settings, view);
			check(line_count(draw.seal(0.0f)) > 0, "a stale selection is ignored");
		}

		void check_gizmo_settings(Check& check)
		{
			namespace fs = std::filesystem;
			const fs::path file = fs::temp_directory_path() / "wiwa_gizmo_settings_smoke.json";
			std::error_code error;
			fs::remove(file, error);

			check(GizmoSettings::load(file) == GizmoSettings{}, "a missing file gives the defaults");

			GizmoSettings changed;
			changed.Lights = false;
			changed.Cameras = false;
			changed.Bounds = false;
			changed.Outline.Color = glm::vec3(0.1f, 0.2f, 0.3f);
			changed.Outline.WidthPx = 3.0f;
			changed.Outline.OccludedAlpha = 0.6f;
			changed.Outline.Enabled = false;
			check(changed.save(file), "the settings can be saved");
			check(GizmoSettings::load(file) == changed, "and load back as they were");

			auto write = [&](const std::string& text)
			{
				std::ofstream out(file, std::ios::binary | std::ios::trunc);
				out << text;
			};

			write("this is not json {{{");
			check(GizmoSettings::load(file) == GizmoSettings{}, "a corrupt file gives the defaults");
			write("");
			check(GizmoSettings::load(file) == GizmoSettings{}, "an empty file gives the defaults");
			write("[1, 2, 3]");
			check(GizmoSettings::load(file) == GizmoSettings{}, "json of the wrong shape gives the defaults");

			write(R"({"lights": false})");
			GizmoSettings partial = GizmoSettings::load(file);
			check(!partial.Lights && partial.Cameras && partial.Bounds && partial.Outline == SelectionOutlineStyle{}, "keys that are missing keep their defaults");

			write(R"({"lights": "yes", "outline": {"width": "wide", "color": [1, 2], "occludedAlpha": null}})");
			check(GizmoSettings::load(file) == GizmoSettings{}, "values of the wrong type are ignored");

			write(R"({"outline": {"width": 100, "occludedAlpha": 7, "color": [5, -5, 0.5]}})");
			const GizmoSettings clamped = GizmoSettings::load(file);
			check(clamped.Outline.WidthPx == 8.0f && clamped.Outline.OccludedAlpha == 1.0f && clamped.Outline.Color == glm::vec3(1.0f, 0.0f, 0.5f), "out-of-range values are clamped");

			write(R"({"outline": {"width": 0}})");
			check(GizmoSettings::load(file).Outline.WidthPx == 1.0f, "a width of 0 becomes the smallest width");

			fs::remove(file, error);
			check(!changed.save(fs::temp_directory_path() / "wiwa_no_such_folder_xyz" / "settings.json"), "saving where it cannot fails without a crash");
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
		check_scene_gizmos(check);
		check_gizmo_settings(check);

		if (check.Failures == 0)
		{
			WI_GIZMO_LOG(Info, "Gizmo smoke test passed");
		}
		return check.Failures == 0;
	}
}
