# Audio

Code: `engine/src/audio` (module, backend interface, Null backend, scene sync, bank metadata, settings blob), `engine/src/audio/wwise` (the Wwise backend, built only with `WWISESDK`), Lua `scripting/bindings/bind_audio.cpp`, importer `editor/src/asset_pipeline/importers/sound_bank_importer.cpp`, editor `editor/src/audio` (`audio_edit_model.*` (`audio_event_rows`, `audio_event_unknown`, `audio_attenuation_radius`: the event list, unknown-event check and emitter radius, without ImGui), smoke), `editor/src/panels/audio_panel.*`, `audio_event_picker.*`, `editor/src/settings/audio_section.*`. Verified by `--audio-smoke` and `--audio-panel-smoke`. Lua API: `docs/scripting.md`. Design record: `audio-wwise-design.md`.

## Shape

`AudioModule` (priority 20, after the scripts and animation; added before `ScriptingModule` because scripts read its pointer in their init) is main thread only. It owns one `IAudioBackend`. Everything else (components, Lua, the editor) talks to that interface; **`AK::` never leaves `engine/src/audio/wwise/`**. Adding a vendor (FMOD) means one new `IAudioBackend`, an `AudioBackendKind` value (append, it is written to files) and a case in `AudioModule::select_backend`.

`IAudioBackend` is event-centric (events, RTPCs, switches, states, banks, emitters, one listener): the model Wwise and FMOD share. Names are the vendor's own. The **sound of a video does not go through `IAudioBackend`** (video.md): `VideoAudioOut` plays it on a miniaudio device of its own, so it is heard under Wwise, MiniAudio and Null alike. `NullAudioBackend` plays nothing and records every call (`calls()`); it is the fallback and what the smokes run on.

## Backend choice and the SDK

`WWISESDK` (env var, like `VULKAN_SDK`) switches `WI_AUDIO_WWISE` on for the engine, editor and game (premake `WwiseDefines()` / `WwiseLinks()`). Without it the Wwise backend is not compiled. This is a feature toggle, never an editor-vs-game decision.

**No silent fallback.** Selecting Wwise when it is not built in, or when it fails to start, logs an error, keeps the reason in `AudioModule::last_error()` (the Settings page and the Audio panel show it) and runs the Null backend. `select_backend` returns false.

The Wwise backend creates a stream manager but **no streaming device**: banks load from memory (`LoadBankMemoryView`), so generate them with their media embedded, not streamed. It compiles and links against **Wwise 2025.1.12** (`WWISESDK=C:\Audiokinetic\Wwise2025.1.12.9287\SDK`) but has not been run. 2025.1 differences from older SDKs that the code follows: there is no separate music engine (no `AkMusicEngine.h`/lib), `AkMemoryMgrModule.h` replaces `AkModule.h`, `AkMemSettings` has no size limit (so the **memory pool setting is not applied** to Wwise), and the event callback takes the event info and cookie as separate arguments.

## MiniAudio backend

`engine/src/audio/mini` (`MiniAudioBackend`, on the vendored single-header `engine/vendor/miniaudio`, 0.11.22, public domain / MIT-0; its implementation is in `miniaudio_impl.cpp`, which premake builds without the PCH) is a backend that needs no vendor SDK and is always built in. Select it in Settings, Audio.

- An **event is the name of a sound file** without its extension, in any letter case: `Audio.post("Play_Door")` plays the clip `Play_Door.wav`. A name ending in `_loop` loops. Formats: **wav, mp3, flac, ogg** (Ogg Vorbis through the vendored `stb_vorbis.c`, public domain, compiled into the miniaudio implementation file).
- Sounds are `AssetType::AudioClip` assets (`AudioClipImporter`, append-only enum value after `SoundBank`): the encoded bytes are cooked as they are, so they live in the Library and in the pak like any asset. The backend finds a clip by the catalog (`AssetManager::catalog_entries`, indexed by lower-case file stem; rebuilt at most once a second on a miss) and loads it asynchronously through `AssetManager::load`, so a first post may start a few frames late (the id is valid at once).
- **Streaming:** each playing sound owns a `ma_decoder` over the clip's encoded bytes, so mp3 and flac are decoded a few thousand frames at a time on the audio thread and never as a whole. The bytes themselves are held in memory (loaded from the Library or the pak); true file streaming would need a random-access pak read.
- **Packing:** nothing refers to a clip (events name them), so when the audio settings choose MiniAudio the packer adds every `AudioClip` to `AlwaysInclude`.
- Positioned sounds use miniaudio's 3D spatializer (linear attenuation, heard to 50 units times the emitter's `AttenuationScale`, as clips carry no attenuation data). miniaudio is right-handed with -Z forward like the engine, so no axis flip (Wwise needs one). Global sounds are not spatialized.
- Parameters: `Volume` (linear; global or per emitter) and `Pitch` (1 = as recorded); any other RTPC, switches and states are ignored. There are no banks (`load_bank` succeeds and does nothing) and no event metadata, so the editor's event browser is empty for this backend: type event names.
- The backend with `noDevice` (and `render(frames)`) is how `--audio-smoke` plays clips on a machine without a sound card.

## Assets and settings

- `AssetType::SoundBank` (appended before `Last`): `SoundBankData { Name, Bytes, Info, HasInfo }`. `SoundBankImporter` cooks a `.bnk` as it is and, if a `SoundbanksInfo.json` (any letter case) is in the same folder, parses it into `AudioBankInfo` (events with max attenuation, duration and looping, RTPCs, switch and state groups, buses). The JSON is a source dependency; a missing, broken or non-listing file is an import warning and the bank is cooked without metadata. The game never parses JSON.
- Project settings `audio.wsettings` (editor working directory; backend, banks, memory pool KB, language), packed under `AUDIO_SETTINGS_GUID` (`~0, ~0-5`); the packager always packs the banks it names. A game reads the blob in `AudioModule::init`; the editor applies the file once the first import scan has cooked the banks (`EditorModule::update`).
- `Init` is always loaded first (a Wwise bank cannot load before it), whatever order the settings list. Loading is asynchronous; `AudioModule::update` hands banks to the backend in request order.

## Components and play mode

`AudioListener { Active }` and `AudioEmitter { Event, PlayOnStart, AttenuationScale }` (the `Event` field carries `FieldMeta::audio_event()`, which the inspector draws as an event picker). `AudioSync` keeps a backend in step with a scene: an emitter per `AudioEmitter` (created, moved, scaled, destroyed with the entity or the component), the listener from the first active `AudioListener`, else the primary camera. Forward is -Z.

A **world** exists only while a scene plays. With auto start (default, the game and the editor) the module follows `SceneModule`'s active scene while the game clock plays, and stops when another scene replaces it: the editor's Play makes the clone active and plays the clock, Stop shows the edit scene again, so no call from the play-mode host is needed (and its call sequence, checked by the script smoke, did not change). Stopping a world destroys its emitters and calls `stop_all`, so sounds posted globally also end. A paused clock suspends the backend. `AudioModule::emitter_for` starts the world on demand for a script that posts in its first frame.

## Lua

`Audio.post / stop / stop_entity / stop_all / set_rtpc / set_switch / set_state / load_bank / backend`. Posting from an entity needs an `AudioEmitter` on it (its `Event` may be empty): a temporary emitter would have no end-of-event signal and leak.

## Editor

- **Audio panel** (Window menu, closed by default): backend and its error, loaded banks, playing count, event browser (search over the loaded banks' metadata) with an Audition button. `AudioModule::audition` uses an emitter of its own at the listener origin, **only while no world plays**, and stops when a world starts, when the panel closes or on Stop.
- **Inspector**: `AudioEmitter.Event` is a text box plus a list of known events; a name no loaded bank lists keeps working and gets a `(!)` marker. With no metadata loaded nothing is flagged.
- **Gizmos** (`GizmoSettings::Audio`, "Audio emitters" in the viewport menu): a marker and pick icon per emitter, and for the selected one a sphere of `MaxAttenuation * AttenuationScale`.
- **Settings, Audio**: backend, soundbanks (a list of `.bnk` assets), memory pool, language. Every change saves and re-applies the settings, which recreates the backend and reloads the banks.

## Traps

- The Wwise-only checks in `--audio-smoke` run only in a `WWISESDK` build, and the playback ones need `editor/assets/audio/Init.bnk` and `Test.bnk` (events `Play_Test`, a short one-shot, and `Play_TestLoop`, looping; RTPC `TestVolume`; switch group `TestSwitch` with `A`, `B`) generated with the JSON info file enabled. Without the banks they log a **skipped** warning; read the log, a passing smoke there is not a playback check.
- The sound engine is process-global: `WwiseAudioBackend::init` refuses a second instance. The Wwise checks of `--audio-smoke` create their own backend, so run them with the project backend set to Null (or no `audio.wsettings`).
- Banks reach the backend only when every requested bank has loaded (Init first, the rest in listed order), and the auto-started world waits for that, so a PlayOnStart event does not fire before its bank exists. `Audio.post` from a script in its first frame can still precede the banks.
- The Null backend keeps only the newest `MAX_CALLS` calls; checks that read `calls()` must look before that many calls pass.
- Positions and directions are flipped in Z for Wwise (right-handed engine, left-handed Wwise).
- `Audio.post` before the world exists starts the world; the first `AudioSync::update` then finds the emitter already made.
- A paused game skips `AudioSync::update`, so emitters that move while paused catch up on resume.
- The demo (`editor/assets/audio/clips`, CC0 sounds fetched by `scripts/gen_demo_scene.py`, credits in the folder's `LICENSE.txt`) plays through MiniAudio: `editor/audio.wsettings` selects it. The `Audio demo` entity (at the beacon, `AttenuationScale` 4) plays `Amb_Hall_loop` on start and its script `audio_demo.lua` posts `Beacon_Blip` every 4 s; the animated warrior (`AudioEmitter` with an empty event) posts `Footstep_1..3` from its "footstep" clip events; the physics hopper posts `Impact_Soft` on a hard hit; the HUD buttons post `UI_Click`. `--demo-smoke` checks the calls on the Null backend, and that something played on MiniAudio (or Wwise with banks). Under Wwise the same event names must exist in your banks.
