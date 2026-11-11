# Audio integration (Wwise first, vendor-modular) - design

Status: draft for review. Branch `audio`.

## Goal

A vendor-neutral audio layer in the engine with Wwise as the first backend, so another vendor (e.g. FMOD) can be added by implementing one interface, with no change to components, Lua, the editor or game code. First cut includes runtime, components, Lua API, soundbank assets, and editor tooling (Audio panel, event browser, auditioning).

## Decisions (agreed)

- Scope: runtime + components + Lua + editor tooling.
- Backend chosen at run time from project settings (`Null`, `Wwise`). Approach "thin event-based vendor interface".
- SDK found through the `WWISESDK` env var (like `VULKAN_SDK`). If unset, premake does not define `WI_AUDIO_WWISE` and the Wwise backend is not compiled. This is a feature toggle, not an editor-vs-game switch.
- Wwise 2024.1.x or newer. Not found on the dev machine at design time: the Wwise backend cannot be compiled or verified until `WWISESDK` is set.
- Authoring (Wwise project, events, banks) stays in the Wwise tool. The editor only imports generated banks and metadata.

## Architecture

`engine/src/audio/`:

- `audio_module.*`: `IModule`, update priority above 10 (after scripts 0 and animation 10), so events posted and transforms moved this frame are sent in the same frame; exact value fixed in the plan. Main thread only. Calls the backend once per frame. Never touched from render or physics threads.
- `audio_backend.h`: `IAudioBackend` and plain-data types (`AudioId` hashed name, `EmitterHandle`, `AudioVec3`). No vendor type appears here.
- `null_backend.*`: logging no-op with a recorded call log (used by smokes and as fallback).
- `wwise/wwise_backend.*`: compiled only with `WI_AUDIO_WWISE`. `JPH::`-style containment rule: `AK::` stays inside `engine/src/audio/wwise/`.

`IAudioBackend` (event-centric): init/shutdown, update, set listener transform, register/unregister emitter, set emitter transform, post event (name or id, emitter) returning a playing handle, stop event / stop emitter / stop all, set RTPC (global or emitter), set switch, set state, load/unload bank (from memory), query loaded banks and playing events.

Wwise owns an audio thread; the module only calls `RenderAudio()` per frame from main.

### Components (`WI_COMPONENT`/`WI_FIELD`)

- `AudioListener`.
- `AudioEmitter`: default event, play on start, attenuation scale.

Serializer, inspector, overrides, clone, Lua and stubs derive from the field description.

### Play rules

Audio runs only while the game clock plays. Stop silences everything. Editor Play runs on a clone of the edit scene: clone emitters register on creation and unregister on destruction.

### Assets

New `AssetType` `SoundBank`, appended before `Last`. Banks and `Init.bnk` are CPU-side bytes in the pak, loaded through `IAssetSource` and handed to the backend from memory. `AudioBankInfo` (events, RTPCs, switch groups, states, buses, per-event max attenuation and duration) is cooked next to the bank. Assets store `AssetRef`, never handles.

### Lua

`Audio.post(event, [entity])`, `Audio.stop`, `Audio.set_rtpc`, `Audio.set_switch`, `Audio.set_state`, `Audio.load_bank`. Documented in `docs/scripting.md`; LuaLS stubs generated.

### Project settings

New Audio page: backend, bank list, memory pool size, Wwise language. Round trip like other pages.

## Editor (`editor/src/audio`, `editor/src/asset_pipeline`)

- `SoundBankImporter` (no SDK dependency): imports `.bnk`, parses `SoundbanksInfo.json` into `AudioBankInfo`, registered like other importers (`.meta` GUIDs, packer, cook). The game never parses JSON.
- Audio panel: backend and status, loaded banks, live playing events (Wwise), event browser with search, Audition button (preview emitter at editor camera, works in edit mode through the module, stops on panel close).
- Inspector: event fields on `AudioEmitter` use a picker fed by the event browser; unknown typed names allowed with a warning.
- Debug draw: emitter icons and attenuation radius.

## Error handling

- Wwise selected but not compiled, or init fails (bad pool, missing `Init.bnk`): log an error once, fall back to Null, show it in the Audio panel. Never silent.
- Unknown event or bank: log once per name, return an invalid handle, no throw.
- Missing language bank: warning.
- Shutdown: stop emitters, unload banks, terminate the engine, all before the window and renderer go away.

## Testing

Smokes follow CLAUDE.md: trusted only after seen to fail under a negative control.

- `--audio-smoke` (editor): Null backend call log (register, play, stop, RTPC, switch, state, transforms); `SoundbanksInfo.json` parsing on a fixture; cook/pack/load round trip; Stop silences, clone emitters unregister; Lua API and stubs; settings round trip. When built with `WI_AUDIO_WWISE`: real init with a tiny test bank, post an event, playing count rises, clean shutdown.
- `--audio-panel-smoke`: panel for 130 frames with ImGui asserts on.
- Demo: emitter and listener in `editor/assets/scenes/demo.wscene` (via `scripts/gen_demo_scene.py`), covered by `--demo-smoke`.

## Open items

- The user authors the Wwise project and generates the test banks (`Init.bnk`, a test bank, `SoundbanksInfo.json`). The plan will specify the folder layout and generation settings.
- Set `WWISESDK` on the build machine to verify the Wwise backend.
- Exact module priority (> 10) fixed in the plan.

## Docs

New `docs/superpowers/specs/audio.md` (what exists, why, traps) when implemented, plus a row in the CLAUDE.md area table.
