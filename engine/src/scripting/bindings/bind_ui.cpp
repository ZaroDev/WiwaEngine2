#include <pch.h>
#include "bindings.h"

#include <algorithm>
#include <cctype>

#include <ui/ui_module.h>
#include <video/video_module.h>
#include "../ui_handlers.h"

namespace wiwa
{
	namespace
	{
		// A panel as a script holds it: the entity of the UICanvas. Everything is looked up through the UI module on each use, so a handle
		// to a canvas that is gone, or to a scene that was replaced, is just not there any more.
		struct LuaUiPanel
		{
			EntityGuid Canvas{};
			ScriptContext* Context = nullptr;
		};

		struct LuaUiWidget
		{
			EntityGuid Canvas{};
			UiId Id = 0;
			ScriptContext* Context = nullptr;
		};

		UiModule& ui_of(ScriptContext& context)
		{
			if (context.Ui == nullptr)
			{
				throw sol::error("there is no UI module");
			}
			return *context.Ui;
		}

		// Null when the canvas, its scene or its panel is not there (yet).
		UiPanelInstance* instance_for(ScriptContext& context, const EntityGuid& canvas)
		{
			if (context.Ui == nullptr || context.ActiveScene == nullptr)
			{
				return nullptr;
			}
			UiPanelInstance* instance = context.Ui->instance_for(*context.ActiveScene, canvas);
			return instance != nullptr && instance->ready() ? instance : nullptr;
		}

		UiWidget* widget_for(ScriptContext& context, const LuaUiWidget& widget)
		{
			UiPanelInstance* instance = instance_for(context, widget.Canvas);
			return instance != nullptr ? instance->data().find(widget.Id) : nullptr;
		}

		std::string normalized(std::string_view name)
		{
			std::string result;
			for (const char c : name)
			{
				if (c != '_')
				{
					result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
			}
			return result;
		}

		// A script writes text, font_size, world_anchor; the field is Text, FontSize, WorldEntity.
		const UiField& field_named(std::string_view name)
		{
			std::string wanted = normalized(name);
			if (wanted == "min") { wanted = "minvalue"; }
			else if (wanted == "max") { wanted = "maxvalue"; }
			else if (wanted == "worldanchor") { wanted = "worldentity"; }

			for (const UiField& field : ui_fields())
			{
				if (normalized(field.Name) == wanted)
				{
					return field;
				}
			}
			throw sol::error("a widget has no property '" + std::string(name) + "'");
		}

		std::optional<UiEventKind> event_kind(const std::string& name)
		{
			if (name == "click") { return UiEventKind::Click; }
			if (name == "value_changed") { return UiEventKind::ValueChanged; }
			if (name == "hover_enter") { return UiEventKind::HoverEnter; }
			if (name == "hover_exit") { return UiEventKind::HoverExit; }
			if (name == "focus") { return UiEventKind::Focus; }
			if (name == "finished") { return UiEventKind::Finished; }
			return std::nullopt;
		}

		UiEventKind require_event_kind(const std::string& name)
		{
			const std::optional<UiEventKind> kind = event_kind(name);
			if (!kind)
			{
				throw sol::error("unknown UI event '" + name + "' (click, value_changed, hover_enter, hover_exit, focus, finished)");
			}
			return *kind;
		}

		UiNav require_nav(const std::string& name)
		{
			if (name == "up") { return UiNav::Up; }
			if (name == "down") { return UiNav::Down; }
			if (name == "left") { return UiNav::Left; }
			if (name == "right") { return UiNav::Right; }
			if (name == "submit") { return UiNav::Submit; }
			if (name == "cancel") { return UiNav::Cancel; }
			throw sol::error("unknown navigation '" + name + "' (up, down, left, right, submit, cancel)");
		}

		VideoModule& video_of(ScriptContext& context)
		{
			if (context.Video == nullptr)
			{
				throw sol::error("there is no video module");
			}
			return *context.Video;
		}

		// The data of a Video widget, null when the widget (or its canvas) is gone; a widget of another kind is a Lua error.
		const UiWidget* video_widget_for(ScriptContext& context, const LuaUiWidget& widget, const char* method)
		{
			const UiWidget* data = widget_for(context, widget);
			if (data != nullptr && data->Kind != WidgetKind::Video)
			{
				throw sol::error(std::string(method) + ": the widget is not a Video widget");
			}
			return data;
		}

		const char* video_state_name(VideoState state)
		{
			switch (state)
			{
			case VideoState::Idle: return "idle";
			case VideoState::Loading: return "loading";
			case VideoState::Playing: return "playing";
			case VideoState::Paused: return "paused";
			case VideoState::Ended: return "ended";
			case VideoState::Failed: return "failed";
			}
			return "idle";
		}

		sol::object nil_object(sol::this_state state)
		{
			return sol::object(sol::state_view(state), sol::lua_nil);
		}

		void bind_panel(sol::state& lua, ScriptContext& context)
		{
			lua.new_usertype<LuaUiPanel>("UiPanel", sol::no_constructor,
				"find", [&context](const LuaUiPanel& panel, const std::string& path, sol::this_state state) -> sol::object
				{
					UiPanelInstance* instance = instance_for(context, panel.Canvas);
					if (instance == nullptr)
					{
						return nil_object(state);
					}
					const std::optional<UiId> id = instance->find(path);
					return id ? sol::make_object(state, LuaUiWidget{ panel.Canvas, *id, &context }) : nil_object(state);
				},
				"ready", sol::property([&context](const LuaUiPanel& panel) { return instance_for(context, panel.Canvas) != nullptr; }),
				"visible", sol::property(
					[&context](const LuaUiPanel& panel, sol::this_state state) -> sol::object
					{
						UiPanelInstance* instance = instance_for(context, panel.Canvas);
						return instance != nullptr ? sol::make_object(state, instance->visible()) : nil_object(state);
					},
					[&context](const LuaUiPanel& panel, bool visible)
					{
						if (UiPanelInstance* instance = instance_for(context, panel.Canvas))
						{
							instance->set_visible(visible);
						}
					}));
		}

		void bind_widget(sol::state& lua, ScriptContext& context, const sol::table& methods)
		{
			lua.new_usertype<LuaUiWidget>("UiWidget", sol::no_constructor,
				sol::meta_function::index, [&context, methods](const LuaUiWidget& widget, sol::stack_object key, sol::this_state state) -> sol::object
				{
					if (key.get_type() != sol::type::string)
					{
						throw sol::error("a widget is indexed by property name");
					}
					const std::string name = key.as<std::string>();

					const sol::object method = methods[name];
					if (method.valid() && method.get_type() != sol::type::lua_nil)
					{
						return method;
					}

					const UiField& field = field_named(name);
					const UiWidget* data = widget_for(context, widget);
					if (data == nullptr)
					{
						return nil_object(state);
					}
					return field_to_lua(sol::state_view(state), field.Get(*data), field, FieldLink{ EntityHandle{ EntityGuid{}, &context }, std::string{}, 0 });
				},
				sol::meta_function::new_index, [&context](const LuaUiWidget& widget, sol::stack_object key, sol::stack_object value)
				{
					if (key.get_type() != sol::type::string)
					{
						throw sol::error("a widget is indexed by property name");
					}
					const std::string name = key.as<std::string>();
					const UiField& field = field_named(name);
					if (field.Name == "Name")
					{
						throw sol::error("name cannot be changed: scripts find widgets by it");
					}

					FieldValue converted;
					std::string error;
					if (!field_from_lua(field, sol::object(value), converted, error))
					{
						throw sol::error(name + ": " + error);
					}

					if (UiWidget* data = widget_for(context, widget))
					{
						if (!field.Set(*data, converted))
						{
							throw sol::error(name + ": the value is not valid for the property");
						}
					}
				});
		}

		sol::table make_widget_methods(sol::state& lua, ScriptContext& context)
		{
			sol::table methods = lua.create_table();

			// widget:on("click", function(self, value) ... end): self is the script instance, value is the new value of value_changed
			methods.set_function("on", [&context](const LuaUiWidget& widget, const std::string& event, sol::object function)
				{
					const UiEventKind kind = require_event_kind(event);
					if (function.get_type() != sol::type::function)
					{
						throw sol::error("on: the handler is not a function");
					}
					if (context.UiHandlers == nullptr || context.UiHandlers->current() == nullptr)
					{
						throw sol::error("on: handlers are registered from a script hook (on_init, on_update, ...)");
					}
					if (widget_for(context, widget) == nullptr)
					{
						return; // the widget is gone: nobody will ever raise this event
					}

					UiHandler handler;
					handler.Canvas = widget.Canvas;
					handler.Widget = widget.Id;
					handler.Kind = kind;
					handler.Function = sol::protected_function(function);
					handler.Owner = *context.UiHandlers->current();
					context.UiHandlers->add(std::move(handler));
				});

			// widget:off("click") removes this script's handlers for the event; widget:off("click", fn) only that function
			methods.set_function("off", [&context](const LuaUiWidget& widget, const std::string& event, sol::optional<sol::object> function)
				{
					const UiEventKind kind = require_event_kind(event);
					if (context.UiHandlers == nullptr || context.UiHandlers->current() == nullptr)
					{
						throw sol::error("off: handlers are removed from a script hook (on_init, on_update, ...)");
					}
					context.UiHandlers->remove(*context.UiHandlers->current(), widget.Canvas, widget.Id, kind, function.value_or(sol::object{}));
				});

			// A Video widget plays its video (the video module; one video at a time). All of them do nothing, and read nil, for a widget whose canvas
			// is gone; on a widget that is not a Video they are Lua errors.
			methods.set_function("video_play", [&context](const LuaUiWidget& widget) -> bool
				{
					return video_widget_for(context, widget, "video_play") != nullptr && video_of(context).play(VideoKey{ widget.Canvas, widget.Id }, context.ActiveSceneRef);
				});
			methods.set_function("video_pause", [&context](const LuaUiWidget& widget)
				{
					if (video_widget_for(context, widget, "video_pause") != nullptr)
					{
						video_of(context).pause(VideoKey{ widget.Canvas, widget.Id });
					}
				});
			methods.set_function("video_stop", [&context](const LuaUiWidget& widget)
				{
					if (video_widget_for(context, widget, "video_stop") != nullptr)
					{
						video_of(context).stop(VideoKey{ widget.Canvas, widget.Id });
					}
				});
			methods.set_function("video_seek", [&context](const LuaUiWidget& widget, double seconds)
				{
					if (video_widget_for(context, widget, "video_seek") != nullptr)
					{
						video_of(context).seek(VideoKey{ widget.Canvas, widget.Id }, seconds);
					}
				});
			methods.set_function("video_state", [&context](const LuaUiWidget& widget, sol::this_state state) -> sol::object
				{
					if (video_widget_for(context, widget, "video_state") == nullptr)
					{
						return nil_object(state);
					}
					return sol::make_object(state, std::string(video_state_name(video_of(context).state(VideoKey{ widget.Canvas, widget.Id }))));
				});
			methods.set_function("video_time", [&context](const LuaUiWidget& widget, sol::this_state state) -> sol::object
				{
					if (video_widget_for(context, widget, "video_time") == nullptr)
					{
						return nil_object(state);
					}
					return sol::make_object(state, video_of(context).time(VideoKey{ widget.Canvas, widget.Id }));
				});
			methods.set_function("video_duration", [&context](const LuaUiWidget& widget, sol::this_state state) -> sol::object
				{
					if (video_widget_for(context, widget, "video_duration") == nullptr)
					{
						return nil_object(state);
					}
					return sol::make_object(state, video_of(context).duration(VideoKey{ widget.Canvas, widget.Id }));
				});
			methods.set_function("video_error", [&context](const LuaUiWidget& widget, sol::this_state state) -> sol::object
				{
					if (video_widget_for(context, widget, "video_error") == nullptr)
					{
						return nil_object(state);
					}
					return sol::make_object(state, video_of(context).error(VideoKey{ widget.Canvas, widget.Id }));
				});

			methods.set_function("clear_world_anchor", [&context](const LuaUiWidget& widget)
				{
					if (UiWidget* data = widget_for(context, widget))
					{
						data->WorldEntity = EntityGuid{};
					}
				});
			return methods;
		}

		sol::table make_input_table(sol::state& lua, ScriptContext& context)
		{
			sol::table input = lua.create_table();
			input.set_function("move_to", [&context](float x, float y) { ui_of(context).scripted_input().move_to({ x, y }); });
			input.set_function("clear_pointer", [&context]() { ui_of(context).scripted_input().clear_pointer(); });
			input.set_function("press", [&context](int button) { ui_of(context).scripted_input().press(button); });
			input.set_function("release", [&context](int button) { ui_of(context).scripted_input().release(button); });
			input.set_function("nav", [&context](const std::string& name) { ui_of(context).scripted_input().nav(require_nav(name)); });
			input.set_function("scroll", [&context](float amount) { ui_of(context).scripted_input().scroll(amount); });
			return input;
		}
	}

	void bind_ui(ScriptRuntime& runtime, ScriptContext& context)
	{
		sol::state& lua = runtime.state();

		const sol::table methods = make_widget_methods(lua, context);
		bind_panel(lua, context);
		bind_widget(lua, context, methods);

		sol::table ui = lua.create_table();
		// UI.panel(entity): the panel shown by the entity's UICanvas, or nil if it has none
		ui.set_function("panel", [&context](const EntityHandle& entity, sol::this_state state) -> sol::object
			{
				ui_of(context);
				const entt::entity resolved = entity.resolve();
				if (resolved == entt::null)
				{
					throw sol::error("the entity no longer exists");
				}
				if (!context.ActiveScene->registry().all_of<UICanvas>(resolved))
				{
					return nil_object(state);
				}
				return sol::make_object(state, LuaUiPanel{ entity.Guid, &context });
			});
		ui["input"] = make_input_table(lua, context);
		runtime.sandbox()["UI"] = ui;
	}
}
