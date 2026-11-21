# Benchmarking

Code: `engine/src/core/bench_recorder.*` (records), `game/src/game.cpp` (`--bench`), `scripts/bench_stress.py` (runs), `scripts/bench_report.py` (report).

## Flow

`wiwa_game --scene <scene> --bench [seconds] --bench-out result.json` loads the scene, turns vsync off, waits until the asset cache has had nothing pending for 2 s (at most 120 s), then records every frame for the seconds (default 20) and quits. Exit code 0 when the average FPS reached `--bench-target` (default 60), 1 when it did not, 2 when the result could not be written. `--bench-label <text>` names the run, `--bench-meta key=value` (repeatable) adds header lines to the report.

`BenchRecorder::sample` takes, once per frame: the main thread's real frame time (`Engine::on_update` gets **milliseconds**), the render thread's frame time and CPU time, the physics step, process RAM and VRAM, the renderer's counters (draw calls, visible instances, primitives per level of detail, scene size, cache size) and, per frame, the time of every module on the main thread (`PerfStats::modules`) and of every render pass on the GPU (`RenderSnapshot::PassMs`, the latest the render thread published). The file is `wiwa-bench/1` JSON: metadata, a summary and parallel arrays with one value per frame (about 400 KB a minute at 60 FPS). Memory and pass times are the newest values the engine had when the frame ran, so they are exact for the render thread and about a frame old for the main thread.

`python scripts/bench_report.py result.json [-o report.html] [--baseline before.json] [--fail-on-regression 5]` makes one self-contained HTML file (inline SVG, no scripts, light and dark). Sections: the verdict and what the run is bound by (main thread, render thread CPU, or GPU, with the numbers), the frame time over the run, the distribution, the threads, GPU hotspots (per pass) and main thread hotspots (per module), **what grows in the slow frames** (the slowest 5% against all frames: where to look first), memory, the worst frames and what took longest in each, draw calls and levels of detail, and with a baseline every metric and the passes and modules that changed most. `--fail-on-regression N` exits with 3 when the average FPS or the 1% low fell more than N percent.

`python scripts/bench_stress.py [--scene=stress|demo] [--seconds=20] [--api=...] [--label=text] [--baseline=last|file] [--fail-on-regression=N] [--open]` packs the scene in a temporary folder (the project's `game/assets.wpak` and `editor/build_settings.json` stay as they are), runs the game and writes `bench_results/<time>-<scene>-<label>/` (git-ignored): `result.json`, `report.html`, `run.log`. `--baseline=last` compares with the previous run of the same scene and API (`bench_results/<scene>-<api>-last.json`, updated after each run). The scene is `stress` (scripts/gen_stress_scene.py, see its docstring) or the `demo` hall.

## Reading a result

- The frame time is what a player gets; the GPU passes are timestamps on the render thread, so they add up to the render thread's GPU work, not to the frame when the main thread is the limit. The report says which one binds the run: a main thread bound run (modules sum to the frame) is not helped by cheaper shaders.
- A module's time includes waiting (the physics module waits for its thread), so a big module is a place to look, not always CPU work.
- Two runs of the same build on this machine differ by about 1% in average FPS (the stress scene: 60.8 and 61.2); treat smaller changes as noise, and compare 1% lows only over runs of at least 20 s.
- `--bench` is the game's flag: it needs a pack, so the editor's own frame rate is not measured here (the Performance panel shows that).

## Checks

`python scripts/bench_report.py --selftest` builds two synthetic runs and checks the percentile rule, that spikes are reported and attributed to the right pass, the regression detection, that every chart is well formed XML, that the baseline section appears only with a baseline, and that malformed files are refused with a reason.
