#include <pch.h>
#include "bindings.h"

#include <audio/audio_module.h>

namespace wiwa
{
	namespace
	{
		// A playing event as a script holds it: the result of Audio.post. It is a value; it stays valid to pass to Audio.stop after the event ended.
		struct LuaAudioHandle
		{
			PlayingId Id{};
		};

		AudioModule& audio_of(ScriptContext& context)
		{
			return require_module(context.Audio, "the audio module is not available");
		}

		// The emitter of an entity argument: invalid (the global scope) when there is none. An entity needs an AudioEmitter component to have a
		// position; the component's Event may be empty when it is only there to give the entity a voice.
		EmitterHandle emitter_of(ScriptContext& context, const sol::optional<EntityHandle>& entity)
		{
			if (!entity)
			{
				return {};
			}
			const entt::entity resolved = entity->resolve();
			if (resolved == entt::null)
			{
				throw sol::error("the entity no longer exists");
			}
			const EmitterHandle emitter = audio_of(context).emitter_for(context.ActiveSceneRef, resolved);
			if (!emitter.valid())
			{
				throw sol::error("the entity has no AudioEmitter component (add one, with an empty Event if it only plays what scripts post)");
			}
			return emitter;
		}
	}

	void bind_audio(ScriptRuntime& runtime, ScriptContext& context)
	{
		sol::state& lua = runtime.state();

		lua.new_usertype<LuaAudioHandle>("AudioHandle", sol::no_constructor,
			"is_valid", [](const LuaAudioHandle& handle) { return handle.Id.valid(); });

		sol::table audio = lua.create_table();

		// Posts an event (the vendor's name, such as Play_Footstep) from the entity, or globally without one. Returns a handle, or nil when the event did
		// not start (an unknown name is logged once).
		audio.set_function("post", [&context](const std::string& event, sol::optional<EntityHandle> entity) -> sol::optional<LuaAudioHandle>
			{
				AudioModule& module = audio_of(context);
				const PlayingId playing = module.backend().post_event(event, emitter_of(context, entity));
				if (!playing.valid())
				{
					return sol::nullopt;
				}
				return LuaAudioHandle{ playing };
			});

		audio.set_function("stop", [&context](const LuaAudioHandle& handle) { audio_of(context).backend().stop_playing(handle.Id); });

		audio.set_function("stop_entity", [&context](const EntityHandle& entity)
			{
				audio_of(context).backend().stop_emitter(emitter_of(context, entity));
			});

		audio.set_function("stop_all", [&context]() { audio_of(context).backend().stop_all(); });

		// A game parameter (RTPC): global, or of one entity.
		audio.set_function("set_rtpc", [&context](const std::string& name, float value, sol::optional<EntityHandle> entity)
			{
				audio_of(context).backend().set_rtpc(name, value, emitter_of(context, entity));
			});

		audio.set_function("set_switch", [&context](const std::string& group, const std::string& state, sol::optional<EntityHandle> entity)
			{
				audio_of(context).backend().set_switch(group, state, emitter_of(context, entity));
			});

		audio.set_function("set_state", [&context](const std::string& group, const std::string& state)
			{
				audio_of(context).backend().set_state(group, state);
			});

		// Loads a SoundBank asset (load_asset("assets/audio/Music.bnk")) in the background.
		audio.set_function("load_bank", [&context](const LuaAsset& asset)
			{
				if (context.Assets == nullptr)
				{
					throw sol::error("assets are not available");
				}
				if (!asset.Ref.valid())
				{
					throw sol::error("the asset is not valid");
				}
				audio_of(context).load_bank(*context.Assets, asset.Ref);
			});

		// "Null" or "Wwise": the backend that plays.
		audio.set_function("backend", [&context]() { return std::string(audio_backend_name(audio_of(context).active_kind())); });

		runtime.sandbox()["Audio"] = audio;
	}
}
