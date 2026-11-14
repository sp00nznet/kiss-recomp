# Building

[Setup.cmd](../Setup.cmd) runs exactly these steps; this is the same thing by hand.

## The game folder

You need **your own copy of KISS Psycho Circus: The Nightmare Child** (the US
CD), patched to 1.13:

1. Copy the CD's `Psycho` folder somewhere (no installer has to run).
2. Get the two official US patches from archive.org's Game Patches and Fixes
   Archive (`video-game-patches_k`, folder *KISS Psycho Circus - The
   Nightmare Child*): `patch 1.1/US/kiss11us.zip` and
   `patch 1.10 to 1.13/US/kpc110us113.zip`.
3. Run `kpcptch11.exe`, then `kpcptch_US_110_to_113.exe`, **from inside the
   game folder** (they patch their working directory when the CD install's
   registry key is missing). From a command prompt in that folder:
   ```
   set __COMPAT_LAYER=RunAsInvoker
   D:\patches\kpcptch11.exe
   D:\patches\kpcptch_US_110_to_113.exe
   ```
   (`RunAsInvoker` stops Windows asking for administrator rights because of
   the file name.) Each says "Patching was successful". The folder then holds
   `client.exe` and `KPCPatch.rez`.

Nothing is downloaded for you.

## Step by step

Prerequisites: Windows 10/11, **Python 3.10+** (`py -3 --version`), **git**,
**Visual Studio 2022** (or its Build Tools) with *Desktop development with
C++*, ffmpeg on `PATH` for `--record` only, and pcrecomp cloned **beside**
this repository as `pcrecomp`:

```
some-folder\
  pcrecomp\        <- git clone https://github.com/sp00nznet/pcrecomp
  kiss-recomp\     <- this repository
```

1. Python packages:
   ```
   py -3 -m pip install --user pefile capstone
   ```
2. Point `game` at your 1.13 folder:
   ```
   mklink /J game D:\Games\Psycho
   ```
3. Relocate the modules:
   ```
   py -3 tools\rebase.py
   ```
   Expected, at the end: `cshell.dll   0x20000000-0x201eb000`.
4. The function catalog, all 8 modules in parallel (about a minute):
   ```
   py -3 tools\catalog.py
   ```
   Each module's log is `work\<module>.catalog.log`; the summary line for
   `client.exe` is `[*] Functions: 4857 ...`.
5. Recompile to C, then build (MSVC's 64-bit-hosted x86 compiler, CMake and
   Ninja; all three come with the C++ workload):
   ```
   py -3 run_lift.py
   build.cmd
   ```
   Expected from the lift: `28494 functions in the dispatch table, lift errors 0`.
6. Check it without opening a window:
   ```
   build\kiss.exe
   ```
   Expected: eight `[bind] ... 0 unresolved` lines, then
   `(dry run: mapped and bound; --run enters 0x01D2339C)`.
7. Play: `build\kiss.exe --run -- +windowed 1 +RGBRast 1`.

The usual trip-ups: `python` opening the Microsoft Store (Windows' alias
placeholder; use `py -3`, or turn the alias off in *Settings > Apps > Advanced
app settings > App execution aliases*); a freshly installed Python not on
`PATH` until you open a new window; a pcrecomp older than the
current `main`; and a game folder that is not patched (the 1.0
`client.exe` is a different build and is not what `run_lift.py` lifted).

## Options

`PCRECOMP` (Python) and `-DPCRECOMP=` (CMake, through
`set CMAKE_ARGS=...` for `build.cmd`) point at a toolkit checkout other than
`..\pcrecomp`; the lifter and the runtime must come from the same tree. The
generated C builds at `/Od` by default (`-DGEN_OPT=/O2` for a faster game and
a much longer build). On the farm: `netlab build kiss-recomp`. What the game
needed, in the order it turned up, is [docs/bringup.md](bringup.md).
