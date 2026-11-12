# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[SemVer](https://semver.org/).

## [Unreleased]

### Added
- First boot of KISS Psycho Circus 1.13 on pcrecomp `native32`: 8 modules
  (`client.exe`, `server.dll`, `d3d.ren`, `cshell.dll`, `cres.dll`,
  `object.lto`, `sres.dll`, `cdaudio.dll`), 28,494 functions, 0 lift errors,
  no IDA. Started from the Shogo recomp ([docs/lithtech.md](docs/lithtech.md)
  says what carried over).
- Through the menus into a new game: the Third Law logo, the profile screen,
  the main menu, New Game, the difficulty, the character select (3D models)
  and the in-engine intro in the first world, driven by `--key`.
- `--headless` (pcrecomp `native32_headless`): the game on a private desktop
  that is never shown; `--shot` reads `d3d.ren`'s back buffer after its
  present, `--record FILE.mp4` pipes every frame to ffmpeg.
- Diagnostics: `--heap-check`, `--native MODULE`, `--di-probe`, and a crash
  report that names the faulting page's state and the native stack.
- In the first level: moving, turning and jumping, headless. `--key
  VK[:MS]@S` holds a key and feeds native32's synthetic DirectInput keyboard
  under `--headless`; `L<S>` times count from the level load.
- `NOTICE.md`, `CONTRIBUTING.md`, `CONTRIBUTORS.md`, `docs/building.md`,
  `docs/testing.md`.
- `Setup.cmd` (`tools/setup.ps1`), `launcher/launcher.json`, the hand-run
  `installer` workflow, `docs/bringup.md`, `docs/lithtech.md`.

### Fixed
- Setup and the launcher fetched pcrecomp's `pcrecomp-lithtech2` branch, which
  lacks `native32_input_key`, so a fresh clone did not compile. They use
  `pcrecomp-ingame` now, and Setup checks for it.
