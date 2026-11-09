# Main menu scene: brass and plum, at golden hour

Status: design approved 2026-11-09, not implemented. Not committed (spec/plan docs of this kind are not committed). Extends `2026-11-09-game-polish-design.md` (section D).

## Goal

The front end of the demo game is a **dedicated scene** (`main_menu.wscene`, the default scene of the build) with its own diorama, camera and UI (title, hero select, loading); Options is its own UI panel shared with the level, whose pause, dead and won screens use the same design. The scene is: the courtyard at golden hour is the background, the two heroes stand in lantern light playing their idle clip, and the UI sits over it. It must look intentional, not default.

## Tokens

- Color: dusk plum `#171220` (panels, 60-90% alpha), bone `#EDE3CF` (text), brass `#D9A856` (the only accent: title, focus, active tab, filled bars), ember `#C4492F` (danger only: HP, boss, "You died"), dusk haze `#6B5B7B` (secondary text, dividers).
- Type: Almendra SC (OFL, downloaded by `gen_game.py` into `game/fonts/`) for the title and screen headings; Lato (the project font) for everything read or clicked. Sentence case, plain verbs, one colour per headline.
- No gradients or images in the UI: the dark band behind the left column is three stepped translucent panels. The only automatic motion is the camera.

## Screens

- **Title:** left-aligned column (title in display type, a one-line tagline, Play, Options, Quit as text rows with a brass bar on hover/focus, no boxes), version bottom-left, controls hint bottom-right. The camera drifts on a slow arc round the stage heroes, who sit right of centre.
- **Hero select:** the camera glides to a front view of the heroes; under each one a card with its name, role line, three stat bars (Power, Reach, Speed) and a "Choose <hero>" button; Back below.
- **Options:** a right-hand drawer (760 px) over a light dim, the scene visible on the left; tabs Graphics / Controls / Game as text with the active tab in brass; rows are flat translucent bands with the label left and the control right; Back at the foot.
- **Pause:** the left column of the title (Resume, Options, Restart level, Main menu). **Dead / Won:** centred display text ("You died" in ember, "Victory" in brass) over a dim, with text-row buttons.
- The HUD keeps its layout and takes the palette.

## Scene

- Golden hour: low warm sun, bloom, vignette, ambient AO; applies to the whole level.
- Stage: a rug under the heroes, a warm spot key light, a cool rim light, two braziers.
- Camera (`camera_follow.lua`): menu poses (title arc, hero front) and the gameplay follow camera; a change between them blends position, pitch and yaw over about 0.9 s (the one orchestrated motion).

## Code

`gen_game.py` (`build_ui`, `build_level`, fonts, scene settings), `lib/menus.lua` (hover bars, active tab, version text), `camera_follow.lua`, `lib/game.lua` (`Game.stage`, `Game.pitch_of`), `selftest.lua`. Widget paths used by `menus.lua` stay as they are; the self test still checks that all of them exist.

## Verification

`--game-smoke` (menus step: the stage heroes play idle, the camera has reached the hero pose after the blend, the player is hidden; every widget path resolves, including the new `Accent` bars). Looks are not machine-checkable: the user plays it.
