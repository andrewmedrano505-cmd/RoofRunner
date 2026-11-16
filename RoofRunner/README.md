# ROOF RUNNER — a Mirror's Edge-style parkour game for PS Vita

First-person rooftop free-running in a white, hazy city. Follow the **red** objects
("runner vision") from the start roof to the finish gate. Build momentum, vault, slide,
wall-run, wall-jump and roll out of big drops. Best time is saved to `ux0:data/ROOFRUN/best.txt`.

## Controls
| Input | Action |
|---|---|
| Left stick / Right stick | Move / look |
| R or Cross | Jump. Hold in the air to grab ledges and to wall-run along tall walls |
| L | Slide (while running), crouch (when slow). Tap just before landing from a big drop to roll |
| Triangle | Wall-jump (while wall-running) |
| Start / Select | Restart level / respawn at last checkpoint |
| Start + Select | Quit |

Tips: speed builds the longer you run. Vault red boxes without stopping. For the first
wall-run, line up on the roof, jump at the edge, hold jump and steer into the wall.
Releasing and re-pressing jump within 0.3s of leaving a wall also wall-jumps.

## Building
Requires a hacked Vita (HENkaku/Enso) to run homebrew.

1. Install [VitaSDK](https://vitasdk.org) and set `VITASDK`.
2. Install [vitaGL](https://github.com/Rinnegatamante/vitaGL) (`make install`) plus its deps (vitashark, mathneon, SceShaccCg, taihen stubs).
3. Build:
   ```
   mkdir build && cd build
   cmake ..
   make
   ```
4. Copy `RoofRunner.vpk` to the Vita and install with VitaShell.

If the linker complains about a missing `Sce*_stub`, check vitaGL's README for the current link
list for your version and edit `target_link_libraries` in `CMakeLists.txt`.

## Project layout
- `src/main.c` – whole game: level, physics, parkour moves, renderer, HUD
- `CMakeLists.txt` – VitaSDK build

## Tweaking
Level layout is `buildLevel()`; movement constants are in `step()`. Add boxes with `roof()`,
`prop()` (red, runner-vision) and `slideBar()`.
