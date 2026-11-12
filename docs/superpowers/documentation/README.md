# Documentation

Reference for Claude (and people) on how each area works **now**, and why. One file per area, rewritten when the area changes. They are not dated plans: history is in git.

CLAUDE.md has the one-paragraph map of every area; read the matching doc here before changing that area.

| Doc | Area |
| --- | --- |
| [assets.md](assets.md) | Asset system, importers, packing, build settings, project settings files |
| [scene.md](scene.md) | Components, reflection, `.wscene`, prefabs, async loading, scene settings, play-mode clone |
| [rendering.md](rendering.md) | Render thread, GPU resources, copy queue, frame passes, post chain, TAA, shaders, debug draw, picking |
| [materials.md](materials.md) | `.wmat` materials and `.wshader` surface shaders |
| [decals.md](decals.md) | Projected decals: the `Decal` component, lifetime, the decal pass and targets, `Decal.spawn` |
| [vfx.md](vfx.md) | GPU particles: `.wfx` effects, `ParticleSystem`, simulation and render passes, quality limits, the VFX editor, `Vfx` Lua API |
| [audio.md](audio.md) | Audio module, the Null, MiniAudio and Wwise backends, soundbanks and clips, emitters, the Audio panel |
| [scripting.md](scripting.md) | Lua runtime, lifecycle, hot reload, play mode, LuaLS stubs (user API: `docs/scripting.md`) |
| [physics.md](physics.md) | Jolt integration, physics thread, components, events, queries |
| [animation.md](animation.md) | Skeletons, clips, the Animator, GPU skinning, the Lua API, the Animation panel |
| [views.md](views.md) | Offscreen scene views: previews, UI widgets, readback; `ViewHost`, `ViewImage` |
| [behavior-trees.md](behavior-trees.md) | Behaviour trees (`.wbt`), the `AIModule`, Lua leaves, perception, the tree editor |
| [navigation.md](navigation.md) | Navmesh baking (Recast), queries and crowd agents (Detour), `.wnav`, the Navigation panel, the `Nav` Lua API |
| [input.md](input.md) | Raw input, actions, rebinding, `.winput`, UI source |
| [ui.md](ui.md) | Game UI: `.wuipanel`, layout, interaction, rendering, UI editor |
| [project-settings.md](project-settings.md) | `project.wproject`, the Settings page and its sections |
| [editor.md](editor.md) | Editor panels, gizmos, play mode, export, performance stats |

Files with a date prefix (`2026-11-..-<area>-design.md`) are the design records written before an area was built. They keep the reasoning and the alternatives that were rejected, but not the current state: the doc of the area above is.

## Rules for these docs

- Describe what exists. Do not record plans, task lists, acceptance steps or phases.
- Keep decisions and the rejected alternative when the reason is not obvious from the code.
- Keep invariants and traps (thread rules, register ranges, format versions, ordering) - they are what breaks silently.
- Do not repeat what the code says (field lists, signatures). Point at the file.
- When code and a doc disagree, fix the doc in the same change.
- Plans (`docs/superpowers/plans/`) are git-ignored scratch for the implementer and may be stale. Do not trust them over the code.
