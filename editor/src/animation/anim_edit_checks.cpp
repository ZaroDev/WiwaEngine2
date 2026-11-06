#include <editor_common.h>
#include "anim_check.h"
#include "animation_edit_model.h"
#include "animation_preview.h"

namespace wiwa
{
	namespace
	{
		AnimationEditModel make_model()
		{
			return AnimationEditModel::from_settings({ { "Walk", 2.0f }, { "Run", 1.0f } }, nlohmann::json::parse(R"({
				"generatePhysicsShapes": true,
				"animationEvents": [
					{"clip":"Walk","time":1.5,"name":"right"},
					{"clip":"Walk","time":0.5,"name":"left"},
					{"clip":"Gone","time":0.1,"name":"orphan"}]})"));
		}

		std::string names(const AnimationEditModel& model, size_t clip)
		{
			std::string text;
			for (const AnimationEvent& event : model.clips()[clip].Events)
			{
				text += (text.empty() ? "" : ",") + event.Name;
			}
			return text;
		}

		void edit_model(AnimCheck& check)
		{
			AnimationEditModel model = make_model();
			check(model.clips().size() == 2 && names(model, 0) == "left,right" && names(model, 1).empty(), "events are read from the settings, sorted, for the clips the model has");
			check(!model.dirty() && !model.can_undo(), "a new model is clean");

			const size_t added = model.add_event(0, 1.0f, "  mid ");
			check(added == 1 && names(model, 0) == "left,mid,right", "an event is added in time order, its name trimmed");
			check(model.dirty() && model.can_undo(), "and that is a step");

			check(model.add_event(0, 5.0f, "late") == SIZE_MAX && model.add_event(0, -1.0f, "early") == SIZE_MAX, "a time outside the clip is refused");
			check(model.add_event(0, 0.5f, "   ") == SIZE_MAX && model.add_event(0, std::nanf(""), "nan") == SIZE_MAX, "an empty name and a time that is not a number are refused");
			check(model.add_event(7, 0.5f, "x") == SIZE_MAX, "a clip that does not exist is refused");

			const size_t moved = model.move_event(0, 0, 1.8f);
			check(moved == 2 && names(model, 0) == "mid,right,left", "moving an event past others gives its new place");
			check(model.move_event(0, 2, 9.0f) == SIZE_MAX, "moving it out of the clip is refused");

			check(model.rename_event(0, 0, "centre") && names(model, 0) == "centre,right,left", "rename");
			check(!model.rename_event(0, 0, "") && !model.rename_event(0, 9, "x"), "an empty name and a missing event are refused");

			check(model.remove_event(0, 1) && names(model, 0) == "centre,left", "remove");
			check(!model.remove_event(0, 5), "removing a missing event is refused");

			// every call above that worked is one step (an add, a move, a rename, a remove); going back through them all gives the first state
			size_t steps = 0;
			while (model.can_undo())
			{
				model.undo();
				steps++;
			}
			check(steps == 4 && names(model, 0) == "left,right" && !model.dirty(), "undo walks back to the clean state in four steps");
			model.redo();
			check(names(model, 0) == "left,mid,right" && model.dirty(), "redo goes forward again");
			model.add_event(1, 0.25f, "turn");
			check(!model.can_redo(), "a new edit drops what could be redone");

			// setting a value to what it already is makes no step
			const size_t before = model.can_undo() ? 1 : 0;
			model.rename_event(1, 0, "turn");
			model.undo();
			check(before == 1 && names(model, 1).empty(), "renaming to the same name made no step: one undo removed the add");
		}

		void edit_model_settings(AnimCheck& check)
		{
			AnimationEditModel model = make_model();
			model.add_event(1, 0.75f, "land");
			model.remove_event(0, 0);

			nlohmann::json settings = nlohmann::json::parse(R"({"generatePhysicsShapes":true,"materialFiles":{"a":"b"},"animationEvents":[]})");
			model.write_to(settings);
			check(settings.value("generatePhysicsShapes", false) && settings.contains("materialFiles"), "writing the events keeps the other settings");
			check(settings["animationEvents"].size() == 2, "and writes every event of every clip");
			bool found = false;
			for (const nlohmann::json& entry : settings["animationEvents"])
			{
				found = found || (entry["clip"] == "Run" && entry["name"] == "land" && std::abs(entry["time"].get<float>() - 0.75f) < 1e-6f);
			}
			check(found, "an entry is { clip, time, name }");

			// the events of a clip the model no longer has are not carried on
			check(settings["animationEvents"][0]["clip"] != "Gone" && settings["animationEvents"][1]["clip"] != "Gone", "events of clips that are gone are dropped when saved");

			AnimationEditModel empty = AnimationEditModel::from_settings({ { "Walk", 2.0f } }, nlohmann::json::object());
			nlohmann::json clean = nlohmann::json::parse(R"({"animationEvents":[{"clip":"Walk","time":1,"name":"x"}]})");
			empty.write_to(clean);
			check(!clean.contains("animationEvents"), "without events the key is not written");

			// reading junk does not crash
			AnimationEditModel junk = AnimationEditModel::from_settings({ { "Walk", 2.0f } }, nlohmann::json::parse(R"({"animationEvents":[1,"a",{"clip":"Walk"},{"clip":"Walk","time":"x","name":"y"},{"clip":"Walk","time":9,"name":"far"}]})"));
			check(junk.clips()[0].Events.empty(), "entries that are not usable are ignored");
		}
	}

	void run_edit_checks(AnimCheck& check)
	{
		const entt::entity a = static_cast<entt::entity>(1);
		const entt::entity b = static_cast<entt::entity>(2);
		check(preview_continues(a, a, true), "a preview goes on while its entity stays selected and the scene is edited");
		check(!preview_continues(a, b, true) && !preview_continues(a, entt::null, true), "it ends when the selection moves to another entity or to none");
		check(!preview_continues(a, a, false), "and when the game plays");
		check(!preview_continues(entt::null, a, true), "no preview, nothing to go on");
		edit_model(check);
		edit_model_settings(check);
	}
}
