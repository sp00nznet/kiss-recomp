# Roadmap

## Done
- All 8 modules lifted (28,494 functions, 0 errors, no IDA) and linked into
  one host on `native32`.
- The menus, a new game and the in-engine intro; moving, turning and jumping
  in the first level, headless.
- Setup.cmd from a fresh clone to the shortcut.

## Next
- **Play the first level** for real: fire (mouse button 0 / Ctrl), mouse look
  (native32's synthetic mouse), through Madam Raven's tent into the first world.
- **Sound**: runs are muted (`+soundenable 0`); turn Miles on and check it,
  and the Interactive Music (`ima.dll`) score.
- **A conformance harness** (REPO_RULES section 9): fixed milestones (the
  dry-run bind counts, the profile screen, the main menu, the character
  select, the intro), each checked by a back-buffer shot, with a pass count
  in the README and a `QA` line in the netlab recipe.
- **Finish a farm build**: `netlab build kiss-recomp` (generated C at /Od);
  the testbox VM was unreachable when the recipe went in.
- **pcrecomp-ingame (with pcrecomp-lithtech2) into pcrecomp main**, then point Setup, the launcher
  and the netlab recipe at `main`.
- Key timing: menu presses are at fixed seconds and drift when a run is
  slower (`--record`); in-level ones can count from the level load (`L<S>`).

## Later
- The Direct3D hardware path (only the RGB software rasterizer has run:
  `+RGBRast 1`).
- Fullscreen, and the 32-bit desktop without the `16BITCOLOR` relaunch.
- Multiplayer and `Dedicated.exe` (not lifted).
- The launcher, `psycho.exe` (MFC; it only writes the command line).
- A game-file helper that builds the 1.13 folder from the CD and the two
  patches.

## Out of scope
- Shipping any game file, the patches, or the generated C.
